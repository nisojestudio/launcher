#include "platform/webview_host.hpp"

#ifdef _WIN32

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <thread>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <wrl.h>
#include <wrl/client.h>

#include "WebView2.h"

namespace {

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

using CreateCoreWebView2EnvironmentWithOptionsFn = HRESULT(STDAPICALLTYPE*)(
    PCWSTR,
    PCWSTR,
    ICoreWebView2EnvironmentOptions*,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);

bool ensure_winsock_initialized() {
    static const bool initialized = []() {
        WSADATA wsa_data{};
        return WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0;
    }();
    return initialized;
}

std::wstring utf8_to_wide(std::string_view value) {
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
}

std::string wide_to_utf8(const std::wstring& value) {
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
}

std::wstring module_directory_path() {
    std::array<wchar_t, MAX_PATH> buffer{};
    const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return {};
    }

    std::filesystem::path path(buffer.data());
    return path.parent_path().wstring();
}

std::wstring default_loader_dll_path() {
    auto module_dir = module_directory_path();
    if (module_dir.empty()) {
        return L"WebView2Loader.dll";
    }
    return (std::filesystem::path(module_dir) / "WebView2Loader.dll").wstring();
}

std::wstring default_user_data_dir() {
    wchar_t buffer[MAX_PATH]{};
    const auto length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, static_cast<DWORD>(std::size(buffer)));
    std::filesystem::path base_path;
    if (length > 0 && length < std::size(buffer)) {
        base_path = buffer;
    } else {
        base_path = std::filesystem::temp_directory_path();
    }

    return (base_path / "PanelLive3" / "WebView2").wstring();
}

std::wstring format_hresult(HRESULT hr) {
    std::wostringstream output;
    output << L"0x" << std::hex << std::uppercase << static_cast<unsigned long>(hr);
    return output.str();
}

std::wstring navigation_error_text(COREWEBVIEW2_WEB_ERROR_STATUS status) {
    switch (status) {
    case COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN:
        return L"unknown";
    case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_COMMON_NAME_IS_INCORRECT:
        return L"certificate_common_name_incorrect";
    case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_EXPIRED:
        return L"certificate_expired";
    case COREWEBVIEW2_WEB_ERROR_STATUS_CLIENT_CERTIFICATE_CONTAINS_ERRORS:
        return L"client_certificate_error";
    case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_REVOKED:
        return L"certificate_revoked";
    case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_IS_INVALID:
        return L"certificate_invalid";
    case COREWEBVIEW2_WEB_ERROR_STATUS_SERVER_UNREACHABLE:
        return L"server_unreachable";
    case COREWEBVIEW2_WEB_ERROR_STATUS_TIMEOUT:
        return L"timeout";
    case COREWEBVIEW2_WEB_ERROR_STATUS_ERROR_HTTP_INVALID_SERVER_RESPONSE:
        return L"http_invalid_server_response";
    case COREWEBVIEW2_WEB_ERROR_STATUS_CONNECTION_ABORTED:
        return L"connection_aborted";
    case COREWEBVIEW2_WEB_ERROR_STATUS_CONNECTION_RESET:
        return L"connection_reset";
    case COREWEBVIEW2_WEB_ERROR_STATUS_DISCONNECTED:
        return L"disconnected";
    case COREWEBVIEW2_WEB_ERROR_STATUS_CANNOT_CONNECT:
        return L"cannot_connect";
    case COREWEBVIEW2_WEB_ERROR_STATUS_HOST_NAME_NOT_RESOLVED:
        return L"host_name_not_resolved";
    case COREWEBVIEW2_WEB_ERROR_STATUS_OPERATION_CANCELED:
        return L"operation_canceled";
    case COREWEBVIEW2_WEB_ERROR_STATUS_REDIRECT_FAILED:
        return L"redirect_failed";
    case COREWEBVIEW2_WEB_ERROR_STATUS_UNEXPECTED_ERROR:
        return L"unexpected_error";
    default:
        return L"web_error_" + std::to_wstring(static_cast<int>(status));
    }
}

