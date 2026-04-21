#include "bridge/tiktok_bridge_stub_session.hpp"

#include <memory>
#include <utility>

#include "bridge/tiktok_stub_event_source.hpp"

namespace nlp3::bridge {

TikTokBridgeStubSession::TikTokBridgeStubSession(TikTokBridgeConfig config) noexcept
    : TikTokBridgeStubSession(
          config,
          std::make_unique<TikTokStubEventSource>(config)) {
}

TikTokBridgeStubSession::TikTokBridgeStubSession(
    TikTokBridgeConfig config,
    std::unique_ptr<ITikTokEventSource> event_source) noexcept
    : config_(std::move(config)),
      event_source_(std::move(event_source)) {
}

bool TikTokBridgeStubSession::start() {
    if (!config_.enabled) {
        state_ = TikTokBridgeSessionState::stopped;
        return false;
    }

    state_ = TikTokBridgeSessionState::starting;

    if (!config_.stub_mode) {
        state_ = TikTokBridgeSessionState::faulted;
        last_fault_ = TikTokBridgeFault{
            "stub_mode_disabled",
            "TikTokBridgeStubSession requires stub_mode=true",
            0,
        };
        ++metrics_.fault_count;
        return false;
    }

    if (event_source_ == nullptr || !event_source_->start()) {
        state_ = TikTokBridgeSessionState::stopped;
        return false;
    }

    state_ = TikTokBridgeSessionState::running;
    return true;
}

void TikTokBridgeStubSession::stop() {
    if (event_source_ != nullptr) {
        event_source_->stop();
    }

    state_ = TikTokBridgeSessionState::stopped;
}

void TikTokBridgeStubSession::reset() {
    if (event_source_ != nullptr) {
        event_source_->reset();
    }

    metrics_ = {};
    last_fault_.reset();
    state_ = TikTokBridgeSessionState::stopped;
}

TikTokBridgeSessionState TikTokBridgeStubSession::state() const noexcept {
    return state_;
}

const TikTokBridgeMetrics& TikTokBridgeStubSession::metrics() const noexcept {
    return metrics_;
}

std::optional<TikTokBridgeFault> TikTokBridgeStubSession::last_fault() const {
    return last_fault_;
}

std::vector<TikTokRawEvent> TikTokBridgeStubSession::poll(std::size_t max_events) {
    ++metrics_.poll_calls;

    if (state_ != TikTokBridgeSessionState::running) {
        ++metrics_.empty_polls;
        return {};
    }

    if (event_source_ == nullptr) {
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

bool TikTokBridgeStubSession::inject_event(TikTokRawEvent raw_event) {
    if (state_ != TikTokBridgeSessionState::running) {
        ++metrics_.raw_events_dropped;
        return false;
    }

    auto* stub_source = dynamic_cast<TikTokStubEventSource*>(event_source_.get());
    if (stub_source == nullptr || !stub_source->inject_event(raw_event)) {
        ++metrics_.raw_events_dropped;
        return false;
    }

    metrics_.last_event_timestamp_ms = raw_event.metadata.timestamp_ms;
    ++metrics_.raw_events_received;
    return true;
}

void TikTokBridgeStubSession::inject_fault(
    std::string code,
    std::string message,
    std::int64_t timestamp_ms) {
    state_ = TikTokBridgeSessionState::faulted;
    last_fault_ = TikTokBridgeFault{
        std::move(code),
        std::move(message),
        timestamp_ms,
    };
    ++metrics_.fault_count;
}

std::size_t TikTokBridgeStubSession::queued_raw_event_count() const noexcept {
    const auto* stub_source = dynamic_cast<const TikTokStubEventSource*>(event_source_.get());
    return stub_source != nullptr ? stub_source->queued_raw_event_count() : 0;
}

} // namespace nlp3::bridge
