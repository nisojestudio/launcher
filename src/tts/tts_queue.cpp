#include "tts/tts_queue.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace nlp3::tts {

namespace {

// Edad comparable: enqueued_at_ms (cola del scheduler) o created_at_ms (fallback).
// 0 = timestamp desconocido → se resuelve por posición de inserción.
std::int64_t age_key(const TtsMessage& message) noexcept {
    if (message.enqueued_at_ms > 0) {
        return message.enqueued_at_ms;
    }
    return message.created_at_ms;
}

// Índice del mensaje a evacuar cuando la cola está llena.
// drop_oldest: el más antiguo de toda la cola (auditoria D3).
// !drop_oldest: el más antiguo entre los de prioridad mínima (preempa por prioridad).
std::size_t select_overflow_victim(
    const std::deque<TtsMessage>& messages,
    bool drop_oldest) noexcept {
    int lowest_priority = std::numeric_limits<int>::max();
    if (!drop_oldest) {
        for (const auto& message : messages) {
            lowest_priority = std::min(
                lowest_priority,
                static_cast<int>(message.priority));
        }
    }

    std::size_t best = 0;
    bool found = false;
    for (std::size_t index = 0; index < messages.size(); ++index) {
        if (!drop_oldest
            && static_cast<int>(messages[index].priority) != lowest_priority) {
            continue;
        }

        if (!found) {
            best = index;
            found = true;
            continue;
        }

        const auto age = age_key(messages[index]);
        const auto best_age = age_key(messages[best]);
        // Ambos con timestamp: gana el más antiguo. Si alguno no tiene,
        // se conserva el primero encontrado (orden de inserción).
        if (age > 0 && best_age > 0 && age < best_age) {
            best = index;
        }
    }

    return best;
}

} // namespace

bool TtsQueue::push(TtsMessage message, std::size_t max_size, bool drop_oldest_on_overflow) {
    if (max_size > 0 && messages_.size() >= max_size) {
        const auto incoming_priority = static_cast<int>(message.priority);

        if (!drop_oldest_on_overflow) {
            int lowest_priority = std::numeric_limits<int>::max();
            for (const auto& existing : messages_) {
                lowest_priority = std::min(
                    lowest_priority,
                    static_cast<int>(existing.priority));
            }
            // Prioridad igual o menor que la mínima de la cola: rechazar (FIFO entre iguales).
            if (incoming_priority <= lowest_priority) {
                return false;
            }
        }

        messages_.erase(
            messages_.begin()
            + static_cast<std::ptrdiff_t>(select_overflow_victim(messages_, drop_oldest_on_overflow)));
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

void TtsQueue::push_front(TtsMessage message) {
    messages_.push_front(std::move(message));
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
