#include "live/normalizer.hpp"

namespace nlp3::live {

namespace {

int normalized_count(int quantity) noexcept {
    return quantity > 0 ? quantity : 1;
}

} // namespace

std::optional<LiveEvent> normalize_external_event(const ExternalEvent& external_event) {
    const LiveActor actor{external_event.actor_id, external_event.actor_name};

    if (external_event.raw_type == "comment" || external_event.raw_type == "chat_message") {
        return LiveEvent{
            external_event.source,
            LiveEventKind::chat_message,
            actor,
            ChatMessageData{external_event.text},
        };
    }

    if (external_event.raw_type == "like" || external_event.raw_type == "reaction") {
        return LiveEvent{
            external_event.source,
            LiveEventKind::reaction,
            actor,
            ReactionData{normalized_count(external_event.quantity)},
        };
    }

    if (external_event.raw_type == "gift") {
        return LiveEvent{
            external_event.source,
            LiveEventKind::gift,
            actor,
            GiftData{
                external_event.asset_name,
                normalized_count(external_event.quantity),
                external_event.value,
            },
        };
    }

    if (external_event.raw_type == "follow") {
        return LiveEvent{
            external_event.source,
            LiveEventKind::follow,
            actor,
            FollowData{},
        };
    }

    if (external_event.raw_type == "share") {
        return LiveEvent{
            external_event.source,
            LiveEventKind::share,
            actor,
            ShareData{},
        };
    }

    return std::nullopt;
}

} // namespace nlp3::live
