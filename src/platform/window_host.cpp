#include "platform/window_host.hpp"

#ifdef _WIN32

#include <algorithm>
#include <string_view>
#include <windowsx.h>

namespace {

constexpr wchar_t kWindowClassName[] = L"NisojeStudio.WindowHost";

RECT resolve_monitor_work_area(HWND preferred_hwnd = nullptr) {
    MONITORINFO monitor_info{};
    monitor_info.cbSize = sizeof(monitor_info);
    RECT fallback{0, 0, 1280, 720};

    HMONITOR monitor = nullptr;
    if (preferred_hwnd != nullptr) {
        monitor = MonitorFromWindow(preferred_hwnd, MONITOR_DEFAULTTONEAREST);
    }
    if (monitor == nullptr) {
        POINT cursor{};
        if (GetCursorPos(&cursor) != FALSE) {
            monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
        }
    }
    if (monitor == nullptr) {
        const auto foreground = GetForegroundWindow();
        if (foreground != nullptr) {
            monitor = MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST);
        }
    }
    if (monitor == nullptr) {
        const POINT primary_anchor{0, 0};
        monitor = MonitorFromPoint(primary_anchor, MONITOR_DEFAULTTOPRIMARY);
    }
    if (monitor != nullptr && GetMonitorInfoW(monitor, &monitor_info) != FALSE) {
        return monitor_info.rcWork;
    }

    return fallback;
}

int clamp_dimension(int preferred, int min_value, int max_value) {
    if (max_value <= 0) {
        return std::max(min_value, preferred);
    }

    const auto effective_min = std::min(min_value, max_value);
    return std::clamp(preferred, effective_min, max_value);
}

RECT build_window_rect_for_client_size(int client_width, int client_height, DWORD style) {
    RECT rect{0, 0, client_width, client_height};
    AdjustWindowRectEx(&rect, style, FALSE, 0);
    return rect;
}

DWORD resolve_window_style(const nlp3::platform::WindowHostOptions& options) {
    if (options.custom_chrome) {
        return WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU;
    }
    return WS_OVERLAPPEDWINDOW;
}

UINT resolve_window_dpi(HWND hwnd) {
    const auto user32 = GetModuleHandleW(L"user32.dll");
    if (user32 != nullptr) {
        using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
        const auto get_dpi_for_window =
            reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(user32, "GetDpiForWindow"));
        if (get_dpi_for_window != nullptr && hwnd != nullptr) {
            return get_dpi_for_window(hwnd);
        }
    }
    return 96;
}

int resolve_resize_border_thickness(HWND hwnd) {
    const auto user32 = GetModuleHandleW(L"user32.dll");
    const auto dpi = resolve_window_dpi(hwnd);
    int frame = GetSystemMetrics(SM_CXSIZEFRAME);
    int padded = GetSystemMetrics(SM_CXPADDEDBORDER);

    if (user32 != nullptr) {
        using GetSystemMetricsForDpiFn = int(WINAPI*)(int, UINT);
        const auto get_system_metrics_for_dpi =
            reinterpret_cast<GetSystemMetricsForDpiFn>(GetProcAddress(user32, "GetSystemMetricsForDpi"));
        if (get_system_metrics_for_dpi != nullptr) {
            frame = get_system_metrics_for_dpi(SM_CXSIZEFRAME, dpi);
            padded = get_system_metrics_for_dpi(SM_CXPADDEDBORDER, dpi);
        }
    }

    return std::max(8, frame + padded);
}

LRESULT resolve_resize_hit_test(HWND hwnd, LPARAM lparam) {
    if (hwnd == nullptr || IsZoomed(hwnd) != FALSE) {
        return HTCLIENT;
    }

    RECT rect{};
    if (GetClientRect(hwnd, &rect) == 0) {
        return HTCLIENT;
    }

    POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
    ScreenToClient(hwnd, &point);

    const auto border = resolve_resize_border_thickness(hwnd);
    const bool left = point.x >= rect.left && point.x < rect.left + border;
    const bool right = point.x < rect.right && point.x >= rect.right - border;
    const bool top = point.y >= rect.top && point.y < rect.top + border;
    const bool bottom = point.y < rect.bottom && point.y >= rect.bottom - border;

    if (top && left) {
        return HTTOPLEFT;
    }
    if (top && right) {
        return HTTOPRIGHT;
    }
    if (bottom && left) {
        return HTBOTTOMLEFT;
    }
    if (bottom && right) {
        return HTBOTTOMRIGHT;
    }
    if (left) {
        return HTLEFT;
    }
    if (right) {
        return HTRIGHT;
    }
    if (top) {
        return HTTOP;
    }
    if (bottom) {
        return HTBOTTOM;
    }
    return HTCLIENT;
}

