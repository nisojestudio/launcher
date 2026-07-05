#include "bridge/tiktok_external_ws_server.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "bridge/tiktok_external_event_codec.hpp"
#include "bridge/tiktok_external_session_status_codec.hpp"
#include "platform/panel_app.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#endif

namespace {

#ifdef _WIN32

constexpr std::string_view kWebSocketGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

bool ensure_winsock_initialized() {
    static const bool initialized = []() {
        WSADATA wsa_data{};
        return WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0;
    }();

    return initialized;
}

void close_socket(SOCKET& socket_handle) {
    if (socket_handle != INVALID_SOCKET) {
        closesocket(socket_handle);
        socket_handle = INVALID_SOCKET;
    }
}

bool socket_readable(SOCKET socket_handle) {
    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(socket_handle, &read_set);

    timeval timeout{};
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    const auto result = select(0, &read_set, nullptr, nullptr, &timeout);
    return result > 0 && FD_ISSET(socket_handle, &read_set);
}

std::string trim_copy(std::string_view value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) {
        return {};
    }

    const auto end = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(begin, end - begin + 1));
}

std::string to_lower_ascii(std::string_view value) {
    std::string lowered;
    lowered.reserve(value.size());

    for (const auto character : value) {
        if (character >= 'A' && character <= 'Z') {
            lowered.push_back(static_cast<char>(character - 'A' + 'a'));
        } else {
            lowered.push_back(static_cast<char>(character));
        }
    }

    return lowered;
}

std::optional<std::string> extract_websocket_key(std::string_view request) {
    std::size_t line_begin = 0;
    while (line_begin < request.size()) {
        const auto line_end = request.find("\r\n", line_begin);
        const auto line = request.substr(
            line_begin,
            line_end == std::string_view::npos ? request.size() - line_begin : line_end - line_begin);

        const auto separator = line.find(':');
        if (separator != std::string_view::npos) {
            const auto header_name = to_lower_ascii(line.substr(0, separator));
            if (header_name == "sec-websocket-key") {
                const auto key = trim_copy(line.substr(separator + 1));
                if (!key.empty()) {
                    return key;
                }
            }
        }

        if (line_end == std::string_view::npos) {
            break;
        }
        line_begin = line_end + 2;
    }

    return std::nullopt;
}

std::optional<std::string> extract_header_value(
    std::string_view request,
    std::string_view expected_header_name) {
    const auto expected = to_lower_ascii(expected_header_name);

    std::size_t line_begin = 0;
    while (line_begin < request.size()) {
        const auto line_end = request.find("\r\n", line_begin);
        const auto line = request.substr(
            line_begin,
            line_end == std::string_view::npos ? request.size() - line_begin : line_end - line_begin);

        const auto separator = line.find(':');
        if (separator != std::string_view::npos) {
            const auto header_name = to_lower_ascii(line.substr(0, separator));
            if (header_name == expected) {
                const auto value = trim_copy(line.substr(separator + 1));
                if (!value.empty()) {
                    return value;
                }
                return std::string{};
            }
        }

        if (line_end == std::string_view::npos) {
            break;
        }
        line_begin = line_end + 2;
    }

    return std::nullopt;
}

bool is_allowed_loopback_origin(std::string_view origin) {
    const auto normalized = to_lower_ascii(trim_copy(origin));

    std::string_view authority{};
    if (normalized.rfind("http://", 0) == 0) {
        authority = std::string_view(normalized).substr(7);
    } else if (normalized.rfind("https://", 0) == 0) {
        authority = std::string_view(normalized).substr(8);
    } else {
        return false;
    }

    const auto path_start = authority.find('/');
    if (path_start != std::string_view::npos) {
        authority = authority.substr(0, path_start);
    }

    if (authority.empty()) {
        return false;
    }

    std::string_view host = authority;
    if (authority.front() == '[') {
        const auto bracket_end = authority.find(']');
        if (bracket_end == std::string_view::npos) {
            return false;
        }
        host = authority.substr(1, bracket_end - 1);
    } else {
        const auto port_separator = authority.find(':');
        if (port_separator != std::string_view::npos) {
            host = authority.substr(0, port_separator);
        }
    }

    return host == "127.0.0.1" || host == "localhost" || host == "::1";
}

