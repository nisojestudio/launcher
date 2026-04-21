#include "host/host_periodic_tts.hpp"

#include <utility>

namespace nlp3::host {

HostPeriodicTtsEngine::HostPeriodicTtsEngine(HostPeriodicTtsConfig config) noexcept
    : config_(std::move(config)) {
}

const HostPeriodicTtsConfig& HostPeriodicTtsEngine::config() const noexcept {
    return config_;
}

void HostPeriodicTtsEngine::set_config(HostPeriodicTtsConfig config) noexcept {
    config_ = std::move(config);
}

bool HostPeriodicTtsEngine::should_emit(std::uint64_t now_ms) const noexcept {
    if (!config_.enabled || config_.messages.empty()) {
        return false;
    }

    if (config_.interval_ms == 0) {
        return true;
    }

    if (last_emit_at_ms_ == 0) {
        return now_ms >= config_.interval_ms;
    }

    if (now_ms < last_emit_at_ms_) {
        return false;
    }

    return (now_ms - last_emit_at_ms_) >= config_.interval_ms;
}

std::string HostPeriodicTtsEngine::take_next_message(std::uint64_t now_ms) {
    if (!should_emit(now_ms)) {
        return {};
    }

    const auto message = config_.messages[next_message_index_];
    last_emit_at_ms_ = now_ms;
    next_message_index_ = (next_message_index_ + 1) % config_.messages.size();
    return message;
}

void HostPeriodicTtsEngine::reset() noexcept {
    last_emit_at_ms_ = 0;
    next_message_index_ = 0;
}

} // namespace nlp3::host
