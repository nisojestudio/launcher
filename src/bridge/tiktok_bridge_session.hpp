#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "bridge/tiktok_raw_event.hpp"

namespace nlp3::bridge {

enum class TikTokBridgeSessionState {
    stopped,
    starting,
    running,
    faulted,
};

struct TikTokBridgeMetrics {
    std::uint64_t raw_events_received = 0;
    std::uint64_t raw_events_emitted = 0;
    std::uint64_t raw_events_dropped = 0;
    std::uint64_t poll_calls = 0;
    std::uint64_t empty_polls = 0;
    std::uint64_t fault_count = 0;
    std::int64_t last_event_timestamp_ms = 0;
};

struct TikTokBridgeFault {
    std::string code;
    std::string message;
    std::int64_t timestamp_ms = 0;
};

class ITikTokBridgeSession {
public:
    virtual ~ITikTokBridgeSession() = default;

    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual void reset() = 0;

    virtual TikTokBridgeSessionState state() const noexcept = 0;
    virtual const TikTokBridgeMetrics& metrics() const noexcept = 0;
    virtual std::optional<TikTokBridgeFault> last_fault() const = 0;

    virtual std::vector<TikTokRawEvent> poll(std::size_t max_events = 0) = 0;
};

constexpr std::string_view to_string(TikTokBridgeSessionState state) noexcept {
    switch (state) {
    case TikTokBridgeSessionState::stopped:
        return "stopped";
    case TikTokBridgeSessionState::starting:
        return "starting";
    case TikTokBridgeSessionState::running:
        return "running";
    case TikTokBridgeSessionState::faulted:
        return "faulted";
    }
    return "unknown";
}

} // namespace nlp3::bridge
