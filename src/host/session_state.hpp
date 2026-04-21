#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "events/host_event.hpp"

namespace nlp3::host {

struct HostSessionSnapshot {
    std::size_t total_events = 0;
    std::size_t chat_messages = 0;
    std::size_t likes = 0;
    std::size_t gifts = 0;
    std::size_t follows = 0;
    std::size_t shares = 0;
    std::size_t viewer_joins = 0;
    std::size_t viewer_count_updates = 0;
    std::size_t live_starts = 0;
    std::size_t live_ends = 0;
    std::size_t moderation_events = 0;
    std::size_t custom_events = 0;
    std::optional<events::HostActor> last_actor;
    std::optional<events::GiftEventData> last_gift;
    std::optional<events::HostEvent> last_event;
};

class HostSessionState {
public:
    void apply_event(const events::HostEvent& event);
    void reset() noexcept;
    const HostSessionSnapshot& snapshot() const noexcept;

private:
    HostSessionSnapshot snapshot_{};
};

} // namespace nlp3::host
