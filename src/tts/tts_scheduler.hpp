#pragma once

#include <cstddef>

#include "tts/tts_backend.hpp"
#include "tts/tts_config.hpp"
#include "tts/tts_policy.hpp"
#include "tts/tts_queue.hpp"

namespace nlp3::tts {

// B10: TtsScheduler no es thread-safe. Todas las llamadas publicas deben
// provenir del hilo que posee el servicio TTS (hilo principal del panel o
// el worker unico de RealTtsBackend). No proteger con mutex: el diseño es
// single-thread por ownership, no por bloqueo.
class TtsScheduler {
public:
    // B8: no noexcept — backend_->apply_config() puede lanzar (alloc/lock).
    TtsScheduler(TtsConfig config, TtsPolicy policy, ITtsBackend& backend);

    bool submit(TtsMessage message);
    std::size_t dispatch_pending(std::size_t max_messages = 0);
    bool available() const noexcept;
    std::size_t queued_message_count() const noexcept;
    void clear_pending() noexcept;
    void set_config(TtsConfig config);
    void set_policy(TtsPolicy policy) noexcept;

private:
    bool sanitize_message(TtsMessage& message) const;

    TtsConfig config_;
    TtsPolicy policy_;
    ITtsBackend* backend_ = nullptr;
    TtsQueue queue_{};
};

} // namespace nlp3::tts
