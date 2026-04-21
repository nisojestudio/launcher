#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif

#include "platform/panel_app.hpp"
#include "platform/panel_console.hpp"
#include "platform/panel_http_server.hpp"
#include "platform/runtime.hpp"
#include "platform/panel_view_model_builder.hpp"
#include "platform/webview_host.hpp"
#include "platform/window_host.hpp"

#ifdef _WIN32
#include "platform/app_resource.h"
#endif

namespace {

struct LaunchOptions {
    bool console_mode = false;
    bool ui_mode = false;
    bool disable_browser_fallback = false;
    std::optional<std::uint16_t> ui_port_override{};
};

std::string join_section_items(const nlp3::platform::PanelViewSection& section) {
    std::string line;
    for (std::size_t index = 0; index < section.items.size(); ++index) {
        if (index > 0) {
            line += ", ";
        }
        line += section.items[index].label + "=" + section.items[index].value;
    }
    return line;
}

std::string trim_copy(std::string_view value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) {
        return {};
    }

    const auto end = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(begin, end - begin + 1));
}

std::string escape_json(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (const auto ch : value) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }
    return escaped;
}

std::string wide_to_utf8(const std::wstring& value) {
#ifdef _WIN32
    if (value.empty()) {
        return {};
    }

    const auto required = WideCharToMultiByte(
        CP_UTF8,
        0,
        value.c_str(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (required <= 0) {
        return {};
    }

    std::string utf8(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        value.c_str(),
        static_cast<int>(value.size()),
        utf8.data(),
        required,
        nullptr,
        nullptr);
    return utf8;
#else
    return std::string(value.begin(), value.end());
#endif
}

std::wstring utf8_to_wide(std::string_view value) {
#ifdef _WIN32
    if (value.empty()) {
        return {};
    }

    const auto required = MultiByteToWideChar(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0);
    if (required <= 0) {
        return {};
    }

    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        wide.data(),
        required);
    return wide;
#else
    return std::wstring(value.begin(), value.end());
#endif
}

std::uint64_t now_wall_clock_ms() {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

#ifdef _WIN32
void enable_dpi_awareness() {
    const auto user32 = GetModuleHandleW(L"user32.dll");
    if (user32 == nullptr) {
        return;
    }

    using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(HANDLE);
    const auto set_context =
        reinterpret_cast<SetProcessDpiAwarenessContextFn>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (set_context != nullptr) {
        if (set_context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != FALSE) {
            return;
        }
        if (set_context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE) != FALSE) {
            return;
        }
    }

    using SetProcessDPIAwareFn = BOOL(WINAPI*)(VOID);
    const auto set_legacy =
        reinterpret_cast<SetProcessDPIAwareFn>(GetProcAddress(user32, "SetProcessDPIAware"));
    if (set_legacy != nullptr) {
        set_legacy();
    }
}

HICON load_app_icon() {
    return static_cast<HICON>(
        LoadImageW(
            GetModuleHandleW(nullptr),
            MAKEINTRESOURCEW(IDI_NLP3_APP_ICON),
            IMAGE_ICON,
            0,
            0,
            LR_DEFAULTSIZE));
}
#endif

void emit_startup_log(std::string_view phase, std::string_view message, bool ok = true) {
    std::ostringstream line;
    line << "{"
         << "\"timestamp_ms\":" << now_wall_clock_ms() << ","
         << "\"component\":\"embedded_ui\","
         << "\"phase\":\"" << escape_json(phase) << "\","
         << "\"ok\":" << (ok ? "true" : "false") << ","
         << "\"message\":\"" << escape_json(message) << "\""
         << "}";

#ifdef _WIN32
    const auto utf8 = line.str();
    OutputDebugStringA((utf8 + "\n").c_str());
#endif

    std::cout << line.str() << "\n";

    try {
        const auto log_dir = std::filesystem::temp_directory_path() / "NisojeStudio";
        std::filesystem::create_directories(log_dir);
        std::ofstream output(log_dir / "embedded_ui.log", std::ios::app | std::ios::binary);
        output << line.str() << "\n";
    } catch (...) {
    }
}

std::optional<std::uint16_t> parse_port_value(std::string_view value) {
    try {
        std::size_t consumed = 0;
        const auto parsed = std::stoull(std::string(value), &consumed);
        if (consumed != value.size() || parsed > 65535) {
            return std::nullopt;
        }
        return static_cast<std::uint16_t>(parsed);
    } catch (...) {
        return std::nullopt;
    }
}

std::uint16_t resolve_external_ws_port(const nlp3::platform::PanelApp& app) {
    return app.config().external_ws_port == 0 ? static_cast<std::uint16_t>(8765) : app.config().external_ws_port;
}

LaunchOptions parse_launch_options(const std::vector<std::string>& arguments) {
    LaunchOptions options{};
    for (std::size_t index = 1; index < arguments.size(); ++index) {
        const std::string_view argument = arguments[index];
        if (argument == "--console") {
            options.console_mode = true;
        } else if (argument == "--ui") {
            options.ui_mode = true;
        } else if (argument == "--no-browser") {
            options.disable_browser_fallback = true;
        } else if (argument == "--ui-port" && index + 1 < arguments.size()) {
            const auto parsed_port = parse_port_value(arguments[++index]);
            if (parsed_port.has_value()) {
                options.ui_port_override = parsed_port;
            }
        }
    }
    return options;
}

void handle_embedded_ui_window_message(
    std::string_view message,
    const nlp3::platform::WindowHost& window_host) {
    if (message == "window:minimize") {
        window_host.minimize();
        return;
    }
    if (message == "window:toggle-maximize") {
        window_host.toggle_maximize();
        return;
    }
    if (message == "window:drag") {
        window_host.begin_move_drag();
        return;
    }
    if (message == "window:close") {
        window_host.request_close();
    }
}

void print_startup_overview(nlp3::platform::PanelApp& app) {
    const auto manifest = nlp3::platform::build_runtime_manifest();
    const auto panel_snapshot = app.snapshot();
    const nlp3::platform::PanelViewModelBuilder view_model_builder;
    const auto view_model = view_model_builder.build(panel_snapshot);

    std::cout << manifest.engine.name
              << " v"
              << manifest.engine.version_major
              << "."
              << manifest.engine.version_minor
              << "\n";

    for (const auto& module : manifest.modules) {
        std::cout << "- "
                  << module.name
                  << " ["
                  << nlp3::to_string(module.stage)
                  << "] "
                  << module.responsibility
                  << "\n";
    }

    std::cout << "Registered local games: " << app.registered_game_count() << "\n";
    std::cout << "Active game attached: " << (panel_snapshot.game.has_active_game ? "yes" : "no") << "\n";
    std::cout << view_model.title << "\n";
    for (const auto& section : view_model.sections) {
        std::cout << "* " << section.title << ": " << join_section_items(section) << "\n";
    }
}

bool setup_console_io(bool allocate_if_missing) {
#ifdef _WIN32
    bool console_ready = false;
    if (AttachConsole(ATTACH_PARENT_PROCESS) != FALSE) {
        console_ready = true;
    } else if (GetLastError() == ERROR_ACCESS_DENIED) {
        console_ready = true;
    } else if (allocate_if_missing && AllocConsole() != FALSE) {
        console_ready = true;
    }

    if (!console_ready) {
        return false;
    }

    FILE* stream = nullptr;
    freopen_s(&stream, "CONIN$", "r", stdin);
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);
    std::ios::sync_with_stdio(true);
    std::cin.clear();
    std::cout.clear();
    std::cerr.clear();
    SetConsoleOutputCP(CP_UTF8);
    return true;
#else
    (void)allocate_if_missing;
    return true;
#endif
}

