#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "events/host_event.hpp"
#include "tts/tts_message.hpp"
#include "tts/voice_catalog.hpp"

namespace nlp3::tts {

struct TtsPolicy {
    bool allow_chat_messages = true;
    bool allow_scheduled_messages = true;
    bool allow_manual_messages = true;
    bool include_actor_name_for_chat = false;
    std::size_t min_text_length = 1;
    TtsChatFilterMode chat_filter_mode = TtsChatFilterMode::everyone;
    std::uint64_t chat_cooldown_ms = 0;
    std::string chat_message_template = "{message}";
};

bool allows_message(const TtsPolicy& policy, const TtsMessage& message) noexcept;
bool allows_chat_actor(const TtsPolicy& policy, const events::HostActor& actor) noexcept;

} // namespace nlp3::tts
