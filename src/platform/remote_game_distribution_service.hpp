#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "gamesdk/game_catalog.hpp"
#include "platform/external_game_manifest.hpp"
#include "platform/panel_command.hpp"
#include "platform/panel_config.hpp"

namespace nlp3::platform {

struct RemoteGameCatalogRecord {
    std::string game_id{};
    std::string display_name{};
    std::string version{};
    std::string source = "remote";
    bool licensed = true;
    std::string sha256{};
    std::string download_url{};
    std::string download_url_expires_at{};
    std::string package_path{};
    gamesdk::GameManifest manifest{};
};

struct RemoteGameCatalogParseResult {
    bool ok = false;
    std::string message{};
    std::vector<RemoteGameCatalogRecord> games{};
};

struct RemoteGameInstallRecord {
    std::string game_id{};
    std::string display_name{};
    std::string version{};
    std::string sha256{};
    std::filesystem::path install_root{};
    std::int64_t installed_at_ms = 0;
};

struct RemoteGameOperationStatus {
    std::string game_id{};
    std::string state{};
    std::string message{};
    std::uint64_t progress_percent = 0;
};

RemoteGameCatalogParseResult parse_remote_game_catalog_response(std::string_view body);
std::vector<RemoteGameInstallRecord> load_remote_game_install_registry(const std::filesystem::path& path);
bool save_remote_game_install_registry(
    const std::filesystem::path& path,
    const std::vector<RemoteGameInstallRecord>& entries);

class RemoteGameDistributionService {
public:
    explicit RemoteGameDistributionService(PanelAuthConfig config = {}) noexcept;
    ~RemoteGameDistributionService();

    void update_config(PanelAuthConfig config) noexcept;
    void set_access_context(
        std::string firebase_uid,
        std::string email,
        std::string id_token,
        std::string license_key);
    void clear_access_context() noexcept;

    bool refresh_catalog(std::string* error_message = nullptr);
    std::vector<gamesdk::GameCatalogEntry> catalog_entries() const;
    std::vector<ExternalGameManifest> installed_game_manifests() const;
    std::vector<RemoteGameOperationStatus> operation_statuses() const;
    PanelCommandResult start_download(const std::string& game_id);

    std::filesystem::path managed_root() const;
    std::filesystem::path managed_games_root() const;
    std::filesystem::path registry_path() const;

private:
    void join_worker_if_idle();
    void run_install_job(RemoteGameCatalogRecord record);

    PanelAuthConfig config_{};
    std::filesystem::path managed_root_{};
    std::filesystem::path registry_path_{};
    std::vector<RemoteGameCatalogRecord> catalog_{};
    std::vector<RemoteGameInstallRecord> install_registry_{};
    std::vector<RemoteGameOperationStatus> operation_statuses_{};
    std::string firebase_uid_{};
    std::string email_{};
    std::string id_token_{};
    std::string license_key_{};
    std::thread worker_thread_{};
    mutable std::mutex mutex_{};
    bool stop_requested_ = false;
    bool worker_running_ = false;
};

} // namespace nlp3::platform
