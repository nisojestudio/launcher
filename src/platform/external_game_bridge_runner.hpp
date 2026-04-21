#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nlp3::platform {

struct ExternalGameBridgeStartRequest {
    std::string game_id{};
    std::string game_root{};
};

struct ExternalGameBridgeRunnerStatus {
    bool running = false;
    std::uint32_t process_id = 0;
    std::string game_id{};
    std::string game_root{};
    bool has_exit_code = false;
    std::int32_t last_exit_code = 0;
    std::string last_error{};
    std::vector<std::string> recent_log_lines{};
};

class ExternalGameBridgeRunner {
public:
    ExternalGameBridgeRunner() noexcept;
    ~ExternalGameBridgeRunner();

    bool start(const ExternalGameBridgeStartRequest& request);
    void stop();
    void poll();

    ExternalGameBridgeRunnerStatus status() const noexcept;

private:
    void reset_process_handles() noexcept;

    ExternalGameBridgeRunnerStatus status_{};
    void* process_handle_ = nullptr;
    void* thread_handle_ = nullptr;
    void* stdout_read_handle_ = nullptr;
    void* stderr_read_handle_ = nullptr;
    bool stop_requested_by_panel_ = false;
    std::string stdout_partial_buffer_{};
    std::string stderr_partial_buffer_{};
};

} // namespace nlp3::platform
