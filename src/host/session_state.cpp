#include "host/session_state.hpp"

namespace nlp3::host {

void HostSessionState::apply_event(const events::HostEvent& event) {
    ++snapshot_.total_events;
    snapshot_.last_actor = event.actor;
    snapshot_.last_event = event;

    switch (event.kind) {
    case events::HostEventKind::chat_message:
        ++snapshot_.chat_messages;
        break;
    case events::HostEventKind::like:
        snapshot_.likes += static_cast<std::size_t>(event.magnitude > 0 ? event.magnitude : 1);
        break;
    case events::HostEventKind::gift:
        ++snapshot_.gifts;
        snapshot_.last_gift = event.gift;
        break;
    case events::HostEventKind::follow:
        ++snapshot_.follows;
        break;
    case events::HostEventKind::share:
        ++snapshot_.shares;
        break;
    case events::HostEventKind::viewer_join:
        ++snapshot_.viewer_joins;
        break;
    case events::HostEventKind::viewer_count:
        ++snapshot_.viewer_count_updates;
        break;
    case events::HostEventKind::live_start:
        ++snapshot_.live_starts;
        break;
    case events::HostEventKind::live_end:
        ++snapshot_.live_ends;
        break;
    case events::HostEventKind::moderation:
        ++snapshot_.moderation_events;
        break;
    case events::HostEventKind::custom_raw:
        ++snapshot_.custom_events;
        break;
    }
}

void HostSessionState::reset() noexcept {
    snapshot_ = HostSessionSnapshot{};
}

const HostSessionSnapshot& HostSessionState::snapshot() const noexcept {
    return snapshot_;
}

} // namespace nlp3::host
