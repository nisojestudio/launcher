#pragma once

#include <optional>

#include "events/host_event.hpp"
#include "live/events.hpp"

namespace nlp3::live {

std::optional<events::HostEvent> to_host_event(const LiveEvent& event);

} // namespace nlp3::live
