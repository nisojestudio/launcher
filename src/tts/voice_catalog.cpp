#include "tts/voice_catalog.hpp"

#include <algorithm>
#include <cctype>

namespace nlp3::tts {

namespace {

std::string normalize_key(std::string_view value) {
    std::string normalized{};
    normalized.reserve(value.size());
    for (const auto ch : value) {
        normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return normalized;
}

} // namespace

TtsChatFilterMode parse_tts_chat_filter_mode(std::string_view value) noexcept {
    const auto normalized = normalize_key(value);
    if (normalized == "followers_only" || normalized == "followers") {
        return TtsChatFilterMode::followers_only;
    }
    if (normalized == "subscribers_only" || normalized == "subscribers") {
        return TtsChatFilterMode::subscribers_only;
    }
    if (normalized == "moderators_only" || normalized == "moderators") {
        return TtsChatFilterMode::moderators_only;
    }
    return TtsChatFilterMode::everyone;
}

std::vector<TtsVoiceDescriptor> build_curated_voice_catalog() {
    return {
        {"spanish-female", "Spanish female", "es", "female", false, ""},
        {"spanish-male", "Spanish male", "es", "male", false, ""},
        {"spanish-neutral", "Spanish neutral", "es", "neutral", false, ""},
        {"english-female", "English female", "en", "female", false, ""},
        {"english-male", "English male", "en", "male", false, ""},
    };
}

} // namespace nlp3::tts
