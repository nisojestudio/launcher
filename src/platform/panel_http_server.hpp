#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace nlp3::platform {

class PanelApp;

struct PanelHttpServerStatus {
    bool running = false;
    std::uint16_t port = 0;
    std::size_t requests_served = 0;
    std::string last_error{};
};

class PanelHttpServer {
public:
    explicit PanelHttpServer(PanelApp* app) noexcept;
    ~PanelHttpServer();

    bool start(std::uint16_t port = 8080);
    void stop();
    void poll();

    bool running() const noexcept;
    PanelHttpServerStatus status() const noexcept;

private:
    PanelApp* app_ = nullptr;
    void* listen_socket_ = nullptr;
    void* client_socket_ = nullptr;
    std::string request_buffer_{};
    std::string pending_response_{};
    PanelHttpServerStatus status_{};
};

std::string panel_http_ui_url(std::uint16_t port);
bool open_panel_http_ui_in_browser(std::uint16_t port);
bool open_panel_http_ui_in_browser(std::string_view url);

} // namespace nlp3::platform
