#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace nlp3::bridge {

enum class TikTokRawEventKind {
    chat,
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

struct TikTokRawActor {
    std::string user_id;
    std::string username;
    std::string display_name;
    std::string avatar_url;
    bool is_follower = false;
    bool is_subscriber = false;
    bool is_moderator = false;
};

struct TikTokRawMetadata {
    std::string event_id;
    std::string room_id;
    std::string raw_event_type;
    std::int64_t timestamp_ms = 0;
};

struct TikTokRawGiftData {
    std::string gift_id;
    std::string gift_name;
    int repeat_count = 0;
    int diamond_value = 0;
};

struct TikTokRawEvent {
    TikTokRawEventKind kind;
    TikTokRawActor actor;
    TikTokRawMetadata metadata;
    std::string message;
    std::optional<TikTokRawGiftData> gift;
    int like_count = 0;
    int viewer_count = 0;
    std::string moderation_action;
    std::string raw_payload;
};

constexpr std::string_view to_string(TikTokRawEventKind kind) noexcept {
    switch (kind) {
    case TikTokRawEventKind::chat:
        return "chat";
    case TikTokRawEventKind::like:
        return "like";
    case TikTokRawEventKind::gift:
        return "gift";
    case TikTokRawEventKind::follow:
        return "follow";
    case TikTokRawEventKind::share:
        return "share";
    case TikTokRawEventKind::viewer_join:
        return "viewer_join";
    case TikTokRawEventKind::viewer_count:
        return "viewer_count";
    case TikTokRawEventKind::live_start:
        return "live_start";
    case TikTokRawEventKind::live_end:
        return "live_end";
    case TikTokRawEventKind::moderation:
        return "moderation";
    case TikTokRawEventKind::custom_raw:
        return "custom_raw";
    }

    return "unknown";
}

} // namespace nlp3::bridge
