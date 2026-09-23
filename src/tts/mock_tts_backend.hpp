#pragma once

#include <vector>

#include "tts/tts_backend.hpp"

namespace nlp3::tts {

class MockTtsBackend final : public ITtsBackend {
public:
    std::string_view backend_name() const noexcept override;
    bool available() const noexcept override;
    void apply_config(const TtsConfig& config) override;
    std::vector<TtsVoiceDescriptor> voice_catalog() const override;
    bool speak(const TtsMessage& message) override;
    std::size_t queued_message_count() const noexcept override;
    void clear_pending() noexcept override;

    const std::vector<TtsMessage>& spoken_messages() const noexcept;

    // Test hook: hace fallar las siguientes N llamadas a speak().
    void set_fail_speak_count(std::size_t count) noexcept;

private:
    TtsConfig config_{};
    std::vector<TtsMessage> spoken_messages_{};
    std::size_t fail_speak_count_ = 0;
};

} // namespace nlp3::tts
