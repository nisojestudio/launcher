#include "core/live_input.hpp"

#include "core/runtime_state.hpp"

#include <utility>

namespace nlp3::core {

LiveInputPort::LiveInputPort(RuntimeState& runtime_state) noexcept
    : runtime_state_(runtime_state) {
}

void LiveInputPort::submit(CoreInputEvent event) {
    ++state_.accepted_events;
    runtime_state_.apply(event);
    state_.last_event = std::move(event);
}

const LiveInputState& LiveInputPort::snapshot() const noexcept {
    return state_;
}

const RuntimeSnapshot& LiveInputPort::runtime_snapshot() const noexcept {
    return runtime_state_.snapshot();
}

} // namespace nlp3::core
