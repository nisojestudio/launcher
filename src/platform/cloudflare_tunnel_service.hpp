#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace nlp3::platform {

class CloudflareTunnelService {
public:
    using TunnelUrlCallback = std::function<void(const std::string& url)>;

    CloudflareTunnelService();
    ~CloudflareTunnelService();

    CloudflareTunnelService(const CloudflareTunnelService&) = delete;
    CloudflareTunnelService& operator=(const CloudflareTunnelService&) = delete;

    /// Arranca cloudflared apuntando al puerto local indicado. Fase 3: ese puerto
    /// es el listener *solo overlay* del panel, no el puerto de su UI, para que
    /// el tunel no exponga /api/state, licencia ni metricas.
    bool start_tunnel(std::uint16_t port, TunnelUrlCallback on_url);
    void stop_tunnel();
    bool is_running() const noexcept;

    /// URL publica base del tunel (`https://xxx.trycloudflare.com`), SIN ninguna
    /// ruta. Un servicio de tunel generico no debe conocer la ruta de un juego:
    /// quien la compone es la capa que si sabe de overlays (PanelApp).
    std::string public_base_url() const noexcept;
    std::string last_error() const noexcept;

private:
    void reader_thread(std::uint16_t port);
    bool is_process_alive() const noexcept;
    std::uint16_t port_ = 0;
    std::uint16_t overlay_port_ = 0;

    void* process_handle_ = nullptr;
    void* stdout_read_ = nullptr;
    void* stdout_write_ = nullptr;
    std::unique_ptr<std::thread> reader_thread_;
    mutable std::mutex mutex_;
    std::string public_base_url_;
    std::string last_error_;
    bool running_ = false;
    TunnelUrlCallback on_url_callback_;
};

} // namespace nlp3::platform
