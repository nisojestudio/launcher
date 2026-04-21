#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace nlp3::gamesdk {

enum class GameInputEventKind {
    unknown,
    chat_message,
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

struct GameInputActor {
    std::string id;
    std::string username;
    std::string display_name;
    std::string avatar_url;
};

struct GameInputGift {
    std::string gift_id;
    std::string gift_name;
    std::uint32_t quantity = 0;
    std::uint32_t diamond_count = 0;
};

struct GameInputMetadata {
    std::string source;
    std::string source_event_id;
    std::string source_room_id;
    std::string source_event_type;
    std::int64_t source_timestamp_ms = 0;
};

struct GameInputEvent {
    GameInputEventKind kind = GameInputEventKind::unknown;
    GameInputActor actor{};
    std::string text;
    std::optional<GameInputGift> gift;
    std::uint32_t viewer_count = 0;
    std::string raw_payload;
    GameInputMetadata metadata{};
};

} // namespace nlp3::gamesdk
