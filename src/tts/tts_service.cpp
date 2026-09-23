#include "tts/tts_service.hpp"

#include <chrono>
#include <string>
#include <utility>

#include "tts/tts_template_formatter.hpp"

namespace nlp3::tts {

HostTtsService::HostTtsService(TtsConfig config, TtsPolicy policy, ITtsBackend& backend)
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

    // P1.3/M2: cooldown con reloj de RECEPCION (wall clock al encolar),
    // no con source_timestamp_ms — eventos con timestamp=0 o atrasados
    // no deben saltarse el cooldown.
    const auto now_ms = static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    if (policy_.chat_cooldown_ms > 0 && last_chat_enqueued_at_ms_ > 0) {
        if (now_ms >= last_chat_enqueued_at_ms_
            && static_cast<std::uint64_t>(now_ms - last_chat_enqueued_at_ms_) < policy_.chat_cooldown_ms) {
            return false;
        }
    }

    const auto accepted = submit(build_chat_message(event));
    if (accepted) {
        last_chat_enqueued_at_ms_ = now_ms;
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

    // P2.2/M5: aplicar plantilla SIEMPRE. Si include_actor_name_for_chat esta off,
    // {user} se rellena con cadena vacia; el formatter+sanitize colapsa espacios
    // sobrantes ("Dice : hola" -> "Dice: hola").
    std::string formatted{};
    if (!policy_.chat_message_template.empty()) {
        if (!policy_.include_actor_name_for_chat) {
            context.user.clear();
        }
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
