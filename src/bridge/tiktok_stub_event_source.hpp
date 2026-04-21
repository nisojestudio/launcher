#pragma once

#include <cstddef>
#include <deque>
#include <vector>

#include "bridge/tiktok_bridge_config.hpp"
#include "bridge/tiktok_event_source.hpp"

namespace nlp3::bridge {

class TikTokStubEventSource final : public ITikTokEventSource {
public:
    explicit TikTokStubEventSource(TikTokBridgeConfig config = {}) noexcept;

    bool start() override;
    void stop() override;
    void reset() override;

    std::vector<TikTokRawEvent> poll(std::size_t max_events = 0) override;

    bool inject_event(TikTokRawEvent raw_event);
    std::size_t queued_raw_event_count() const noexcept;

private:
    TikTokBridgeConfig config_{};
    bool running_ = false;
    std::deque<TikTokRawEvent> queued_{};
};

} // namespace nlp3::bridge