#ifdef _WIN32
std::vector<std::string> collect_process_arguments() {
    std::vector<std::string> arguments{};
    const wchar_t* command_line = GetCommandLineW();
    if (command_line == nullptr || *command_line == L'\0') {
        return {"NisojeStudio"};
    }

    std::wstring current{};
    bool in_quotes = false;
    for (const wchar_t* cursor = command_line; *cursor != L'\0'; ++cursor) {
        const auto ch = *cursor;
        if (ch == L'"') {
            in_quotes = !in_quotes;
            continue;
        }

        if (!in_quotes && (ch == L' ' || ch == L'\t')) {
            if (!current.empty()) {
                arguments.push_back(wide_to_utf8(current));
                current.clear();
            }
            continue;
        }

        current.push_back(ch);
    }

    if (!current.empty()) {
        arguments.push_back(wide_to_utf8(current));
    }
    if (arguments.empty()) {
        arguments.emplace_back("NisojeStudio");
    }
    return arguments;
}
#endif

struct EmbeddedUiLaunchContext {
    std::string url{};
    std::uint16_t port = 0;
    bool fallback_to_browser = false;
    std::uint64_t startup_timeout_ms = 8000;
    bool devtools_enabled = false;
};

EmbeddedUiLaunchContext resolve_embedded_ui_context(
    const nlp3::platform::PanelApp& app,
    const LaunchOptions& launch_options) {
    EmbeddedUiLaunchContext context{};
    context.devtools_enabled = app.config().embedded_ui_devtools;
    context.fallback_to_browser = app.config().embedded_ui_fallback_to_browser && !launch_options.disable_browser_fallback;
    context.startup_timeout_ms = app.config().embedded_ui_startup_timeout_ms == 0
        ? 8000
        : app.config().embedded_ui_startup_timeout_ms;

    if (launch_options.ui_port_override.has_value()) {
        context.port = *launch_options.ui_port_override;
        context.url = nlp3::platform::build_loopback_ui_url(context.port);
        return context;
    }

    auto parsed_url = nlp3::platform::parse_embedded_ui_url(app.config().embedded_ui_url);
    if (!parsed_url.valid || !parsed_url.loopback) {
        context.port = 18913;
        context.url = nlp3::platform::build_loopback_ui_url(context.port);
        return context;
    }

    context.port = parsed_url.port == 0 ? static_cast<std::uint16_t>(18913) : parsed_url.port;
    context.url = parsed_url.raw_url;
    if (!context.url.empty() && context.url.back() != '/') {
        context.url.push_back('/');
    }
    return context;
}

