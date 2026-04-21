#pragma once

#include <cstddef>
#include <deque>
#include <vector>

#include "bridge/tiktok_event_source.hpp"

namespace nlp3::bridge {

class TikTokExternalEventSource final : public ITikTokEventSource {
public:
    TikTokExternalEventSource() noexcept = default;

    bool start() override;
    void stop() override;
    void reset() override;

    std::vector<TikTokRawEvent> poll(std::size_t max_events = 0) override;

    bool submit_external_event(TikTokRawEvent raw_event);
    std::size_t queued_raw_event_count() const noexcept;
    bool running() const noexcept;

private:
    bool running_ = false;
    std::deque<TikTokRawEvent> queued_{};
};

} // namespace nlp3::bridge
