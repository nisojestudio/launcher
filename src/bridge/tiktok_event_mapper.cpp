#include "bridge/tiktok_event_mapper.hpp"

#include <algorithm>
#include <cctype>

namespace nlp3::bridge {

namespace {

std::string trim_copy(std::string_view input) {
    auto begin = input.begin();
    auto end = input.end();

    while (begin != end && std::isspace(static_cast<unsigned char>(*begin)) != 0) {
        ++begin;
    }

    while (begin != end) {
        const auto last = end - 1;
        if (std::isspace(static_cast<unsigned char>(*last)) == 0) {
            break;
        }

        end = last;
    }

    return std::string(begin, end);
}

} // namespace

TikTokEventMapper::TikTokEventMapper(TikTokBridgeConfig config) noexcept
    : config_(std::move(config)) {
}

std::optional<events::HostEvent> TikTokEventMapper::map(const TikTokRawEvent& raw_event) const {
    if (!config_.enabled || !is_enabled(raw_event.kind)) {
        return std::nullopt;
    }

    events::HostEvent mapped{};
    mapped.actor = events::HostActor{
        !raw_event.actor.user_id.empty() ? raw_event.actor.user_id : raw_event.actor.username,
        !raw_event.actor.display_name.empty() ? raw_event.actor.display_name : raw_event.actor.username,
        config_.passthrough_avatar_url ? normalize_avatar_url(raw_event.actor.avatar_url) : "",
        raw_event.actor.is_follower,
        raw_event.actor.is_subscriber,
        raw_event.actor.is_moderator,
    };
    mapped.metadata = events::HostEventMetadata{
        !config_.source_name.empty() ? config_.source_name : (config_.stub_mode ? "tiktok-stub" : "tiktok-external"),
        !raw_event.metadata.raw_event_type.empty()
            ? raw_event.metadata.raw_event_type
            : std::string{to_string(raw_event.kind)},
        raw_event.metadata.event_id,
        raw_event.metadata.room_id,
        raw_event.metadata.timestamp_ms,
    };

    switch (raw_event.kind) {
    case TikTokRawEventKind::chat:
        mapped.kind = events::HostEventKind::chat_message;
        mapped.message = raw_event.message;
        mapped.magnitude = 1;
        return mapped;
    case TikTokRawEventKind::like:
        mapped.kind = events::HostEventKind::like;
        mapped.magnitude = std::max(raw_event.like_count, 1);
        return mapped;
    case TikTokRawEventKind::gift:
        if (!raw_event.gift.has_value()) {
            return std::nullopt;
        }

        mapped.kind = events::HostEventKind::gift;
        mapped.gift = events::GiftEventData{
            raw_event.gift->gift_name,
            std::max(raw_event.gift->repeat_count, 1),
            raw_event.gift->diamond_value,
        };
        mapped.magnitude = mapped.gift->quantity;
        return mapped;
    case TikTokRawEventKind::follow:
        mapped.kind = events::HostEventKind::follow;
        mapped.magnitude = 1;
        return mapped;
    case TikTokRawEventKind::share:
        mapped.kind = events::HostEventKind::share;
        mapped.magnitude = 1;
        return mapped;
    case TikTokRawEventKind::viewer_join:
        mapped.kind = events::HostEventKind::viewer_join;
        mapped.magnitude = 1;
        return mapped;
    case TikTokRawEventKind::viewer_count:
        mapped.kind = events::HostEventKind::viewer_count;
        mapped.viewer_count = std::max(raw_event.viewer_count, 0);
        mapped.magnitude = mapped.viewer_count;
        mapped.message = std::to_string(mapped.viewer_count);
        return mapped;
    case TikTokRawEventKind::live_start:
        mapped.kind = events::HostEventKind::live_start;
        mapped.magnitude = 1;
        mapped.message = raw_event.message;
        mapped.raw_payload = raw_event.raw_payload;
        return mapped;
    case TikTokRawEventKind::live_end:
        mapped.kind = events::HostEventKind::live_end;
        mapped.magnitude = 1;
        mapped.message = raw_event.message;
        mapped.raw_payload = raw_event.raw_payload;
        return mapped;
    case TikTokRawEventKind::moderation:
        mapped.kind = events::HostEventKind::moderation;
        mapped.magnitude = 1;
        mapped.message = !raw_event.moderation_action.empty() ? raw_event.moderation_action : raw_event.message;
        mapped.raw_payload = raw_event.raw_payload;
        return mapped;
    case TikTokRawEventKind::custom_raw:
        mapped.kind = events::HostEventKind::custom_raw;
        mapped.magnitude = 1;
        mapped.message = raw_event.message;
        mapped.raw_payload = raw_event.raw_payload;
        return mapped;
    }

    return std::nullopt;
}

bool TikTokEventMapper::is_enabled(TikTokRawEventKind kind) const noexcept {
    switch (kind) {
    case TikTokRawEventKind::chat:
        return config_.emit_chat_events;
    case TikTokRawEventKind::like:
        return config_.emit_like_events;
    case TikTokRawEventKind::gift:
        return config_.emit_gift_events;
    case TikTokRawEventKind::follow:
        return config_.emit_follow_events;
    case TikTokRawEventKind::share:
        return config_.emit_share_events;
    case TikTokRawEventKind::viewer_join:
        return config_.emit_viewer_join_events;
    case TikTokRawEventKind::viewer_count:
        return config_.emit_viewer_count_events;
    case TikTokRawEventKind::live_start:
        return config_.emit_live_start_events;
    case TikTokRawEventKind::live_end:
        return config_.emit_live_end_events;
    case TikTokRawEventKind::moderation:
        return config_.emit_moderation_events;
    case TikTokRawEventKind::custom_raw:
        return config_.emit_custom_raw_events;
    }

    return false;
}

std::string TikTokEventMapper::normalize_avatar_url(std::string_view value) const {
    auto normalized = trim_copy(value);
    if (normalized.empty() || normalized.size() > 2048) {
        return "";
    }

    const auto is_http = normalized.rfind("http://", 0) == 0;
    const auto is_https = normalized.rfind("https://", 0) == 0;
    return (is_http || is_https) ? normalized : "";
}

} // namespace nlp3::bridge