std::string make_http_error_response(
    std::string_view status_line,
    std::string_view body) {
    return std::string("HTTP/1.1 ")
        + std::string(status_line)
        + "\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: "
        + std::to_string(body.size())
        + "\r\nConnection: close\r\n\r\n"
        + std::string(body);
}

std::array<std::uint8_t, 20> sha1_bytes(std::string_view input) {
    std::array<std::uint8_t, 20> digest{};
    std::array<std::uint32_t, 5> state{
        0x67452301u,
        0xEFCDAB89u,
        0x98BADCFEu,
        0x10325476u,
        0xC3D2E1F0u,
    };

    std::string message(input);
    const auto original_bit_length = static_cast<std::uint64_t>(message.size()) * 8ULL;
    message.push_back(static_cast<char>(0x80));
    while ((message.size() % 64) != 56) {
        message.push_back('\0');
    }

    for (int shift = 7; shift >= 0; --shift) {
        message.push_back(static_cast<char>((original_bit_length >> (shift * 8)) & 0xFFu));
    }

    auto left_rotate = [](std::uint32_t value, int bits) {
        return static_cast<std::uint32_t>((value << bits) | (value >> (32 - bits)));
    };

    for (std::size_t chunk = 0; chunk < message.size(); chunk += 64) {
        std::array<std::uint32_t, 80> words{};
        for (std::size_t index = 0; index < 16; ++index) {
            const auto base = chunk + (index * 4);
            words[index] =
                (static_cast<std::uint32_t>(static_cast<unsigned char>(message[base])) << 24)
                | (static_cast<std::uint32_t>(static_cast<unsigned char>(message[base + 1])) << 16)
                | (static_cast<std::uint32_t>(static_cast<unsigned char>(message[base + 2])) << 8)
                | static_cast<std::uint32_t>(static_cast<unsigned char>(message[base + 3]));
        }

        for (std::size_t index = 16; index < 80; ++index) {
            words[index] = left_rotate(
                words[index - 3] ^ words[index - 8] ^ words[index - 14] ^ words[index - 16],
                1);
        }

        auto a = state[0];
        auto b = state[1];
        auto c = state[2];
        auto d = state[3];
        auto e = state[4];

        for (std::size_t index = 0; index < 80; ++index) {
            std::uint32_t function = 0;
            std::uint32_t constant = 0;
            if (index < 20) {
                function = (b & c) | ((~b) & d);
                constant = 0x5A827999u;
            } else if (index < 40) {
                function = b ^ c ^ d;
                constant = 0x6ED9EBA1u;
            } else if (index < 60) {
                function = (b & c) | (b & d) | (c & d);
                constant = 0x8F1BBCDCu;
            } else {
                function = b ^ c ^ d;
                constant = 0xCA62C1D6u;
            }

            const auto temp =
                left_rotate(a, 5) + function + e + constant + words[index];
            e = d;
            d = c;
            c = left_rotate(b, 30);
            b = a;
            a = temp;
        }

        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
    }

    for (std::size_t index = 0; index < state.size(); ++index) {
        digest[(index * 4)] = static_cast<std::uint8_t>((state[index] >> 24) & 0xFFu);
        digest[(index * 4) + 1] = static_cast<std::uint8_t>((state[index] >> 16) & 0xFFu);
        digest[(index * 4) + 2] = static_cast<std::uint8_t>((state[index] >> 8) & 0xFFu);
        digest[(index * 4) + 3] = static_cast<std::uint8_t>(state[index] & 0xFFu);
    }

    return digest;
}

