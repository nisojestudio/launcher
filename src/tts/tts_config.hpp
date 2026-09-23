#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace nlp3::tts {

struct TtsConfig {
    bool enabled = true;
    // 0 = sin tope. Defaults con tope para evitar colas ilimitadas en floods (auditoria A2/D4).
    std::size_t max_queue_size = 50;
    std::size_t max_dispatch_per_tick = 1;
    std::size_t max_text_length = 160;
    bool drop_oldest_on_overflow = false;
    // 0 = sin tope en la cola interna del backend.
    std::size_t backend_queue_size = 20;
    // 0 = sin TTL. Mensajes encolados hace mas de esta edad se descartan (auditoria A2/D2).
    std::uint64_t max_message_age_ms = 30000;
    std::string selected_voice_id = "spanish-neutral";
    std::string selected_language = "es";
    std::string frequency = "normal";
    // B7: volumen SAPI 0..100 (al FINAL del struct para no romper la
    // inicializacion agregada posicional existente).
    std::uint32_t volume = 100;
    // M8: ventana del rate-limit de POST /api/tts/test en ms.
    // 0 = sin limite. Al FINAL por la misma razon que volume.
    std::uint64_t test_rate_limit_ms = 1000;
};

// True si un mensaje encolado en enqueued_at_ms ya supera el TTL configurado.
// max_age_ms == 0 desactiva el TTL; timestamps no utiles nunca caducan.
inline bool tts_message_expired(
    std::uint64_t max_age_ms,
    std::int64_t enqueued_at_ms,
    std::int64_t now_ms) noexcept {
    if (max_age_ms == 0 || enqueued_at_ms <= 0 || now_ms <= enqueued_at_ms) {
        return false;
    }
    return static_cast<std::uint64_t>(now_ms - enqueued_at_ms) > max_age_ms;
}

} // namespace nlp3::tts
