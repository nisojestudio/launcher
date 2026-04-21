#include "host/host_runtime.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "platform/panel_activity.hpp"
#include "tts/tts_template_formatter.hpp"

namespace {

constexpr int kLikeBatchThreshold = 30;
constexpr std::int64_t kLikeBatchWindowMs = 5000;

std::string resolve_actor_name(const nlp3::events::HostActor& actor) {
    if (!actor.display_name.empty()) {
        return actor.display_name;
    }

    return actor.id;
}

std::string build_event_details(const nlp3::events::HostEvent& event) {
    if (event.kind == nlp3::events::HostEventKind::chat_message) {
        return event.message;
    }

    if (event.kind == nlp3::events::HostEventKind::gift && event.gift.has_value()) {
        std::string details = event.gift->gift_name;
        if (event.gift->quantity > 0) {
            details += " x" + std::to_string(event.gift->quantity);
        }
        return details;
    }

    if (event.kind == nlp3::events::HostEventKind::like) {
        const auto like_count = event.magnitude > 0 ? event.magnitude : 1;
        return std::to_string(like_count) + (like_count == 1 ? " like" : " likes");
    }

    if (event.kind == nlp3::events::HostEventKind::viewer_count) {
        return "viewer_count=" + std::to_string(event.viewer_count);
    }

    if (event.kind == nlp3::events::HostEventKind::live_start
        || event.kind == nlp3::events::HostEventKind::live_end
        || event.kind == nlp3::events::HostEventKind::moderation) {
        if (!event.message.empty()) {
            return event.message;
        }
    }

    if (event.kind == nlp3::events::HostEventKind::custom_raw && !event.raw_payload.empty()) {
        return event.raw_payload;
    }

    return event.metadata.source_event_type;
}

void push_activity(
    nlp3::platform::PanelActivityLog* activity_log,
    nlp3::platform::PanelActivityEntry entry) {
    if (activity_log != nullptr) {
        activity_log->push(std::move(entry));
    }
}

} // namespace

