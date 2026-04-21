#include "host/host_automation.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

#include "tts/tts_template_formatter.hpp"

namespace nlp3::host {

HostAutomationEngine::HostAutomationEngine(HostAutomationConfig config) noexcept
    : config_(std::move(config)) {
}

const HostAutomationConfig& HostAutomationEngine::config() const noexcept {
    return config_;
}

void HostAutomationEngine::set_config(HostAutomationConfig config) noexcept {
    config_ = std::move(config);
}

bool HostAutomationEngine::allow_with_cooldown(
    std::int64_t now_ms,
    std::uint64_t cooldown_ms,
    std::int64_t& last_at_ms) const noexcept {
    if (cooldown_ms == 0 || last_at_ms <= 0 || now_ms <= 0) {
        last_at_ms = now_ms;
        return true;
    }
    if (now_ms < last_at_ms) {
        return false;
    }
    if (static_cast<std::uint64_t>(now_ms - last_at_ms) < cooldown_ms) {
        return false;
    }
    last_at_ms = now_ms;
    return true;
}

bool HostAutomationEngine::is_subscriber_event(const events::HostEvent& event) const {
    std::string source_type = event.metadata.source_event_type;
    std::transform(
        source_type.begin(),
        source_type.end(),
        source_type.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return source_type.find("subscribe") != std::string::npos
        || source_type.find("subscription") != std::string::npos;
}

std::optional<tts::TtsMessage> HostAutomationEngine::build_tts_message(
    const events::HostEvent& event,
    const HostSessionSnapshot& session) {
    const auto actor_name =
        !event.actor.display_name.empty() ? event.actor.display_name : event.actor.id;
    tts::TtsTemplateContext context{};
    context.user = actor_name;
    context.message = event.message;
    context.gift = event.gift.has_value() ? event.gift->gift_name : "";
    context.count = event.gift.has_value() ? event.gift->quantity : std::max(event.magnitude, 1);
    context.viewers = event.viewer_count > 0
        ? event.viewer_count
        : (session.last_event.has_value() ? session.last_event->viewer_count : 0);

    auto build_message = [&](std::string_view text, tts::TtsPriority priority, tts::TtsMessageCategory category) {
        auto resolved = tts::format_tts_template(text, context);
        if (resolved.empty()) {
            return std::optional<tts::TtsMessage>{};
        }
        return std::optional<tts::TtsMessage>{tts::TtsMessage{
            tts::TtsTrigger::scheduled_message,
            priority,
            category,
            resolved,
            context.message,
            actor_name,
            event.metadata.source,
            event.metadata.source_timestamp_ms,
        }};
    };

    switch (event.kind) {
    case events::HostEventKind::gift:
        if (config_.enable_gift_thanks_tts
            && allow_with_cooldown(
                event.metadata.source_timestamp_ms,
                config_.gift_thanks_cooldown_ms,
                last_gift_tts_at_ms_)) {
            return build_message(config_.gift_thanks_template, tts::TtsPriority::high, tts::TtsMessageCategory::gift);
        }
        break;
    case events::HostEventKind::follow:
        if (config_.enable_follow_thanks_tts
            && allow_with_cooldown(
                event.metadata.source_timestamp_ms,
                config_.follow_thanks_cooldown_ms,
                last_follow_tts_at_ms_)) {
            return build_message(config_.follow_thanks_template, tts::TtsPriority::normal, tts::TtsMessageCategory::follow);
        }
        break;
    case events::HostEventKind::like:
        if (config_.enable_like_thanks_tts
            && allow_with_cooldown(
                event.metadata.source_timestamp_ms,
                config_.like_thanks_cooldown_ms,
                last_like_tts_at_ms_)) {
            return build_message(config_.like_thanks_template, tts::TtsPriority::normal, tts::TtsMessageCategory::like);
        }
        break;
    case events::HostEventKind::share:
        if (config_.enable_share_thanks_tts
            && allow_with_cooldown(
                event.metadata.source_timestamp_ms,
                config_.share_thanks_cooldown_ms,
                last_share_tts_at_ms_)) {
            return build_message(config_.share_thanks_template, tts::TtsPriority::normal, tts::TtsMessageCategory::share);
        }
        break;
    case events::HostEventKind::custom_raw:
        if (config_.enable_subscriber_thanks_tts
            && is_subscriber_event(event)
            && allow_with_cooldown(
                event.metadata.source_timestamp_ms,
                config_.subscriber_thanks_cooldown_ms,
                last_subscriber_tts_at_ms_)) {
            return build_message(
                config_.subscriber_thanks_template,
                tts::TtsPriority::high,
                tts::TtsMessageCategory::subscriber);
        }
        break;
    case events::HostEventKind::chat_message:
    case events::HostEventKind::viewer_join:
    case events::HostEventKind::viewer_count:
    case events::HostEventKind::live_start:
    case events::HostEventKind::live_end:
    case events::HostEventKind::moderation:
        break;
    }

    return std::nullopt;
}

} // namespace nlp3::host
