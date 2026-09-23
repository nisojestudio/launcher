#include "tts/mock_tts_backend.hpp"

namespace nlp3::tts {

std::string_view MockTtsBackend::backend_name() const noexcept {
    return "mock-tts-backend";
}

bool MockTtsBackend::available() const noexcept {
    return true;
}

void MockTtsBackend::apply_config(const TtsConfig& config) {
    config_ = config;
}

std::vector<TtsVoiceDescriptor> MockTtsBackend::voice_catalog() const {
    auto catalog = build_curated_voice_catalog();
    for (auto& voice : catalog) {
        if (voice.id == "spanish-neutral" || voice.id == "english-female") {
            voice.available = true;
            voice.backend_voice_id = voice.id;
        }
    }
    return catalog;
}

bool MockTtsBackend::speak(const TtsMessage& message) {
    if (fail_speak_count_ > 0) {
        --fail_speak_count_;
        return false;
    }
    if (!config_.enabled) {
        return false;
    }
    spoken_messages_.push_back(message);
    return true;
}

std::size_t MockTtsBackend::queued_message_count() const noexcept {
    return 0;
}

void MockTtsBackend::clear_pending() noexcept {
}

const std::vector<TtsMessage>& MockTtsBackend::spoken_messages() const noexcept {
    return spoken_messages_;
}

void MockTtsBackend::set_fail_speak_count(std::size_t count) noexcept {
    fail_speak_count_ = count;
}

} // namespace nlp3::tts
