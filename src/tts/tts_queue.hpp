#pragma once

#include <deque>
#include <optional>

#include "tts/tts_message.hpp"

namespace nlp3::tts {

class TtsQueue {
public:
    bool push(TtsMessage message, std::size_t max_size, bool drop_oldest_on_overflow);
    std::optional<TtsMessage> pop();
    std::size_t size() const noexcept;
    bool empty() const noexcept;
    void clear() noexcept;

private:
    std::deque<TtsMessage> messages_{};
};

} // namespace nlp3::tts
