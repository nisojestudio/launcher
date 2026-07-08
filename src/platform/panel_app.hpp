#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "events/host_event.hpp"
#include "gamesdk/game_catalog.hpp"
#include "gamesdk/game_module.hpp"
#include "host/host_automation.hpp"
#include "host/host_periodic_tts.hpp"
#include "host/session_state.hpp"
#include "bridge/tiktok_external_inbox_adapter.hpp"
#include "bridge/tiktok_external_ws_server.hpp"
#include "platform/external_bridge_manifest.hpp"
#include "platform/external_game_manifest.hpp"
#include "platform/external_game_state.hpp"
#include "platform/panel_http_server.hpp"
#include "platform/external_bridge_runner.hpp"
#include "platform/external_game_bridge_runner.hpp"
#include "platform/panel_command.hpp"
#include "platform/panel_config.hpp"
#include "platform/panel_diagnostics.hpp"
#include "platform/panel_run_result.hpp"
#include "platform/panel_snapshot.hpp"
#include "platform/panel_tick_result.hpp"
#include "games/live_timer_game.hpp"
#include "gamesdk/game_input_mapper.hpp"
#include "platform/cloudflare_tunnel_service.hpp"
#include "platform/panel_updater_service.hpp"

namespace nlp3 {
namespace bridge {
class ITikTokBridgeSession;
class TikTokBridgeController;
struct TikTokExternalSessionStatus;
struct TikTokRawEvent;
}
namespace host {
class HostRuntime;
}
namespace gamesdk {
class GameRegistry;
class GameRuntimeController;
class GameFactoryRegistry;
}
namespace tts {
class ITtsBackend;
class HostTtsService;
struct TtsVoiceDescriptor;
}
namespace platform {
class IGameCatalogSource;
class PanelController;
class PanelActivityLog;
class PanelConfigStorage;
class ServerLicenseService;
class RemoteGameDistributionService;
struct PanelAuthLoginRequest;
struct PanelAuthLoginResult;
}
} // namespace nlp3

namespace nlp3::platform {

class PanelApp {
public:
    PanelApp();
    ~PanelApp();

    bool initialize(const std::string& config_path = "panel_config.json");
    bool save_config(const std::string& config_path = "panel_config.json") const;
    bool apply_live_config();
    bool reload_config(const std::string& config_path = "panel_config.json");

    PanelSnapshot snapshot() const;
    PanelDiagnosticsReport diagnostics() const;
    PanelCommandResult execute_command(const PanelCommand& command);
    PanelTickResult tick(std::uint64_t now_ms = 0);
    PanelRunResult run_ticks(
        std::size_t tick_count,
        std::uint64_t start_now_ms = 0,
        std::uint64_t step_ms = 0);

    PanelConfig& config() noexcept;
    const PanelConfig& config() const noexcept;

    ExternalBridgeManifest external_bridge_manifest() const;
    bool is_external_bridge_mode() const noexcept;
    bool start_external_bridge_recording(const std::string& path);
    void stop_external_bridge_recording();
    bool is_external_bridge_recording() const noexcept;
    std::string external_bridge_recording_path() const;
    std::size_t replay_external_bridge_file(const std::string& path);
    bool record_external_bridge_event(const bridge::TikTokRawEvent& raw_event, const std::string& path);
    bool submit_external_bridge_event(const bridge::TikTokRawEvent& raw_event);
    bool submit_external_session_status(const bridge::TikTokExternalSessionStatus& status);
    bool start_external_ws(std::uint16_t port = 8765);
    void stop_external_ws();
    bridge::TikTokExternalWsStatus external_ws_status() const;
    bool start_external_runner(const std::string& target_user = {}, std::uint64_t max_seconds = 0);
    void stop_external_runner();
    ExternalBridgeRunnerStatus external_runner_status() const;
    bool start_http_ui(std::uint16_t port = 8080);
    void stop_http_ui();
    PanelHttpServerStatus http_ui_status() const;
    bool submit_external_ws_payload(const std::string& payload);
    bridge::TikTokExternalInboxResult process_external_inbox(const std::string& inbox_dir);
    bridge::TikTokExternalInboxResult process_external_inbox_and_tick(
        const std::string& inbox_dir,
        std::uint64_t now_ms = 0);
    std::size_t tick_bridge(std::size_t max_events = 0);
    bool tick_periodic_tts(std::uint64_t now_ms);
    std::vector<std::string> available_game_ids() const;
    std::vector<gamesdk::GameCatalogEntry> available_games() const;
    gamesdk::GameManifest active_game_manifest() const;
    std::vector<gamesdk::GameTelemetryItem> active_game_telemetry() const;
    const gamesdk::IGameModule* active_runtime_game() const noexcept;
    gamesdk::IGameModule* active_runtime_game() noexcept;
    host::HostSessionSnapshot host_session_snapshot() const;
    host::HostAutomationConfig host_automation_config() const;
    host::HostPeriodicTtsConfig host_periodic_tts_config() const;
    tts::TtsConfig host_tts_runtime_config() const;
    std::vector<tts::TtsVoiceDescriptor> tts_voice_catalog() const;
    std::string tts_backend_name() const;
    bool tts_backend_available() const noexcept;
    std::uint64_t uptime_ms() const noexcept;
    bool auth_required() const noexcept;
    bool access_granted() const noexcept;
    PanelAuthStatus auth_status() const;
    PanelAuthLoginResult authenticate_access(const PanelAuthLoginRequest& request);
    void logout_access() noexcept;
    bool trigger_panel_update();
    PanelCommandResult start_remote_game_download(const std::string& game_id);
    bool activate_game_by_id(const std::string& game_id);
    bool pause_active_game();
    bool resume_active_game();
    bool restart_active_game();
    bool reset_metrics();
    bool reconnect_external_pipeline();
    bool inject_host_event(const events::HostEvent& event);
    std::size_t registered_game_count() const noexcept;
    PanelExternalGameStatus external_game_status() const;
    bool has_active_external_game() const noexcept;

