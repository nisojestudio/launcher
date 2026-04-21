#pragma once

#include <functional>
#include <string>

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
using HICON = void*;
#endif

namespace nlp3::platform {

struct WindowHostSize {
    int width = 0;
    int height = 0;
};

struct WindowHostOptions {
    std::wstring title = L"Nisoje Studio";
    int width = 1460;
    int height = 880;
    int min_width = 1100;
    int min_height = 680;
    HICON icon = nullptr;
    bool custom_chrome = true;
    bool fit_to_monitor_work_area = true;
    int work_area_fill_percent = 97;
};

class WindowHost {
public:
    using ResizeCallback = std::function<void(WindowHostSize)>;
    using CloseCallback = std::function<void()>;

    WindowHost() = default;
    ~WindowHost();

    bool create(const WindowHostOptions& options = {});
    void show(int show_command = 1);
    bool pump_messages();
    void close();

    void set_resize_callback(ResizeCallback callback);
    void set_close_callback(CloseCallback callback);

    void show_message_overlay(const std::wstring& message);
    void clear_message_overlay();
    void minimize() const;
    void toggle_maximize() const;
    void begin_move_drag() const;
    void request_close() const;

    [[nodiscard]] HWND hwnd() const noexcept;
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] WindowHostSize client_size() const noexcept;
    [[nodiscard]] bool is_maximized() const noexcept;

#ifdef _WIN32
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
#endif

private:
#ifdef _WIN32
    LRESULT handle_message(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    void update_overlay_bounds() const;
    void notify_resize() const;
#endif

    WindowHostOptions options_{};
    ResizeCallback resize_callback_{};
    CloseCallback close_callback_{};
    HWND hwnd_ = nullptr;
    HWND overlay_hwnd_ = nullptr;
    int min_track_width_ = 0;
    int min_track_height_ = 0;
    bool running_ = false;
};

} // namespace nlp3::platform
