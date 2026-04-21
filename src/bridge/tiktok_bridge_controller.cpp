#include "bridge/tiktok_bridge_controller.hpp"

#include <utility>

namespace nlp3::bridge {

TikTokBridgeController::TikTokBridgeController(std::unique_ptr<ITikTokBridgeSession> session) noexcept
    : session_(std::move(session)) {
}

bool TikTokBridgeController::start() {
    return session_ != nullptr && session_->start();
}

void TikTokBridgeController::stop() {
    if (session_ != nullptr) {
        session_->stop();
    }
}

void TikTokBridgeController::reset() {
    if (session_ != nullptr) {
        session_->reset();
    }
}

bool TikTokBridgeController::available() const noexcept {
    return session_ != nullptr;
}

TikTokBridgeSessionState TikTokBridgeController::state() const noexcept {
    return session_ != nullptr ? session_->state() : TikTokBridgeSessionState::stopped;
}

TikTokBridgeHealth TikTokBridgeController::health() const {
    TikTokBridgeHealth snapshot{};
    snapshot.available = available();
    snapshot.state = std::string(to_string(state()));
    snapshot.running = state() == TikTokBridgeSessionState::running;
    snapshot.faulted = state() == TikTokBridgeSessionState::faulted;

    if (session_ == nullptr) {
        return snapshot;
    }

    const auto& metrics = session_->metrics();
    snapshot.raw_events_received = metrics.raw_events_received;
    snapshot.raw_events_emitted = metrics.raw_events_emitted;
    snapshot.raw_events_dropped = metrics.raw_events_dropped;
    snapshot.poll_calls = metrics.poll_calls;
    snapshot.empty_polls = metrics.empty_polls;
    snapshot.fault_count = metrics.fault_count;
    snapshot.last_event_timestamp_ms = metrics.last_event_timestamp_ms;
    snapshot.last_fault = session_->last_fault();
    return snapshot;
}

std::size_t TikTokBridgeController::poll_into(
    std::vector<TikTokRawEvent>& out_events,
    std::size_t max_events) {
    if (session_ == nullptr) {
        return 0;
    }

    auto polled_events = session_->poll(max_events);
    const auto added = polled_events.size();
    out_events.reserve(out_events.size() + added);

    for (auto& event : polled_events) {
        out_events.push_back(std::move(event));
    }

    return added;
}

ITikTokBridgeSession* TikTokBridgeController::session() noexcept {
    return session_.get();
}

const ITikTokBridgeSession* TikTokBridgeController::session() const noexcept {
    return session_.get();
}

} // namespace nlp3::bridge
