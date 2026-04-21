#pragma once

#include <string>
#include <string_view>
#include <variant>

namespace nlp3::live {

enum class LiveEventKind {
    chat_message,
    reaction,
    gift,
    follow,
    share,
};

struct LiveActor {
    std::string id;
    std::string display_name;
};

struct ChatMessageData {
    std::string text;
};

struct ReactionData {
    int count = 0;
};

struct GiftData {
    std::string name;
    int quantity = 0;
    int value = 0;
};

struct FollowData {};

struct ShareData {};

using LiveEventPayload = std::variant<ChatMessageData, ReactionData, GiftData, FollowData, ShareData>;

struct LiveEvent {
    std::string source;
    LiveEventKind kind;
    LiveActor actor;
    LiveEventPayload payload;
};

struct ExternalEvent {
    std::string source;
    std::string raw_type;
    std::string actor_id;
    std::string actor_name;
    std::string text;
    std::string asset_name;
    int quantity = 0;
    int value = 0;
};

constexpr std::string_view to_string(LiveEventKind kind) noexcept {
    switch (kind) {
    case LiveEventKind::chat_message:
        return "chat_message";
    case LiveEventKind::reaction:
        return "reaction";
    case LiveEventKind::gift:
        return "gift";
    case LiveEventKind::follow:
        return "follow";
    case LiveEventKind::share:
        return "share";
    }

    return "unknown";
}

} // namespace nlp3::live