bool pump_windows_messages_once() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) {
            return false;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return true;
}

bool probe_http_health(
    std::string_view url,
    const std::function<void()>& pump_callback,
    std::string* last_error) {
    if (!ensure_winsock_initialized()) {
        if (last_error != nullptr) {
            *last_error = "winsock startup failed";
        }
        return false;
    }

    const auto parsed = nlp3::platform::parse_embedded_ui_url(url);
    if (!parsed.valid || !parsed.loopback) {
        if (last_error != nullptr) {
            *last_error = "embedded ui url must be a valid loopback http url";
        }
        return false;
    }

    SOCKET socket_handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_handle == INVALID_SOCKET) {
        if (last_error != nullptr) {
            *last_error = "socket create failed";
        }
        return false;
    }

    const DWORD timeout_ms = 40;
    setsockopt(
        socket_handle,
        SOL_SOCKET,
        SO_RCVTIMEO,
        reinterpret_cast<const char*>(&timeout_ms),
        sizeof(timeout_ms));
    setsockopt(
        socket_handle,
        SOL_SOCKET,
        SO_SNDTIMEO,
        reinterpret_cast<const char*>(&timeout_ms),
        sizeof(timeout_ms));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(parsed.port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    const auto connect_result = connect(
        socket_handle,
        reinterpret_cast<const sockaddr*>(&address),
        sizeof(address));
    if (connect_result == SOCKET_ERROR) {
        closesocket(socket_handle);
        if (last_error != nullptr) {
            *last_error = "connect failed";
        }
        return false;
    }

    const auto request_path = parsed.path.empty() ? "/" : parsed.path;
    const auto request = "GET " + request_path + "health HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Connection: close\r\n\r\n";
    const auto send_result = send(
        socket_handle,
        request.data(),
        static_cast<int>(request.size()),
        0);
    if (send_result != static_cast<int>(request.size())) {
        closesocket(socket_handle);
        if (last_error != nullptr) {
            *last_error = "send failed";
        }
        return false;
    }

    std::array<char, 256> buffer{};
    int received = SOCKET_ERROR;
    for (int attempt = 0; attempt < 50; ++attempt) {
        if (pump_callback) {
            pump_callback();
        }

        received = recv(socket_handle, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (received > 0 || received == 0) {
            break;
        }

        const auto error_code = WSAGetLastError();
        if (error_code != WSAEWOULDBLOCK && error_code != WSAETIMEDOUT) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    closesocket(socket_handle);
    if (received <= 0) {
        if (last_error != nullptr) {
            *last_error = "recv failed";
        }
        return false;
    }

    std::string response(buffer.data(), static_cast<std::size_t>(received));
    if (response.find("HTTP/1.1 200 OK") == std::string::npos) {
        if (last_error != nullptr) {
            *last_error = "health endpoint returned non-200";
        }
        return false;
    }

    return true;
}

} // namespace

namespace nlp3::platform {

struct WebViewHost::Impl {
    HMODULE loader_module = nullptr;
    HWND parent_window = nullptr;
    bool com_initialized = false;
    CreateCoreWebView2EnvironmentWithOptionsFn create_environment_with_options = nullptr;
    std::function<void()> pump_callback{};
    std::function<void(const std::string&)> web_message_callback{};
    ComPtr<ICoreWebView2Environment> environment;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;
    EventRegistrationToken navigation_completed_token{};
    bool navigation_handler_registered = false;
    EventRegistrationToken web_message_received_token{};
    bool web_message_handler_registered = false;
};

EmbeddedUiUrl parse_embedded_ui_url(std::string_view url) {
    EmbeddedUiUrl parsed{};
    parsed.raw_url = std::string(url);
    const auto scheme_separator = url.find("://");
    if (scheme_separator == std::string_view::npos) {
        return parsed;
    }

    parsed.scheme = std::string(url.substr(0, scheme_separator));
    auto remainder = url.substr(scheme_separator + 3);
    const auto path_separator = remainder.find('/');
    std::string_view authority = remainder;
    if (path_separator != std::string_view::npos) {
        authority = remainder.substr(0, path_separator);
        parsed.path = std::string(remainder.substr(path_separator));
    }

    const auto port_separator = authority.rfind(':');
    if (port_separator == std::string_view::npos) {
        parsed.host = std::string(authority);
        parsed.port = parsed.scheme == "https" ? static_cast<std::uint16_t>(443) : static_cast<std::uint16_t>(80);
    } else {
        parsed.host = std::string(authority.substr(0, port_separator));
        try {
            std::size_t consumed = 0;
            const auto parsed_port = std::stoull(std::string(authority.substr(port_separator + 1)), &consumed);
            if (consumed != authority.size() - port_separator - 1 || parsed_port > 65535) {
                return parsed;
            }
            parsed.port = static_cast<std::uint16_t>(parsed_port);
        } catch (...) {
            return parsed;
        }
    }

    if (parsed.path.empty()) {
        parsed.path = "/";
    } else if (parsed.path.back() != '/') {
        parsed.path += '/';
    }

    parsed.loopback = parsed.host == "127.0.0.1" || parsed.host == "localhost";
    parsed.valid = (parsed.scheme == "http" || parsed.scheme == "https") && !parsed.host.empty();
    return parsed;
}

std::string build_loopback_ui_url(std::uint16_t port, std::string_view path) {
    std::string normalized_path = path.empty() ? "/" : std::string(path);
    if (normalized_path.front() != '/') {
        normalized_path.insert(normalized_path.begin(), '/');
    }
    if (normalized_path.back() != '/') {
        normalized_path.push_back('/');
    }
    return "http://127.0.0.1:" + std::to_string(port) + normalized_path;
}

bool wait_for_embedded_ui_server_ready(
    std::string_view url,
    std::uint64_t timeout_ms,
    const std::function<void()>& pump_callback,
    std::string* last_error) {
    const auto start = std::chrono::steady_clock::now();
    std::string probe_error = "startup timeout";
    while (true) {
        if (pump_callback) {
            pump_callback();
        }

        if (probe_http_health(url, pump_callback, &probe_error)) {
            if (last_error != nullptr) {
                last_error->clear();
            }
            return true;
        }

        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        if (elapsed.count() >= static_cast<long long>(timeout_ms)) {
            if (last_error != nullptr) {
                *last_error = probe_error;
            }
            return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
}

WebViewHost::WebViewHost()
    : impl_(std::make_unique<Impl>()) {
}

WebViewHost::~WebViewHost() {
    close();
}

bool WebViewHost::initialize(HWND parent_window, const WebViewHostOptions& options) {
    close();
    status_ = {};
    status_.initial_url = options.initial_url;
    status_.devtools_enabled = options.devtools_enabled;
    impl_->pump_callback = options.pump_callback;
    impl_->web_message_callback = options.web_message_callback;

    if (parent_window == nullptr) {
        set_last_error(L"parent window is null");
        return false;
    }

    if (!ensure_com_initialized()) {
        return false;
    }
    if (!load_loader_library(options.loader_dll_path)) {
        return false;
    }
    if (!create_environment(options)) {
        return false;
    }
    if (!create_controller(parent_window, options.navigation_timeout_ms)) {
        return false;
    }
    if (!configure_settings()) {
        return false;
    }
    if (!navigate_with_retry(options.initial_url, options.navigation_timeout_ms)) {
        return false;
    }

    status_.initialized = true;
    status_.runtime_available = true;
    return true;
}

void WebViewHost::resize() {
    if (impl_ == nullptr || impl_->controller == nullptr) {
        return;
    }

    const auto bounds = current_parent_bounds();
    impl_->controller->put_Bounds(bounds);
}

void WebViewHost::close() {
    if (impl_ == nullptr) {
        return;
    }

    if (impl_->webview != nullptr && impl_->navigation_handler_registered) {
        impl_->webview->remove_NavigationCompleted(impl_->navigation_completed_token);
        impl_->navigation_handler_registered = false;
    }
    if (impl_->webview != nullptr && impl_->web_message_handler_registered) {
        impl_->webview->remove_WebMessageReceived(impl_->web_message_received_token);
        impl_->web_message_handler_registered = false;
    }
    impl_->webview.Reset();

    if (impl_->controller != nullptr) {
        impl_->controller->Close();
    }
    impl_->controller.Reset();
    impl_->environment.Reset();
    impl_->parent_window = nullptr;

    if (impl_->loader_module != nullptr) {
        FreeLibrary(impl_->loader_module);
        impl_->loader_module = nullptr;
    }

    if (impl_->com_initialized) {
        CoUninitialize();
        impl_->com_initialized = false;
    }
}

bool WebViewHost::ready() const noexcept {
    return status_.initialized && status_.navigation_succeeded;
}

WebViewHostStatus WebViewHost::status() const {
    return status_;
}

bool WebViewHost::ensure_com_initialized() {
    if (impl_ == nullptr) {
        return false;
    }
    if (impl_->com_initialized) {
        return true;
    }

    const auto hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        set_last_error(L"CoInitializeEx failed: " + format_hresult(hr));
        return false;
    }

    impl_->com_initialized = (hr == S_OK || hr == S_FALSE);
    return true;
}

bool WebViewHost::load_loader_library(const std::wstring& explicit_path) {
    if (impl_ == nullptr) {
        return false;
    }

    const auto loader_path = explicit_path.empty() ? default_loader_dll_path() : explicit_path;
    impl_->loader_module = LoadLibraryW(loader_path.c_str());
    if (impl_->loader_module == nullptr && explicit_path.empty()) {
        impl_->loader_module = LoadLibraryW(L"WebView2Loader.dll");
    }
    if (impl_->loader_module == nullptr) {
        set_last_error(L"WebView2Loader.dll not found");
        return false;
    }

    impl_->create_environment_with_options =
        reinterpret_cast<CreateCoreWebView2EnvironmentWithOptionsFn>(
            GetProcAddress(impl_->loader_module, "CreateCoreWebView2EnvironmentWithOptions"));
    if (impl_->create_environment_with_options == nullptr) {
        set_last_error(L"CreateCoreWebView2EnvironmentWithOptions not found in loader");
        return false;
    }

    return true;
}

bool WebViewHost::create_environment(const WebViewHostOptions& options) {
    if (impl_ == nullptr || impl_->create_environment_with_options == nullptr) {
        return false;
    }

    const auto user_data_dir = options.user_data_dir.empty() ? default_user_data_dir() : options.user_data_dir;
    std::error_code error_code;
    std::filesystem::create_directories(user_data_dir, error_code);

    HRESULT create_hr = E_FAIL;
    bool completed = false;

    const auto callback = Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
        [this, &create_hr, &completed](
            HRESULT result,
            ICoreWebView2Environment* environment) -> HRESULT {
            create_hr = result;
            if (SUCCEEDED(result) && impl_ != nullptr) {
                impl_->environment = environment;
            }
            completed = true;
            return S_OK;
        });

    const auto hr = impl_->create_environment_with_options(
        nullptr,
        user_data_dir.c_str(),
        nullptr,
        callback.Get());
    if (FAILED(hr)) {
        set_last_error(L"CreateCoreWebView2EnvironmentWithOptions failed: " + format_hresult(hr));
        return false;
    }

    if (!pump_until([&completed]() { return completed; }, options.navigation_timeout_ms)) {
        set_last_error(L"WebView2 environment creation timed out");
        return false;
    }
    if (FAILED(create_hr) || impl_->environment == nullptr) {
        set_last_error(L"WebView2 environment creation failed: " + format_hresult(create_hr));
        return false;
    }

    status_.runtime_available = true;
    return true;
}

bool WebViewHost::create_controller(HWND parent_window, std::uint64_t timeout_ms) {
    if (impl_ == nullptr || impl_->environment == nullptr) {
        return false;
    }

    impl_->parent_window = parent_window;

    HRESULT controller_hr = E_FAIL;
    bool completed = false;
    const auto callback = Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
        [this, &controller_hr, &completed](
            HRESULT result,
            ICoreWebView2Controller* controller) -> HRESULT {
            controller_hr = result;
            if (SUCCEEDED(result) && impl_ != nullptr) {
                impl_->controller = controller;
                if (controller != nullptr) {
                    controller->get_CoreWebView2(&impl_->webview);
                }
            }
            completed = true;
            return S_OK;
        });

    const auto hr = impl_->environment->CreateCoreWebView2Controller(parent_window, callback.Get());
    if (FAILED(hr)) {
        set_last_error(L"CreateCoreWebView2Controller failed: " + format_hresult(hr));
        return false;
    }

    if (!pump_until([&completed]() { return completed; }, timeout_ms)) {
        set_last_error(L"WebView2 controller creation timed out");
        return false;
    }
    if (FAILED(controller_hr) || impl_->controller == nullptr || impl_->webview == nullptr) {
        set_last_error(L"WebView2 controller creation failed: " + format_hresult(controller_hr));
        return false;
    }

    resize();
    impl_->controller->put_IsVisible(TRUE);

    const auto message_handler = Callback<ICoreWebView2WebMessageReceivedEventHandler>(
        [this](
            ICoreWebView2*,
            ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
            if (args == nullptr || impl_ == nullptr || !impl_->web_message_callback) {
                return S_OK;
            }

            LPWSTR raw_message = nullptr;
            const auto message_hr = args->TryGetWebMessageAsString(&raw_message);
            if (FAILED(message_hr) || raw_message == nullptr) {
                return S_OK;
            }

            const std::wstring scoped_message(raw_message);
            CoTaskMemFree(raw_message);
            impl_->web_message_callback(wide_to_utf8(scoped_message));
            return S_OK;
        });

    const auto add_message_hr =
        impl_->webview->add_WebMessageReceived(message_handler.Get(), &impl_->web_message_received_token);
    if (FAILED(add_message_hr)) {
        set_last_error(L"WebView2 web message handler registration failed: " + format_hresult(add_message_hr));
        return false;
    }
    impl_->web_message_handler_registered = true;
    return true;
}

bool WebViewHost::configure_settings() {
    if (impl_ == nullptr || impl_->webview == nullptr) {
        return false;
    }

    ComPtr<ICoreWebView2Settings> settings;
    const auto hr = impl_->webview->get_Settings(&settings);
    if (FAILED(hr) || settings == nullptr) {
        set_last_error(L"WebView2 settings unavailable: " + format_hresult(hr));
        return false;
    }

    settings->put_AreDevToolsEnabled(status_.devtools_enabled ? TRUE : FALSE);
    settings->put_AreDefaultContextMenusEnabled(status_.devtools_enabled ? TRUE : FALSE);
    settings->put_IsStatusBarEnabled(FALSE);

    ComPtr<ICoreWebView2Controller2> controller2;
    if (SUCCEEDED(impl_->controller.As(&controller2)) && controller2 != nullptr) {
        controller2->put_DefaultBackgroundColor(COREWEBVIEW2_COLOR{0xFF, 0x0B, 0x12, 0x20});
    }

    return true;
}

bool WebViewHost::navigate_with_retry(const std::wstring& url, std::uint64_t timeout_ms) {
    if (impl_ == nullptr || impl_->webview == nullptr) {
        return false;
    }

    status_.initial_url = url;

    bool navigation_completed = false;
    bool navigation_succeeded = false;
    COREWEBVIEW2_WEB_ERROR_STATUS web_error_status = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;

    const auto handler = Callback<ICoreWebView2NavigationCompletedEventHandler>(
        [&navigation_completed, &navigation_succeeded, &web_error_status](
            ICoreWebView2*,
            ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
            navigation_completed = true;
            if (args != nullptr) {
                BOOL is_success = FALSE;
                args->get_IsSuccess(&is_success);
                navigation_succeeded = is_success != FALSE;
                args->get_WebErrorStatus(&web_error_status);
            }
            return S_OK;
        });

    const auto add_handler_hr =
        impl_->webview->add_NavigationCompleted(handler.Get(), &impl_->navigation_completed_token);
    if (FAILED(add_handler_hr)) {
        set_last_error(L"WebView2 navigation handler registration failed: " + format_hresult(add_handler_hr));
        return false;
    }
    impl_->navigation_handler_registered = true;

    const auto started_at = std::chrono::steady_clock::now();
    do {
        navigation_completed = false;
        navigation_succeeded = false;
        web_error_status = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;

        const auto hr = impl_->webview->Navigate(url.c_str());
        if (FAILED(hr)) {
            set_last_error(L"WebView2 Navigate failed: " + format_hresult(hr));
            return false;
        }

        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started_at);
        const auto remaining = elapsed.count() >= static_cast<long long>(timeout_ms)
            ? 0
            : timeout_ms - static_cast<std::uint64_t>(elapsed.count());
        const auto attempt_timeout = std::min<std::uint64_t>(remaining, 1500);
        if (attempt_timeout == 0) {
            break;
        }

        if (pump_until([&navigation_completed]() { return navigation_completed; }, attempt_timeout)
            && navigation_succeeded) {
            status_.navigation_succeeded = true;
            status_.last_error.clear();
            return true;
        }
    } while (std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::steady_clock::now() - started_at).count()
             < static_cast<long long>(timeout_ms));

    set_last_error(L"WebView2 navigation failed: " + navigation_error_text(web_error_status));
    return false;
}

