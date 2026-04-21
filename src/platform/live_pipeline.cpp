#include "platform/live_pipeline.hpp"

#include "live/host_event_adapter.hpp"
#include "live/normalizer.hpp"

namespace nlp3::platform {

std::optional<LiveDispatchResult> dispatch_external_live_event(
    const live::ExternalEvent& external_event,
    host::HostRuntime& runtime) {
    auto normalized_event = live::normalize_external_event(external_event);
    if (!normalized_event.has_value()) {
        return std::nullopt;
    }

    auto host_event = live::to_host_event(*normalized_event);
    if (!host_event.has_value()) {
        return std::nullopt;
    }

    runtime.receive_event(*host_event);

    return LiveDispatchResult{
        *host_event,
        runtime.snapshot(),
    };
}

} // namespace nlp3::platform