int run_application(const std::vector<std::string>& arguments) {
#ifdef _WIN32
    enable_dpi_awareness();
#endif
    auto launch_options = parse_launch_options(arguments);
    if (launch_options.console_mode) {
        setup_console_io(true);
    }

    emit_startup_log("runtime_start", "Initializing PanelApp");

    nlp3::platform::PanelApp app;
    if (!app.initialize()) {
        emit_startup_log("runtime_start", "PanelApp initialization failed", false);
#ifdef _WIN32
        if (!launch_options.console_mode) {
            MessageBoxW(nullptr, L"PanelApp initialization failed.", L"Nisoje Studio", MB_OK | MB_ICONERROR);
        }
#endif
        return 1;
    }

    if (!launch_options.ui_mode && !launch_options.console_mode && app.config().embedded_ui_enabled) {
        launch_options.ui_mode = true;
    }

    if (!launch_options.console_mode && !launch_options.ui_mode) {
        return 0;
    }

    if (launch_options.console_mode) {
        print_startup_overview(app);
    }

    if (app.is_external_bridge_mode()) {
        const auto external_ws_port = resolve_external_ws_port(app);
        const auto ws_status = app.external_ws_status();
        if (!ws_status.running || ws_status.port != external_ws_port) {
            if (app.start_external_ws(external_ws_port)) {
                emit_startup_log("runtime_start", "External bridge WS auto-started on port " + std::to_string(external_ws_port));
            } else {
                emit_startup_log("runtime_start", "External bridge WS auto-start failed on port " + std::to_string(external_ws_port), false);
            }
        }
    }

    nlp3::platform::PanelConsole console{&app, &std::cin, &std::cout};
    if (launch_options.console_mode) {
        console.print_overview();
    }

    const auto runtime_loop_started_at = std::chrono::steady_clock::now();
    std::optional<EmbeddedUiLaunchContext> embedded_ui{};
    nlp3::platform::WindowHost window_host;
    nlp3::platform::WebViewHost webview_host;

    if (launch_options.ui_mode) {
        embedded_ui = resolve_embedded_ui_context(app, launch_options);

        if (!app.start_http_ui(embedded_ui->port)) {
            const auto http_status = app.http_ui_status();
            const auto message = http_status.last_error.empty()
                ? std::string{"Failed to start local panel HTTP server"}
                : "Failed to start local panel HTTP server: " + http_status.last_error;
            emit_startup_log("http_server_ready", message, false);
#ifdef _WIN32
            if (!launch_options.console_mode) {
                MessageBoxW(nullptr, utf8_to_wide(message).c_str(), L"Nisoje Studio", MB_OK | MB_ICONERROR);
            }
#endif
            return 1;
        }

        std::string reachability_error{};
        const auto health_ready = nlp3::platform::wait_for_embedded_ui_server_ready(
            embedded_ui->url,
            embedded_ui->startup_timeout_ms,
            [&]() {
                const auto now_ms = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - runtime_loop_started_at).count());
                app.tick(now_ms);
            },
            &reachability_error);
        if (!health_ready) {
            const auto message = "Local panel HTTP server did not become reachable: " + reachability_error;
            emit_startup_log("http_server_ready", message, false);
#ifdef _WIN32
            if (!launch_options.console_mode) {
                MessageBoxW(nullptr, utf8_to_wide(message).c_str(), L"Nisoje Studio", MB_OK | MB_ICONERROR);
            }
#endif
            return 1;
        }
        emit_startup_log("http_server_ready", "HTTP panel ready at " + embedded_ui->url);

        nlp3::platform::WindowHostOptions window_options{};
#ifdef _WIN32
        window_options.icon = load_app_icon();
        window_options.fit_to_monitor_work_area = true;
        window_options.work_area_fill_percent = 97;
