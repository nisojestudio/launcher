#pragma once

#include <cstdint>
#include <functional>
#include <memory>
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

    bool start_tunnel(std::uint16_t port, TunnelUrlCallback on_url);
    void stop_tunnel();
    bool is_running() const noexcept;
    std::string tunnel_url() const noexcept;
    std::string last_error() const noexcept;

private:
    void reader_thread(std::uint16_t port);
    void watchdog_thread(std::uint16_t port, TunnelUrlCallback on_url);
    bool is_process_alive() const noexcept;
    void restart_tunnel(std::uint16_t port, TunnelUrlCallback on_url);
    std::uint16_t port_ = 0;

    void* process_handle_ = nullptr;
    void* stdout_read_ = nullptr;
    void* stdout_write_ = nullptr;
    std::unique_ptr<std::thread> reader_thread_;
    std::unique_ptr<std::thread> watchdog_thread_;
    mutable std::mutex mutex_;
    std::string tunnel_url_;
    std::string last_error_;
    bool running_ = false;
    TunnelUrlCallback on_url_callback_;
};

} // namespace nlp3::platform
