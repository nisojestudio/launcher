#include "bridge/tiktok_stub_event_source.hpp"

#include <utility>

namespace nlp3::bridge {

TikTokStubEventSource::TikTokStubEventSource(TikTokBridgeConfig config) noexcept
    : config_(std::move(config)) {
}

bool TikTokStubEventSource::start() {
    if (!config_.enabled) {
        running_ = false;
        return false;
    }

    running_ = true;
    return true;
}

void TikTokStubEventSource::stop() {
    running_ = false;
}

void TikTokStubEventSource::reset() {
    queued_.clear();
    running_ = false;
}

std::vector<TikTokRawEvent> TikTokStubEventSource::poll(std::size_t max_events) {
    if (!running_) {
        return {};
    }

    const auto limit = max_events == 0 ? queued_.size() : max_events;

    std::vector<TikTokRawEvent> drained{};
    drained.reserve(limit);

    while (!queued_.empty() && drained.size() < limit) {
        drained.push_back(std::move(queued_.front()));
        queued_.pop_front();
    }

    return drained;
}

bool TikTokStubEventSource::inject_event(TikTokRawEvent raw_event) {
    if (!running_) {
        return false;
    }

    queued_.push_back(std::move(raw_event));
    return true;
}

std::size_t TikTokStubEventSource::queued_raw_event_count() const noexcept {
    return queued_.size();
}

} // namespace nlp3::bridge
