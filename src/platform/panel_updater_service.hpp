#pragma once

#include <mutex>
#include <string>
#include <thread>

#include "platform/panel_config.hpp"

namespace nlp3::platform {

class PanelUpdaterService {
public:
    PanelUpdaterService();
    ~PanelUpdaterService();

    PanelUpdaterService(const PanelUpdaterService&) = delete;
    PanelUpdaterService& operator=(const PanelUpdaterService&) = delete;

    void start(const PanelAuthConfig& config, const std::string& current_version);
    void stop();
    void update_config(const PanelAuthConfig& config) noexcept;

    std::string current_version() const;
    std::string latest_version() const;
    std::string latest_installer_url() const;

    bool trigger_update();

private:
    void check_worker();

    mutable std::mutex mutex_{};
    PanelAuthConfig config_{};
    std::string current_version_{};
    std::string latest_version_{};
    std::string latest_installer_url_{};
    std::unique_ptr<std::thread> worker_thread_{};
    bool running_ = false;
};

} // namespace nlp3::platform
