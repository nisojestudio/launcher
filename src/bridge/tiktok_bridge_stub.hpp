#pragma once

#include <deque>
#include <vector>

#include "bridge/bridge_adapter.hpp"
#include "bridge/tiktok_bridge_config.hpp"
#include "bridge/tiktok_event_mapper.hpp"

namespace nlp3::bridge {

class TikTokBridgeStub final : public IBridgeAdapter {
public:
    explicit TikTokBridgeStub(TikTokBridgeConfig config = {}) noexcept;

    std::string_view adapter_name() const noexcept override;
    void start() override;
    void stop() override;

    bool enqueue_raw_event(TikTokRawEvent raw_event);
    std::size_t queued_raw_event_count() const noexcept;
    std::vector<events::HostEvent> drain_host_events(std::size_t max_events = 0);

private:
    TikTokBridgeConfig config_;
    TikTokEventMapper mapper_;
    std::deque<TikTokRawEvent> raw_events_{};
    bool running_ = false;
};

} // namespace nlp3::bridge
