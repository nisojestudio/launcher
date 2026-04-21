#include "bridge/tiktok_bridge_external_session.hpp"

#include <memory>
#include <utility>

namespace nlp3::bridge {

TikTokBridgeExternalSession::TikTokBridgeExternalSession(TikTokBridgeConfig config) noexcept
    : config_(std::move(config)) {
}

bool TikTokBridgeExternalSession::start() {
    if (!config_.enabled) {
        state_ = TikTokBridgeSessionState::stopped;
        return false;
    }

    if (event_source_ == nullptr) {
        event_source_ = std::make_unique<TikTokExternalEventSource>();
    }

    state_ = TikTokBridgeSessionState::starting;

    if (!event_source_->start()) {
        state_ = TikTokBridgeSessionState::faulted;
        last_fault_ = TikTokBridgeFault{
            "external_source_start_failed",
            "TikTokBridgeExternalSession could not start its event source",
            0,
        };
        ++metrics_.fault_count;
        return false;
    }

    state_ = TikTokBridgeSessionState::running;
    return true;
}

void TikTokBridgeExternalSession::stop() {
    if (event_source_ != nullptr) {
        event_source_->stop();
    }

    state_ = TikTokBridgeSessionState::stopped;
}

void TikTokBridgeExternalSession::reset() {
    if (event_source_ != nullptr) {
        event_source_->reset();
    }

    metrics_ = {};
    last_fault_.reset();
    state_ = TikTokBridgeSessionState::stopped;
}

TikTokBridgeSessionState TikTokBridgeExternalSession::state() const noexcept {
    return state_;
}

const TikTokBridgeMetrics& TikTokBridgeExternalSession::metrics() const noexcept {
    return metrics_;
}

std::optional<TikTokBridgeFault> TikTokBridgeExternalSession::last_fault() const {
    return last_fault_;
}

std::vector<TikTokRawEvent> TikTokBridgeExternalSession::poll(std::size_t max_events) {
    ++metrics_.poll_calls;

    if (state_ != TikTokBridgeSessionState::running || event_source_ == nullptr) {
        ++metrics_.empty_polls;
        return {};
    }

    auto drained = event_source_->poll(max_events);
    if (drained.empty()) {
        ++metrics_.empty_polls;
        return {};
    }

    for (const auto& next : drained) {
        metrics_.last_event_timestamp_ms = next.metadata.timestamp_ms;
        ++metrics_.raw_events_emitted;
    }

    return drained;
}

bool TikTokBridgeExternalSession::submit_external_event(TikTokRawEvent raw_event) {
    if (state_ != TikTokBridgeSessionState::running || event_source_ == nullptr) {
        ++metrics_.raw_events_dropped;
        return false;
    }

    if (!event_source_->submit_external_event(raw_event)) {
        ++metrics_.raw_events_dropped;
        return false;
    }

    metrics_.last_event_timestamp_ms = raw_event.metadata.timestamp_ms;
    ++metrics_.raw_events_received;
    return true;
}

std::size_t TikTokBridgeExternalSession::queued_raw_event_count() const noexcept {
    return event_source_ != nullptr ? event_source_->queued_raw_event_count() : 0;
}

} // namespace nlp3::bridge
