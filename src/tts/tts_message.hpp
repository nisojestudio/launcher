#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace nlp3::tts {

enum class TtsTrigger {
    chat_event,
    scheduled_message,
    manual_message,
};

enum class TtsPriority {
    low = 0,
    normal = 1,
    high = 2,
    critical = 3,
};

enum class TtsMessageCategory {
    chat,
    gift,
    follow,
    like,
    subscriber,
    share,
    periodic,
    manual,
    system,
};

struct TtsMessage {
    TtsTrigger trigger = TtsTrigger::manual_message;
    TtsPriority priority = TtsPriority::normal;
    TtsMessageCategory category = TtsMessageCategory::manual;
    std::string text;
    std::string content_text;
    std::string actor_name;
    std::string source;
    std::int64_t created_at_ms = 0;
};

constexpr std::string_view to_string(TtsTrigger trigger) noexcept {
    switch (trigger) {
    case TtsTrigger::chat_event:
        return "chat_event";
    case TtsTrigger::scheduled_message:
        return "scheduled_message";
    case TtsTrigger::manual_message:
        return "manual_message";
    }

    return "unknown";
}

constexpr std::string_view to_string(TtsPriority priority) noexcept {
    switch (priority) {
    case TtsPriority::low:
        return "low";
    case TtsPriority::normal:
        return "normal";
    case TtsPriority::high:
        return "high";
    case TtsPriority::critical:
        return "critical";
    }

    return "normal";
}

constexpr std::string_view to_string(TtsMessageCategory category) noexcept {
    switch (category) {
    case TtsMessageCategory::chat:
        return "chat";
    case TtsMessageCategory::gift:
        return "gift";
    case TtsMessageCategory::follow:
        return "follow";
    case TtsMessageCategory::like:
        return "like";
    case TtsMessageCategory::subscriber:
        return "subscriber";
    case TtsMessageCategory::share:
        return "share";
    case TtsMessageCategory::periodic:
        return "periodic";
    case TtsMessageCategory::manual:
        return "manual";
    case TtsMessageCategory::system:
        return "system";
    }

    return "manual";
}

} // namespace nlp3::tts