namespace nlp3::host {

HostRuntime::HostRuntime(
    gamesdk::IGameModule* active_game,
    tts::ITtsService* tts_service,
    bridge::IBridgeAdapter* bridge_adapter,
    bridge::ITikTokBridgeSession* bridge_session,
    bridge::TikTokEventMapper bridge_mapper,
    bridge::TikTokBridgeController* bridge_controller,
    HostAutomationEngine automation,
    HostPeriodicTtsEngine periodic_tts,
    platform::PanelActivityLog* activity_log) noexcept
    : active_game_(active_game),
      tts_service_(tts_service),
      bridge_adapter_(bridge_adapter),
      bridge_session_(bridge_session),
      bridge_mapper_(std::move(bridge_mapper)),
      bridge_controller_(bridge_controller),
      automation_(std::move(automation)),
      periodic_tts_(std::move(periodic_tts)),
      activity_log_(activity_log) {
}

void HostRuntime::attach_game(gamesdk::IGameModule* active_game) noexcept {
    owned_game_.reset();
    active_game_ = active_game;
    game_dispatch_enabled_ = active_game_ != nullptr;

    if (active_game_ != nullptr) {
        active_game_->on_activated();
    }
}

void HostRuntime::activate_game(std::unique_ptr<gamesdk::IGameModule> active_game) {
    owned_game_ = std::move(active_game);
    active_game_ = owned_game_.get();
    game_dispatch_enabled_ = active_game_ != nullptr;

    if (active_game_ != nullptr) {
        active_game_->on_activated();
    }
}

void HostRuntime::set_game_dispatch_enabled(bool enabled) noexcept {
    game_dispatch_enabled_ = enabled && active_game_ != nullptr;
}

void HostRuntime::process_event(const events::HostEvent& event) {
    session_state_.apply_event(event);

    push_activity(activity_log_, platform::PanelActivityEntry{
        platform::PanelActivityKind::host_event,
        std::string(events::to_string(event.kind)),
        event.metadata.source,
        resolve_actor_name(event.actor),
        build_event_details(event),
        event.metadata.source_timestamp_ms,
    });

    if (tts_service_ != nullptr && event.kind == events::HostEventKind::chat_message) {
        if (tts_service_->enqueue_chat_read(event)) {
            push_activity(activity_log_, platform::PanelActivityEntry{
                platform::PanelActivityKind::tts_chat_enqueued,
                "chat_read",
                "tts_chat",
                resolve_actor_name(event.actor),
                event.message,
                event.metadata.source_timestamp_ms,
            });
        }
    }

    const auto automation_message = automation_.build_tts_message(event, session_state_.snapshot());
    if (tts_service_ != nullptr
        && automation_message.has_value()
        && tts_service_->submit(*automation_message)) {
        push_activity(activity_log_, platform::PanelActivityEntry{
            platform::PanelActivityKind::tts_automation_enqueued,
            std::string(events::to_string(event.kind)),
            "tts_automation",
            resolve_actor_name(event.actor),
            automation_message->text,
            event.metadata.source_timestamp_ms,
        });
    }

    if (active_game_ != nullptr && game_dispatch_enabled_) {
        active_game_->on_host_event(event, session_state_.snapshot());
        active_game_->on_game_input_event(game_input_mapper_.map(event), session_state_.snapshot());
    }

    (void)bridge_adapter_;
}

std::string HostRuntime::like_batch_key_for(const events::HostEvent& event) {
    const auto actor_id = !event.actor.id.empty()
        ? event.actor.id
        : (!event.actor.display_name.empty() ? event.actor.display_name : event.metadata.source_event_id);
    return event.metadata.source + ":" + actor_id;
}

void HostRuntime::flush_like_batch(std::string_view key) {
    const auto found = pending_like_batches_.find(std::string(key));
    if (found == pending_like_batches_.end()) {
        return;
    }

    const auto batch = found->second;
    pending_like_batches_.erase(found);

    if (batch.total_magnitude <= 0) {
        return;
    }

    process_event(events::HostEvent{
        events::HostEventKind::like,
        batch.actor,
        batch.metadata,
        {},
        std::nullopt,
        batch.total_magnitude,
        0,
        {},
    });
}

void HostRuntime::flush_due_like_batches(std::int64_t observed_at_ms) {
    if (observed_at_ms <= 0 || pending_like_batches_.empty()) {
        return;
    }

    std::vector<std::string> ready_keys{};
    ready_keys.reserve(pending_like_batches_.size());
    for (const auto& [key, batch] : pending_like_batches_) {
        if (batch.window_started_at_ms > 0
            && observed_at_ms - batch.window_started_at_ms >= kLikeBatchWindowMs) {
            ready_keys.push_back(key);
        }
    }

    for (const auto& key : ready_keys) {
        flush_like_batch(key);
    }
}

void HostRuntime::receive_event(const events::HostEvent& event, std::int64_t observed_at_ms) {
    const auto resolved_observed_at_ms = observed_at_ms > 0
        ? observed_at_ms
        : event.metadata.source_timestamp_ms;

    flush_due_like_batches(resolved_observed_at_ms);

    if (event.kind != events::HostEventKind::like) {
        process_event(event);
        return;
    }

    auto batch = PendingLikeBatch{};
    const auto key = like_batch_key_for(event);
    if (const auto found = pending_like_batches_.find(key); found != pending_like_batches_.end()) {
        batch = found->second;
    }

    if (batch.window_started_at_ms <= 0) {
        batch.actor = event.actor;
        batch.metadata = event.metadata;
        batch.window_started_at_ms = resolved_observed_at_ms;
        batch.last_source_timestamp_ms = event.metadata.source_timestamp_ms;
        batch.total_magnitude = 0;
    } else {
        batch.actor = event.actor;
        batch.metadata = event.metadata;
        if (event.metadata.source_timestamp_ms > 0) {
            batch.last_source_timestamp_ms = event.metadata.source_timestamp_ms;
        }
    }

    if (batch.last_source_timestamp_ms > 0) {
        batch.metadata.source_timestamp_ms = batch.last_source_timestamp_ms;
    }
    batch.total_magnitude += event.magnitude > 0 ? event.magnitude : 1;
    pending_like_batches_[key] = batch;

    if (batch.total_magnitude >= kLikeBatchThreshold) {
        flush_like_batch(key);
    }
}

std::size_t HostRuntime::tick_bridge(std::size_t max_events, std::int64_t observed_at_ms) {
    std::vector<bridge::TikTokRawEvent> raw_events{};

    if (bridge_controller_ != nullptr) {
        bridge_controller_->poll_into(raw_events, max_events);
    } else if (bridge_session_ != nullptr) {
        raw_events = bridge_session_->poll(max_events);
    } else {
        return 0;
    }

    std::size_t accepted = 0;

    for (const auto& raw_event : raw_events) {
        auto mapped = bridge_mapper_.map(raw_event);
        if (!mapped.has_value()) {
            continue;
        }

        receive_event(*mapped, observed_at_ms);
        ++accepted;
    }

    return accepted;
}

bool HostRuntime::tick_like_batches(std::int64_t observed_at_ms) {
    const auto pending_before = pending_like_batches_.size();
    flush_due_like_batches(observed_at_ms);
    return pending_like_batches_.size() != pending_before;
}

bool HostRuntime::tick_periodic_tts(std::uint64_t now_ms) {
    const auto message = periodic_tts_.take_next_message(now_ms);
    if (message.empty() || tts_service_ == nullptr) {
        return false;
    }

    tts::TtsTemplateContext context{};
    if (session_state_.snapshot().last_actor.has_value()) {
        context.user = resolve_actor_name(*session_state_.snapshot().last_actor);
    }
    if (session_state_.snapshot().last_event.has_value()) {
        context.message = session_state_.snapshot().last_event->message;
        context.viewers = session_state_.snapshot().last_event->viewer_count;
        if (session_state_.snapshot().last_event->gift.has_value()) {
            context.gift = session_state_.snapshot().last_event->gift->gift_name;
            context.count = session_state_.snapshot().last_event->gift->quantity;
        } else {
            context.count = session_state_.snapshot().last_event->magnitude;
        }
    }

    const auto formatted = tts::format_tts_template(message, context);
    if (formatted.empty()) {
        return false;
    }

    if (!tts_service_->submit(tts::TtsMessage{
            tts::TtsTrigger::scheduled_message,
            tts::TtsPriority::low,
            tts::TtsMessageCategory::periodic,
            formatted,
            formatted,
            "",
            "periodic",
            static_cast<std::int64_t>(now_ms),
        })) {
        return false;
    }

    push_activity(activity_log_, platform::PanelActivityEntry{
        platform::PanelActivityKind::tts_periodic_enqueued,
        "periodic_tts",
        "tts_periodic",
        "",
        formatted,
        static_cast<std::int64_t>(now_ms),
    });
    return true;
}

void HostRuntime::apply_automation_config(const HostAutomationConfig& config) {
    automation_.set_config(config);
}

void HostRuntime::apply_periodic_tts_config(const HostPeriodicTtsConfig& config) {
    periodic_tts_.set_config(config);
    periodic_tts_.reset();
}

void HostRuntime::apply_bridge_mapper_config(const bridge::TikTokBridgeConfig& config) {
    bridge_mapper_ = bridge::TikTokEventMapper{config};
}

HostBridgeStatus HostRuntime::bridge_status() const {
    HostBridgeStatus status{};

    if (bridge_controller_ != nullptr) {
        const auto health = bridge_controller_->health();
        status.integrated = health.available;
        status.state = bridge_controller_->state();
        status.metrics = bridge::TikTokBridgeMetrics{
            health.raw_events_received,
            health.raw_events_emitted,
            health.raw_events_dropped,
            health.poll_calls,
            health.empty_polls,
            health.fault_count,
            health.last_event_timestamp_ms,
        };
        status.last_fault = health.last_fault;
        return status;
    }

    if (bridge_session_ == nullptr) {
        status.integrated = false;
        return status;
    }

    status.state = bridge_session_->state();
    status.metrics = bridge_session_->metrics();
    status.last_fault = bridge_session_->last_fault();
    return status;
}

bool HostRuntime::queue_tts_announcement(std::string_view message) {
    return tts_service_ != nullptr && tts_service_->enqueue_announcement(message);
}

std::size_t HostRuntime::flush_tts(std::size_t max_messages) {
    return tts_service_ != nullptr ? tts_service_->dispatch_pending(max_messages) : 0;
}

void HostRuntime::clear_pending_live_backlog() noexcept {
    pending_like_batches_.clear();
    periodic_tts_.reset();
    if (tts_service_ != nullptr) {
        tts_service_->clear_pending();
    }
}

void HostRuntime::reset_session_metrics() noexcept {
    session_state_.reset();
    pending_like_batches_.clear();
}

const HostAutomationConfig& HostRuntime::automation_config() const noexcept {
    return automation_.config();
}

const HostPeriodicTtsConfig& HostRuntime::periodic_tts_config() const noexcept {
    return periodic_tts_.config();
}

const HostSessionSnapshot& HostRuntime::snapshot() const noexcept {
    return session_state_.snapshot();
}

gamesdk::HostCompatibilityProfile HostRuntime::compatibility_profile() const noexcept {
    return gamesdk::HostCompatibilityProfile{
        1,
        tts_service_ != nullptr,
        "windows",
    };
}

std::string_view HostRuntime::active_game_id() const noexcept {
    return active_game_ != nullptr ? active_game_->game_id() : std::string_view{};
}

bool HostRuntime::has_active_game() const noexcept {
    return active_game_ != nullptr;
}

bool HostRuntime::game_dispatch_enabled() const noexcept {
    return game_dispatch_enabled_;
}

bool HostRuntime::has_tts_service() const noexcept {
    return tts_service_ != nullptr && tts_service_->available();
}

bool HostRuntime::tts_available() const noexcept {
    return has_tts_service();
}

std::size_t HostRuntime::queued_tts_messages() const noexcept {
    return tts_service_ != nullptr ? tts_service_->queued_message_count() : 0;
}

} // namespace nlp3::host
