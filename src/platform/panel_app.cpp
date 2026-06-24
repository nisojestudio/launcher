#include "platform/panel_app.hpp"

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "bridge/tiktok_bridge_controller.hpp"
#include "bridge/tiktok_bridge_external_session.hpp"
#include "bridge/tiktok_external_inbox_adapter.hpp"
#include "bridge/tiktok_external_session_status.hpp"
#include "bridge/tiktok_external_ws_server.hpp"
#include "bridge/tiktok_external_event_replay.hpp"
#include "bridge/tiktok_external_event_recorder.hpp"
#include "bridge/tiktok_bridge_stub_session.hpp"
#include "events/host_event.hpp"
#include "games/event_counter_game.hpp"
#include "games/live_timer_game.hpp"
#include "gamesdk/game_factory.hpp"
#include "gamesdk/game_input_mapper.hpp"
#include "gamesdk/game_registry.hpp"
#include "gamesdk/game_runtime_controller.hpp"
#include "bridge/tiktok_event_mapper.hpp"
#include "host/host_runtime.hpp"
#include "platform/game_catalog_source.hpp"
#include "platform/license_service.hpp"
#include "platform/local_game_catalog_source.hpp"
#include "platform/external_bridge_runner.hpp"
#include "platform/external_game_manifest.hpp"
#include "platform/external_game_state.hpp"
#include "platform/external_game_bridge_runner.hpp"
#include "platform/cloudflare_tunnel_service.hpp"
#include "platform/panel_activity.hpp"
#include "platform/panel_config_storage.hpp"
#include "platform/panel_controller.hpp"
#include "platform/panel_diagnostics_builder.hpp"
#include "platform/panel_snapshot_builder.hpp"
#include "platform/remote_game_distribution_service.hpp"
#include "platform/server_license_service.hpp"
#include "tts/real_tts_backend.hpp"
#include "tts/tts_service.hpp"

namespace {

constexpr std::string_view kDefaultPanelConfigName = "panel_config.json";

class NullGame final : public nlp3::gamesdk::IGameModule {
public:
    std::string_view game_id() const noexcept override {
        return "null-game";
    }

    void on_activated() override {
    }

    void on_host_event(
        const nlp3::events::HostEvent&,
        const nlp3::host::HostSessionSnapshot&) override {
    }
};

class NullFactory final : public nlp3::gamesdk::IGameFactory {
public:
    const nlp3::gamesdk::GameManifest& manifest() const noexcept override {
        return manifest_;
    }

    std::unique_ptr<nlp3::gamesdk::IGameModule> create() const override {
        return std::make_unique<NullGame>();
    }

private:
    nlp3::gamesdk::GameManifest manifest_{
        "null-game",
        "Null Game",
        "0.1.0",
        {},
    };
};

std::string resolve_bridge_mode(std::string_view bridge_mode) {
    return bridge_mode == "external" ? "external" : "stub";
}

bool is_default_config_request(std::string_view config_path) {
    return config_path.empty() || config_path == kDefaultPanelConfigName;
}

std::filesystem::path resolve_module_path() {
#ifdef _WIN32
    std::wstring buffer(MAX_PATH, L'\0');
    while (true) {
        const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return {};
        }

        if (length < buffer.size() - 1) {
            buffer.resize(length);
            return std::filesystem::path(buffer);
        }

        buffer.resize(buffer.size() * 2, L'\0');
    }
#else
    return {};
#endif
}

std::wstring read_env_w(const wchar_t* name) {
#ifdef _WIN32
    if (name == nullptr || *name == L'\0') {
        return {};
    }

    const auto required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0) {
        return {};
    }

    std::wstring value(static_cast<std::size_t>(required), L'\0');
    const auto written = GetEnvironmentVariableW(name, value.data(), required);
    if (written == 0 || written >= required) {
        return {};
    }

    value.resize(written);
    return value;
#else
    (void)name;
    return {};
#endif
}

std::wstring to_lower_copy(std::wstring_view value) {
    std::wstring lowered(value);
    std::transform(
        lowered.begin(),
        lowered.end(),
        lowered.begin(),
        [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return lowered;
}

std::wstring normalize_path_for_compare(const std::filesystem::path& path) {
    auto normalized = path.lexically_normal().wstring();
    while (!normalized.empty() && (normalized.back() == L'\\' || normalized.back() == L'/')) {
        normalized.pop_back();
    }
    return to_lower_copy(normalized);
}

bool path_has_prefix(
    const std::filesystem::path& candidate,
    const std::filesystem::path& prefix) {
    if (candidate.empty() || prefix.empty()) {
        return false;
    }

    const auto normalized_candidate = normalize_path_for_compare(candidate);
    const auto normalized_prefix = normalize_path_for_compare(prefix);
    if (normalized_candidate.size() < normalized_prefix.size()) {
        return false;
    }
    if (normalized_candidate.compare(0, normalized_prefix.size(), normalized_prefix) != 0) {
        return false;
    }
    return normalized_candidate.size() == normalized_prefix.size()
        || normalized_candidate[normalized_prefix.size()] == L'\\'
        || normalized_candidate[normalized_prefix.size()] == L'/';
}

bool prefer_user_config_path(const std::filesystem::path& module_directory) {
#ifdef _WIN32
    if (module_directory.empty()) {
        return false;
    }

    const auto program_files = read_env_w(L"ProgramFiles");
    if (!program_files.empty() && path_has_prefix(module_directory, std::filesystem::path(program_files))) {
        return true;
    }

    const auto program_files_x86 = read_env_w(L"ProgramFiles(x86)");
    if (!program_files_x86.empty()
        && path_has_prefix(module_directory, std::filesystem::path(program_files_x86))) {
        return true;
    }
#else
    (void)module_directory;
#endif
    return false;
}

std::filesystem::path resolve_local_panel_config_path() {
#ifdef _WIN32
    const auto local_app_data = read_env_w(L"LOCALAPPDATA");
    if (!local_app_data.empty()) {
        return std::filesystem::path(local_app_data) / L"NisojeStudio" / std::filesystem::path(kDefaultPanelConfigName);
    }
#endif
    return {};
}

std::string resolve_panel_config_path(std::string_view requested_path) {
    if (!is_default_config_request(requested_path)) {
        return std::string(requested_path);
    }

    const auto requested_name = std::filesystem::path(kDefaultPanelConfigName);
    const auto module_directory = resolve_module_path().parent_path();
    const auto local_appdata_candidate = resolve_local_panel_config_path();

    if (!module_directory.empty() && !prefer_user_config_path(module_directory)) {
        const auto module_candidate = module_directory / requested_name;
        std::error_code module_error;
        if (std::filesystem::exists(module_candidate, module_error) && !module_error) {
            return module_candidate.string();
        }
    }

    std::error_code cwd_error;
    const auto working_directory = std::filesystem::current_path(cwd_error);
    if (!cwd_error && !working_directory.empty()) {
        const auto cwd_candidate = working_directory / requested_name;
        std::error_code exists_error;
        if (std::filesystem::exists(cwd_candidate, exists_error) && !exists_error) {
            return cwd_candidate.string();
        }
    }

    if (!local_appdata_candidate.empty()) {
        std::error_code local_exists_error;
        if (std::filesystem::exists(local_appdata_candidate, local_exists_error) && !local_exists_error) {
            return local_appdata_candidate.string();
        }
    }

    if (prefer_user_config_path(module_directory) && !local_appdata_candidate.empty()) {
        return local_appdata_candidate.string();
    }

    if (!module_directory.empty()) {
        return (module_directory / requested_name).string();
    }

    if (!local_appdata_candidate.empty()) {
        return local_appdata_candidate.string();
    }

    return std::string(kDefaultPanelConfigName);
}

std::filesystem::path resolve_bundled_panel_config_template_path(const std::string& active_config_path) {
    const auto module_directory = resolve_module_path().parent_path();
    if (module_directory.empty()) {
        return {};
    }

    const auto bundled_config = module_directory / std::filesystem::path(kDefaultPanelConfigName);
    std::error_code exists_error;
    if (!std::filesystem::exists(bundled_config, exists_error) || exists_error) {
        return {};
    }

    if (normalize_path_for_compare(bundled_config)
        == normalize_path_for_compare(std::filesystem::path(active_config_path))) {
        return {};
    }

    return bundled_config;
}

bool load_panel_config_with_template_fallback(
    const nlp3::platform::PanelConfigStorage& storage,
    const std::string& resolved_config_path,
    nlp3::platform::PanelConfig& config) {
    if (storage.load_from_file(resolved_config_path, config)) {
        return true;
    }

    const auto bundled_template = resolve_bundled_panel_config_template_path(resolved_config_path);
    if (bundled_template.empty()) {
        return false;
    }

    if (!storage.load_from_file(bundled_template.string(), config)) {
        return false;
    }

    storage.save_to_file(config, resolved_config_path);
    return true;
}

std::string trim_copy(std::string_view value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) {
        return {};
    }

    const auto end = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(begin, end - begin + 1));
}

