#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "bridge/tiktok_bridge_config.hpp"
#include "bridge/tiktok_event_source.hpp"
#include "bridge/tiktok_bridge_session.hpp"

namespace nlp3::bridge {

class TikTokBridgeStubSession final : public ITikTokBridgeSession {
public:
    explicit TikTokBridgeStubSession(TikTokBridgeConfig config = {}) noexcept;
    TikTokBridgeStubSession(
        TikTokBridgeConfig config,
        std::unique_ptr<ITikTokEventSource> event_source) noexcept;

    bool start() override;
    void stop() override;
    void reset() override;

    TikTokBridgeSessionState state() const noexcept override;
    const TikTokBridgeMetrics& metrics() const noexcept override;
    std::optional<TikTokBridgeFault> last_fault() const override;

    std::vector<TikTokRawEvent> poll(std::size_t max_events = 0) override;

    bool inject_event(TikTokRawEvent raw_event);
    void inject_fault(std::string code, std::string message, std::int64_t timestamp_ms = 0);

    std::size_t queued_raw_event_count() const noexcept;

private:
    TikTokBridgeConfig config_;
    TikTokBridgeSessionState state_ = TikTokBridgeSessionState::stopped;
    TikTokBridgeMetrics metrics_{};
    std::optional<TikTokBridgeFault> last_fault_;
    std::unique_ptr<ITikTokEventSource> event_source_{};
};

} // namespace nlp3::bridge
