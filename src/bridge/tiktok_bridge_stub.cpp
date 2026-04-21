#include "bridge/tiktok_bridge_stub.hpp"

#include <utility>

namespace nlp3::bridge {

TikTokBridgeStub::TikTokBridgeStub(TikTokBridgeConfig config) noexcept
    : config_(config),
      mapper_(config_) {
}

std::string_view TikTokBridgeStub::adapter_name() const noexcept {
    return "tiktok-bridge-stub";
}

void TikTokBridgeStub::start() {
    running_ = config_.enabled;
}

void TikTokBridgeStub::stop() {
    running_ = false;
}

bool TikTokBridgeStub::enqueue_raw_event(TikTokRawEvent raw_event) {
    if (!running_ || !config_.stub_mode) {
        return false;
    }

    raw_events_.push_back(std::move(raw_event));
    return true;
}

std::size_t TikTokBridgeStub::queued_raw_event_count() const noexcept {
    return raw_events_.size();
}

std::vector<events::HostEvent> TikTokBridgeStub::drain_host_events(std::size_t max_events) {
    std::vector<events::HostEvent> mapped_events;
    const auto limit = max_events == 0 ? raw_events_.size() : max_events;

    while (!raw_events_.empty() && mapped_events.size() < limit) {
        TikTokRawEvent next = std::move(raw_events_.front());
        raw_events_.pop_front();

        auto mapped = mapper_.map(next);
        if (mapped.has_value()) {
            mapped_events.push_back(std::move(*mapped));
        }
    }

    return mapped_events;
}

} // namespace nlp3::bridge
