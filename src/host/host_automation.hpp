#pragma once

#include <optional>
#include <string>

#include "events/host_event.hpp"
#include "host/session_state.hpp"
#include "tts/tts_message.hpp"

namespace nlp3::host {

struct HostAutomationConfig {
    bool enable_gift_thanks_tts = true;
    bool enable_follow_thanks_tts = false;
    bool enable_like_thanks_tts = false;
    bool enable_subscriber_thanks_tts = false;
    bool enable_share_thanks_tts = false;

    std::uint64_t gift_thanks_cooldown_ms = 2000;
    std::uint64_t follow_thanks_cooldown_ms = 4000;
    std::uint64_t like_thanks_cooldown_ms = 3000;
    std::uint64_t subscriber_thanks_cooldown_ms = 3000;
    std::uint64_t share_thanks_cooldown_ms = 4000;

    std::string gift_thanks_template = "Gracias {user} por el regalo {gift}";
    std::string follow_thanks_template = "Gracias {user} por seguir la cuenta";
    std::string like_thanks_template = "Gracias {user} por enviar {count} likes";
    std::string subscriber_thanks_template = "Gracias {user} por suscribirte";
    std::string share_thanks_template = "Gracias {user} por compartir el directo";
};

class HostAutomationEngine {
public:
    explicit HostAutomationEngine(HostAutomationConfig config = {}) noexcept;

    const HostAutomationConfig& config() const noexcept;
    void set_config(HostAutomationConfig config) noexcept;

    std::optional<tts::TtsMessage> build_tts_message(
        const events::HostEvent& event,
        const HostSessionSnapshot& session);

private:
    bool allow_with_cooldown(std::int64_t now_ms, std::uint64_t cooldown_ms, std::int64_t& last_at_ms) const noexcept;
    bool is_subscriber_event(const events::HostEvent& event) const;

    HostAutomationConfig config_{};
    std::int64_t last_gift_tts_at_ms_ = 0;
    std::int64_t last_follow_tts_at_ms_ = 0;
    std::int64_t last_like_tts_at_ms_ = 0;
    std::int64_t last_subscriber_tts_at_ms_ = 0;
    std::int64_t last_share_tts_at_ms_ = 0;
};

} // namespace nlp3::host