std::string base64_encode(const std::uint8_t* data, std::size_t size) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string encoded;
    encoded.reserve(((size + 2) / 3) * 4);

    for (std::size_t index = 0; index < size; index += 3) {
        const auto octet_a = data[index];
        const auto octet_b = index + 1 < size ? data[index + 1] : 0;
        const auto octet_c = index + 2 < size ? data[index + 2] : 0;
        const auto triple =
            (static_cast<std::uint32_t>(octet_a) << 16)
            | (static_cast<std::uint32_t>(octet_b) << 8)
            | static_cast<std::uint32_t>(octet_c);

        encoded.push_back(kAlphabet[(triple >> 18) & 0x3Fu]);
        encoded.push_back(kAlphabet[(triple >> 12) & 0x3Fu]);
        encoded.push_back(index + 1 < size ? kAlphabet[(triple >> 6) & 0x3Fu] : '=');
        encoded.push_back(index + 2 < size ? kAlphabet[triple & 0x3Fu] : '=');
    }

    return encoded;
}

std::string build_websocket_accept(std::string_view key) {
    const auto digest = sha1_bytes(std::string(key) + std::string(kWebSocketGuid));
    return base64_encode(digest.data(), digest.size());
}

bool send_all(SOCKET socket_handle, const std::string& payload) {
    std::size_t sent_total = 0;
    while (sent_total < payload.size()) {
        const auto sent = send(
            socket_handle,
            payload.data() + sent_total,
            static_cast<int>(payload.size() - sent_total),
            0);
        if (sent == SOCKET_ERROR) {
            return false;
        }
        if (sent <= 0) {
            return false;
        }
        sent_total += static_cast<std::size_t>(sent);
    }

    return true;
}

std::string build_server_frame(unsigned char opcode, std::string_view payload) {
    std::string frame;
    frame.push_back(static_cast<char>(0x80u | opcode));

    if (payload.size() <= 125) {
        frame.push_back(static_cast<char>(payload.size()));
    } else if (payload.size() <= 0xFFFFu) {
        frame.push_back(static_cast<char>(126));
        frame.push_back(static_cast<char>((payload.size() >> 8) & 0xFFu));
        frame.push_back(static_cast<char>(payload.size() & 0xFFu));
    } else {
        frame.push_back(static_cast<char>(127));
        for (int shift = 7; shift >= 0; --shift) {
            frame.push_back(static_cast<char>((payload.size() >> (shift * 8)) & 0xFFu));
        }
    }

    frame.append(payload.data(), payload.size());
    return frame;
}

void try_cleanup_stale_port_listeners(std::uint16_t target_port) {
    // Enumerate all TCP listeners to find stale entries on our target port.
    // Stale listeners are from processes that have crashed or been killed
    // but whose TCP socket remained in the kernel table (common with
    // SO_REUSEADDR + TerminateProcess).
    //
    // Strategy:
    //   1. Terminate any living process still holding the port.
    //   2. Use SetTcpEntry with DELETE_TCB to forcibly remove any remaining
    //      zombie TCP entries from dead processes. This requires admin
    //      elevation; if it fails we fall back to SO_REUSEADDR.
    ULONG buffer_size = 0;
    ULONG result = GetExtendedTcpTable(
        nullptr, &buffer_size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0);
    if (result != ERROR_INSUFFICIENT_BUFFER || buffer_size == 0) {
        return;
    }

    std::vector<std::uint8_t> buffer(buffer_size);
    auto* table = reinterpret_cast<MIB_TCPTABLE_OWNER_PID*>(buffer.data());
    result = GetExtendedTcpTable(
        table, &buffer_size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0);
    if (result != NO_ERROR) {
        return;
    }

    const auto port_network = htons(target_port);
    for (DWORD index = 0; index < table->dwNumEntries; ++index) {
        const auto& row = table->table[index];
        if (row.dwLocalPort != port_network) {
            continue;
        }
        if (row.dwOwningPid == 0) {
            continue;
        }
        if (row.dwOwningPid == GetCurrentProcessId()) {
            continue;
        }

        // Check if the owning process is still alive.
        const auto process_handle = OpenProcess(
            PROCESS_QUERY_INFORMATION | PROCESS_TERMINATE, FALSE, row.dwOwningPid);
        if (process_handle != nullptr) {
            DWORD exit_code = STILL_ACTIVE;
            if (GetExitCodeProcess(process_handle, &exit_code) && exit_code == STILL_ACTIVE) {
                // Process is alive — it might be another panel instance.
                // Try to terminate it so we can claim the port.
                TerminateProcess(process_handle, 1);
                WaitForSingleObject(process_handle, 2000);
            }
            CloseHandle(process_handle);
        }

        // Forcibly remove the TCP listener entry from the kernel table.
        // This handles zombie entries from already-dead processes and any
        // entries left behind by the termination above.
        // Requires admin elevation — silently ignored if it fails.
        MIB_TCPROW delete_row{};
        delete_row.dwState = MIB_TCP_STATE_DELETE_TCB;  // 12
        delete_row.dwLocalAddr = row.dwLocalAddr;
        delete_row.dwLocalPort = row.dwLocalPort;
        delete_row.dwRemoteAddr = 0;   // listener: no remote
        delete_row.dwRemotePort = 0;   // listener: no remote
        SetTcpEntry(&delete_row);
    }
}

