#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace nlp3::platform {

struct ExternalGameManifest {
    std::string game_id{};
    std::string display_name{};
    std::string description{};
    std::string type{};
    std::filesystem::path module_root{};
    std::filesystem::path manifest_path{};
    std::filesystem::path entry_path{};
    bool entry_exists = false;
    std::vector<std::string> launch_args{};
    std::filesystem::path config_file{};
    std::filesystem::path inbox_file{};
    std::filesystem::path status_file{};
    std::filesystem::path log_file{};
};

std::filesystem::path resolve_local_games_root();
std::optional<ExternalGameManifest> load_external_game_manifest(
    const std::filesystem::path& module_root);
std::vector<ExternalGameManifest> discover_external_game_manifests(
    const std::filesystem::path& games_root = {});
const ExternalGameManifest* find_external_game_manifest(
    const std::vector<ExternalGameManifest>& manifests,
    std::string_view game_id) noexcept;

std::filesystem::path external_game_bridge_root(const ExternalGameManifest& manifest);
std::filesystem::path external_game_bridge_inbox_file(const ExternalGameManifest& manifest);
std::filesystem::path external_game_bridge_state_file(const ExternalGameManifest& manifest);
std::filesystem::path external_game_bridge_log_file(const ExternalGameManifest& manifest);
std::filesystem::path external_game_bridge_stop_file(const ExternalGameManifest& manifest);

} // namespace nlp3::platform