void apply_product_migrations(nlp3::platform::PanelConfig& config) {
    const auto trimmed_name = trim_copy(config.panel_name);
    if (trimmed_name.empty()
        || trimmed_name == "Panel live 3.0"
        || trimmed_name == "Panel Live 3.0") {
        config.panel_name = "Nisoje Studio";
    }

    // Preserve explicit user overrides, but upgrade the old stock TTS defaults so
    // chat messages are not silently skipped in active lives.
    if (config.tts.min_text_length == 3) {
        config.tts.min_text_length = 1;
    }
    if (config.tts.chat_cooldown_ms == 2500) {
        config.tts.chat_cooldown_ms = 0;
    }
    if (config.tts_runtime.max_queue_size == 16) {
        config.tts_runtime.max_queue_size = 0;
    }
    if (config.tts_runtime.backend_queue_size == 32) {
        config.tts_runtime.backend_queue_size = 0;
    }
}

std::string to_lower_copy(std::string_view value) {
    std::string lowered(value);
    std::transform(
        lowered.begin(),
        lowered.end(),
        lowered.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return lowered;
}

std::optional<std::string> getenv_copy(const char* name) {
    if (name == nullptr) {
        return std::nullopt;
    }

    const char* raw = std::getenv(name);
    if (raw == nullptr) {
        return std::nullopt;
    }

    return std::string(raw);
}

std::optional<bool> parse_env_bool(const char* name) {
    const auto raw = getenv_copy(name);
    if (!raw.has_value()) {
        return std::nullopt;
    }

    const auto lowered = to_lower_copy(trim_copy(*raw));
    if (lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on") {
        return true;
    }
    if (lowered == "0" || lowered == "false" || lowered == "no" || lowered == "off") {
        return false;
    }
    return std::nullopt;
}

template <typename T>
std::optional<T> parse_env_unsigned(const char* name) {
    const auto raw = getenv_copy(name);
    if (!raw.has_value()) {
        return std::nullopt;
    }

    try {
        std::size_t consumed = 0;
        const auto parsed = std::stoull(trim_copy(*raw), &consumed);
        if (consumed != trim_copy(*raw).size()) {
            return std::nullopt;
        }
        return static_cast<T>(parsed);
    } catch (...) {
        return std::nullopt;
    }
}

void apply_embedded_ui_env_overrides(nlp3::platform::PanelConfig& config) {
    if (const auto value = parse_env_bool("NLP3_EMBEDDED_UI_ENABLED"); value.has_value()) {
        config.embedded_ui_enabled = *value;
    }
    if (const auto value = parse_env_bool("NLP3_EMBEDDED_UI_FALLBACK_TO_BROWSER"); value.has_value()) {
        config.embedded_ui_fallback_to_browser = *value;
    }
    if (const auto value = parse_env_bool("NLP3_EMBEDDED_UI_DEVTOOLS"); value.has_value()) {
        config.embedded_ui_devtools = *value;
    }
    if (const auto value = getenv_copy("NLP3_EMBEDDED_UI_URL"); value.has_value()) {
        const auto trimmed = trim_copy(*value);
        if (!trimmed.empty()) {
            config.embedded_ui_url = trimmed;
        }
    }
    if (const auto value = parse_env_unsigned<std::uint64_t>("NLP3_EMBEDDED_UI_STARTUP_TIMEOUT_MS");
        value.has_value() && *value > 0) {
        config.embedded_ui_startup_timeout_ms = *value;
    }
}

std::string resolve_external_actor_name(const nlp3::bridge::TikTokRawEvent& raw_event) {
    if (!raw_event.actor.display_name.empty()) {
        return raw_event.actor.display_name;
    }
    if (!raw_event.actor.username.empty()) {
        return raw_event.actor.username;
    }
    return raw_event.actor.user_id;
}

std::int64_t now_wall_clock_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::uint16_t resolve_runner_control_port(std::uint16_t ws_port) {
    constexpr std::uint16_t kDefaultControlPort = 8770;
    if (ws_port == 0 || ws_port > static_cast<std::uint16_t>(65530)) {
        return kDefaultControlPort;
    }
    return static_cast<std::uint16_t>(ws_port + 5);
}

std::string resolve_external_game_event_actor_id(const nlp3::events::HostActor& actor) {
    if (!actor.id.empty()) {
        return actor.id;
    }
    if (!actor.display_name.empty()) {
        return actor.display_name;
    }
    return "panel-user";
}

std::string resolve_external_game_event_actor_name(const nlp3::events::HostActor& actor) {
    if (!actor.display_name.empty()) {
        return actor.display_name;
    }
    if (!actor.id.empty()) {
        return actor.id;
    }
    return "Panel User";
}

std::string resolve_external_game_event_actor_id(const nlp3::bridge::TikTokRawActor& actor) {
    if (!actor.user_id.empty()) {
        return actor.user_id;
    }
    if (!actor.username.empty()) {
        return actor.username;
    }
    if (!actor.display_name.empty()) {
        return actor.display_name;
    }
    return "live-user";
}

std::string resolve_external_game_event_actor_name(const nlp3::bridge::TikTokRawActor& actor) {
    if (!actor.display_name.empty()) {
        return actor.display_name;
    }
    if (!actor.username.empty()) {
        return actor.username;
    }
    if (!actor.user_id.empty()) {
        return actor.user_id;
    }
    return "Live User";
}

nlp3::gamesdk::GameCatalogEntry make_external_game_catalog_entry(
    const nlp3::platform::ExternalGameManifest& manifest) {
    nlp3::gamesdk::GameManifest game_manifest{};
    game_manifest.game_id = manifest.game_id;
    game_manifest.display_name = manifest.display_name;
    game_manifest.version = "external";
    game_manifest.description = manifest.description;
    game_manifest.author = "Nisoje external module";
    game_manifest.capabilities.uses_chat_messages = true;
    game_manifest.capabilities.uses_gifts = true;
    game_manifest.capabilities.uses_follows = true;
    game_manifest.capabilities.uses_shares = true;
    game_manifest.capabilities.uses_viewer_joins = true;
    game_manifest.capabilities.uses_avatar_data = true;
    game_manifest.capabilities.uses_tts = false;

    return nlp3::gamesdk::GameCatalogEntry{
        manifest.game_id,
        manifest.display_name,
        "external",
        "external-local",
        manifest.entry_exists,
        manifest.entry_exists,
        false,
        {},
        {},
        0,
        game_manifest,
    };
}

void upsert_catalog_entry(
    std::vector<nlp3::gamesdk::GameCatalogEntry>& entries,
    nlp3::gamesdk::GameCatalogEntry entry) {
    auto existing = std::find_if(
        entries.begin(),
        entries.end(),
        [&](const nlp3::gamesdk::GameCatalogEntry& item) { return item.game_id == entry.game_id; });
    if (existing == entries.end()) {
        entries.push_back(std::move(entry));
        return;
    }

    existing->display_name = !entry.display_name.empty() ? entry.display_name : existing->display_name;
    existing->version = !entry.version.empty() ? entry.version : existing->version;
    existing->source = !entry.source.empty() ? entry.source : existing->source;
    existing->installed = existing->installed || entry.installed;
    existing->enabled = existing->enabled || entry.enabled;
    existing->update_available = existing->update_available || entry.update_available;
    if (!entry.install_state.empty()) {
        existing->install_state = std::move(entry.install_state);
    }
    if (!entry.install_message.empty()) {
        existing->install_message = std::move(entry.install_message);
    }
    if (entry.install_progress_percent > 0 || !existing->install_state.empty()) {
        existing->install_progress_percent = entry.install_progress_percent;
    }
    if (!entry.manifest.game_id.empty()) {
        existing->manifest = std::move(entry.manifest);
    }
}

std::vector<nlp3::platform::ExternalGameManifest> merge_external_manifests(
    std::vector<nlp3::platform::ExternalGameManifest> local_manifests,
    const std::vector<nlp3::platform::ExternalGameManifest>& managed_manifests) {
    std::set<std::string> seen_ids{};
    std::vector<nlp3::platform::ExternalGameManifest> merged{};
    merged.reserve(local_manifests.size() + managed_manifests.size());

    for (auto& manifest : local_manifests) {
        if (!seen_ids.insert(manifest.game_id).second) {
            continue;
        }
        merged.push_back(std::move(manifest));
    }
    for (const auto& manifest : managed_manifests) {
        if (!seen_ids.insert(manifest.game_id).second) {
            continue;
        }
        merged.push_back(manifest);
    }
    return merged;
}

const nlp3::platform::ExternalGameManifest* resolve_snapshot_external_manifest(
    const std::vector<nlp3::platform::ExternalGameManifest>& manifests,
    std::string_view active_game_id,
    std::string_view default_game_id) {
    if (!active_game_id.empty()) {
        if (const auto* manifest = find_external_game_manifest(manifests, active_game_id); manifest != nullptr) {
            return manifest;
        }
    }
    if (!default_game_id.empty()) {
        if (const auto* manifest = find_external_game_manifest(manifests, default_game_id); manifest != nullptr) {
            return manifest;
        }
    }
    return manifests.empty() ? nullptr : &manifests.front();
}

std::string build_external_game_event_line(
    std::string_view kind,
    std::string actor_id,
    std::string actor_name,
    std::string avatar_url,
    const nlohmann::json& data,
    std::int64_t timestamp_ms) {
    nlohmann::json event = {
        {"kind", std::string(kind)},
        {"user", {
            {"id", std::move(actor_id)},
            {"name", std::move(actor_name)},
            {"avatar", avatar_url},
            {"avatarUrl", avatar_url},
            {"avatar_url", std::move(avatar_url)},
        }},
        {"data", data},
        {"ts", timestamp_ms > 0 ? timestamp_ms : now_wall_clock_ms()},
    };
    return event.dump();
}

} // namespace

