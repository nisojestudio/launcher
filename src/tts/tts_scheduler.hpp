#pragma once

#include <cstddef>

#include "tts/tts_backend.hpp"
#include "tts/tts_config.hpp"
#include "tts/tts_policy.hpp"
#include "tts/tts_queue.hpp"

namespace nlp3::tts {

class TtsScheduler {
public:
    TtsScheduler(TtsConfig config, TtsPolicy policy, ITtsBackend& backend) noexcept;

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
