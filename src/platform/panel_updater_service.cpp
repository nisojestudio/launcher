#include "platform/panel_updater_service.hpp"

#include <chrono>
#include <cstdlib>
#include <regex>
#include <string>

#include <nlohmann/json.hpp>

#include "platform/win_http_client.hpp"

namespace {

std::string trim_copy(const std::string& s) {
    const auto start = s.find_first_not_of(" \t\n\r\f\v");
    if (start == std::string::npos) return {};
    const auto end = s.find_last_not_of(" \t\n\r\f\v");
    return s.substr(start, end - start + 1);
}

} // anonymous namespace

namespace nlp3::platform {

PanelUpdaterService::PanelUpdaterService() = default;

PanelUpdaterService::~PanelUpdaterService() {
    stop();
}

void PanelUpdaterService::start(const PanelAuthConfig& config, const std::string& current_version) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    current_version_ = current_version;
    if (running_) {
        return;
    }
    running_ = true;
    worker_thread_ = std::make_unique<std::thread>(&PanelUpdaterService::check_worker, this);
}

void PanelUpdaterService::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
    }
    if (worker_thread_ && worker_thread_->joinable()) {
        worker_thread_->join();
        worker_thread_.reset();
    }
}

void PanelUpdaterService::update_config(const PanelAuthConfig& config) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
}

std::string PanelUpdaterService::current_version() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_version_;
}

std::string PanelUpdaterService::latest_version() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return latest_version_;
}

std::string PanelUpdaterService::latest_installer_url() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return latest_installer_url_;
}

void PanelUpdaterService::check_worker() {
    for (;;) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!running_) {
                return;
            }
        }

        std::string api_base;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            api_base = trim_copy(config_.nisoje_api_base);
        }
        if (api_base.empty()) {
            api_base = "https://nisoje.com";
        }

        const auto version_url = api_base + "/api/version/latest";
        const auto response = http_request("GET", version_url, {}, {}, {});

        if (response.error.empty() && response.status_code >= 200 && response.status_code < 300) {
            const auto parsed = nlohmann::json::parse(response.body, nullptr, false);
            if (!parsed.is_discarded() && parsed.is_object()) {
                std::lock_guard<std::mutex> lock(mutex_);
                const auto ver = parsed.value("latest_version", std::string{});
                const auto url = parsed.value("installer_url", std::string{});
                if (!ver.empty()) {
                    latest_version_ = ver;
                }
                if (!url.empty()) {
                    latest_installer_url_ = url;
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::hours(6));
    }
}

bool PanelUpdaterService::trigger_update() {
    std::string installer_url;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        installer_url = latest_installer_url_;
    }

    if (installer_url.empty()) {
        return false;
    }

    const auto temp_dir = std::filesystem::temp_directory_path();
    const auto update_path = temp_dir / "panel-live-update.exe";

    const auto result = download_to_file(installer_url, update_path, {}, {});
    if (!result.error.empty() || result.status_code < 200 || result.status_code >= 300) {
        return false;
    }

    const auto cmd = std::string("\"") + update_path.string() + "\" /VERYSILENT /SUPPRESSMSGBOXES /CLOSEAPPLICATIONS /NORESTART";

    std::system(cmd.c_str());
    return true;
}

} // namespace nlp3::platform