bool WebViewHost::pump_until(const std::function<bool()>& predicate, std::uint64_t timeout_ms) {
    const auto started_at = std::chrono::steady_clock::now();
    while (!predicate()) {
        if (impl_ != nullptr && impl_->pump_callback) {
            impl_->pump_callback();
        }
        if (!pump_windows_messages_once()) {
            return false;
        }

        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started_at);
        if (elapsed.count() >= static_cast<long long>(timeout_ms)) {
            return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return true;
}

void WebViewHost::set_last_error(std::wstring message) {
    status_.last_error = std::move(message);
    status_.navigation_succeeded = false;
}

RECT WebViewHost::current_parent_bounds() const {
    RECT bounds{};
    if (impl_ == nullptr || impl_->parent_window == nullptr) {
        return bounds;
    }

    GetClientRect(impl_->parent_window, &bounds);
    return bounds;
}

} // namespace nlp3::platform

#else

namespace nlp3::platform {

EmbeddedUiUrl parse_embedded_ui_url(std::string_view url) {
    EmbeddedUiUrl parsed{};
    parsed.raw_url = std::string(url);
    return parsed;
}

std::string build_loopback_ui_url(std::uint16_t port, std::string_view path) {
    (void)port;
    (void)path;
    return {};
}

bool wait_for_embedded_ui_server_ready(
    std::string_view,
    std::uint64_t,
    const std::function<void()>&,
    std::string* last_error) {
    if (last_error != nullptr) {
        *last_error = "embedded ui is only available on windows";
    }
    return false;
}

struct WebViewHost::Impl {};

WebViewHost::WebViewHost()
    : impl_(std::make_unique<Impl>()) {
}

WebViewHost::~WebViewHost() = default;

bool WebViewHost::initialize(HWND, const WebViewHostOptions&) {
    status_.last_error = L"WebView2 is only available on Windows";
    return false;
}

void WebViewHost::resize() {
}

void WebViewHost::close() {
}

bool WebViewHost::ready() const noexcept {
    return false;
}

WebViewHostStatus WebViewHost::status() const {
    return status_;
}

} // namespace nlp3::platform

#endif
