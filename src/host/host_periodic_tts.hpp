#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nlp3::host {

struct HostPeriodicTtsConfig {
    bool enabled = false;
    std::uint64_t interval_ms = 60000;
    std::vector<std::string> messages{};
};

class HostPeriodicTtsEngine {
public:
    explicit HostPeriodicTtsEngine(HostPeriodicTtsConfig config = {}) noexcept;

    const HostPeriodicTtsConfig& config() const noexcept;
    void set_config(HostPeriodicTtsConfig config) noexcept;

    bool should_emit(std::uint64_t now_ms) const noexcept;
    std::string take_next_message(std::uint64_t now_ms);
    void reset() noexcept;

private:
    HostPeriodicTtsConfig config_{};
    std::uint64_t last_emit_at_ms_ = 0;
    std::size_t next_message_index_ = 0;
};

} // namespace nlp3::host
