#include "platform/external_game_manifest.hpp"

#include <cstdlib>
#include <fstream>
#include <optional>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <ShlObj.h>
#endif

namespace {

std::string read_env_value(const char* name) {
    if (name == nullptr) {
        return {};
    }

    const char* raw = std::getenv(name);
    return raw != nullptr ? std::string(raw) : std::string{};
}

std::filesystem::path resolve_relative_path(
    const std::filesystem::path& root,
    const std::string& raw_value,
    const std::filesystem::path& fallback) {
    const auto candidate = raw_value.empty() ? fallback : std::filesystem::path(raw_value);
    return candidate.is_absolute() ? candidate : (root / candidate);
}

std::filesystem::path resolve_entry_path(
    const std::filesystem::path& module_root,
    const std::string& raw_value) {
    const auto direct_path = resolve_relative_path(module_root, raw_value, raw_value);
    if (std::filesystem::exists(direct_path)) {
        return direct_path;
    }

    const auto entry_name = std::filesystem::path(raw_value).filename();
    if (entry_name.empty()) {
        return direct_path;
    }

    const std::vector<std::filesystem::path> common_candidates = {
        module_root / "build" / "Release" / entry_name,
        module_root / "build" / "Debug" / entry_name,
        module_root / "build" / "RelWithDebInfo" / entry_name,
        module_root / "dist" / entry_name,
        module_root / "bin" / entry_name,
    };
    for (const auto& candidate : common_candidates) {
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }

    std::error_code error;
    for (std::filesystem::recursive_directory_iterator iterator(
             module_root,
             std::filesystem::directory_options::skip_permission_denied,
             error);
         iterator != std::filesystem::recursive_directory_iterator();
         iterator.increment(error)) {
        if (error) {
            error.clear();
            continue;
        }
        if (!iterator->is_regular_file(error)) {
            error.clear();
            continue;
        }
        if (iterator->path().filename() == entry_name) {
            return iterator->path();
        }
    }

    return direct_path;
}

std::filesystem::path resolve_default_games_root() {
#ifdef _WIN32
    PWSTR desktop_path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, KF_FLAG_DEFAULT, nullptr, &desktop_path)) &&
        desktop_path != nullptr) {
        std::filesystem::path desktop(desktop_path);
        CoTaskMemFree(desktop_path);
        return desktop / "Juegos";
    }
    if (desktop_path != nullptr) {
        CoTaskMemFree(desktop_path);
    }
#endif

    const auto user_profile = read_env_value("USERPROFILE");
    if (!user_profile.empty()) {
        return std::filesystem::path(user_profile) / "Desktop" / "Juegos";
    }

    const auto home_drive = read_env_value("HOMEDRIVE");
    const auto home_path = read_env_value("HOMEPATH");
    if (!home_drive.empty() && !home_path.empty()) {
        return std::filesystem::path(home_drive + home_path) / "Desktop" / "Juegos";
    }

    return std::filesystem::path("Juegos");
}

std::optional<nlp3::platform::ExternalGameManifest> load_manifest_from_root(
    const std::filesystem::path& module_root) {
    const auto manifest_path = module_root / "module_manifest.json";
    if (!std::filesystem::exists(manifest_path)) {
        return std::nullopt;
    }

    std::ifstream input(manifest_path, std::ios::binary);
    if (!input) {
        return std::nullopt;
    }

    nlohmann::json json;
    try {
        input >> json;
    } catch (...) {
        return std::nullopt;
    }

    const auto communication = json.value("communication", nlohmann::json::object());
    const auto entry_name = json.value("entryExecutable", std::string{});
    if (entry_name.empty()) {
        return std::nullopt;
    }

    nlp3::platform::ExternalGameManifest manifest{};
    manifest.game_id = json.value("id", std::string{});
    manifest.display_name = json.value("displayName", std::string{});
    manifest.description = json.value("description", std::string{});
    manifest.type = json.value("type", std::string{});
    manifest.module_root = module_root;
    manifest.manifest_path = manifest_path;
    manifest.entry_path = resolve_entry_path(module_root, entry_name);
    manifest.entry_exists = std::filesystem::exists(manifest.entry_path);
    manifest.launch_args = json.value("launchArgs", std::vector<std::string>{});
    manifest.config_file = resolve_relative_path(
        module_root,
        communication.value("configFile", std::string{}),
        std::filesystem::path("config/live_config.json"));
    manifest.inbox_file = resolve_relative_path(
        module_root,
        communication.value("inboxFile", std::string{}),
        std::filesystem::path("runtime/inbox/events.jsonl"));
    manifest.status_file = resolve_relative_path(
        module_root,
        communication.value("statusFile", std::string{}),
        std::filesystem::path("runtime/status.json"));
    manifest.log_file = resolve_relative_path(
        module_root,
        communication.value("logFile", std::string{}),
        std::filesystem::path("runtime/host.log.jsonl"));

    if (manifest.game_id.empty()) {
        manifest.game_id = manifest.entry_path.stem().string();
    }
    if (manifest.display_name.empty()) {
        manifest.display_name = manifest.game_id;
    }

    return manifest;
}

} // namespace

namespace nlp3::platform {

std::filesystem::path resolve_local_games_root() {
    const auto override_root = read_env_value("NLP3_LOCAL_GAMES_ROOT");
    if (!override_root.empty()) {
        return std::filesystem::path(override_root);
    }

    return resolve_default_games_root();
}

std::optional<ExternalGameManifest> load_external_game_manifest(
    const std::filesystem::path& module_root) {
    return load_manifest_from_root(module_root);
}

std::vector<ExternalGameManifest> discover_external_game_manifests(
    const std::filesystem::path& games_root) {
    const auto root = games_root.empty() ? resolve_local_games_root() : games_root;
    std::vector<ExternalGameManifest> manifests{};
    if (root.empty() || !std::filesystem::exists(root)) {
        return manifests;
    }

    if (auto manifest = load_manifest_from_root(root); manifest.has_value()) {
        manifests.push_back(std::move(*manifest));
    }

    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        if (!entry.is_directory()) {
            continue;
        }

        auto manifest = load_manifest_from_root(entry.path());
        if (!manifest.has_value()) {
            continue;
        }

        manifests.push_back(std::move(*manifest));
    }

    return manifests;
}

const ExternalGameManifest* find_external_game_manifest(
    const std::vector<ExternalGameManifest>& manifests,
    std::string_view game_id) noexcept {
    for (const auto& manifest : manifests) {
        if (manifest.game_id == game_id) {
            return &manifest;
        }
    }

    return nullptr;
}

std::filesystem::path external_game_bridge_root(const ExternalGameManifest& manifest) {
    return manifest.module_root / "runtime" / "panel_bridge";
}

std::filesystem::path external_game_bridge_inbox_file(const ExternalGameManifest& manifest) {
    return external_game_bridge_root(manifest) / "inbox" / "panel_events.jsonl";
}

std::filesystem::path external_game_bridge_state_file(const ExternalGameManifest& manifest) {
    return external_game_bridge_root(manifest) / "state.json";
}

std::filesystem::path external_game_bridge_log_file(const ExternalGameManifest& manifest) {
    return external_game_bridge_root(manifest) / "bridge.log.jsonl";
}

std::filesystem::path external_game_bridge_stop_file(const ExternalGameManifest& manifest) {
    return external_game_bridge_root(manifest) / "control" / "stop.flag";
}

} // namespace nlp3::platform
