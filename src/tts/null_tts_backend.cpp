#include "tts/null_tts_backend.hpp"

namespace nlp3::tts {

std::string_view NullTtsBackend::backend_name() const noexcept {
    return "null-tts-backend";
}

bool NullTtsBackend::available() const noexcept {
    return false;
}

void NullTtsBackend::apply_config(const TtsConfig& config) {
    (void)config;
}

std::vector<TtsVoiceDescriptor> NullTtsBackend::voice_catalog() const {
    return build_curated_voice_catalog();
}

bool NullTtsBackend::speak(const TtsMessage& message) {
    (void)message;
    return false;
}

std::size_t NullTtsBackend::queued_message_count() const noexcept {
    return 0;
}

void NullTtsBackend::clear_pending() noexcept {
}

} // namespace nlp3::tts
