#include "core/runtime_state.hpp"

namespace nlp3::core {

void RuntimeState::apply(const CoreInputEvent& event) {
    ++state_.total_events;
    state_.last_actor = ActorSnapshot{
        event.actor_id,
        event.actor_name,
    };

    switch (event.type) {
    case CoreInputEventType::chat_message:
        ++state_.counters.chat_messages;
        break;
    case CoreInputEventType::reaction:
        ++state_.counters.reactions;
        break;
    case CoreInputEventType::gift:
        ++state_.counters.gifts;
        state_.last_gift = GiftSnapshot{
            event.actor_id,
            event.actor_name,
            event.label,
            event.magnitude,
        };
        break;
    case CoreInputEventType::follow:
        ++state_.counters.follows;
        break;
    case CoreInputEventType::share:
        ++state_.counters.shares;
        break;
    }
}

const RuntimeSnapshot& RuntimeState::snapshot() const noexcept {
    return state_;
}

} // namespace nlp3::core
