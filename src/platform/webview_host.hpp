#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
using HWND = void*;
#endif

namespace nlp3::platform {

struct EmbeddedUiUrl {
    std::string raw_url{};
    std::string scheme{};
    std::string host{};
    std::string path{"/"};
    std::uint16_t port = 0;
    bool valid = false;
    bool loopback = false;
};

EmbeddedUiUrl parse_embedded_ui_url(std::string_view url);
std::string build_loopback_ui_url(std::uint16_t port, std::string_view path = "/");
bool wait_for_embedded_ui_server_ready(
    std::string_view url,
    std::uint64_t timeout_ms,
    const std::function<void()>& pump_callback,
    std::string* last_error = nullptr);

struct WebViewHostOptions {
    std::wstring initial_url{};
    bool devtools_enabled = false;
    std::wstring loader_dll_path{};
    std::wstring user_data_dir{};
    std::uint64_t navigation_timeout_ms = 8000;
    std::function<void()> pump_callback{};
    std::function<void(const std::string&)> web_message_callback{};
};

struct WebViewHostStatus {
    bool initialized = false;
    bool navigation_succeeded = false;
    bool runtime_available = false;
    bool devtools_enabled = false;
    std::wstring initial_url{};
    std::wstring last_error{};
};

class WebViewHost {
public:
    WebViewHost();
    ~WebViewHost();

    bool initialize(HWND parent_window, const WebViewHostOptions& options);
    void resize();
    void close();

    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] WebViewHostStatus status() const;

private:
    bool ensure_com_initialized();
    bool load_loader_library(const std::wstring& explicit_path);
    bool create_environment(const WebViewHostOptions& options);
    bool create_controller(HWND parent_window, std::uint64_t timeout_ms);
    bool configure_settings();
    bool navigate_with_retry(const std::wstring& url, std::uint64_t timeout_ms);
    bool pump_until(const std::function<bool()>& predicate, std::uint64_t timeout_ms);
    void set_last_error(std::wstring message);
#ifdef _WIN32
    RECT current_parent_bounds() const;
#endif
    struct Impl;
    std::unique_ptr<Impl> impl_{};
    WebViewHostStatus status_{};
};

} // namespace nlp3::platform
