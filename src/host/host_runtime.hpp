#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "bridge/bridge_adapter.hpp"
#include "bridge/tiktok_bridge_controller.hpp"
#include "bridge/tiktok_bridge_session.hpp"
#include "bridge/tiktok_event_mapper.hpp"
#include "events/host_event.hpp"
#include "gamesdk/game_factory.hpp"
#include "gamesdk/game_input_mapper.hpp"
#include "gamesdk/game_module.hpp"
#include "host/host_automation.hpp"
#include "host/host_periodic_tts.hpp"
#include "host/session_state.hpp"
#include "tts/tts_service.hpp"

namespace nlp3::platform {

class PanelActivityLog;

} // namespace nlp3::platform

namespace nlp3::host {

struct HostBridgeStatus {
    std::string provider = "tiktok";
    bridge::TikTokBridgeSessionState state = bridge::TikTokBridgeSessionState::stopped;
    bridge::TikTokBridgeMetrics metrics{};
    std::optional<bridge::TikTokBridgeFault> last_fault;
    bool integrated = true;
};

class HostRuntime {
public:
    HostRuntime(
        gamesdk::IGameModule* active_game,
        tts::ITtsService* tts_service,
        bridge::IBridgeAdapter* bridge_adapter,
        bridge::ITikTokBridgeSession* bridge_session = nullptr,
        bridge::TikTokEventMapper bridge_mapper = bridge::TikTokEventMapper{},
        bridge::TikTokBridgeController* bridge_controller = nullptr,
        HostAutomationEngine automation = HostAutomationEngine{},
        HostPeriodicTtsEngine periodic_tts = HostPeriodicTtsEngine{},
        platform::PanelActivityLog* activity_log = nullptr) noexcept;

    void attach_game(gamesdk::IGameModule* active_game) noexcept;
    void activate_game(std::unique_ptr<gamesdk::IGameModule> active_game);
    void set_game_dispatch_enabled(bool enabled) noexcept;
    void receive_event(const events::HostEvent& event, std::int64_t observed_at_ms = 0);
    std::size_t tick_bridge(std::size_t max_events = 0, std::int64_t observed_at_ms = 0);
    bool tick_like_batches(std::int64_t observed_at_ms);
    bool tick_periodic_tts(std::uint64_t now_ms);
    void apply_automation_config(const HostAutomationConfig& config);
    void apply_periodic_tts_config(const HostPeriodicTtsConfig& config);
    void apply_bridge_mapper_config(const bridge::TikTokBridgeConfig& config);
    HostBridgeStatus bridge_status() const;
    bool queue_tts_announcement(std::string_view message);
    std::size_t flush_tts(std::size_t max_messages = 0);
    void clear_pending_tts() noexcept;
    void clear_pending_live_backlog() noexcept;
    void reset_session_metrics() noexcept;
    const HostAutomationConfig& automation_config() const noexcept;
    const HostPeriodicTtsConfig& periodic_tts_config() const noexcept;

    const HostSessionSnapshot& snapshot() const noexcept;
    gamesdk::HostCompatibilityProfile compatibility_profile() const noexcept;
    std::string_view active_game_id() const noexcept;
    bool has_active_game() const noexcept;
    bool game_dispatch_enabled() const noexcept;
    bool has_tts_service() const noexcept;
    bool tts_available() const noexcept;
    std::size_t queued_tts_messages() const noexcept;

private:
    struct PendingLikeBatch {
        events::HostActor actor{};
        events::HostEventMetadata metadata{};
        std::int64_t window_started_at_ms = 0;
        std::int64_t last_source_timestamp_ms = 0;
        int total_magnitude = 0;
    };

    void process_event(const events::HostEvent& event);
    void flush_due_like_batches(std::int64_t observed_at_ms);
    void flush_like_batch(std::string_view key);
    static std::string like_batch_key_for(const events::HostEvent& event);

    HostSessionState session_state_{};
    std::unique_ptr<gamesdk::IGameModule> owned_game_{};
    gamesdk::IGameModule* active_game_ = nullptr;
    tts::ITtsService* tts_service_ = nullptr;
    bridge::IBridgeAdapter* bridge_adapter_ = nullptr;
    bridge::ITikTokBridgeSession* bridge_session_ = nullptr;
    bridge::TikTokEventMapper bridge_mapper_;
    bridge::TikTokBridgeController* bridge_controller_ = nullptr;
    HostAutomationEngine automation_{};
    HostPeriodicTtsEngine periodic_tts_{};
    gamesdk::GameInputEventMapper game_input_mapper_{};
    platform::PanelActivityLog* activity_log_ = nullptr;
    std::unordered_map<std::string, PendingLikeBatch> pending_like_batches_{};
    bool game_dispatch_enabled_ = true;
};

} // namespace nlp3::host