RECT fit_window_rect_to_work_area(
    int client_width,
    int client_height,
    int work_width,
    int work_height,
    DWORD style) {
    auto fitted_client_width = client_width;
    auto fitted_client_height = client_height;
    auto rect = build_window_rect_for_client_size(fitted_client_width, fitted_client_height, style);

    auto window_width = static_cast<int>(rect.right - rect.left);
    auto window_height = static_cast<int>(rect.bottom - rect.top);

    if (window_width > work_width) {
        fitted_client_width = std::max(320, fitted_client_width - (window_width - work_width));
    }
    if (window_height > work_height) {
        fitted_client_height = std::max(240, fitted_client_height - (window_height - work_height));
    }

    return build_window_rect_for_client_size(fitted_client_width, fitted_client_height, style);
}

ATOM ensure_window_class_registered() {
    static ATOM atom = []() -> ATOM {
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.style = CS_HREDRAW | CS_VREDRAW;
        window_class.lpfnWndProc = &nlp3::platform::WindowHost::window_proc;
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        window_class.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        window_class.lpszClassName = kWindowClassName;
        const auto registered = RegisterClassExW(&window_class);
        if (registered != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
            return 1;
        }
        return 0;
    }();
    return atom;
}

} // namespace

namespace nlp3::platform {

WindowHost::~WindowHost() {
    close();
}

bool WindowHost::create(const WindowHostOptions& options) {
    close();
    options_ = options;

    if (ensure_window_class_registered() == 0) {
        return false;
    }

    const DWORD style = resolve_window_style(options);
    const RECT work_area = resolve_monitor_work_area();
    const auto work_width = std::max(640, static_cast<int>(work_area.right - work_area.left));
    const auto work_height = std::max(520, static_cast<int>(work_area.bottom - work_area.top));
    const auto max_client_width = std::max(640, work_width - 24);
    const auto max_client_height = std::max(520, work_height - 24);
    const auto fill_percent = std::clamp(options.work_area_fill_percent, 84, 100);
    const auto preferred_client_width = options.fit_to_monitor_work_area
        ? std::max(options.min_width, (max_client_width * fill_percent) / 100)
        : options.width;
    const auto preferred_client_height = options.fit_to_monitor_work_area
        ? std::max(options.min_height, (max_client_height * fill_percent) / 100)
        : options.height;
    const auto client_width = clamp_dimension(preferred_client_width, options.min_width, max_client_width);
    const auto client_height = clamp_dimension(preferred_client_height, options.min_height, max_client_height);
    const auto min_client_width = clamp_dimension(options.min_width, 640, max_client_width);
    const auto min_client_height = clamp_dimension(options.min_height, 520, max_client_height);

    const RECT window_rect = fit_window_rect_to_work_area(
        client_width,
        client_height,
        work_width,
        work_height,
        style);
    const RECT min_window_rect = fit_window_rect_to_work_area(
        min_client_width,
        min_client_height,
        work_width,
        work_height,
        style);
    min_track_width_ = min_window_rect.right - min_window_rect.left;
    min_track_height_ = min_window_rect.bottom - min_window_rect.top;

    const auto window_width = static_cast<int>(window_rect.right - window_rect.left);
    const auto window_height = static_cast<int>(window_rect.bottom - window_rect.top);
    const auto start_x = static_cast<int>(work_area.left) + std::max(0, (work_width - window_width) / 2);
    const auto start_y = static_cast<int>(work_area.top) + std::max(0, (work_height - window_height) / 2);

    hwnd_ = CreateWindowExW(
        0,
        kWindowClassName,
        options.title.c_str(),
        style,
        start_x,
        start_y,
        window_width,
        window_height,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        this);
    if (hwnd_ == nullptr) {
        return false;
    }

    if (options.icon != nullptr) {
        SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(options.icon));
        SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(options.icon));
    }

    running_ = true;
    notify_resize();
    return true;
}