namespace nlp3::platform {

PanelApp::PanelApp() = default;

PanelApp::~PanelApp() {
    try {
        stop_http_ui();
        stop_external_game();
        stop_external_runner();
        stop_external_ws();
        if (bridge_controller_ != nullptr) {
            bridge_controller_->stop();
        }
    } catch (...) {
        // Best-effort shutdown only. Destructors must not throw.
    }
}

bool PanelApp::initialize(const std::string& config_path) {
    try {
        initialized_ = false;
        stop_external_game();
        external_game_manifests_.clear();
        active_external_game_id_.clear();
        external_game_status_cache_ = {};
        external_bridge_recording_ = false;
        external_bridge_recording_path_.clear();
        external_bridge_last_replay_path_.clear();
        external_bridge_last_replay_accepted_events_ = 0;
        total_external_events_submitted_ = 0;
        external_bridge_target_user_.clear();
        external_bridge_connection_state_.clear();
        external_bridge_last_status_message_.clear();
        external_bridge_last_status_timestamp_ms_ = 0;
        external_bridge_current_room_id_.clear();
        external_bridge_last_event_kind_.clear();
        external_bridge_last_event_actor_.clear();
        external_bridge_last_event_timestamp_ms_ = 0;
        external_bridge_chat_events_ = 0;
        external_bridge_like_events_ = 0;
        external_bridge_gift_events_ = 0;
        external_bridge_follow_events_ = 0;
        external_bridge_share_events_ = 0;
        external_bridge_viewer_join_events_ = 0;
        external_bridge_viewer_count_events_ = 0;
        external_bridge_live_start_events_ = 0;
        external_bridge_live_end_events_ = 0;
        external_bridge_moderation_events_ = 0;
        external_bridge_custom_raw_events_ = 0;
        external_ws_server_.reset();
        external_runner_.reset();
        http_ui_server_.reset();
        remote_game_distribution_service_.reset();

        config_path_ = resolve_panel_config_path(config_path);
        config_ = {};
        config_storage_ = std::make_unique<PanelConfigStorage>();
        load_panel_config_with_template_fallback(*config_storage_, config_path_, config_);
        config_.bridge_mode = resolve_bridge_mode(config_.bridge_mode);
        apply_product_migrations(config_);
        apply_embedded_ui_env_overrides(config_);

        activity_log_ = std::make_unique<PanelActivityLog>(256);
        license_service_ = std::make_unique<ServerLicenseService>(config_.auth);
        remote_game_distribution_service_ = std::make_unique<RemoteGameDistributionService>(config_.auth);
        panel_updater_service_ = std::make_unique<PanelUpdaterService>();
        panel_updater_service_->start(config_.auth, NLP3_PANEL_VERSION);

        if (config_.bridge_mode == "external") {
            bridge_session_ = std::make_unique<bridge::TikTokBridgeExternalSession>(config_.bridge);
            external_runner_ = std::make_unique<ExternalBridgeRunner>();
            external_runner_->refresh_runtime_status();
        } else {
            bridge_session_ = std::make_unique<bridge::TikTokBridgeStubSession>(config_.bridge);
        }
        bridge_controller_ = std::make_unique<bridge::TikTokBridgeController>(std::move(bridge_session_));
        bridge_controller_->start();

        tts_backend_ = std::make_unique<tts::RealTtsBackend>();
        tts_service_ = std::make_unique<tts::HostTtsService>(
            config_.tts_runtime,
            config_.tts,
            *tts_backend_);

        game_factories_ = std::make_unique<gamesdk::GameFactoryRegistry>();
        game_registry_ = std::make_unique<gamesdk::GameRegistry>(game_factories_.get());
        game_runtime_controller_ = std::make_unique<gamesdk::GameRuntimeController>(game_registry_.get());

        const games::EventCounterGame event_counter_metadata{};
        const auto event_counter_manifest = event_counter_metadata.manifest();

        game_factories_->register_factory(std::make_unique<NullFactory>());
        game_factories_->register_factory(std::make_unique<games::EventCounterGameFactory>());

        gamesdk::GameCatalog local_catalog{};
        local_catalog.add(gamesdk::GameCatalogEntry{
            "null-game",
            "Null Game",
            "0.1.0",
            "local",
            true,
            true,
            false,
            {},
            {},
            0,
        });
        local_catalog.add(gamesdk::GameCatalogEntry{
            "event-counter",
            "Event Counter",
            "0.1.0",
            "local",
            true,
            true,
            false,
            {},
            {},
            0,
            event_counter_manifest,
        });

        live_timer_game_ = std::make_unique<games::LiveTimerGame>();
        live_timer_game_->apply_config(live_timer_game_->default_config());
        live_timer_game_->on_activated();

        refresh_external_game_manifests();

        auto local_catalog_source = std::make_unique<LocalGameCatalogSource>();
        local_catalog_source->set_catalog(local_catalog);
        game_catalog_source_ = std::move(local_catalog_source);
        game_registry_->catalog() = game_catalog_source_->load_catalog();
        sync_remote_distribution_auth_context(false);

        if (!auth_required() || access_granted()) {
            const auto preferred_game_id =
                !config_.default_game_id.empty() ? config_.default_game_id : std::string{"event-counter"};
            if (find_external_game_manifest(external_game_manifests_, preferred_game_id) != nullptr) {
                game_runtime_controller_->activate("null-game");
            } else if (!game_runtime_controller_->activate(preferred_game_id)) {
                game_runtime_controller_->activate("null-game");
            }
        }

        host_runtime_ = std::make_unique<host::HostRuntime>(
            nullptr,
            tts_service_.get(),
            nullptr,
            nullptr,
            bridge::TikTokEventMapper{config_.bridge},
            bridge_controller_.get(),
            host::HostAutomationEngine{config_.automation},
            host::HostPeriodicTtsEngine{config_.periodic_tts},
            activity_log_.get());

        if (auto* active_game = game_runtime_controller_->active_game(); active_game != nullptr) {
            active_game->apply_config(active_game->default_config());
            host_runtime_->attach_game(active_game);
        }

        panel_controller_ = std::make_unique<PanelController>(
            host_runtime_.get(),
            bridge_controller_.get(),
            game_runtime_controller_.get());

        started_at_ms_ = now_wall_clock_ms();
        refresh_external_game_status();
        initialized_ = true;
        return true;
    } catch (...) {
        initialized_ = false;
        return false;
    }
}

bool PanelApp::save_config(const std::string& config_path) const {
    const auto resolved_config_path =
        is_default_config_request(config_path) ? config_path_ : config_path;

    if (config_storage_ != nullptr) {
        return config_storage_->save_to_file(config_, resolved_config_path);
    }

    PanelConfigStorage storage;
    return storage.save_to_file(config_, resolved_config_path);
}

bool PanelApp::apply_live_config() {
    if (!initialized_ || host_runtime_ == nullptr) {
        return false;
    }

    host_runtime_->clear_pending_tts();
    host_runtime_->apply_automation_config(config_.automation);
    host_runtime_->apply_periodic_tts_config(config_.periodic_tts);
    host_runtime_->apply_bridge_mapper_config(config_.bridge);
    if (tts_service_ != nullptr) {
        tts_service_->set_config(config_.tts_runtime);
        tts_service_->set_policy(config_.tts);
    }
    if (remote_game_distribution_service_ != nullptr) {
        remote_game_distribution_service_->update_config(config_.auth);
    }
    return true;
}

bool PanelApp::reload_config(const std::string& config_path) {
    const auto resolved_config_path =
        is_default_config_request(config_path) ? config_path_ : config_path;

    if (config_storage_ == nullptr) {
        config_storage_ = std::make_unique<PanelConfigStorage>();
    }

    PanelConfig reloaded_config{};
    if (!config_storage_->load_from_file(resolved_config_path, reloaded_config)) {
        return false;
    }

    reloaded_config.bridge_mode = resolve_bridge_mode(reloaded_config.bridge_mode);
    apply_product_migrations(reloaded_config);
    config_ = std::move(reloaded_config);
    config_path_ = resolved_config_path;
    if (license_service_ != nullptr) {
        license_service_->update_config(config_.auth);
    }
    sync_remote_distribution_auth_context(access_granted());
    return apply_live_config();
}

PanelSnapshot PanelApp::snapshot() const {
    if (!initialized_
        || host_runtime_ == nullptr
        || game_runtime_controller_ == nullptr) {
        PanelSnapshot empty_snapshot{};
        empty_snapshot.panel_name = config_.panel_name;
        empty_snapshot.bridge_mode = config_.bridge_mode;
        const auto external_manifest = external_bridge_manifest();
        empty_snapshot.external_bridge.external_mode = external_manifest.external_mode;
        empty_snapshot.external_bridge.recording = external_manifest.recording;
        empty_snapshot.external_bridge.recording_path = external_manifest.recording_path;
        empty_snapshot.external_bridge.last_replay_path = external_manifest.last_replay_path;
        empty_snapshot.external_bridge.last_replay_accepted_events =
            external_manifest.last_replay_accepted_events;
        empty_snapshot.external_bridge.total_external_events_submitted =
            external_manifest.total_external_events_submitted;
        empty_snapshot.external_bridge.target_user = external_manifest.target_user;
        empty_snapshot.external_bridge.connection_state = external_manifest.connection_state;
        empty_snapshot.external_bridge.last_status_message = external_manifest.last_status_message;
        empty_snapshot.external_bridge.last_status_timestamp_ms =
            external_manifest.last_status_timestamp_ms;
        empty_snapshot.external_bridge.current_room_id = external_manifest.current_room_id;
        empty_snapshot.external_bridge.last_event_kind = external_manifest.last_event_kind;
        empty_snapshot.external_bridge.last_event_actor = external_manifest.last_event_actor;
        empty_snapshot.external_bridge.last_event_timestamp_ms = external_manifest.last_event_timestamp_ms;
        empty_snapshot.external_bridge.chat_events = external_manifest.chat_events;
        empty_snapshot.external_bridge.like_events = external_manifest.like_events;
        empty_snapshot.external_bridge.gift_events = external_manifest.gift_events;
        empty_snapshot.external_bridge.follow_events = external_manifest.follow_events;
        empty_snapshot.external_bridge.share_events = external_manifest.share_events;
        empty_snapshot.external_bridge.viewer_join_events = external_manifest.viewer_join_events;
        empty_snapshot.external_bridge.viewer_count_events = external_manifest.viewer_count_events;
        empty_snapshot.external_bridge.live_start_events = external_manifest.live_start_events;
        empty_snapshot.external_bridge.live_end_events = external_manifest.live_end_events;
        empty_snapshot.external_bridge.moderation_events = external_manifest.moderation_events;
        empty_snapshot.external_bridge.custom_raw_events = external_manifest.custom_raw_events;
        empty_snapshot.external_bridge.runner_running = external_manifest.runner_running;
        empty_snapshot.external_bridge.runner_process_id = external_manifest.runner_process_id;
        empty_snapshot.external_bridge.runner_ws_url = external_manifest.runner_ws_url;
        empty_snapshot.external_bridge.runtime_checked = external_manifest.runtime_checked;
        empty_snapshot.external_bridge.runtime_ready = external_manifest.runtime_ready;
        empty_snapshot.external_bridge.runtime_checked_timestamp_ms =
            external_manifest.runtime_checked_timestamp_ms;
        empty_snapshot.external_bridge.runtime_summary = external_manifest.runtime_summary;
        empty_snapshot.external_bridge.runtime_alerts = external_manifest.runtime_alerts;
        empty_snapshot.external_bridge.runner_has_exit_code = external_manifest.runner_has_exit_code;
        empty_snapshot.external_bridge.runner_last_exit_code = external_manifest.runner_last_exit_code;
        empty_snapshot.external_bridge.runner_last_error = external_manifest.runner_last_error;
        empty_snapshot.external_bridge.runner_recent_log_lines = external_manifest.runner_recent_log_lines;
        const auto ws_status = external_ws_status();
        empty_snapshot.external_ws = PanelExternalWsStatus{
            ws_status.running,
            ws_status.port,
            ws_status.accepted_messages,
            ws_status.rejected_messages,
        };
        if (license_service_ != nullptr) {
            empty_snapshot.license = PanelLicenseStatus{
                license_service_->snapshot().status,
                license_service_->snapshot().message,
                license_service_->snapshot().tier,
            };
            empty_snapshot.auth = license_service_->auth_snapshot();
        }
        empty_snapshot.external_game = external_game_status_cache_;
        if (panel_updater_service_ != nullptr) {
            empty_snapshot.panel_version = panel_updater_service_->current_version();
            empty_snapshot.latest_version = panel_updater_service_->latest_version();
            empty_snapshot.latest_installer_url = panel_updater_service_->latest_installer_url();
        }
        return empty_snapshot;
    }

    const auto ws_status = external_ws_status();
    auto external_game_status = external_game_status_cache_;
    auto snapshot = build_panel_snapshot(
        config_,
        *host_runtime_,
        *game_runtime_controller_,
        activity_log_.get(),
        license_service_.get(),
        auth_status(),
        external_bridge_manifest(),
        PanelExternalWsStatus{
            ws_status.running,
            ws_status.port,
            ws_status.accepted_messages,
            ws_status.rejected_messages,
        },
        std::move(external_game_status));
    if (panel_updater_service_ != nullptr) {
        snapshot.panel_version = panel_updater_service_->current_version();
        snapshot.latest_version = panel_updater_service_->latest_version();
        snapshot.latest_installer_url = panel_updater_service_->latest_installer_url();
    }

    if (live_timer_game_ != nullptr) {
        const auto& s = live_timer_game_->state();
        snapshot.timer.has_timer = true;
        snapshot.timer.timer_id = std::string(games::kLiveTimerGameId);
        snapshot.timer.remaining_seconds = live_timer_game_->remaining_seconds();
        snapshot.timer.remaining_formatted = live_timer_game_->format_time();
        snapshot.timer.running = live_timer_game_->is_running();
        snapshot.timer.paused = s.paused;
        snapshot.timer.enabled = live_timer_game_->is_enabled();
        snapshot.timer.completed = s.completed;
        snapshot.timer.title = s.title_text;
        snapshot.timer.subtitle = s.subtitle_text;
        {
            auto host = config_.overlay_host.empty() ? "localhost" : config_.overlay_host;
            auto port = http_ui_server_ != nullptr ? http_ui_server_->status().port : 18913;
            snapshot.timer.overlay_url = "http://" + host + ":" + std::to_string(port) + "/overlay/live-timer";
            if (tunnel_service_ != nullptr) {
                snapshot.timer.overlay_tunnel_url = tunnel_service_->tunnel_url();
            }
        }
    }

    return snapshot;
}

bool PanelApp::trigger_panel_update() {
    if (panel_updater_service_ == nullptr) {
        return false;
    }
    return panel_updater_service_->trigger_update();
}

PanelCommandResult PanelApp::execute_command(const PanelCommand& command) {
    if (!initialized_ || panel_controller_ == nullptr) {
        return {false, "panel_app_not_initialized"};
    }

    return panel_controller_->execute(command);
}

PanelDiagnosticsReport PanelApp::diagnostics() const {
    if (!initialized_) {
        PanelDiagnosticsReport report{};
        report.ok = false;
        report.entries.push_back(PanelDiagnosticEntry{
            PanelDiagnosticLevel::error,
            "panel.not_initialized",
            "PanelApp no esta inicializada",
        });
        return report;
    }

    const PanelDiagnosticsBuilder builder;
    return builder.build(snapshot());
}

PanelTickResult PanelApp::tick(std::uint64_t now_ms) {
    PanelTickResult result{};
    result.now_ms = now_ms;

    if (!initialized_ || host_runtime_ == nullptr) {
        return result;
    }

    if (external_ws_server_ != nullptr && external_ws_server_->running()) {
        external_ws_server_->poll();
    }
    if (external_runner_ != nullptr) {
        external_runner_->poll();
    }
    if (external_game_bridge_runner_ != nullptr) {
        external_game_bridge_runner_->poll();
    }
    if (http_ui_server_ != nullptr && http_ui_server_->running()) {
        http_ui_server_->poll();
    }
    refresh_external_game_status();

    const auto observed_at_ms = now_wall_clock_ms();
    result.bridge_events_processed = host_runtime_ != nullptr
        ? host_runtime_->tick_bridge(0, observed_at_ms)
        : 0;
    if (host_runtime_ != nullptr) {
        host_runtime_->tick_like_batches(observed_at_ms);
    }
    result.periodic_tts_enqueued = tick_periodic_tts(now_ms);
    if (host_runtime_ != nullptr) {
        host_runtime_->flush_tts();
    }

    if (live_timer_game_ != nullptr) {
        live_timer_game_->poll_completion_sound();
    }

    return result;
}

PanelRunResult PanelApp::run_ticks(
    std::size_t tick_count,
    std::uint64_t start_now_ms,
    std::uint64_t step_ms) {
    PanelRunResult result{};
    if (!initialized_ || tick_count == 0) {
        return result;
    }

    for (std::size_t index = 0; index < tick_count; ++index) {
        const auto now_ms = start_now_ms + (index * step_ms);
        const auto tick_result = tick(now_ms);
        ++result.ticks_executed;
        result.total_bridge_events_processed += tick_result.bridge_events_processed;
        if (tick_result.periodic_tts_enqueued) {
            ++result.periodic_tts_enqueues;
        }
        result.last_now_ms = tick_result.now_ms;
    }

    return result;
}

PanelConfig& PanelApp::config() noexcept {
    return config_;
}

const PanelConfig& PanelApp::config() const noexcept {
    return config_;
}

ExternalBridgeManifest PanelApp::external_bridge_manifest() const {
    const auto runner_status = external_runner_status();
    return ExternalBridgeManifest{
        is_external_bridge_mode(),
        external_bridge_recording_,
        external_bridge_recording_path_,
        external_bridge_last_replay_path_,
        external_bridge_last_replay_accepted_events_,
        total_external_events_submitted_,
        external_bridge_target_user_,
        external_bridge_connection_state_,
        external_bridge_last_status_message_,
        external_bridge_last_status_timestamp_ms_,
        external_bridge_current_room_id_,
        external_bridge_last_event_kind_,
        external_bridge_last_event_actor_,
        external_bridge_last_event_timestamp_ms_,
        external_bridge_chat_events_,
        external_bridge_like_events_,
        external_bridge_gift_events_,
        external_bridge_follow_events_,
        external_bridge_share_events_,
        external_bridge_viewer_join_events_,
        external_bridge_viewer_count_events_,
        external_bridge_live_start_events_,
        external_bridge_live_end_events_,
        external_bridge_moderation_events_,
        external_bridge_custom_raw_events_,
        runner_status.running,
        runner_status.process_id,
        runner_status.ws_url,
        runner_status.runtime_checked,
        runner_status.runtime_ready,
        runner_status.runtime_checked_timestamp_ms,
        runner_status.runtime_summary,
        runner_status.runtime_alerts,
        runner_status.has_exit_code,
        runner_status.last_exit_code,
        runner_status.last_error,
        runner_status.recent_log_lines,
    };
}

bool PanelApp::is_external_bridge_mode() const noexcept {
    return bridge_controller_ != nullptr
        && dynamic_cast<bridge::TikTokBridgeExternalSession*>(bridge_controller_->session()) != nullptr;
}

bool PanelApp::start_external_bridge_recording(const std::string& path) {
    if (!initialized_ || !is_external_bridge_mode() || path.empty()) {
        return false;
    }

    external_bridge_recording_ = true;
    external_bridge_recording_path_ = path;
    return true;
}

void PanelApp::stop_external_bridge_recording() {
    external_bridge_recording_ = false;
    external_bridge_recording_path_.clear();
}

bool PanelApp::is_external_bridge_recording() const noexcept {
    return external_bridge_recording_;
}

std::string PanelApp::external_bridge_recording_path() const {
    return external_bridge_recording_path_;
}

std::size_t PanelApp::replay_external_bridge_file(const std::string& path) {
    if (!initialized_ || !is_external_bridge_mode() || path.empty()) {
        return 0;
    }

    bridge::TikTokExternalEventReplay replay{this};
    external_bridge_last_replay_path_ = path;
    external_bridge_last_replay_accepted_events_ = replay.replay_jsonl_file(path);
    return external_bridge_last_replay_accepted_events_;
}

bool PanelApp::record_external_bridge_event(
    const bridge::TikTokRawEvent& raw_event,
    const std::string& path) {
    if (!initialized_ || !is_external_bridge_mode() || path.empty()) {
        return false;
    }

    const bridge::TikTokExternalEventRecorder recorder{};
    return recorder.append_jsonl(path, raw_event);
}

bool PanelApp::submit_external_bridge_event(const bridge::TikTokRawEvent& raw_event) {
    if (!initialized_ || bridge_controller_ == nullptr) {
        return false;
    }

    auto* external_session =
        dynamic_cast<bridge::TikTokBridgeExternalSession*>(bridge_controller_->session());
    if (external_session == nullptr || !external_session->submit_external_event(raw_event)) {
        return false;
    }

    ++total_external_events_submitted_;
    external_bridge_current_room_id_ = raw_event.metadata.room_id;
    external_bridge_last_event_kind_ = std::string(bridge::to_string(raw_event.kind));
    external_bridge_last_event_actor_ = resolve_external_actor_name(raw_event);
    external_bridge_last_event_timestamp_ms_ = raw_event.metadata.timestamp_ms;
    switch (raw_event.kind) {
    case bridge::TikTokRawEventKind::chat:
        ++external_bridge_chat_events_;
        break;
    case bridge::TikTokRawEventKind::like:
        ++external_bridge_like_events_;
        break;
    case bridge::TikTokRawEventKind::gift:
        ++external_bridge_gift_events_;
        break;
    case bridge::TikTokRawEventKind::follow:
        ++external_bridge_follow_events_;
        break;
    case bridge::TikTokRawEventKind::share:
        ++external_bridge_share_events_;
        break;
    case bridge::TikTokRawEventKind::viewer_join:
        ++external_bridge_viewer_join_events_;
        break;
    case bridge::TikTokRawEventKind::viewer_count:
        ++external_bridge_viewer_count_events_;
        break;
    case bridge::TikTokRawEventKind::live_start:
        ++external_bridge_live_start_events_;
        break;
    case bridge::TikTokRawEventKind::live_end:
        ++external_bridge_live_end_events_;
        break;
    case bridge::TikTokRawEventKind::moderation:
        ++external_bridge_moderation_events_;
        break;
    case bridge::TikTokRawEventKind::custom_raw:
        ++external_bridge_custom_raw_events_;
        break;
    }

    if (external_bridge_recording_ && !external_bridge_recording_path_.empty()) {
        const bridge::TikTokExternalEventRecorder recorder{};
        if (!recorder.append_jsonl(external_bridge_recording_path_, raw_event)) {
            stop_external_bridge_recording();
        }
    }

    // Forward to timer if enabled
    if (live_timer_game_ != nullptr) {
        bridge::TikTokEventMapper raw_mapper{config_.bridge};
        auto host_event = raw_mapper.map(raw_event);
        if (host_event.has_value()) {
            forward_event_to_timer(host_event.value());
        }
    }

    forward_raw_event_to_external_game(raw_event);

    return true;
}

bool PanelApp::submit_external_session_status(const bridge::TikTokExternalSessionStatus& status) {
    if (!initialized_ || !is_external_bridge_mode()) {
        return false;
    }

    if (!status.target_user.empty()) {
        external_bridge_target_user_ = status.target_user;
    }
    external_bridge_connection_state_ = std::string(bridge::to_string(status.connection_state));
    external_bridge_last_status_message_ = status.message;
    external_bridge_last_status_timestamp_ms_ = status.timestamp_ms;
    if (!status.room_id.empty()) {
        external_bridge_current_room_id_ = status.room_id;
    }
    return true;
}

bool PanelApp::start_external_ws(std::uint16_t port) {
    if (!initialized_ || !is_external_bridge_mode()) {
        return false;
    }

    if (external_ws_server_ == nullptr) {
        external_ws_server_ = std::make_unique<bridge::TikTokExternalWsServer>(this);
    }

    return external_ws_server_->start(port);
}

void PanelApp::stop_external_ws() {
    if (external_ws_server_ != nullptr) {
        external_ws_server_->stop();
    }
}

bridge::TikTokExternalWsStatus PanelApp::external_ws_status() const {
    if (!initialized_ || external_ws_server_ == nullptr) {
        return {};
    }

    return external_ws_server_->status();
}

bool PanelApp::start_external_runner(const std::string& target_user, std::uint64_t max_seconds) {
    if (!initialized_ || !is_external_bridge_mode()) {
        return false;
    }

    auto resolved_target_user = target_user.empty() ? config_.external_target_user : target_user;
    if (resolved_target_user.empty()) {
        return false;
    }

    config_.external_target_user = resolved_target_user;
    external_bridge_target_user_ = resolved_target_user;

    const auto configured_port =
        config_.external_ws_port == 0 ? static_cast<std::uint16_t>(8765) : config_.external_ws_port;
    const auto ws_status = external_ws_status();
    if (!ws_status.running || ws_status.port != configured_port) {
        if (!start_external_ws(configured_port)) {
            return false;
        }
    }

    if (external_runner_ == nullptr) {
        external_runner_ = std::make_unique<ExternalBridgeRunner>();
    }

    const auto started = external_runner_->start(ExternalBridgeRunnerStartRequest{
        resolved_target_user,
        "ws://127.0.0.1:" + std::to_string(configured_port),
        resolve_runner_control_port(configured_port),
        max_seconds,
    });
    if (started) {
        external_bridge_connection_state_ = "starting";
        external_bridge_last_status_message_ = "Launching external runner";
        external_bridge_last_status_timestamp_ms_ = now_wall_clock_ms();
    }
    return started;
}

void PanelApp::stop_external_runner() {
    if (external_runner_ != nullptr) {
        external_runner_->stop();
    }
    if (is_external_bridge_mode() && bridge_controller_ != nullptr) {
        bridge_controller_->reset();
        bridge_controller_->start();
    }
    if (host_runtime_ != nullptr) {
        host_runtime_->clear_pending_live_backlog();
    }
    if (is_external_bridge_mode()) {
        external_bridge_connection_state_ = "disconnected";
        external_bridge_last_status_message_ = "Runner stopped by panel";
        external_bridge_last_status_timestamp_ms_ = now_wall_clock_ms();
        external_bridge_current_room_id_.clear();
    }
}

ExternalBridgeRunnerStatus PanelApp::external_runner_status() const {
    return external_runner_ != nullptr ? external_runner_->status() : ExternalBridgeRunnerStatus{};
}

bool PanelApp::start_http_ui(std::uint16_t port) {
    if (!initialized_) {
        return false;
    }

    if (http_ui_server_ == nullptr) {
        http_ui_server_ = std::make_unique<PanelHttpServer>(this);
    }

    if (!http_ui_server_->start(port)) {
        return false;
    }

    if (tunnel_service_ == nullptr) {
        tunnel_service_ = std::make_unique<CloudflareTunnelService>();
    }
    tunnel_service_->start_tunnel(port, nullptr);

    return true;
}

void PanelApp::stop_http_ui() {
    if (tunnel_service_ != nullptr) {
        tunnel_service_->stop_tunnel();
    }
    if (http_ui_server_ != nullptr) {
        http_ui_server_->stop();
    }
}

PanelHttpServerStatus PanelApp::http_ui_status() const {
    return http_ui_server_ != nullptr ? http_ui_server_->status() : PanelHttpServerStatus{};
}

bool PanelApp::submit_external_ws_payload(const std::string& payload) {
    if (!initialized_ || !is_external_bridge_mode() || external_ws_server_ == nullptr) {
        return false;
    }

    return external_ws_server_->handle_text_message(payload);
}

bridge::TikTokExternalInboxResult PanelApp::process_external_inbox(const std::string& inbox_dir) {
    if (!initialized_ || !is_external_bridge_mode()) {
        return {};
    }

    bridge::TikTokExternalInboxAdapter adapter{this};
    return adapter.process_inbox(inbox_dir);
}

bridge::TikTokExternalInboxResult PanelApp::process_external_inbox_and_tick(
    const std::string& inbox_dir,
    std::uint64_t now_ms) {
    const auto result = process_external_inbox(inbox_dir);
    tick(now_ms);
    return result;
}

std::size_t PanelApp::tick_bridge(std::size_t max_events) {
    return host_runtime_ != nullptr ? host_runtime_->tick_bridge(max_events) : 0;
}

bool PanelApp::tick_periodic_tts(std::uint64_t now_ms) {
    if (host_runtime_ == nullptr) {
        return false;
    }

    if (is_external_bridge_mode()
        && external_bridge_connection_state_ != "connected"
        && total_external_events_submitted_ == 0) {
        return false;
    }

    return host_runtime_->tick_periodic_tts(now_ms);
}

std::vector<std::string> PanelApp::available_game_ids() const {
    std::vector<std::string> game_ids{};
    const auto games = available_games();
    game_ids.reserve(games.size());
    for (const auto& entry : games) {
        game_ids.push_back(entry.game_id);
    }
    return game_ids;
}

std::vector<gamesdk::GameCatalogEntry> PanelApp::available_games() const {
    if (auth_required() && !access_granted()) {
        return {};
    }

    std::vector<gamesdk::GameCatalogEntry> entries{};
    if (game_catalog_source_ != nullptr) {
        entries = game_catalog_source_->load_catalog().entries();
    } else if (game_registry_ != nullptr) {
        entries = game_registry_->catalog().entries();
    }

    for (const auto& manifest : external_game_manifests_) {
        upsert_catalog_entry(entries, make_external_game_catalog_entry(manifest));
    }
    if (remote_game_distribution_service_ != nullptr) {
        for (auto& entry : remote_game_distribution_service_->catalog_entries()) {
            upsert_catalog_entry(entries, std::move(entry));
        }
    }

    return entries;
}

const gamesdk::IGameModule* PanelApp::active_runtime_game() const noexcept {
    if (game_runtime_controller_ == nullptr) return nullptr;
    return game_runtime_controller_->active_game();
}

gamesdk::IGameModule* PanelApp::active_runtime_game() noexcept {
    if (game_runtime_controller_ == nullptr) return nullptr;
    return game_runtime_controller_->active_game();
}

games::LiveTimerGame* PanelApp::live_timer() const noexcept {
    return live_timer_game_.get();
}

gamesdk::GameManifest PanelApp::active_game_manifest() const {
    if (game_runtime_controller_ == nullptr || game_runtime_controller_->active_game() == nullptr) {
        return {};
    }

    return game_runtime_controller_->active_game()->manifest();
}

std::vector<gamesdk::GameTelemetryItem> PanelApp::active_game_telemetry() const {
    if (game_runtime_controller_ == nullptr || game_runtime_controller_->active_game() == nullptr) {
        return {};
    }

    return game_runtime_controller_->active_game()->telemetry();
}

host::HostSessionSnapshot PanelApp::host_session_snapshot() const {
    return host_runtime_ != nullptr ? host_runtime_->snapshot() : host::HostSessionSnapshot{};
}

host::HostAutomationConfig PanelApp::host_automation_config() const {
    return host_runtime_ != nullptr ? host_runtime_->automation_config() : host::HostAutomationConfig{};
}

host::HostPeriodicTtsConfig PanelApp::host_periodic_tts_config() const {
    return host_runtime_ != nullptr ? host_runtime_->periodic_tts_config() : host::HostPeriodicTtsConfig{};
}

tts::TtsConfig PanelApp::host_tts_runtime_config() const {
    return config_.tts_runtime;
}

std::vector<tts::TtsVoiceDescriptor> PanelApp::tts_voice_catalog() const {
    return tts_backend_ != nullptr ? tts_backend_->voice_catalog() : std::vector<tts::TtsVoiceDescriptor>{};
}

std::string PanelApp::tts_backend_name() const {
    return tts_backend_ != nullptr ? std::string(tts_backend_->backend_name()) : std::string{};
}

bool PanelApp::tts_backend_available() const noexcept {
    return tts_backend_ != nullptr && tts_backend_->available();
}

std::uint64_t PanelApp::uptime_ms() const noexcept {
    if (!initialized_ || started_at_ms_ <= 0) {
        return 0;
    }

    const auto now_ms = now_wall_clock_ms();
    return now_ms > started_at_ms_ ? static_cast<std::uint64_t>(now_ms - started_at_ms_) : 0;
}

bool PanelApp::auth_required() const noexcept {
    return license_service_ != nullptr && license_service_->access_required();
}

bool PanelApp::access_granted() const noexcept {
    return license_service_ == nullptr || license_service_->access_granted();
}

PanelAuthStatus PanelApp::auth_status() const {
    return license_service_ != nullptr ? license_service_->auth_snapshot() : PanelAuthStatus{};
}

PanelAuthLoginResult PanelApp::authenticate_access(const PanelAuthLoginRequest& request) {
    if (license_service_ == nullptr) {
        PanelAuthLoginResult result{};
        result.message = "license_service_unavailable";
        result.error_code = "license_service_unavailable";
        return result;
    }
    auto result = license_service_->authenticate(request);
    if (result.ok) {
        std::string catalog_error{};
        sync_remote_distribution_auth_context(true, &catalog_error);
        if (!catalog_error.empty()) {
            result.remote_catalog_error = std::move(catalog_error);
        }
    } else {
        sync_remote_distribution_auth_context(false);
    }
    return result;
}

void PanelApp::logout_access() noexcept {
    if (external_runner_ != nullptr && external_runner_->status().running) {
        stop_external_runner();
    } else if (host_runtime_ != nullptr) {
        host_runtime_->clear_pending_live_backlog();
    }

    if (host_runtime_ != nullptr) {
        host_runtime_->reset_session_metrics();
    }
    if (activity_log_ != nullptr) {
        activity_log_->clear();
    }

    total_external_events_submitted_ = 0;
    external_bridge_connection_state_ = "disconnected";
    external_bridge_last_status_message_ = "Access session closed";
    external_bridge_last_status_timestamp_ms_ = now_wall_clock_ms();
    external_bridge_current_room_id_.clear();
    external_bridge_last_event_kind_.clear();
    external_bridge_last_event_actor_.clear();
    external_bridge_last_event_timestamp_ms_ = 0;
    external_bridge_chat_events_ = 0;
    external_bridge_like_events_ = 0;
    external_bridge_gift_events_ = 0;
    external_bridge_follow_events_ = 0;
    external_bridge_share_events_ = 0;
    external_bridge_viewer_join_events_ = 0;
    external_bridge_viewer_count_events_ = 0;
    external_bridge_live_start_events_ = 0;
    external_bridge_live_end_events_ = 0;
    external_bridge_moderation_events_ = 0;
    external_bridge_custom_raw_events_ = 0;

    if (license_service_ != nullptr) {
        license_service_->logout();
    }
    sync_remote_distribution_auth_context(false);
}

PanelCommandResult PanelApp::start_remote_game_download(const std::string& game_id) {
    if (!initialized_ || remote_game_distribution_service_ == nullptr || game_id.empty()) {
        return {false, "game_download_unavailable"};
    }
    return remote_game_distribution_service_->start_download(game_id);
}

bool PanelApp::activate_game_by_id(const std::string& game_id) {
    if (!initialized_ || game_runtime_controller_ == nullptr || game_id.empty()) {
        return false;
    }

    refresh_external_game_manifests();

    if (find_external_game_manifest(external_game_manifests_, game_id) != nullptr) {
        return activate_external_game_by_id(game_id);
    }

    stop_external_game();
    if (!game_runtime_controller_->activate(game_id)) {
        return false;
    }

    if (host_runtime_ != nullptr) {
        host_runtime_->attach_game(game_runtime_controller_->active_game());
    }
    config_.default_game_id = game_id;
    return true;
}

bool PanelApp::pause_active_game() {
    if (has_active_external_game()) {
        return false;
    }
    if (!initialized_ || game_runtime_controller_ == nullptr || host_runtime_ == nullptr) {
        return false;
    }

    if (!game_runtime_controller_->pause()) {
        return false;
    }

    host_runtime_->set_game_dispatch_enabled(false);
    return true;
}

bool PanelApp::resume_active_game() {
    if (has_active_external_game()) {
        return false;
    }
    if (!initialized_ || game_runtime_controller_ == nullptr || host_runtime_ == nullptr) {
        return false;
    }

    if (!game_runtime_controller_->resume()) {
        return false;
    }

    host_runtime_->set_game_dispatch_enabled(true);
    return true;
}

bool PanelApp::restart_active_game() {
    if (has_active_external_game()) {
        return activate_external_game_by_id(active_external_game_id_);
    }
    if (!initialized_ || game_runtime_controller_ == nullptr) {
        return false;
    }

    if (!game_runtime_controller_->restart()) {
        return false;
    }

    if (host_runtime_ != nullptr) {
        host_runtime_->attach_game(game_runtime_controller_->active_game());
    }
    return true;
}

bool PanelApp::reset_metrics() {
    if (!initialized_ || host_runtime_ == nullptr) {
        return false;
    }

    host_runtime_->reset_session_metrics();
    return true;
}

bool PanelApp::reconnect_external_pipeline() {
    if (!initialized_) {
        return false;
    }

    const auto runner_status = external_runner_status();
    const auto target_user = !config_.external_target_user.empty()
        ? config_.external_target_user
        : external_bridge_target_user_;

    if (runner_status.running) {
        stop_external_runner();
    }

    if (bridge_controller_ != nullptr) {
        bridge_controller_->stop();
        bridge_controller_->start();
    }

    if (is_external_bridge_mode()) {
        const auto configured_port =
            config_.external_ws_port == 0 ? static_cast<std::uint16_t>(8765) : config_.external_ws_port;
        const auto ws_status = external_ws_status();
        if (!ws_status.running || ws_status.port != configured_port) {
            stop_external_ws();
            if (!start_external_ws(configured_port)) {
                return false;
            }
        }
        if (target_user.empty()) {
            external_bridge_connection_state_ = "config_error";
            external_bridge_last_status_message_ =
                "Configura external_target_user antes de reconectar TikTok";
            external_bridge_last_status_timestamp_ms_ = now_wall_clock_ms();
            return false;
        }
        return start_external_runner(target_user, 0);
    }

    return true;
}

bool PanelApp::inject_host_event(const events::HostEvent& event) {
    if (!initialized_) {
        return false;
    }

    forward_event_to_timer(event);

    const auto forwarded_to_external = forward_host_event_to_external_game(event);
    if (host_runtime_ != nullptr) {
        host_runtime_->receive_event(event, now_wall_clock_ms());
        return true;
    }

    return forwarded_to_external;
}

void PanelApp::refresh_external_game_manifests() {
    auto local_manifests = discover_external_game_manifests();
    if (remote_game_distribution_service_ != nullptr) {
        external_game_manifests_ = merge_external_manifests(
            std::move(local_manifests),
            remote_game_distribution_service_->installed_game_manifests());
        return;
    }

    external_game_manifests_ = std::move(local_manifests);
}

void PanelApp::sync_remote_distribution_auth_context(bool refresh_catalog, std::string* catalog_error_out) {
    if (remote_game_distribution_service_ == nullptr) {
        return;
    }

    remote_game_distribution_service_->update_config(config_.auth);
    if (license_service_ == nullptr || !access_granted()) {
        remote_game_distribution_service_->clear_access_context();
        refresh_external_game_manifests();
        return;
    }

    const auto auth = license_service_->auth_snapshot();
    remote_game_distribution_service_->set_access_context(
        auth.firebase_uid,
        auth.email,
        license_service_->id_token(),
        auth.license_key);
    if (refresh_catalog) {
        std::string error_message{};
        if (!remote_game_distribution_service_->refresh_catalog(&error_message) && catalog_error_out != nullptr) {
            *catalog_error_out = std::move(error_message);
        }
    }
    refresh_external_game_manifests();
}

std::size_t PanelApp::registered_game_count() const noexcept {
    return available_games().size();
}

PanelExternalGameStatus PanelApp::external_game_status() const {
    return external_game_status_cache_;
}

bool PanelApp::has_active_external_game() const noexcept {
    return !active_external_game_id_.empty();
}

bool PanelApp::activate_external_game_by_id(const std::string& game_id) {
    const auto* manifest = find_external_game_manifest(external_game_manifests_, game_id);
    if (manifest == nullptr || !manifest->entry_exists) {
        return false;
    }

    if (game_runtime_controller_ != nullptr) {
        game_runtime_controller_->activate("null-game");
        if (host_runtime_ != nullptr) {
            host_runtime_->attach_game(game_runtime_controller_->active_game());
        }
    }

    if (active_external_game_id_ == game_id
        && external_game_bridge_runner_ != nullptr
        && external_game_bridge_runner_->status().running) {
        config_.default_game_id = game_id;
        refresh_external_game_status();
        return true;
    }

    stop_external_game();
    if (external_game_bridge_runner_ == nullptr) {
        external_game_bridge_runner_ = std::make_unique<ExternalGameBridgeRunner>();
    }

    const auto started = external_game_bridge_runner_->start(ExternalGameBridgeStartRequest{
        game_id,
        manifest->module_root.string(),
    });
    if (!started) {
        active_external_game_id_.clear();
        refresh_external_game_status();
        return false;
    }

    active_external_game_id_ = game_id;
    config_.default_game_id = game_id;
    refresh_external_game_status();
    return true;
}

void PanelApp::stop_external_game() {
    if (external_game_bridge_runner_ != nullptr) {
        external_game_bridge_runner_->stop();
    }
    active_external_game_id_.clear();
    refresh_external_game_status();
}

void PanelApp::refresh_external_game_status() {
    const auto* manifest = resolve_snapshot_external_manifest(
        external_game_manifests_,
        active_external_game_id_,
        config_.default_game_id);

    if (manifest == nullptr) {
        external_game_status_cache_ = {};
        return;
    }

    PanelExternalGameStatus status{};
    status.discovered = true;
    status.installed = manifest->entry_exists;
    status.active = !active_external_game_id_.empty() && manifest->game_id == active_external_game_id_;
    status.game_id = manifest->game_id;
    status.display_name = manifest->display_name;
    status.description = manifest->description;
    status.detected_type = manifest->type;
    status.module_root = manifest->module_root.string();
    status.entry_path = manifest->entry_path.string();
    status.config_file = manifest->config_file.string();
    status.inbox_file = manifest->inbox_file.string();
    status.status_file = manifest->status_file.string();
    status.log_file = manifest->log_file.string();
    status.bridge_inbox_file = external_game_bridge_inbox_file(*manifest).string();
    status.bridge_state_file = external_game_bridge_state_file(*manifest).string();
    status.bridge_log_file = external_game_bridge_log_file(*manifest).string();

    if (external_game_bridge_runner_ != nullptr) {
        const auto runner_status = external_game_bridge_runner_->status();
        if (runner_status.game_id == manifest->game_id) {
            status.bridge_running = runner_status.running;
            status.bridge_process_id = runner_status.process_id;
            status.bridge_has_exit_code = runner_status.has_exit_code;
            status.bridge_last_exit_code = runner_status.last_exit_code;
            status.last_error = runner_status.last_error;
            status.recent_log_lines = runner_status.recent_log_lines;
        }
    }

    status = read_external_game_bridge_state(status);
    if (!status.last_error.empty() && status.state.empty()) {
        status.state = "faulted";
    } else if (status.state.empty()) {
        status.state = status.bridge_running ? "starting" : "idle";
    }
    external_game_status_cache_ = std::move(status);
}

bool PanelApp::append_external_game_event_line(const std::string& line) const {
    if (line.empty() || !has_active_external_game()) {
        return false;
    }

    const auto* manifest = find_external_game_manifest(external_game_manifests_, active_external_game_id_);
    if (manifest == nullptr) {
        return false;
    }

    const auto inbox_path = external_game_bridge_inbox_file(*manifest);
    std::error_code error;
    std::filesystem::create_directories(inbox_path.parent_path(), error);
    std::ofstream output(inbox_path, std::ios::binary | std::ios::app);
    if (!output) {
        return false;
    }

    output << line << '\n';
    return output.good();
}

bool PanelApp::forward_raw_event_to_external_game(const bridge::TikTokRawEvent& raw_event) {
    if (!has_active_external_game()) {
        return false;
    }

    const auto actor_id = resolve_external_game_event_actor_id(raw_event.actor);
    const auto actor_name = resolve_external_game_event_actor_name(raw_event.actor);
    const auto avatar_url = raw_event.actor.avatar_url;
    const auto timestamp_ms = raw_event.metadata.timestamp_ms > 0
        ? raw_event.metadata.timestamp_ms
        : now_wall_clock_ms();

    std::string kind;
    nlohmann::json data = nlohmann::json::object();
    switch (raw_event.kind) {
    case bridge::TikTokRawEventKind::chat:
        kind = "chat";
        data["message"] = raw_event.message;
        data["comment"] = raw_event.message;
        break;
    case bridge::TikTokRawEventKind::like:
        kind = "like";
        data["count"] = raw_event.like_count > 0 ? raw_event.like_count : 1;
        break;
    case bridge::TikTokRawEventKind::gift:
        kind = "gift";
        data["giftName"] = raw_event.gift.has_value() ? raw_event.gift->gift_name : std::string{"Gift"};
        data["count"] = raw_event.gift.has_value() ? raw_event.gift->repeat_count : 1;
        data["diamond"] = raw_event.gift.has_value() ? raw_event.gift->diamond_value : 0;
        data["coins"] = raw_event.gift.has_value() ? raw_event.gift->diamond_value : 0;
        break;
    case bridge::TikTokRawEventKind::follow:
        kind = "follow";
        data["count"] = 1;
        break;
    case bridge::TikTokRawEventKind::share:
        kind = "share";
        data["count"] = 1;
        break;
    case bridge::TikTokRawEventKind::viewer_join:
        kind = "join";
        data["message"] = "viewer_join";
        break;
    default:
        return false;
    }

    return append_external_game_event_line(build_external_game_event_line(
        kind,
        actor_id,
        actor_name,
        avatar_url,
        data,
        timestamp_ms));
}

bool PanelApp::forward_host_event_to_external_game(const events::HostEvent& event) {
    if (!has_active_external_game()) {
        return false;
    }

    std::string kind;
    nlohmann::json data = nlohmann::json::object();
    switch (event.kind) {
    case events::HostEventKind::chat_message:
        kind = "chat";
        data["message"] = event.message;
        data["comment"] = event.message;
        break;
    case events::HostEventKind::like:
        kind = "like";
        data["count"] = event.magnitude > 0 ? event.magnitude : 1;
        break;
    case events::HostEventKind::gift:
        kind = "gift";
        data["giftName"] = event.gift.has_value() ? event.gift->gift_name : std::string{"Gift"};
        data["count"] = event.gift.has_value() ? event.gift->quantity : 1;
        data["diamond"] = event.gift.has_value() ? event.gift->value : 0;
        data["coins"] = event.gift.has_value() ? event.gift->value : 0;
        break;
    case events::HostEventKind::follow:
        kind = "follow";
        data["count"] = 1;
        break;
    case events::HostEventKind::share:
        kind = "share";
        data["count"] = 1;
        break;
    case events::HostEventKind::viewer_join:
        kind = "join";
        data["message"] = "viewer_join";
        break;
    case events::HostEventKind::custom_raw:
        try {
            const auto json = nlohmann::json::parse(event.raw_payload);
            if (json.value("kind", std::string{}) != "avatar") {
                return false;
            }
            kind = "avatar";
            data["message"] = json.value("message", std::string{"avatar update"});
        } catch (...) {
            return false;
        }
        break;
    default:
        return false;
    }

    return append_external_game_event_line(build_external_game_event_line(
        kind,
        resolve_external_game_event_actor_id(event.actor),
        resolve_external_game_event_actor_name(event.actor),
        event.actor.avatar_url,
        data,
        event.metadata.source_timestamp_ms > 0 ? event.metadata.source_timestamp_ms : now_wall_clock_ms()));
}

void PanelApp::forward_event_to_timer(const events::HostEvent& event) {
    if (live_timer_game_ == nullptr) return;

    gamesdk::GameInputEventMapper mapper;
    auto game_event = mapper.map(event);
    host::HostSessionSnapshot snapshot{};
    if (host_runtime_ != nullptr) {
        snapshot = host_runtime_->snapshot();
    }
    live_timer_game_->on_game_input_event(game_event, snapshot);
}

} // namespace nlp3::platform
