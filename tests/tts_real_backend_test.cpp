#include <algorithm>
#include <cassert>
#include <cstdlib>

#include "tts/real_tts_backend.hpp"

int main() {
    // B4: por defecto NO Speak audible en CI. Opt-in con NLP3_TTS_AUDIBLE_TESTS=1.
    const bool audible = std::getenv("NLP3_TTS_AUDIBLE_TESTS") != nullptr;

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

    // P2.3/M7: refresh_voice_catalog re-escanea sin romper el catalogo.
    backend.refresh_voice_catalog();
    assert(!backend.voice_catalog().empty());

    nlp3::tts::TtsConfig config{};
    config.enabled = true;
    config.selected_voice_id = "english-female";
    config.selected_language = "en";
    backend.apply_config(config);

    if (audible) {
        nlp3::tts::TtsMessage message{};
        message.text = "Backend smoke";
        message.priority = nlp3::tts::TtsPriority::normal;
        const auto speak_result = backend.speak(message);
        assert(speak_result == backend.available());
        backend.clear_pending();
    } else {
        // Encolar y limpiar sin hablar (el worker puede no haber tomado el
        // mensaje aun; clear_pending drena la cola).
        nlp3::tts::TtsMessage message{};
        message.text = "Backend silent smoke";
        message.priority = nlp3::tts::TtsPriority::normal;
        const auto speak_result = backend.speak(message);
        assert(speak_result == backend.available());
        backend.clear_pending();
        assert(backend.queued_message_count() == 0);
    }

    return 0;
}
