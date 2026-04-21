#include <array>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "bridge/tiktok_external_event_codec.hpp"
#include "bridge/tiktok_external_session_status_codec.hpp"
#include "platform/panel_app.hpp"
#include "platform/panel_config.hpp"
#include "platform/panel_config_storage.hpp"
#include "platform/panel_console.hpp"
#include "test_require.hpp"

#undef assert
#define assert(EXPR) NLP3_TEST_REQUIRE(EXPR)

namespace {

#ifdef _WIN32

bool ensure_test_winsock_initialized() {
    static const bool initialized = []() {
        WSADATA wsa_data{};
        return WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0;
    }();

    return initialized;
}

class WsTestClient {
public:
    ~WsTestClient() {
        close();
    }

    bool connect_tcp(std::uint16_t port) {
        if (!ensure_test_winsock_initialized()) {
            return false;
        }

        close();
        socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket_ == INVALID_SOCKET) {
            return false;
        }

        const DWORD timeout_ms = 2000;
        setsockopt(
            socket_,
            SOL_SOCKET,
            SO_RCVTIMEO,
            reinterpret_cast<const char*>(&timeout_ms),
            sizeof(timeout_ms));
        setsockopt(
            socket_,
            SOL_SOCKET,
            SO_SNDTIMEO,
            reinterpret_cast<const char*>(&timeout_ms),
            sizeof(timeout_ms));

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        if (connect(
                socket_,
                reinterpret_cast<const sockaddr*>(&address),
                sizeof(address)) == SOCKET_ERROR) {
            close();
            return false;
        }

        return true;
    }

    bool send_handshake_request() const {
        if (socket_ == INVALID_SOCKET) {
            return false;
        }

        static constexpr std::string_view request =
            "GET / HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "Sec-WebSocket-Key: dGVzdC1rZXktMTIzNA==\r\n"
            "\r\n";

        return send(socket_, request.data(), static_cast<int>(request.size()), 0)
            == static_cast<int>(request.size());
    }

    bool receive_handshake_response() const {
        if (socket_ == INVALID_SOCKET) {
            return false;
        }

        char buffer[1024];
        const auto received = recv(socket_, buffer, static_cast<int>(sizeof(buffer)), 0);
        if (received <= 0) {
            return false;
        }

        const std::string response(buffer, static_cast<std::size_t>(received));
        return response.find("101 Switching Protocols") != std::string::npos
            && response.find("Sec-WebSocket-Accept:") != std::string::npos;
    }

    bool send_text(std::string_view payload) const {
        if (socket_ == INVALID_SOCKET) {
            return false;
        }

        std::string frame;
        frame.push_back(static_cast<char>(0x81));

        if (payload.size() <= 125) {
            frame.push_back(static_cast<char>(0x80 | payload.size()));
        } else if (payload.size() <= 0xFFFFu) {
            frame.push_back(static_cast<char>(0x80 | 126));
            frame.push_back(static_cast<char>((payload.size() >> 8) & 0xFFu));
            frame.push_back(static_cast<char>(payload.size() & 0xFFu));
        } else {
            return false;
        }

        const std::array<unsigned char, 4> mask{0x12u, 0x34u, 0x56u, 0x78u};
        frame.append(reinterpret_cast<const char*>(mask.data()), mask.size());
        for (std::size_t index = 0; index < payload.size(); ++index) {
            frame.push_back(static_cast<char>(
                static_cast<unsigned char>(payload[index]) ^ mask[index % mask.size()]));
        }

        return send(socket_, frame.data(), static_cast<int>(frame.size()), 0)
            == static_cast<int>(frame.size());
    }

    void close() {
        if (socket_ != INVALID_SOCKET) {
            closesocket(socket_);
            socket_ = INVALID_SOCKET;
        }
    }

private:
    SOCKET socket_ = INVALID_SOCKET;
};

#endif

} // namespace

