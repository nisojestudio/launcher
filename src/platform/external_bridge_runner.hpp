#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nlp3::platform {

struct ExternalBridgeRunnerStartRequest {
    std::string target_user{};
    std::string ws_url{};
    std::uint16_t control_port = 0;
    std::uint64_t max_seconds = 0;
};

struct ExternalBridgeRunnerStatus {
    bool running = false;
    std::uint32_t process_id = 0;
    std::string target_user{};
    std::string ws_url{};
    bool runtime_checked = false;
    bool runtime_ready = false;
    std::int64_t runtime_checked_timestamp_ms = 0;
    std::string runtime_summary{};
    std::vector<std::string> runtime_alerts{};
    bool has_exit_code = false;
    std::int32_t last_exit_code = 0;
    std::string last_error{};
    std::vector<std::string> recent_log_lines{};
};

class ExternalBridgeRunner {
public:
    ExternalBridgeRunner() noexcept;
    ~ExternalBridgeRunner();

    bool start(const ExternalBridgeRunnerStartRequest& request);
    void stop();
    void poll();
    void refresh_runtime_status(bool force = false);

    ExternalBridgeRunnerStatus status() const noexcept;

private:
    void reset_process_handles() noexcept;

    ExternalBridgeRunnerStatus status_{};
    void* process_handle_ = nullptr;
    void* thread_handle_ = nullptr;
    void* stdout_read_handle_ = nullptr;
    void* stderr_read_handle_ = nullptr;
    bool stop_requested_by_panel_ = false;
    std::uint16_t control_port_ = 0;
    std::string stdout_partial_buffer_{};
    std::string stderr_partial_buffer_{};
};

} // namespace nlp3::platform
