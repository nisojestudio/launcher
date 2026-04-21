#pragma once

#include <cstddef>
#include <cstdint>

namespace nlp3::platform {

struct PanelRunResult {
    std::size_t ticks_executed = 0;
    std::size_t total_bridge_events_processed = 0;
    std::size_t periodic_tts_enqueues = 0;
    std::uint64_t last_now_ms = 0;
};

} // namespace nlp3::platform