void WindowHost::show(int show_command) {
    if (hwnd_ == nullptr) {
        return;
    }

    ShowWindow(hwnd_, show_command);
    UpdateWindow(hwnd_);
}

bool WindowHost::pump_messages() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) {
            running_ = false;
            break;
        }

        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return running_;
}

void WindowHost::close() {
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    overlay_hwnd_ = nullptr;
    running_ = false;
}

void WindowHost::set_resize_callback(ResizeCallback callback) {
    resize_callback_ = std::move(callback);
}

void WindowHost::set_close_callback(CloseCallback callback) {
    close_callback_ = std::move(callback);
}

void WindowHost::show_message_overlay(const std::wstring& message) {
    if (hwnd_ == nullptr) {
        return;
    }

    if (overlay_hwnd_ == nullptr) {
        overlay_hwnd_ = CreateWindowExW(
            0,
            L"STATIC",
            message.c_str(),
            WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE | SS_NOPREFIX,
            0,
            0,
            0,
            0,
            hwnd_,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr);
        if (overlay_hwnd_ == nullptr) {
            return;
        }

        SendMessageW(overlay_hwnd_, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    } else {
        SetWindowTextW(overlay_hwnd_, message.c_str());
        ShowWindow(overlay_hwnd_, SW_SHOW);
    }

    update_overlay_bounds();
}

void WindowHost::clear_message_overlay() {
    if (overlay_hwnd_ != nullptr) {
        ShowWindow(overlay_hwnd_, SW_HIDE);
    }
}

void WindowHost::minimize() const {
    if (hwnd_ != nullptr) {
        ShowWindow(hwnd_, SW_MINIMIZE);
    }
}

void WindowHost::toggle_maximize() const {
    if (hwnd_ == nullptr) {
        return;
    }
    ShowWindow(hwnd_, is_maximized() ? SW_RESTORE : SW_MAXIMIZE);
}

void WindowHost::begin_move_drag() const {
    if (hwnd_ == nullptr) {
        return;
    }
    ReleaseCapture();
    SendMessageW(hwnd_, WM_NCLBUTTONDOWN, HTCAPTION, 0);
}

void WindowHost::request_close() const {
    if (hwnd_ != nullptr) {
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    }
}

HWND WindowHost::hwnd() const noexcept {
    return hwnd_;
}

bool WindowHost::running() const noexcept {
    return running_;
}

WindowHostSize WindowHost::client_size() const noexcept {
    WindowHostSize size{};
    if (hwnd_ == nullptr) {
        return size;
    }

    RECT rect{};
    if (GetClientRect(hwnd_, &rect) == 0) {
        return size;
    }

    size.width = rect.right - rect.left;
    size.height = rect.bottom - rect.top;
    return size;
}

bool WindowHost::is_maximized() const noexcept {
    return hwnd_ != nullptr && IsZoomed(hwnd_) != FALSE;
}

LRESULT CALLBACK WindowHost::window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    WindowHost* self = nullptr;

    if (message == WM_NCCREATE) {
        const auto* create_struct = reinterpret_cast<CREATESTRUCTW*>(lparam);
        self = reinterpret_cast<WindowHost*>(create_struct->lpCreateParams);
        if (self != nullptr) {
            self->hwnd_ = hwnd;
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<WindowHost*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self == nullptr) {
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }

    return self->handle_message(hwnd, message, wparam, lparam);
}

LRESULT WindowHost::handle_message(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_NCCALCSIZE:
        if (options_.custom_chrome) {
            return 0;
        }
        break;

    case WM_NCACTIVATE:
        if (options_.custom_chrome) {
            return TRUE;
        }
        break;

    case WM_NCHITTEST:
        if (options_.custom_chrome) {
            const auto hit = resolve_resize_hit_test(hwnd, lparam);
            if (hit != HTCLIENT) {
                return hit;
            }
        }
        break;

    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
        info->ptMinTrackSize.x = min_track_width_ > 0 ? min_track_width_ : options_.min_width;
        info->ptMinTrackSize.y = min_track_height_ > 0 ? min_track_height_ : options_.min_height;

        MONITORINFO monitor_info{};
        monitor_info.cbSize = sizeof(monitor_info);
        const auto monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
        if (monitor != nullptr && GetMonitorInfoW(monitor, &monitor_info) != FALSE) {
            const RECT work_area = monitor_info.rcWork;
            const RECT monitor_area = monitor_info.rcMonitor;
            info->ptMaxPosition.x = work_area.left - monitor_area.left;
            info->ptMaxPosition.y = work_area.top - monitor_area.top;
            info->ptMaxSize.x = work_area.right - work_area.left;
            info->ptMaxSize.y = work_area.bottom - work_area.top;
        }
        return 0;
    }

    case WM_SIZE:
        update_overlay_bounds();
        notify_resize();
        return 0;

    case WM_DPICHANGED: {
        const auto* suggested_rect = reinterpret_cast<RECT*>(lparam);
        SetWindowPos(
            hwnd_,
            nullptr,
            suggested_rect->left,
            suggested_rect->top,
            suggested_rect->right - suggested_rect->left,
            suggested_rect->bottom - suggested_rect->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        update_overlay_bounds();
        notify_resize();
        return 0;
    }

    case WM_DISPLAYCHANGE: {
        if (IsZoomed(hwnd) != FALSE) {
            update_overlay_bounds();
            notify_resize();
            return 0;
        }

        RECT window_rect{};
        if (GetWindowRect(hwnd, &window_rect) == 0) {
            return 0;
        }

        const RECT work_area = resolve_monitor_work_area(hwnd);
        const auto work_width = std::max(640, static_cast<int>(work_area.right - work_area.left));
        const auto work_height = std::max(520, static_cast<int>(work_area.bottom - work_area.top));
        const auto window_width = std::min(work_width, static_cast<int>(window_rect.right - window_rect.left));
        const auto window_height = std::min(work_height, static_cast<int>(window_rect.bottom - window_rect.top));
        const auto next_x = std::clamp(window_rect.left, work_area.left, work_area.right - window_width);
        const auto next_y = std::clamp(window_rect.top, work_area.top, work_area.bottom - window_height);

        SetWindowPos(
            hwnd,
            nullptr,
            next_x,
            next_y,
            window_width,
            window_height,
            SWP_NOZORDER | SWP_NOACTIVATE);
        update_overlay_bounds();
        notify_resize();
        return 0;
    }

    case WM_CLOSE:
        if (close_callback_) {
            close_callback_();
        }
        DestroyWindow(hwnd_);
        return 0;

    case WM_DESTROY:
        overlay_hwnd_ = nullptr;
        hwnd_ = nullptr;
        running_ = false;
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, message, wparam, lparam);
}

