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

    void* process_handle_ = nullptr;
    void* stdout_read_ = nullptr;
    void* stdout_write_ = nullptr;
    std::unique_ptr<std::thread> reader_thread_;
    std::string tunnel_url_;
    std::string last_error_;
    bool running_ = false;
};

} // namespace nlp3::platform