#endif
        if (!window_host.create(window_options)) {
            emit_startup_log("webview_init_start", "Native window creation failed", false);
#ifdef _WIN32
            if (!launch_options.console_mode) {
                MessageBoxW(nullptr, L"Native window creation failed.", L"Nisoje Studio", MB_OK | MB_ICONERROR);
            }
#endif
            return 1;
        }
        window_host.show_message_overlay(L"Loading Nisoje Studio...");
#ifdef _WIN32
        window_host.show(SW_SHOWDEFAULT);
#else
        window_host.show();
#endif

        window_host.set_resize_callback([&webview_host](nlp3::platform::WindowHostSize) {
            webview_host.resize();
        });

        emit_startup_log("webview_init_start", "Initializing WebView2 host");

        const nlp3::platform::WebViewHostOptions webview_options{
            utf8_to_wide(embedded_ui->url),
            embedded_ui->devtools_enabled,
            {},
            {},
            embedded_ui->startup_timeout_ms,
            [&]() {
                const auto now_ms = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - runtime_loop_started_at).count());
                app.tick(now_ms);
            },
            [&window_host](const std::string& message) {
                handle_embedded_ui_window_message(message, window_host);
            },
        };

        const auto webview_ready = webview_host.initialize(window_host.hwnd(), webview_options);
        if (webview_ready) {
            emit_startup_log("webview_init_success", "WebView2 initialized");
            emit_startup_log("webview_navigation_start", "Navigating to " + embedded_ui->url);
            emit_startup_log("webview_navigation_success", "Embedded panel navigation succeeded");
            window_host.clear_message_overlay();
        } else {
            const auto webview_status = webview_host.status();
            const auto error_text = wide_to_utf8(webview_status.last_error);
            emit_startup_log("webview_init_start", error_text.empty() ? "WebView2 initialization failed" : error_text, false);

            std::wstring overlay = L"Embedded UI unavailable.\n\n";
            overlay += webview_status.last_error.empty() ? L"WebView2 initialization failed." : webview_status.last_error;
            if (embedded_ui->fallback_to_browser) {
                overlay += L"\n\nOpening system browser fallback...";
            }
            window_host.show_message_overlay(overlay);

            if (embedded_ui->fallback_to_browser && nlp3::platform::open_panel_http_ui_in_browser(embedded_ui->url)) {
                emit_startup_log("webview_fallback_browser", "Opened fallback browser at " + embedded_ui->url);
            }
        }
    }

    std::mutex input_mutex;
    std::queue<std::string> pending_lines;
    bool input_closed = false;
    std::unique_ptr<std::thread> input_thread{};

    if (launch_options.console_mode) {
        input_thread = std::make_unique<std::thread>([&]() {
            std::string line;
            while (std::getline(std::cin, line)) {
                std::scoped_lock lock(input_mutex);
                pending_lines.push(line);
            }

            std::scoped_lock lock(input_mutex);
            input_closed = true;
        });
        input_thread->detach();
    }

    const auto tick_interval = launch_options.ui_mode
        ? std::chrono::milliseconds(16)
        : std::chrono::milliseconds(50);

    bool running = true;
    while (running) {
        if (launch_options.ui_mode && !window_host.pump_messages()) {
            break;
        }

        const auto now = std::chrono::steady_clock::now();
        const auto now_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now - runtime_loop_started_at).count());
        app.tick(now_ms);

        std::queue<std::string> ready_lines;
        bool local_input_closed = false;
        {
            std::scoped_lock lock(input_mutex);
            ready_lines.swap(pending_lines);
            local_input_closed = input_closed;
        }

        if (launch_options.console_mode) {
            while (!ready_lines.empty()) {
                auto line = std::move(ready_lines.front());
                ready_lines.pop();

                const auto trimmed = trim_copy(line);
                if (trimmed == "exit" || trimmed == "quit") {
                    running = false;
                    break;
                }

                if (!console.execute_line(line)) {
                    std::cout << "error: unknown command\n";
                }
            }
        }

        if (!running) {
            break;
        }

        if (launch_options.console_mode && local_input_closed && ready_lines.empty() && !launch_options.ui_mode) {
            break;
        }

        std::this_thread::sleep_for(tick_interval);
    }

    emit_startup_log("shutdown_begin", "Shutting down embedded UI runtime");
    webview_host.close();
    window_host.close();
    app.stop_http_ui();
    emit_startup_log("shutdown_complete", "Nisoje Studio shutdown complete");
    return 0;
}

} // namespace

#ifdef _WIN32

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    return run_application(collect_process_arguments());
}

#else

int main(int argc, char** argv) {
    std::vector<std::string> arguments;
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    return run_application(arguments);
}

#endif
