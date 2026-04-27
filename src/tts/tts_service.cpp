#include "tts/tts_service.hpp"

#include <chrono>
#include <string>
#include <utility>

#include "tts/tts_template_formatter.hpp"

namespace nlp3::tts {

namespace {

bool chat_template_mentions_user(std::string_view template_text) {
    return template_text.find("{user}") != std::string_view::npos;
}

} // namespace

HostTtsService::HostTtsService(TtsConfig config, TtsPolicy policy, ITtsBackend& backend) noexcept
    : config_(std::move(config)),
      policy_(std::move(policy)),
      scheduler_(config_, policy_, backend) {
}

std::string_view HostTtsService::service_name() const noexcept {
    return "host-tts-service";
}

bool HostTtsService::available() const noexcept {
    return config_.enabled && scheduler_.available();
}

bool HostTtsService::submit(TtsMessage message) {
    return scheduler_.submit(std::move(message));
}

bool HostTtsService::enqueue_chat_read(const events::HostEvent& event) {
    if (event.kind != events::HostEventKind::chat_message) {
        return false;
    }
    if (!allow_chat_event(event)) {
        return false;
    }

    const auto event_time = event.metadata.source_timestamp_ms;
    if (policy_.chat_cooldown_ms > 0 && last_chat_enqueued_at_ms_ > 0 && event_time > 0) {
        if (event_time >= last_chat_enqueued_at_ms_
            && static_cast<std::uint64_t>(event_time - last_chat_enqueued_at_ms_) < policy_.chat_cooldown_ms) {
            return false;
        }
    }

    const auto accepted = submit(build_chat_message(event));
    if (accepted) {
        last_chat_enqueued_at_ms_ = event_time;
    }
    return accepted;
}

bool HostTtsService::enqueue_announcement(std::string_view message) {
    const auto now_ms = static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    return submit(TtsMessage{
        TtsTrigger::manual_message,
        TtsPriority::normal,
        TtsMessageCategory::manual,
        std::string(message),
        std::string(message),
        "",
        "host",
        now_ms,
    });
}

std::size_t HostTtsService::dispatch_pending(std::size_t max_messages) {
    return scheduler_.dispatch_pending(max_messages);
}

std::size_t HostTtsService::queued_message_count() const noexcept {
    return scheduler_.queued_message_count();
}

void HostTtsService::clear_pending() noexcept {
    last_chat_enqueued_at_ms_ = 0;
    scheduler_.clear_pending();
}

void HostTtsService::set_config(TtsConfig config) {
    config_ = std::move(config);
    scheduler_.set_config(config_);
}

void HostTtsService::set_policy(TtsPolicy policy) {
    policy_ = std::move(policy);
    scheduler_.set_policy(policy_);
}

bool HostTtsService::allow_chat_event(const events::HostEvent& event) const {
    return allows_chat_actor(policy_, event.actor);
}

TtsMessage HostTtsService::build_chat_message(const events::HostEvent& event) const {
    TtsTemplateContext context{};
    context.user = !event.actor.display_name.empty() ? event.actor.display_name : event.actor.id;
    context.message = event.message;
    context.viewers = event.viewer_count;

    std::string formatted{};
    if (!policy_.chat_message_template.empty()
        && (policy_.include_actor_name_for_chat || !chat_template_mentions_user(policy_.chat_message_template))) {
        formatted = format_tts_template(policy_.chat_message_template, context);
    }
    if (formatted.empty()) {
        formatted = context.message;
    }

    TtsMessage message{};
    message.trigger = TtsTrigger::chat_event;
    message.priority = TtsPriority::low;
    message.category = TtsMessageCategory::chat;
    message.actor_name = event.actor.display_name;
    message.source = event.metadata.source;
    message.content_text = event.message;
    message.text = std::move(formatted);
    message.created_at_ms = event.metadata.source_timestamp_ms;
    return message;
}

} // namespace nlp3::tts
