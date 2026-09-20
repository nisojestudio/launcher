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
    /// Fase 3 — modo "solo overlay". Un servidor creado con `overlay_only = true`
    /// sirve unicamente `/api/overlay/*` (y `/health`); cualquier otra ruta
    /// responde 404. Es lo que se expone por el tunel publico, para que
    /// `/api/state`, la licencia, las metricas y la UI no salgan a internet.
    PanelHttpServer(PanelApp* app, bool overlay_only) noexcept;
    ~PanelHttpServer();

    bool start(std::uint16_t port = 8080);
    void stop();
    void poll();

    bool running() const noexcept;
    bool overlay_only() const noexcept;
    PanelHttpServerStatus status() const noexcept;

private:
    PanelApp* app_ = nullptr;
    bool overlay_only_ = false;
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
