#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include "tts/tts_message.hpp"
#include "tts/tts_config.hpp"
#include "tts/voice_catalog.hpp"

namespace nlp3::tts {

class ITtsBackend {
public:
    virtual ~ITtsBackend() = default;

    virtual std::string_view backend_name() const noexcept = 0;
    virtual bool available() const noexcept = 0;
    virtual void apply_config(const TtsConfig& config) = 0;
    virtual std::vector<TtsVoiceDescriptor> voice_catalog() const = 0;
    // P2.3/M7: re-escanear voces del sistema bajo demanda (POST/GET con
    // refresh). Default no-op para mocks y NullTtsBackend.
    virtual void refresh_voice_catalog() {}
    virtual bool speak(const TtsMessage& message) = 0;
    virtual std::size_t queued_message_count() const noexcept = 0;
    virtual void clear_pending() noexcept = 0;
};

} // namespace nlp3::tts
