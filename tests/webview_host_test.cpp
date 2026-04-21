#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "platform/panel_app.hpp"
#include "platform/panel_config.hpp"
#include "platform/webview_host.hpp"
#include "platform/window_host.hpp"
#include "test_require.hpp"
#include "test_support.hpp"

#undef assert
#define assert(EXPR) NLP3_TEST_REQUIRE(EXPR)

namespace {

class ScopedEnvironmentOverride {
public:
    ScopedEnvironmentOverride(const char* name, std::string value)
        : name_(name) {
        const auto* previous_value = std::getenv(name);
        had_previous_ = previous_value != nullptr;
        if (previous_value != nullptr) {
            previous_value_ = previous_value;
        }
#ifdef _WIN32
        _putenv_s(name_, value.c_str());
#endif
    }

    ~ScopedEnvironmentOverride() {
#ifdef _WIN32
        if (had_previous_) {
            _putenv_s(name_, previous_value_.c_str());
        } else {
            _putenv_s(name_, "");
        }
#endif
    }

private:
    const char* name_ = nullptr;
    std::string previous_value_{};
    bool had_previous_ = false;
};

} // namespace

int main() {
#ifndef _WIN32
    return 0;
#else
    const auto built_url = nlp3::platform::build_loopback_ui_url(18913);
    assert(built_url == "http://127.0.0.1:18913/");

    const auto parsed_url = nlp3::platform::parse_embedded_ui_url("http://127.0.0.1:18913/");
    assert(parsed_url.valid);
    assert(parsed_url.loopback);
    assert(parsed_url.port == 18913);
    assert(parsed_url.path == "/");

    nlp3::platform::PanelConfig config{};
    config.bridge_mode = "external";
    config.embedded_ui_enabled = false;
    config.embedded_ui_fallback_to_browser = false;
    config.embedded_ui_devtools = false;
    config.embedded_ui_url = "http://127.0.0.1:19990/";
    config.embedded_ui_startup_timeout_ms = 1200;

    const auto config_path = nlp3::testsupport::write_temp_panel_config(
        "nlp3_webview_host_test_config.json",
        config);

    ScopedEnvironmentOverride env_enabled{"NLP3_EMBEDDED_UI_ENABLED", "true"};
    ScopedEnvironmentOverride env_fallback{"NLP3_EMBEDDED_UI_FALLBACK_TO_BROWSER", "true"};
    ScopedEnvironmentOverride env_devtools{"NLP3_EMBEDDED_UI_DEVTOOLS", "true"};
    ScopedEnvironmentOverride env_url{"NLP3_EMBEDDED_UI_URL", "http://127.0.0.1:19991/"};
    ScopedEnvironmentOverride env_timeout{"NLP3_EMBEDDED_UI_STARTUP_TIMEOUT_MS", "2500"};

    nlp3::platform::PanelApp panel_app;
    assert(panel_app.initialize(config_path.string()));
    assert(panel_app.config().embedded_ui_enabled);
    assert(panel_app.config().embedded_ui_fallback_to_browser);
    assert(panel_app.config().embedded_ui_devtools);
    assert(panel_app.config().embedded_ui_url == "http://127.0.0.1:19991/");
    assert(panel_app.config().embedded_ui_startup_timeout_ms == 2500);

    constexpr std::uint16_t kPort = 19991;
    assert(panel_app.start_http_ui(kPort));
    std::uint64_t tick_now_ms = 0;
    std::string wait_error{};
    assert(nlp3::platform::wait_for_embedded_ui_server_ready(
        panel_app.config().embedded_ui_url,
        2500,
        [&panel_app, &tick_now_ms]() {
            panel_app.tick(tick_now_ms);
            tick_now_ms += 16;
        },
        &wait_error));
    assert(wait_error.empty());

    nlp3::platform::WindowHost window_host;
    if (!window_host.create()) {
        std::cerr << "window_host.create failed with GetLastError=" << GetLastError() << '\n';
        return 1;
    }
    window_host.show(SW_HIDE);
    window_host.pump_messages();

    nlp3::platform::WebViewHost missing_loader_host;
    const auto missing_loader_path = (std::filesystem::temp_directory_path() / "nlp3-missing-WebView2Loader.dll").wstring();
    assert(!missing_loader_host.initialize(
        window_host.hwnd(),
        nlp3::platform::WebViewHostOptions{
            L"http://127.0.0.1:19991/",
            false,
            missing_loader_path,
            (std::filesystem::temp_directory_path() / "nlp3-webview-host-missing").wstring(),
            1000,
            {},
        }));
    assert(!missing_loader_host.status().last_error.empty());

    const auto repo_root = std::filesystem::path(__FILE__).parent_path().parent_path();
    const auto loader_path =
        repo_root / "third_party" / "webview2" / "sdk-1.0.3800.47" / "build" / "native" / "x64" / "WebView2Loader.dll";
    if (std::filesystem::exists(loader_path)) {
        nlp3::platform::WebViewHost webview_host;
        const auto initialized = webview_host.initialize(
            window_host.hwnd(),
            nlp3::platform::WebViewHostOptions{
                L"http://127.0.0.1:19991/",
                true,
                loader_path.wstring(),
                (std::filesystem::temp_directory_path() / "nlp3-webview-host-live").wstring(),
                2500,
                [&panel_app, &tick_now_ms]() {
                    panel_app.tick(tick_now_ms);
                    tick_now_ms += 16;
                },
            });
        const auto status = webview_host.status();
        if (initialized) {
            assert(status.initialized);
            assert(status.navigation_succeeded);
            webview_host.resize();
        } else {
            assert(!status.last_error.empty());
        }
        webview_host.close();
    }

    window_host.close();
    panel_app.stop_http_ui();
    std::filesystem::remove(config_path);
    return 0;
#endif
}
