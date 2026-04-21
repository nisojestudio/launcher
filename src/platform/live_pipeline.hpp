#pragma once

#include <optional>

#include "events/host_event.hpp"
#include "host/host_runtime.hpp"
#include "live/events.hpp"

namespace nlp3::platform {

struct LiveDispatchResult {
    events::HostEvent event;
    host::HostSessionSnapshot session;
};

std::optional<LiveDispatchResult> dispatch_external_live_event(
    const live::ExternalEvent& external_event,
    host::HostRuntime& runtime);

} // namespace nlp3::platform