int main() {
    const auto config_path =
        std::filesystem::temp_directory_path() / "nlp3_panel_external_ws_console_test_config.json";
    std::filesystem::remove(config_path);

    nlp3::platform::PanelConfigStorage config_storage;
    nlp3::platform::PanelConfig config{};
    config.bridge_mode = "external";
    assert(config_storage.save_to_file(config, config_path.string()));

    nlp3::platform::PanelApp panel_app;
    assert(panel_app.initialize(config_path.string()));
    assert(panel_app.is_external_bridge_mode());

    const auto codec = nlp3::bridge::TikTokExternalEventCodec{};

    assert(panel_app.start_external_ws(18765));
    const auto started_status = panel_app.external_ws_status();
    assert(started_status.running);
    assert(started_status.port == 18765);
    const auto started_snapshot = panel_app.snapshot();
    assert(started_snapshot.external_ws.running);
    assert(started_snapshot.external_ws.port == 18765);
    const auto before_total_events = started_snapshot.total_events;
    const auto status_codec = nlp3::bridge::TikTokExternalSessionStatusCodec{};

    const auto direct_status_payload = status_codec.encode_json(nlp3::bridge::TikTokExternalSessionStatus{
        "ws_direct_target",
        "room-ws-status-001",
        nlp3::bridge::TikTokExternalSessionConnectionState::connected,
        "Direct WS status connected",
        1710000005005,
    });
    assert(panel_app.submit_external_ws_payload(direct_status_payload));

    const auto valid_payload = codec.encode_json(nlp3::bridge::TikTokRawEvent{
        nlp3::bridge::TikTokRawEventKind::chat,
        nlp3::bridge::TikTokRawActor{
            "ws-user-01",
            "ws_alice",
            "WS Alice",
            "https://cdn.example.com/avatar-ws-alice.png",
        },
        nlp3::bridge::TikTokRawMetadata{
            "evt-ws-chat-001",
            "room-ws-001",
            "comment",
            1710000005010,
        },
        "WS hello",
        std::nullopt,
        0,
    });
    assert(panel_app.submit_external_ws_payload(valid_payload));
    assert(!panel_app.submit_external_ws_payload("{ invalid-ws-json"));

    const auto snapshot_after_direct_payload = panel_app.snapshot();
    assert(snapshot_after_direct_payload.total_events == before_total_events + 1);
    assert(snapshot_after_direct_payload.external_ws.accepted_messages == 2);
    assert(snapshot_after_direct_payload.external_ws.rejected_messages == 1);
    assert(snapshot_after_direct_payload.external_bridge.target_user == "ws_direct_target");
    assert(snapshot_after_direct_payload.external_bridge.connection_state == "connected");
    assert(snapshot_after_direct_payload.external_bridge.last_status_message == "Direct WS status connected");
    assert(snapshot_after_direct_payload.external_bridge.last_status_timestamp_ms == 1710000005005);

    std::istringstream console_input;
    std::ostringstream console_output;
    nlp3::platform::PanelConsole panel_console{
        &panel_app,
        &console_input,
        &console_output,
    };

    assert(panel_console.execute_line("bridge target ws_live_target"));
    assert(panel_console.execute_line("bridge ws port 19876"));
    assert(panel_console.execute_line("bridge attach"));
    assert(panel_console.execute_line("bridge ws"));
    assert(panel_console.execute_line("bridge demo ws"));
    assert(panel_console.execute_line("bridge ws stop"));
    assert(!panel_app.external_ws_status().running);

    assert(panel_console.execute_line("bridge demo live 19876"));
    const auto restarted_status = panel_app.external_ws_status();
    assert(restarted_status.running);
    assert(restarted_status.port == 19876);

#ifdef _WIN32
    WsTestClient ws_test_client;
    assert(ws_test_client.connect_tcp(19876));
    assert(ws_test_client.send_handshake_request());
    assert(panel_console.execute_line("bridge demo ws run 5 0"));
    assert(ws_test_client.receive_handshake_response());

    const auto before_socket_total_events = panel_app.snapshot().total_events;
    const auto socket_status_payload = status_codec.encode_json(nlp3::bridge::TikTokExternalSessionStatus{
        "ws_live_target",
        "room-ws-status-002",
        nlp3::bridge::TikTokExternalSessionConnectionState::connected,
        "Socket WS status connected",
        1710000005015,
    });
    assert(ws_test_client.send_text(socket_status_payload));
    const auto follow_payload = codec.encode_json(nlp3::bridge::TikTokRawEvent{
        nlp3::bridge::TikTokRawEventKind::follow,
        nlp3::bridge::TikTokRawActor{
            "ws-user-02",
            "ws_bob",
            "WS Bob",
            "https://cdn.example.com/avatar-ws-bob.png",
        },
        nlp3::bridge::TikTokRawMetadata{
            "evt-ws-follow-001",
            "room-ws-002",
            "follow",
            1710000005020,
        },
        "",
        std::nullopt,
        0,
    });
    assert(ws_test_client.send_text(follow_payload));
    assert(ws_test_client.send_text("{ invalid-ws-json"));
    assert(panel_console.execute_line("bridge demo ws run 5 0"));

    const auto snapshot_after_socket = panel_app.snapshot();
    assert(snapshot_after_socket.total_events == before_socket_total_events + 1);
    assert(snapshot_after_socket.external_ws.accepted_messages == 2);
    assert(snapshot_after_socket.external_ws.rejected_messages == 1);
    assert(snapshot_after_socket.external_bridge.target_user == "ws_live_target");
    assert(snapshot_after_socket.external_bridge.connection_state == "connected");
    assert(snapshot_after_socket.external_bridge.last_status_message == "Socket WS status connected");
    assert(snapshot_after_socket.external_bridge.last_status_timestamp_ms == 1710000005015);
    assert(snapshot_after_socket.external_bridge.current_room_id == "room-ws-002");
    assert(snapshot_after_socket.external_bridge.last_event_kind == "follow");
    assert(snapshot_after_socket.external_bridge.last_event_actor == "WS Bob");
    assert(snapshot_after_socket.external_bridge.follow_events == 1);

    const auto before_await_total_events = panel_app.snapshot().total_events;
    const auto second_payload = codec.encode_json(nlp3::bridge::TikTokRawEvent{
        nlp3::bridge::TikTokRawEventKind::chat,
        nlp3::bridge::TikTokRawActor{
            "ws-user-03",
            "ws_carol",
            "WS Carol",
            "https://cdn.example.com/avatar-ws-carol.png",
        },
        nlp3::bridge::TikTokRawMetadata{
            "evt-ws-chat-002",
            "room-ws-003",
            "comment",
            1710000005030,
        },
        "WS second hello",
        std::nullopt,
        0,
    });
    assert(ws_test_client.send_text(second_payload));
    assert(panel_console.execute_line("bridge demo ws await 1 10 0"));
    const auto snapshot_after_await = panel_app.snapshot();
    assert(snapshot_after_await.total_events == before_await_total_events + 1);
    assert(snapshot_after_await.external_ws.accepted_messages == 3);
    assert(snapshot_after_await.external_bridge.chat_events >= 2);

    const auto before_session_total_events = panel_app.snapshot().total_events;
    const auto third_payload = codec.encode_json(nlp3::bridge::TikTokRawEvent{
        nlp3::bridge::TikTokRawEventKind::gift,
        nlp3::bridge::TikTokRawActor{
            "ws-user-04",
            "ws_dave",
            "WS Dave",
            "https://cdn.example.com/avatar-ws-dave.png",
        },
        nlp3::bridge::TikTokRawMetadata{
            "evt-ws-gift-003",
            "room-ws-004",
            "gift",
            1710000005040,
        },
        "",
        nlp3::bridge::TikTokRawGiftData{
            "gift-ws-rose",
            "WS Rose",
            1,
            10,
        },
        0,
    });
    assert(ws_test_client.send_text(third_payload));
    assert(panel_console.execute_line("bridge demo session 1 10 0 19876"));
    const auto snapshot_after_session = panel_app.snapshot();
    assert(snapshot_after_session.total_events == before_session_total_events + 1);
    assert(snapshot_after_session.external_ws.accepted_messages == 4);

    const auto before_observe_total_events = panel_app.snapshot().total_events;
    const auto fourth_payload = codec.encode_json(nlp3::bridge::TikTokRawEvent{
        nlp3::bridge::TikTokRawEventKind::chat,
        nlp3::bridge::TikTokRawActor{
            "ws-user-05",
            "ws_erin",
            "WS Erin",
            "https://cdn.example.com/avatar-ws-erin.png",
        },
        nlp3::bridge::TikTokRawMetadata{
            "evt-ws-chat-004",
            "room-ws-005",
            "comment",
            1710000005050,
        },
        "WS observe hello",
        std::nullopt,
        0,
    });
    assert(ws_test_client.send_text(fourth_payload));
    assert(panel_console.execute_line("bridge demo observe 1 10 0 19876"));
    const auto snapshot_after_observe = panel_app.snapshot();
    assert(snapshot_after_observe.total_events == before_observe_total_events + 1);
    assert(snapshot_after_observe.external_ws.accepted_messages == 5);

    ws_test_client.close();
#else
    assert(panel_console.execute_line("bridge demo ws run 1 0"));
    assert(panel_console.execute_line("bridge demo ws await 1 1 0"));
    assert(panel_console.execute_line("bridge demo session 1 1 0 19876"));
    assert(panel_console.execute_line("bridge demo observe 1 1 0 19876"));
#endif

    assert(panel_console.execute_line("bridge ws"));
    assert(panel_console.execute_line("bridge external"));
    assert(panel_console.execute_line("bridge demo ready"));

    const auto output = console_output.str();
    assert(output.find("Bridge WS:") != std::string::npos);
    assert(output.find("bridge attach ready for ws_live_target") != std::string::npos);
    assert(output.find("target_user=ws_live_target") != std::string::npos);
    assert(output.find("connection_state=connected") != std::string::npos);
    assert(output.find("current_room_id=room-ws-005") != std::string::npos);
    assert(output.find("event_counts=chat=") != std::string::npos);
    assert(output.find("Demo ready: yes") != std::string::npos);
    assert(output.find("bridge live demo ready on port 19876") != std::string::npos);
    assert(output.find("panel await: bridge demo ws await 1 200 0") != std::string::npos);
    assert(output.find("ws_running=yes") != std::string::npos);
    assert(output.find("game_active=yes (event-counter)") != std::string::npos);
    assert(output.find("Bridge WS demo:") != std::string::npos);
    assert(
        output.find("sample_command=python tools/bridge_py/sample_events.py --ws ws://127.0.0.1:19876")
            != std::string::npos
        || output.find("sample_command=python tools/bridge_py/sample_events.py --ws ws://127.0.0.1:18765")
            != std::string::npos);
    assert(
        output.find("bridge ws demo run: ticks=5") != std::string::npos
        || output.find("bridge ws demo run: ticks=1") != std::string::npos);
    assert(output.find("bridge ws demo await: ticks=") != std::string::npos);
    assert(output.find("bridge demo session: ticks=") != std::string::npos);
    assert(output.find("bridge demo observe: ticks=") != std::string::npos);
    assert(output.find("diagnostics=ok") != std::string::npos);
    assert(output.find("panel_total_events=") != std::string::npos);
    assert(
        output.find("activity_tail:") != std::string::npos
        || output.find("target_met=no") != std::string::npos);
    assert(
        output.find("target_met=yes") != std::string::npos
        || output.find("target_met=no") != std::string::npos);
    assert(output.find("accepted_messages=") != std::string::npos);
    assert(output.find("bridge ws stopped") != std::string::npos);
    assert(
        output.find("python tools/bridge_py/run_tiktok_bridge.py --user ws_live_target --ws ws://127.0.0.1:19876")
        != std::string::npos);

    panel_app.stop_external_ws();
    assert(!panel_app.external_ws_status().running);
    std::filesystem::remove(config_path);
    return 0;
}
