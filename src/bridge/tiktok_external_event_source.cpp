#include "bridge/tiktok_external_event_source.hpp"

#include <utility>

namespace nlp3::bridge {

bool TikTokExternalEventSource::start() {
    running_ = true;
    return true;
}

void TikTokExternalEventSource::stop() {
    running_ = false;
}

void TikTokExternalEventSource::reset() {
    queued_.clear();
    running_ = false;
}

std::vector<TikTokRawEvent> TikTokExternalEventSource::poll(std::size_t max_events) {
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

bool TikTokExternalEventSource::submit_external_event(TikTokRawEvent raw_event) {
    if (!running_) {
        return false;
    }

    queued_.push_back(std::move(raw_event));
    return true;
}

std::size_t TikTokExternalEventSource::queued_raw_event_count() const noexcept {
    return queued_.size();
}

bool TikTokExternalEventSource::running() const noexcept {
    return running_;
}

} // namespace nlp3::bridge