#endif

} // namespace

namespace nlp3::bridge {

struct TikTokExternalWsServer::Impl {
#ifdef _WIN32
    SOCKET listen_socket = INVALID_SOCKET;
    SOCKET client_socket = INVALID_SOCKET;
    bool handshake_complete = false;
    std::string receive_buffer{};
#endif
};

TikTokExternalWsServer::TikTokExternalWsServer(platform::PanelApp* app) noexcept
    : app_(app),
      impl_(std::make_unique<Impl>()) {
}

TikTokExternalWsServer::~TikTokExternalWsServer() = default;

bool TikTokExternalWsServer::start(std::uint16_t port) {
    if (app_ == nullptr) {
        return false;
    }

    stop();

#ifndef _WIN32
    (void)port;
    return false;
#else
    if (!ensure_winsock_initialized()) {
        return false;
    }

    const auto resolved_port = port == 0 ? static_cast<std::uint16_t>(8765) : port;

    // Clean up any stale listeners from crashed/killed panel instances
    // that might have left zombie sockets on this port.
    try_cleanup_stale_port_listeners(resolved_port);
    // Give the OS a moment to finalize any TCP state cleanup
    // after terminating stale processes above.
    Sleep(200);

    auto listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket == INVALID_SOCKET) {
        return false;
    }

    u_long nonblocking = 1;
    ioctlsocket(listen_socket, FIONBIO, &nonblocking);

    // Use SO_EXCLUSIVEADDRUSE to prevent other processes (including stale
    // zombie sockets) from sharing this port. This avoids the problem where
    // crashed panel instances leave ghost listeners that intercept bridge
    // WebSocket connections, causing handshake timeouts.
    const BOOL exclusive_address = TRUE;
    setsockopt(
        listen_socket,
        SOL_SOCKET,
        SO_EXCLUSIVEADDRUSE,
        reinterpret_cast<const char*>(&exclusive_address),
        sizeof(exclusive_address));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(resolved_port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(
            listen_socket,
            reinterpret_cast<const sockaddr*>(&address),
            sizeof(address)) == SOCKET_ERROR) {
        const auto bind_error = WSAGetLastError();
        close_socket(listen_socket);

        // If the port is still in use after cleanup, attempt emergency
        // fallback with SO_REUSEADDR to coexist with any remaining zombies
        // until the next system reboot clears them.
        if (bind_error == WSAEADDRINUSE) {
            auto fallback_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (fallback_socket != INVALID_SOCKET) {
                ioctlsocket(fallback_socket, FIONBIO, &nonblocking);
                const BOOL reuse_fallback = TRUE;
                setsockopt(
                    fallback_socket,
                    SOL_SOCKET,
                    SO_REUSEADDR,
                    reinterpret_cast<const char*>(&reuse_fallback),
                    sizeof(reuse_fallback));
                if (bind(
                        fallback_socket,
                        reinterpret_cast<const sockaddr*>(&address),
                        sizeof(address)) != SOCKET_ERROR) {
                    listen_socket = fallback_socket;
                } else {
                    closesocket(fallback_socket);
                    return false;
                }
            } else {
                return false;
            }
        } else {
            return false;
        }
    }

