#include "live/host_event_adapter.hpp"

#include <variant>

namespace nlp3::live {

std::optional<events::HostEvent> to_host_event(const LiveEvent& event) {
    events::HostEvent translated{};
    translated.actor = events::HostActor{
        event.actor.id,
        event.actor.display_name,
        "",
    };
    translated.metadata = events::HostEventMetadata{
        event.source,
        std::string{to_string(event.kind)},
    };

    switch (event.kind) {
    case LiveEventKind::chat_message: {
        const auto& payload = std::get<ChatMessageData>(event.payload);
        translated.kind = events::HostEventKind::chat_message;
        translated.message = payload.text;
        translated.magnitude = 1;
        return translated;
    }
    case LiveEventKind::reaction: {
        const auto& payload = std::get<ReactionData>(event.payload);
        translated.kind = events::HostEventKind::like;
        translated.magnitude = payload.count;
        return translated;
    }
    case LiveEventKind::gift: {
        const auto& payload = std::get<GiftData>(event.payload);
        translated.kind = events::HostEventKind::gift;
        translated.gift = events::GiftEventData{
            payload.name,
            payload.quantity,
            payload.value,
        };
        translated.magnitude = payload.quantity;
        return translated;
    }
    case LiveEventKind::follow:
        translated.kind = events::HostEventKind::follow;
        translated.magnitude = 1;
        return translated;
    case LiveEventKind::share:
        translated.kind = events::HostEventKind::share;
        translated.magnitude = 1;
        return translated;
    }

    return std::nullopt;
}

} // namespace nlp3::live
