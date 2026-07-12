#include "platform/cloudflare_tunnel_service.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <mutex>
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
#include <wininet.h>
#pragma comment(lib, "wininet.lib")
#endif

namespace nlp3::platform {

/// Returns the first existing tools/cloudflared/cloudflared.exe
/// by climbing up from the exe directory, or the deepest candidate
/// even if it doesn't exist (for download target).
static std::filesystem::path resolve_cloudflared_path(bool accept_missing = false) {
#ifdef _WIN32
    wchar_t buffer[MAX_PATH];
    const auto length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    std::filesystem::path best;
    auto exe_dir = std::filesystem::path(std::wstring(buffer, buffer + length)).parent_path();

    for (int depth = 0; depth < 8 && !exe_dir.empty(); ++depth) {
        auto candidate = exe_dir / "tools" / "cloudflared" / "cloudflared.exe";
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
        best = candidate; // remember the first project-root-level candidate
        auto parent = exe_dir.parent_path();
        if (parent == exe_dir) break;
        exe_dir = parent;
    }
    if (accept_missing && !best.empty()) {
        return best;
    }
#endif
    return {};
}

/// Download cloudflared.exe from GitHub Releases to the expected tools/ path.
/// If the tool already exists, returns its path immediately.
static std::filesystem::path ensure_cloudflared_downloaded() {
    auto path = resolve_cloudflared_path(/*accept_missing=*/false);
    if (!path.empty()) {
        return path; // already exists
    }

    // Determine where to save it (use the best candidate from project root)
    path = resolve_cloudflared_path(/*accept_missing=*/true);
    if (path.empty()) {
        return {}; // cannot determine target directory
    }

    // Create directory if needed
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return {};

    // Download from GitHub
    constexpr const char* kDownloadUrl =
        "https://github.com/cloudflare/cloudflared/releases/latest/download/"
        "cloudflared-windows-amd64.exe";

    HINTERNET hOpen = InternetOpenW(
        L"NisojeStudio/1.0",
        INTERNET_OPEN_TYPE_PRECONFIG,
        nullptr, nullptr, 0);
    if (!hOpen) return {};

    HINTERNET hFile = InternetOpenUrlA(
        hOpen, kDownloadUrl, nullptr, 0,
        INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE |
        INTERNET_FLAG_PRAGMA_NOCACHE | INTERNET_FLAG_NO_UI,
        0);
    if (!hFile) {
        InternetCloseHandle(hOpen);
        return {};
    }

    HANDLE outFile = CreateFileW(
        path.wstring().c_str(),
        GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (outFile == INVALID_HANDLE_VALUE) {
        InternetCloseHandle(hFile);
        InternetCloseHandle(hOpen);
        return {};
    }

    std::array<char, 65536> buf{};
    DWORD bytesRead = 0;
    while (InternetReadFile(hFile, buf.data(), static_cast<DWORD>(buf.size()), &bytesRead) && bytesRead > 0) {
        DWORD written = 0;
        WriteFile(outFile, buf.data(), bytesRead, &written, nullptr);
    }

    CloseHandle(outFile);
    InternetCloseHandle(hFile);
    InternetCloseHandle(hOpen);

    if (std::filesystem::exists(path) && std::filesystem::file_size(path) > 1024) {
        return path;
    }
    return {};
}

CloudflareTunnelService::CloudflareTunnelService() = default;

CloudflareTunnelService::~CloudflareTunnelService() {
    stop_tunnel();
}

bool CloudflareTunnelService::is_process_alive() const noexcept {
    if (process_handle_ == nullptr) return false;
    HANDLE h = static_cast<HANDLE>(process_handle_);
    DWORD exit_code = 0;
    if (!GetExitCodeProcess(h, &exit_code)) return false;
    return exit_code == STILL_ACTIVE;
}

bool CloudflareTunnelService::start_tunnel(std::uint16_t overlay_port, TunnelUrlCallback on_url) {
    if (running_) {
        last_error_ = "Tunnel already running";
        return false;
    }

    stop_tunnel(); // Cleanup preventivo

    {
        std::lock_guard<std::mutex> lock(mutex_);
        tunnel_url_.clear();
        last_error_.clear();
    }
    overlay_port_ = overlay_port;
    on_url_callback_ = on_url;
    running_ = true;

    // Auto-download cloudflared if missing
    auto cloudflared_path = ensure_cloudflared_downloaded();
    if (cloudflared_path.empty()) {
        last_error_ = "cloudflared.exe not found and could not be downloaded. "
                      "See tools/cloudflared/ manually or run the installer.";
        running_ = false;
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
        running_ = false;
        return false;
    }
    if (!SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(stdout_read); CloseHandle(stdout_write);
        last_error_ = "Failed to set stdout pipe handle info";
        running_ = false;
        return false;
    }

    // cloudflared tunnel --url http://127.0.0.1:<port>
    // Bind to 127.0.0.1 explicitly (not localhost which could resolve to ::1)
    std::wstring args = L"tunnel --url http://127.0.0.1:" + std::to_wstring(overlay_port_);
    std::wstring cmd = L"\"" + cloudflared_path.wstring() + L"\" " + args;

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = stdout_write;
    si.hStdError = stdout_write;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(
            cloudflared_path.wstring().c_str(), cmd.data(),
            nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
            nullptr, nullptr, &si, &pi)) {
        CloseHandle(stdout_read); CloseHandle(stdout_write);
        last_error_ = "Failed to start cloudflared process";
        running_ = false;
        return false;
    }

    CloseHandle(stdout_write);
    CloseHandle(pi.hThread);

    process_handle_ = pi.hProcess;
    stdout_read_ = stdout_read;

    // Reader thread: parses the tunnel URL from cloudflared stdout
    reader_thread_ = std::make_unique<std::thread>([this, overlay_port, on_url]() {
        reader_thread(overlay_port);
        std::string url;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            url = tunnel_url_;
        }
        if (on_url && !url.empty()) {
            on_url(url);
        }
    });