void WindowHost::update_overlay_bounds() const {
    if (hwnd_ == nullptr || overlay_hwnd_ == nullptr) {
        return;
    }

    RECT rect{};
    GetClientRect(hwnd_, &rect);
    SetWindowPos(
        overlay_hwnd_,
        HWND_TOP,
        0,
        0,
        rect.right - rect.left,
        rect.bottom - rect.top,
        SWP_NOACTIVATE);
}

void WindowHost::notify_resize() const {
    if (!resize_callback_) {
        return;
    }

    resize_callback_(client_size());
}

} // namespace nlp3::platform

#else

namespace nlp3::platform {

WindowHost::~WindowHost() = default;

bool WindowHost::create(const WindowHostOptions&) {
    return false;
}

void WindowHost::show(int) {
}

bool WindowHost::pump_messages() {
    return false;
}

void WindowHost::close() {
}

void WindowHost::set_resize_callback(ResizeCallback callback) {
    resize_callback_ = std::move(callback);
}

void WindowHost::set_close_callback(CloseCallback callback) {
    close_callback_ = std::move(callback);
}

void WindowHost::show_message_overlay(const std::wstring&) {
}

void WindowHost::clear_message_overlay() {
}

void WindowHost::minimize() const {
}

void WindowHost::toggle_maximize() const {
}

void WindowHost::begin_move_drag() const {
}

void WindowHost::request_close() const {
}

HWND WindowHost::hwnd() const noexcept {
    return nullptr;
}

bool WindowHost::running() const noexcept {
    return false;
}

WindowHostSize WindowHost::client_size() const noexcept {
    return {};
}

bool WindowHost::is_maximized() const noexcept {
    return false;
}

} // namespace nlp3::platform

#endif