    if (listen(listen_socket, SOMAXCONN) == SOCKET_ERROR) {
        close_socket(listen_socket);
        return false;
    }

    impl_->listen_socket = listen_socket;
    impl_->handshake_complete = false;
    impl_->receive_buffer.clear();
    running_ = true;
    port_ = resolved_port;
    accepted_messages_ = 0;
    rejected_messages_ = 0;
    return true;
#endif
}

void TikTokExternalWsServer::stop() {
#ifdef _WIN32
    if (impl_ != nullptr) {
        close_socket(impl_->client_socket);
        close_socket(impl_->listen_socket);
        impl_->handshake_complete = false;
        impl_->receive_buffer.clear();
    }
#endif

    running_ = false;
    port_ = 0;
}

std::size_t TikTokExternalWsServer::poll() {
    if (!running_ || app_ == nullptr) {
        return 0;
    }

#ifndef _WIN32
    return 0;
#else
    if (impl_ == nullptr || impl_->listen_socket == INVALID_SOCKET) {
        return 0;
    }

    if (impl_->client_socket == INVALID_SOCKET) {
        sockaddr_in client_address{};
        int client_address_size = sizeof(client_address);
        auto accepted_socket = accept(
            impl_->listen_socket,
            reinterpret_cast<sockaddr*>(&client_address),
            &client_address_size);
        if (accepted_socket != INVALID_SOCKET) {
            u_long nonblocking = 1;
            ioctlsocket(accepted_socket, FIONBIO, &nonblocking);
            impl_->client_socket = accepted_socket;
            impl_->handshake_complete = false;
            impl_->receive_buffer.clear();
        } else {
            const auto error_code = WSAGetLastError();
            if (error_code != WSAEWOULDBLOCK) {
                close_socket(impl_->client_socket);
                impl_->handshake_complete = false;
                impl_->receive_buffer.clear();
            }
        }
    }

    if (impl_->client_socket == INVALID_SOCKET) {
        return 0;
    }

    char buffer[4096];
    const auto received = recv(
        impl_->client_socket,
        buffer,
        static_cast<int>(sizeof(buffer)),
        0);
    if (received > 0) {
        impl_->receive_buffer.append(buffer, static_cast<std::size_t>(received));
    } else if (received == 0) {
        close_socket(impl_->client_socket);
        impl_->handshake_complete = false;
        impl_->receive_buffer.clear();
        return 0;
    } else {
        const auto error_code = WSAGetLastError();
        if (error_code != WSAEWOULDBLOCK) {
            close_socket(impl_->client_socket);
            impl_->handshake_complete = false;
            impl_->receive_buffer.clear();
            return 0;
        }
    }

    if (!impl_->handshake_complete) {
        const auto header_end = impl_->receive_buffer.find("\r\n\r\n");
        if (header_end == std::string::npos) {
            return 0;
        }

        const auto request = std::string_view(impl_->receive_buffer).substr(0, header_end + 4);
        const auto websocket_key = extract_websocket_key(request);
        if (!websocket_key.has_value()) {
            close_socket(impl_->client_socket);
            impl_->receive_buffer.clear();
            return 0;
        }

        const auto origin = extract_header_value(request, "origin");
        if (origin.has_value() && !origin->empty() && !is_allowed_loopback_origin(*origin)) {
            const auto forbidden_response =
                make_http_error_response("403 Forbidden", "origin_not_allowed");
            send_all(impl_->client_socket, forbidden_response);
            close_socket(impl_->client_socket);
            impl_->handshake_complete = false;
            impl_->receive_buffer.clear();
            return 0;
        }

        const auto response =
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: "
            + build_websocket_accept(*websocket_key)
            + "\r\n\r\n";
        if (!send_all(impl_->client_socket, response)) {
            close_socket(impl_->client_socket);
            impl_->receive_buffer.clear();
            return 0;
        }

        impl_->handshake_complete = true;
        impl_->receive_buffer.erase(0, header_end + 4);
    }

    std::size_t accepted_payloads = 0;
    while (impl_->client_socket != INVALID_SOCKET) {
        if (impl_->receive_buffer.size() < 2) {
            break;
        }

        const auto* data = reinterpret_cast<const unsigned char*>(impl_->receive_buffer.data());
        const auto fin = (data[0] & 0x80u) != 0;
        const auto opcode = static_cast<unsigned char>(data[0] & 0x0Fu);
        const auto masked = (data[1] & 0x80u) != 0;
        std::uint64_t payload_size = data[1] & 0x7Fu;
        std::size_t header_size = 2;

        if (!fin || !masked) {
            close_socket(impl_->client_socket);
            impl_->handshake_complete = false;
            impl_->receive_buffer.clear();
            break;
        }

        if (payload_size == 126) {
            if (impl_->receive_buffer.size() < 4) {
                break;
            }

            payload_size =
                (static_cast<std::uint64_t>(data[2]) << 8)
                | static_cast<std::uint64_t>(data[3]);
            header_size = 4;
        } else if (payload_size == 127) {
            if (impl_->receive_buffer.size() < 10) {
                break;
            }

            payload_size = 0;
            for (std::size_t index = 0; index < 8; ++index) {
                payload_size =
                    (payload_size << 8)
                    | static_cast<std::uint64_t>(data[2 + index]);
            }
            header_size = 10;
        }

        const auto frame_size = header_size + 4 + payload_size;
        if (impl_->receive_buffer.size() < frame_size) {
            break;
        }

        const auto* mask = data + header_size;
        const auto* payload = data + header_size + 4;
        std::string decoded_payload(payload_size, '\0');
        for (std::size_t index = 0; index < payload_size; ++index) {
            decoded_payload[index] = static_cast<char>(payload[index] ^ mask[index % 4]);
        }

        impl_->receive_buffer.erase(0, frame_size);

        if (opcode == 0x8u) {
            const auto close_frame = build_server_frame(0x8u, {});
            send_all(impl_->client_socket, close_frame);
            close_socket(impl_->client_socket);
            impl_->handshake_complete = false;
            impl_->receive_buffer.clear();
            break;
        }

        if (opcode == 0x9u) {
            const auto pong_frame = build_server_frame(0xAu, decoded_payload);
            if (!send_all(impl_->client_socket, pong_frame)) {
                close_socket(impl_->client_socket);
                impl_->handshake_complete = false;
                impl_->receive_buffer.clear();
                break;
            }
            continue;
        }

        if (opcode != 0x1u) {
            close_socket(impl_->client_socket);
            impl_->handshake_complete = false;
            impl_->receive_buffer.clear();
            break;
        }

        if (process_text_message(decoded_payload, false)) {
            ++accepted_payloads;
        }
    }

    return accepted_payloads;
#endif
}