    // NO watchdog thread - Panel controls lifecycle via UI
    // NO auto-restart - Zero zombie guarantee

    return true;
}

void CloudflareTunnelService::stop_tunnel() {
    if (!running_) return;
    running_ = false;

    // 1. Close stdout_read -> EOF in cloudflared (graceful signal)
    if (stdout_read_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(stdout_read_));
        stdout_read_ = nullptr;
    }

    // 2. Send CTRL_BREAK_EVENT to process group (graceful shutdown for cloudflared)
    if (process_handle_ != nullptr) {
        DWORD pid = GetProcessId(static_cast<HANDLE>(process_handle_));
        if (pid != 0) {
            GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pid);
        }

        // 3. Wait 3s for graceful exit
        DWORD wait = WaitForSingleObject(static_cast<HANDLE>(process_handle_), 3000);
        if (wait == WAIT_TIMEOUT) {
            // 4. Fallback: TerminateProcess with exit code 0 (clean)
            TerminateProcess(static_cast<HANDLE>(process_handle_), 0);
            WaitForSingleObject(static_cast<HANDLE>(process_handle_), 1000);
        }
        CloseHandle(static_cast<HANDLE>(process_handle_));
        process_handle_ = nullptr;
    }

    // 5. Join reader thread
    if (reader_thread_ && reader_thread_->joinable()) {
        reader_thread_->join();
        reader_thread_.reset();
    }

    // 6. Clear state
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tunnel_url_.clear();
        last_error_.clear();
    }
}

bool CloudflareTunnelService::is_running() const noexcept {
    return running_;
}

std::string CloudflareTunnelService::tunnel_url() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
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
                        {
                            std::lock_guard<std::mutex> lock(mutex_);
                            tunnel_url_ = url + "/overlay/live-timer";
                        }
                        return;
                    }
                    pos = sv.find("https://", end);
                }

                if (accumulator.size() > 65536) {
                    accumulator.erase(accumulator.begin(), accumulator.begin() + (accumulator.size() - 32768));
                }
            }
        } else {
            break; // EOF or error
        }

        if (std::chrono::steady_clock::now() - start_time > max_wait) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

} // namespace nlp3::platform