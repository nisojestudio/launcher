#include "tts/tts_queue.hpp"

#include <algorithm>
#include <utility>

namespace nlp3::tts {

bool TtsQueue::push(TtsMessage message, std::size_t max_size, bool drop_oldest_on_overflow) {
    if (max_size > 0 && messages_.size() >= max_size) {
        const auto incoming_priority = static_cast<int>(message.priority);
        const auto lowest_priority = static_cast<int>(messages_.back().priority);
        if (!drop_oldest_on_overflow && incoming_priority <= lowest_priority) {
            return false;
        }

        messages_.pop_back();
    }

    const auto insert_at = std::find_if(
        messages_.begin(),
        messages_.end(),
        [&message](const TtsMessage& existing) {
            return static_cast<int>(message.priority) > static_cast<int>(existing.priority);
        });
    messages_.insert(insert_at, std::move(message));
    return true;
}

std::optional<TtsMessage> TtsQueue::pop() {
    if (messages_.empty()) {
        return std::nullopt;
    }

    TtsMessage next = std::move(messages_.front());
    messages_.pop_front();
    return next;
}

std::size_t TtsQueue::size() const noexcept {
    return messages_.size();
}

bool TtsQueue::empty() const noexcept {
    return messages_.empty();
}

void TtsQueue::clear() noexcept {
    messages_.clear();
}

} // namespace nlp3::tts
