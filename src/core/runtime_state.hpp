#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "core/live_input.hpp"

namespace nlp3::core {

struct EventTypeCounters {
    std::size_t chat_messages = 0;
    std::size_t reactions = 0;
    std::size_t gifts = 0;
    std::size_t follows = 0;
    std::size_t shares = 0;
};

struct ActorSnapshot {
    std::string id;
    std::string name;
};

struct GiftSnapshot {
    std::string actor_id;
    std::string actor_name;
    std::string name;
    int quantity = 0;
};

struct RuntimeSnapshot {
    std::size_t total_events = 0;
    EventTypeCounters counters{};
    std::optional<ActorSnapshot> last_actor;
    std::optional<GiftSnapshot> last_gift;
};

class RuntimeState {
public:
    void apply(const CoreInputEvent& event);
    const RuntimeSnapshot& snapshot() const noexcept;

private:
    RuntimeSnapshot state_{};
};

} // namespace nlp3::core
