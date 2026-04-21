#pragma once

#include "tts/tts_backend.hpp"

namespace nlp3::tts {

class NullTtsBackend final : public ITtsBackend {
public:
    std::string_view backend_name() const noexcept override;
    bool available() const noexcept override;
    void apply_config(const TtsConfig& config) override;
    std::vector<TtsVoiceDescriptor> voice_catalog() const override;
    bool speak(const TtsMessage& message) override;
    std::size_t queued_message_count() const noexcept override;
    void clear_pending() noexcept override;
};

} // namespace nlp3::tts
