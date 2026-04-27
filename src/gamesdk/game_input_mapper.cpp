#include "gamesdk/game_input_mapper.hpp"

#include <cstdint>

namespace nlp3::gamesdk {

GameInputEvent GameInputEventMapper::map(const events::HostEvent& event) const {
    GameInputEvent mapped{};
    mapped.actor = GameInputActor{
        event.actor.id,
        "",
        event.actor.display_name,
        event.actor.avatar_url,
    };
    mapped.text = event.message;
    mapped.metadata = GameInputMetadata{
        event.metadata.source,
        event.metadata.source_event_id,
        event.metadata.source_room_id,
        event.metadata.source_event_type,
        event.metadata.source_timestamp_ms,
    };

    switch (event.kind) {
    case events::HostEventKind::chat_message:
        mapped.kind = GameInputEventKind::chat_message;
        break;
    case events::HostEventKind::like:
        mapped.kind = GameInputEventKind::like;
        mapped.like_count = static_cast<std::uint32_t>(event.magnitude > 0 ? event.magnitude : 1);
        break;
    case events::HostEventKind::gift:
        mapped.kind = GameInputEventKind::gift;
        if (event.gift.has_value()) {
            mapped.gift = GameInputGift{
                "",
                event.gift->gift_name,
                static_cast<std::uint32_t>(event.gift->quantity > 0 ? event.gift->quantity : 0),
                static_cast<std::uint32_t>(event.gift->value > 0 ? event.gift->value : 0),
            };
        }
        break;
    case events::HostEventKind::follow:
        mapped.kind = GameInputEventKind::follow;
        break;
    case events::HostEventKind::share:
        mapped.kind = GameInputEventKind::share;
        break;
    case events::HostEventKind::viewer_join:
        mapped.kind = GameInputEventKind::viewer_join;
        break;
    case events::HostEventKind::viewer_count:
        mapped.kind = GameInputEventKind::viewer_count;
        mapped.viewer_count = static_cast<std::uint32_t>(event.viewer_count > 0 ? event.viewer_count : 0);
        break;
    case events::HostEventKind::live_start:
        mapped.kind = GameInputEventKind::live_start;
        mapped.raw_payload = event.raw_payload;
        break;
    case events::HostEventKind::live_end:
        mapped.kind = GameInputEventKind::live_end;
        mapped.raw_payload = event.raw_payload;
        break;
    case events::HostEventKind::moderation:
        mapped.kind = GameInputEventKind::moderation;
        mapped.raw_payload = event.raw_payload;
        break;
    case events::HostEventKind::custom_raw:
        mapped.kind = GameInputEventKind::custom_raw;
        mapped.raw_payload = event.raw_payload;
        break;
    }

    return mapped;
}

} // namespace nlp3::gamesdk
