#pragma once

#include <deque>
#include <optional>

#include "tts/tts_message.hpp"

namespace nlp3::tts {

class TtsQueue {
public:
    bool push(TtsMessage message, std::size_t max_size, bool drop_oldest_on_overflow);
    // Re-encola al frente tras un fallo de speak(); sin tope (la msg ya salio de la cola).
    void push_front(TtsMessage message);
    std::optional<TtsMessage> pop();
    std::size_t size() const noexcept;
    bool empty() const noexcept;
    void clear() noexcept;

private:
    std::deque<TtsMessage> messages_{};
};

} // namespace nlp3::tts
