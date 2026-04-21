#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace nlp3::tts {

enum class TtsChatFilterMode {
    everyone,
    followers_only,
    subscribers_only,
    moderators_only,
};

struct TtsVoiceDescriptor {
    std::string id;
    std::string display_name;
    std::string language;
    std::string gender;
    bool available = false;
    std::string backend_voice_id;
};

constexpr std::string_view to_string(TtsChatFilterMode mode) noexcept {
    switch (mode) {
    case TtsChatFilterMode::everyone:
        return "everyone";
    case TtsChatFilterMode::followers_only:
        return "followers_only";
    case TtsChatFilterMode::subscribers_only:
        return "subscribers_only";
    case TtsChatFilterMode::moderators_only:
        return "moderators_only";
    }

    return "everyone";
}

TtsChatFilterMode parse_tts_chat_filter_mode(std::string_view value) noexcept;
std::vector<TtsVoiceDescriptor> build_curated_voice_catalog();

} // namespace nlp3::tts
