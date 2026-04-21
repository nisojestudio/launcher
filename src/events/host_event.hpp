#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace nlp3::events {

enum class HostEventKind {
    chat_message,
    like,
    gift,
    follow,
    share,
    viewer_join,
    viewer_count,
    live_start,
    live_end,
    moderation,
    custom_raw,
};

struct HostActor {
    std::string id;
    std::string display_name;
    std::string avatar_url;
    bool is_follower = false;
    bool is_subscriber = false;
    bool is_moderator = false;
};

struct HostEventMetadata {
    std::string source;
    std::string source_event_type;
    std::string source_event_id;
    std::string source_room_id;
    std::int64_t source_timestamp_ms = 0;
};

struct GiftEventData {
    std::string gift_name;
    int quantity = 0;
    int value = 0;
};

struct HostEvent {
    HostEventKind kind;
    HostActor actor;
    HostEventMetadata metadata;
    std::string message;
    std::optional<GiftEventData> gift;
    int magnitude = 0;
    int viewer_count = 0;
    std::string raw_payload;
};

constexpr std::string_view to_string(HostEventKind kind) noexcept {
    switch (kind) {
    case HostEventKind::chat_message:
        return "chat_message";
    case HostEventKind::like:
        return "like";
    case HostEventKind::gift:
        return "gift";
    case HostEventKind::follow:
        return "follow";
    case HostEventKind::share:
        return "share";
    case HostEventKind::viewer_join:
        return "viewer_join";
    case HostEventKind::viewer_count:
        return "viewer_count";
    case HostEventKind::live_start:
        return "live_start";
    case HostEventKind::live_end:
        return "live_end";
    case HostEventKind::moderation:
        return "moderation";
    case HostEventKind::custom_raw:
        return "custom_raw";
    }

    return "unknown";
}

} // namespace nlp3::events
