#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "bridge/tiktok_bridge_config.hpp"
#include "bridge/tiktok_bridge_session.hpp"
#include "bridge/tiktok_external_event_source.hpp"

namespace nlp3::bridge {

class TikTokBridgeExternalSession final : public ITikTokBridgeSession {
public:
    explicit TikTokBridgeExternalSession(TikTokBridgeConfig config = {}) noexcept;

    bool start() override;
    void stop() override;
    void reset() override;

    TikTokBridgeSessionState state() const noexcept override;
    const TikTokBridgeMetrics& metrics() const noexcept override;
    std::optional<TikTokBridgeFault> last_fault() const override;

    std::vector<TikTokRawEvent> poll(std::size_t max_events = 0) override;

    bool submit_external_event(TikTokRawEvent raw_event);
    std::size_t queued_raw_event_count() const noexcept;

private:
    TikTokBridgeConfig config_{};
    TikTokBridgeSessionState state_ = TikTokBridgeSessionState::stopped;
    TikTokBridgeMetrics metrics_{};
    std::optional<TikTokBridgeFault> last_fault_;
    std::unique_ptr<TikTokExternalEventSource> event_source_{};
};

} // namespace nlp3::bridge
