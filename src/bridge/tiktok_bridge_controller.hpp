#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "bridge/tiktok_bridge_session.hpp"

namespace nlp3::bridge {

struct TikTokBridgeHealth {
    bool available = false;
    bool running = false;
    bool faulted = false;
    std::string state = "stopped";
    std::uint64_t raw_events_received = 0;
    std::uint64_t raw_events_emitted = 0;
    std::uint64_t raw_events_dropped = 0;
    std::uint64_t poll_calls = 0;
    std::uint64_t empty_polls = 0;
    std::uint64_t fault_count = 0;
    std::int64_t last_event_timestamp_ms = 0;
    std::optional<TikTokBridgeFault> last_fault;
};

class TikTokBridgeController {
public:
    explicit TikTokBridgeController(std::unique_ptr<ITikTokBridgeSession> session) noexcept;

    bool start();
    void stop();
    void reset();

    bool available() const noexcept;
    TikTokBridgeSessionState state() const noexcept;
    TikTokBridgeHealth health() const;

    std::size_t poll_into(std::vector<TikTokRawEvent>& out_events, std::size_t max_events = 0);

    ITikTokBridgeSession* session() noexcept;
    const ITikTokBridgeSession* session() const noexcept;

private:
    std::unique_ptr<ITikTokBridgeSession> session_;
};

} // namespace nlp3::bridge