    games::LiveTimerGame* live_timer() const noexcept;
    bool save_timer_state();

private:
    bool load_timer_state();
    void forward_event_to_timer(const events::HostEvent& event);
    bool activate_external_game_by_id(const std::string& game_id);
    void stop_external_game();
    void refresh_external_game_status();
    void refresh_external_game_manifests();
    void sync_remote_distribution_auth_context(bool refresh_catalog, std::string* catalog_error_out = nullptr);
    bool append_external_game_event_line(const std::string& line) const;
    bool forward_raw_event_to_external_game(const bridge::TikTokRawEvent& raw_event);
    bool forward_host_event_to_external_game(const events::HostEvent& event);

    PanelConfig config_{};

    std::unique_ptr<CloudflareTunnelService> tunnel_service_{};
    std::unique_ptr<PanelConfigStorage> config_storage_{};
    std::unique_ptr<PanelActivityLog> activity_log_{};
    std::unique_ptr<IGameCatalogSource> game_catalog_source_{};
    std::unique_ptr<ServerLicenseService> license_service_{};
    std::unique_ptr<RemoteGameDistributionService> remote_game_distribution_service_{};
    std::unique_ptr<PanelUpdaterService> panel_updater_service_{};

    std::unique_ptr<bridge::ITikTokBridgeSession> bridge_session_{};
    std::unique_ptr<bridge::TikTokBridgeController> bridge_controller_{};
    std::unique_ptr<bridge::TikTokExternalWsServer> external_ws_server_{};
    std::unique_ptr<ExternalBridgeRunner> external_runner_{};
    std::unique_ptr<ExternalGameBridgeRunner> external_game_bridge_runner_{};
    std::unique_ptr<PanelHttpServer> http_ui_server_{};

    std::unique_ptr<tts::ITtsBackend> tts_backend_{};
    std::unique_ptr<tts::HostTtsService> tts_service_{};

    std::unique_ptr<gamesdk::GameFactoryRegistry> game_factories_{};
    std::unique_ptr<gamesdk::GameRegistry> game_registry_{};
    std::unique_ptr<gamesdk::GameRuntimeController> game_runtime_controller_{};

    std::unique_ptr<host::HostRuntime> host_runtime_{};
    std::unique_ptr<PanelController> panel_controller_{};
    std::unique_ptr<games::LiveTimerGame> live_timer_game_{};
    // P1: cached mapper reused across all timer event forwards (was new per event).
    gamesdk::GameInputEventMapper timer_event_mapper_{};

    std::string config_path_ = "panel_config.json";
    std::string timer_save_path_{};
    std::uint64_t last_timer_save_ms_ = 0;
    std::int64_t last_saved_event_counter_ = -1;  // B7: skip auto-save if no new events
    std::vector<ExternalGameManifest> external_game_manifests_{};
    std::string active_external_game_id_{};
    PanelExternalGameStatus external_game_status_cache_{};
    bool external_bridge_recording_ = false;
    std::string external_bridge_recording_path_{};
    std::string external_bridge_last_replay_path_{};
    std::size_t external_bridge_last_replay_accepted_events_ = 0;
    std::size_t total_external_events_submitted_ = 0;
    std::string external_bridge_target_user_{};
    std::string external_bridge_connection_state_{};
    std::string external_bridge_last_status_message_{};
    std::int64_t external_bridge_last_status_timestamp_ms_ = 0;
    std::string external_bridge_current_room_id_{};
    std::string external_bridge_last_event_kind_{};
    std::string external_bridge_last_event_actor_{};
    std::int64_t external_bridge_last_event_timestamp_ms_ = 0;
    std::size_t external_bridge_chat_events_ = 0;
    std::size_t external_bridge_like_events_ = 0;
    std::size_t external_bridge_gift_events_ = 0;
    std::size_t external_bridge_follow_events_ = 0;
    std::size_t external_bridge_share_events_ = 0;
    std::size_t external_bridge_viewer_join_events_ = 0;
    std::size_t external_bridge_viewer_count_events_ = 0;
    std::size_t external_bridge_live_start_events_ = 0;
    std::size_t external_bridge_live_end_events_ = 0;
    std::size_t external_bridge_moderation_events_ = 0;
    std::size_t external_bridge_custom_raw_events_ = 0;
    std::int64_t started_at_ms_ = 0;
    bool initialized_ = false;
};

} // namespace nlp3::platform
