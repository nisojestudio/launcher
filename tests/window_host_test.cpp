#include <cassert>
#include <iostream>

#include "platform/window_host.hpp"

int main() {
#ifndef _WIN32
    return 0;
#else
    nlp3::platform::WindowHost window_host;
    int resize_notifications = 0;
    window_host.set_resize_callback([&resize_notifications](nlp3::platform::WindowHostSize size) {
        if (size.width > 0 && size.height > 0) {
            ++resize_notifications;
        }
    });

    if (!window_host.create({
            L"Nisoje Studio Test",
            3200,
            2200,
            1800,
            1200,
            nullptr,
        })) {
        std::cerr << "window_host.create failed with GetLastError=" << GetLastError() << '\n';
        return 1;
    }
    window_host.show(SW_HIDE);
    window_host.pump_messages();

    const auto initial_size = window_host.client_size();
    assert(initial_size.width > 0);
    assert(initial_size.height > 0);

    const auto window_style = static_cast<DWORD>(GetWindowLongPtrW(window_host.hwnd(), GWL_STYLE));
    assert((window_style & WS_CAPTION) == 0);
    assert((window_style & WS_THICKFRAME) != 0);
    assert((window_style & WS_MINIMIZEBOX) != 0);
    assert((window_style & WS_MAXIMIZEBOX) != 0);

    RECT window_rect{};
    GetWindowRect(window_host.hwnd(), &window_rect);

    MONITORINFO monitor_info{};
    monitor_info.cbSize = sizeof(monitor_info);
    const auto monitor = MonitorFromWindow(window_host.hwnd(), MONITOR_DEFAULTTOPRIMARY);
    assert(monitor != nullptr);
    assert(GetMonitorInfoW(monitor, &monitor_info) != FALSE);

    const auto work_width = monitor_info.rcWork.right - monitor_info.rcWork.left;
    const auto work_height = monitor_info.rcWork.bottom - monitor_info.rcWork.top;
    const auto window_width = window_rect.right - window_rect.left;
    const auto window_height = window_rect.bottom - window_rect.top;
    assert(window_width <= work_width);
    assert(window_height <= work_height);

    SetWindowPos(
        window_host.hwnd(),
        nullptr,
        0,
        0,
        1500,
        920,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    window_host.pump_messages();
    assert(resize_notifications > 0);

    window_host.show_message_overlay(L"Embedded UI fallback");
    window_host.clear_message_overlay();
    window_host.close();
    assert(!window_host.running());
    return 0;
#endif
}
