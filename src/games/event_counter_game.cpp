#include "games/event_counter_game.hpp"

namespace nlp3::games {

namespace {

constexpr std::string_view kCountChatKey = "count_chat";
constexpr std::string_view kGiftPointsMultiplierKey = "gift_points_multiplier";
constexpr std::string_view kFollowPointsKey = "follow_points";
constexpr std::string_view kUseDiamondCountKey = "use_diamond_count";

std::string resolve_actor_name(const gamesdk::GameInputActor& actor) {
    if (!actor.display_name.empty()) {
        return actor.display_name;
    }

    if (!actor.username.empty()) {
        return actor.username;
    }

    return "unknown";
}

std::string event_label(gamesdk::GameInputEventKind kind) {
    switch (kind) {
    case gamesdk::GameInputEventKind::chat_message:
        return "chat_message";
    case gamesdk::GameInputEventKind::gift:
        return "gift";
    case gamesdk::GameInputEventKind::follow:
        return "follow";
    case gamesdk::GameInputEventKind::share:
        return "share";
    case gamesdk::GameInputEventKind::viewer_join:
        return "viewer_join";
    case gamesdk::GameInputEventKind::viewer_count:
        return "viewer_count";
    case gamesdk::GameInputEventKind::live_start:
        return "live_start";
    case gamesdk::GameInputEventKind::live_end:
        return "live_end";
    case gamesdk::GameInputEventKind::moderation:
        return "moderation";
    case gamesdk::GameInputEventKind::custom_raw:
        return "custom_raw";
    case gamesdk::GameInputEventKind::unknown:
        break;
    }

    return "unknown";
}

} // namespace

EventCounterGame::EventCounterGame() {
    config_ = default_config();
}

std::string_view EventCounterGame::game_id() const noexcept {
    return "event-counter";
}

gamesdk::GameManifest EventCounterGame::manifest() const {
    return gamesdk::GameManifest{
        "event-counter",
        "Event Counter",
        "0.1.0",
        {},
        "Juego de prueba que cuenta eventos del panel",
        "nlp3",
        gamesdk::GameCapabilities{
            true,
            true,
            true,
            false,
            true,
            true,
            false,
        },
    };
}

gamesdk::GameConfig EventCounterGame::default_config() const {
    gamesdk::GameConfig config;
    config.set(std::string(kCountChatKey), true);
    config.set(std::string(kGiftPointsMultiplierKey), std::int64_t{1});
    config.set(std::string(kFollowPointsKey), std::int64_t{1});
    config.set(std::string(kUseDiamondCountKey), true);
    return config;
}

void EventCounterGame::apply_config(const gamesdk::GameConfig& config) {
    auto effective = default_config();

    if (const auto* value = config.find(kCountChatKey); value != nullptr) {
        effective.set(std::string(kCountChatKey), *value);
    }

    if (const auto* value = config.find(kGiftPointsMultiplierKey); value != nullptr) {
        effective.set(std::string(kGiftPointsMultiplierKey), *value);
    }

    if (const auto* value = config.find(kFollowPointsKey); value != nullptr) {
        effective.set(std::string(kFollowPointsKey), *value);
    }

    if (const auto* value = config.find(kUseDiamondCountKey); value != nullptr) {
        effective.set(std::string(kUseDiamondCountKey), *value);
    }

    config_ = std::move(effective);
}

void EventCounterGame::on_activated() {
    state_ = {};
}

void EventCounterGame::on_host_event(
    const events::HostEvent& event,
    const host::HostSessionSnapshot& session_snapshot) {
    (void)event;
    (void)session_snapshot;
}

void EventCounterGame::on_game_input_event(
    const gamesdk::GameInputEvent& event,
    const host::HostSessionSnapshot& session_snapshot) {
    (void)session_snapshot;

    const auto count_chat = config_.get_bool(kCountChatKey, true);
    const auto gift_points_multiplier =
        config_.get_int(kGiftPointsMultiplierKey, std::int64_t{1});
    const auto follow_points = config_.get_int(kFollowPointsKey, std::int64_t{1});
    const auto use_diamond_count = config_.get_bool(kUseDiamondCountKey, true);

    switch (event.kind) {
    case gamesdk::GameInputEventKind::chat_message:
        if (count_chat) {
            ++state_.chat_count;
        }
        break;
    case gamesdk::GameInputEventKind::follow:
        ++state_.follow_count;
        state_.follow_points_total += static_cast<std::uint64_t>(follow_points > 0 ? follow_points : 0);
        break;
    case gamesdk::GameInputEventKind::gift:
        ++state_.gift_count;
        if (event.gift.has_value()) {
            const auto base_points = use_diamond_count && event.gift->diamond_count > 0
                ? event.gift->diamond_count
                : event.gift->quantity;
            const auto multiplier = gift_points_multiplier > 0 ? gift_points_multiplier : std::int64_t{1};
            state_.gift_points += static_cast<std::uint64_t>(base_points * static_cast<std::uint64_t>(multiplier));
        }
        break;
    case gamesdk::GameInputEventKind::share:
    case gamesdk::GameInputEventKind::viewer_join:
    case gamesdk::GameInputEventKind::viewer_count:
    case gamesdk::GameInputEventKind::live_start:
    case gamesdk::GameInputEventKind::live_end:
    case gamesdk::GameInputEventKind::moderation:
    case gamesdk::GameInputEventKind::custom_raw:
    case gamesdk::GameInputEventKind::unknown:
        break;
    }

    state_.last_actor_name = resolve_actor_name(event.actor);
    state_.last_avatar_url = event.actor.avatar_url;
    state_.last_event_label = event_label(event.kind);
}

const EventCounterGameState& EventCounterGame::state() const noexcept {
    return state_;
}

std::vector<gamesdk::GameTelemetryItem> EventCounterGame::telemetry() const {
    return {
        {"chat_count", "Chat count", std::to_string(state_.chat_count), "neutral"},
        {"gift_count", "Gift count", std::to_string(state_.gift_count), "accent"},
        {"gift_points", "Gift points", std::to_string(state_.gift_points), "accent"},
        {"follow_count", "Follow count", std::to_string(state_.follow_count), "neutral"},
        {"follow_points_total", "Follow points", std::to_string(state_.follow_points_total), "neutral"},
        {"last_actor_name", "Last actor", state_.last_actor_name.empty() ? "-" : state_.last_actor_name, "neutral"},
        {"last_event_label", "Last event", state_.last_event_label.empty() ? "-" : state_.last_event_label, "warning"},
    };
}

const gamesdk::GameManifest& EventCounterGameFactory::manifest() const noexcept {
    return manifest_;
}

std::unique_ptr<gamesdk::IGameModule> EventCounterGameFactory::create() const {
    return std::make_unique<EventCounterGame>();
}

} // namespace nlp3::games
