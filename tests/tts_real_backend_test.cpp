#include <algorithm>
#include <cassert>

#include "tts/real_tts_backend.hpp"

int main() {
    nlp3::tts::RealTtsBackend backend;
    const auto catalog = backend.voice_catalog();

    assert(catalog.size() >= 5);
    assert(std::find_if(
               catalog.begin(),
               catalog.end(),
               [](const nlp3::tts::TtsVoiceDescriptor& voice) {
                   return voice.id == "spanish-female";
               })
        != catalog.end());
    assert(std::find_if(
               catalog.begin(),
               catalog.end(),
               [](const nlp3::tts::TtsVoiceDescriptor& voice) {
                   return voice.id == "english-male";
               })
        != catalog.end());

    const auto available_count = static_cast<int>(std::count_if(
        catalog.begin(),
        catalog.end(),
        [](const nlp3::tts::TtsVoiceDescriptor& voice) { return voice.available; }));
    assert(backend.available() == (available_count > 0));

    nlp3::tts::TtsConfig config{};
    config.enabled = true;
    config.selected_voice_id = "english-female";
    config.selected_language = "en";
    backend.apply_config(config);

    nlp3::tts::TtsMessage message{};
    message.text = "Backend smoke";
    message.priority = nlp3::tts::TtsPriority::normal;
    const auto speak_result = backend.speak(message);
    assert(speak_result == backend.available());

    return 0;
}
