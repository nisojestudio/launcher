#pragma once

#include <cstddef>
#include <cstdint>

namespace nlp3::platform {

struct PanelTickResult {
    std::size_t bridge_events_processed = 0;
    bool periodic_tts_enqueued = false;
    std::uint64_t now_ms = 0;
};

} // namespace nlp3::platform
