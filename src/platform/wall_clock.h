#pragma once

#include <chrono>
#include <cstdint>

namespace nlp3::platform {

/// Monotonic-ish wall-clock timestamp in milliseconds.
/// Single definition shared across the entire platform layer.
inline std::int64_t now_wall_clock_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count();
}

} // namespace nlp3::platform