bool TikTokExternalWsServer::handle_text_message(const std::string& payload) {
    return process_text_message(payload, true);
}

bool TikTokExternalWsServer::running() const noexcept {
    return running_;
}

TikTokExternalWsStatus TikTokExternalWsServer::status() const noexcept {
    return TikTokExternalWsStatus{
        running_,
        port_,
        accepted_messages_,
        rejected_messages_,
    };
}

bool TikTokExternalWsServer::process_text_message(
    const std::string& payload,
    bool tick_after_accept) {
    if (!running_ || app_ == nullptr) {
        ++rejected_messages_;
        return false;
    }

    const TikTokExternalSessionStatusCodec status_codec{};
    if (const auto decoded_status = status_codec.decode_json(payload); decoded_status.has_value()) {
        if (!app_->submit_external_session_status(*decoded_status)) {
            ++rejected_messages_;
            return false;
        }

        ++accepted_messages_;
        if (tick_after_accept) {
            app_->tick(0);
        }
        return true;
    }

    const TikTokExternalEventCodec codec{};
    const auto decoded_event = codec.decode_json(payload);
    if (!decoded_event.has_value()) {
        ++rejected_messages_;
        return false;
    }

    if (!app_->submit_external_bridge_event(*decoded_event)) {
        ++rejected_messages_;
        return false;
    }

    ++accepted_messages_;
    if (tick_after_accept) {
        app_->tick(0);
    }
    return true;
}

} // namespace nlp3::bridge
