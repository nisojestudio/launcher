#include "tts/tts_policy.hpp"

namespace nlp3::tts {

bool allows_message(const TtsPolicy& policy, const TtsMessage& message) noexcept {
    switch (message.trigger) {
    case TtsTrigger::chat_event:
        return policy.allow_chat_messages;
    case TtsTrigger::scheduled_message:
        return policy.allow_scheduled_messages;
    case TtsTrigger::manual_message:
        return policy.allow_manual_messages;
    }

    return false;
}

bool allows_chat_actor(const TtsPolicy& policy, const events::HostActor& actor) noexcept {
    switch (policy.chat_filter_mode) {
    case TtsChatFilterMode::everyone:
        return true;
    case TtsChatFilterMode::followers_only:
        return actor.is_follower || actor.is_subscriber || actor.is_moderator;
    case TtsChatFilterMode::subscribers_only:
        return actor.is_subscriber || actor.is_moderator;
    case TtsChatFilterMode::moderators_only:
        return actor.is_moderator;
    }

    return true;
}

} // namespace nlp3::tts
