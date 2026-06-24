#include "platform/cloudflare_tunnel_service.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string_view>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace nlp3::platform {

static std::filesystem::path resolve_cloudflared_path() {
#ifdef _WIN32
    wchar_t buffer[MAX_PATH];
    const auto length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    auto exe_dir = std::filesystem::path(std::wstring(buffer, buffer + length)).parent_path();

    for (int depth = 0; depth < 8 && !exe_dir.empty(); ++depth) {
        auto candidate = exe_dir / "tools" / "cloudflared" / "cloudflared.exe";
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
        auto parent = exe_dir.parent_path();
        if (parent == exe_dir) break;
        exe_dir = parent;
    }
#endif
    return {};
}

CloudflareTunnelService::CloudflareTunnelService() = default;

CloudflareTunnelService::~CloudflareTunnelService() {
    stop_tunnel();
}

bool CloudflareTunnelService::start_tunnel(std::uint16_t port, TunnelUrlCallback on_url) {
    stop_tunnel();
    tunnel_url_.clear();
    last_error_.clear();

    auto cloudflared_path = resolve_cloudflared_path();
    if (cloudflared_path.empty()) {
        last_error_ = "cloudflared.exe not found in tools/cloudflared/";
        return false;
    }

    if (!std::filesystem::exists(cloudflared_path)) {
        last_error_ = "cloudflared.exe not found at " + cloudflared_path.string();
        return false;
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE stdout_read = nullptr;
    HANDLE stdout_write = nullptr;
    if (!CreatePipe(&stdout_read, &stdout_write, &sa, 0)) {
        last_error_ = "Failed to create stdout pipe";
        return false;
    }

    if (!SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(stdout_read);
        CloseHandle(stdout_write);
        last_error_ = "Failed to set stdout pipe handle info";
        return false;
    }

    std::wstring args = L"tunnel --url http://localhost:" + std::to_wstring(port);
    std::wstring cmd = L"\"" + cloudflared_path.wstring() + L"\" " + args;

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = stdout_write;
    si.hStdError = stdout_write;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(
            cloudflared_path.wstring().c_str(),
            cmd.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &si,
            &pi)) {
        CloseHandle(stdout_read);
        CloseHandle(stdout_write);
        last_error_ = "Failed to start cloudflared process";
        return false;
    }

    CloseHandle(stdout_write);
    CloseHandle(pi.hThread);

    process_handle_ = pi.hProcess;
    stdout_read_ = stdout_read;

    running_ = true;

    reader_thread_ = std::make_unique<std::thread>([this, port, on_url]() {
        reader_thread(port);
        if (on_url && !tunnel_url_.empty()) {
            on_url(tunnel_url_);
        }
    });

    return true;
}

void CloudflareTunnelService::stop_tunnel() {
    running_ = false;

    if (reader_thread_ && reader_thread_->joinable()) {
        reader_thread_->join();
        reader_thread_.reset();
    }

    if (process_handle_ != nullptr) {
        HANDLE h = static_cast<HANDLE>(process_handle_);
        TerminateProcess(h, 0);
        CloseHandle(h);
        process_handle_ = nullptr;
    }

    if (stdout_read_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(stdout_read_));
        stdout_read_ = nullptr;
    }
}

bool CloudflareTunnelService::is_running() const noexcept {
    return running_;
}

std::string CloudflareTunnelService::tunnel_url() const noexcept {
    return tunnel_url_;
}

std::string CloudflareTunnelService::last_error() const noexcept {
    return last_error_;
}

void CloudflareTunnelService::reader_thread(std::uint16_t port) {
    HANDLE read_handle = static_cast<HANDLE>(stdout_read_);
    std::array<char, 4096> buffer{};
    std::string accumulator;

    const auto start_time = std::chrono::steady_clock::now();
    constexpr auto max_wait = std::chrono::seconds(15);

    while (running_) {
        DWORD bytes_read = 0;
        if (ReadFile(read_handle, buffer.data(), static_cast<DWORD>(buffer.size() - 1), &bytes_read, nullptr)) {
            if (bytes_read > 0) {
                buffer[bytes_read] = '\0';
                accumulator += buffer.data();

                std::string_view sv(accumulator);

                auto pos = sv.find("https://");
                while (pos != std::string_view::npos) {
                    auto end = pos + 8;
                    while (end < sv.size() && sv[end] != ' ' && sv[end] != '\r' && sv[end] != '\n' && sv[end] != '\t') {
                        ++end;
                    }
                    auto url = std::string(sv.substr(pos, end - pos));

                    if (url.find(".trycloudflare.com") != std::string::npos) {
                        tunnel_url_ = url + "/overlay/live-timer";
                        return;
                    }
                    pos = sv.find("https://", end);
                }

                if (accumulator.size() > 65536) {
                    accumulator.erase(accumulator.begin(), accumulator.begin() + (accumulator.size() - 32768));
                }
            }
        } else {
            break;
        }

        if (!tunnel_url_.empty()) {
            break;
        }

        if (std::chrono::steady_clock::now() - start_time > max_wait) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

} // namespace nlp3::platform
