#include <cassert>
#include <string>

#include "bridge/tiktok_external_event_codec.hpp"

int main() {
    const nlp3::bridge::TikTokExternalEventCodec codec{};

    const nlp3::bridge::TikTokRawEvent viewer_count_event{
        nlp3::bridge::TikTokRawEventKind::viewer_count,
        {},
        {
            "evt-viewer-count-001",
            "room-001",
            "viewer_count",
            1710000001000,
        },
        "",
        std::nullopt,
        0,
        321,
    };
    const auto encoded_viewer_count = codec.encode_json(viewer_count_event);
    const auto decoded_viewer_count = codec.decode_json(encoded_viewer_count);
    assert(decoded_viewer_count.has_value());
    assert(decoded_viewer_count->kind == nlp3::bridge::TikTokRawEventKind::viewer_count);
    assert(decoded_viewer_count->viewer_count == 321);
    assert(decoded_viewer_count->metadata.event_id == "evt-viewer-count-001");

    const nlp3::bridge::TikTokRawEvent moderation_event{
        nlp3::bridge::TikTokRawEventKind::moderation,
        {
            "mod-01",
            "mod_user",
            "Moderator",
            "",
        },
        {
            "evt-moderation-001",
            "room-002",
            "moderation",
            1710000002000,
        },
        "mute",
        std::nullopt,
        0,
        0,
        "mute",
        R"({"event_class":"RoomMessageEvent","action":"mute"})",
    };
    const auto encoded_moderation = codec.encode_json(moderation_event);
    const auto decoded_moderation = codec.decode_json(encoded_moderation);
    assert(decoded_moderation.has_value());
    assert(decoded_moderation->kind == nlp3::bridge::TikTokRawEventKind::moderation);
    assert(decoded_moderation->moderation_action == "mute");
    assert(decoded_moderation->raw_payload.find("RoomMessageEvent") != std::string::npos);

    const std::string custom_raw_payload = R"({
        "message_type":"event",
        "kind":"custom_raw",
        "actor":{"id":"raw-01","username":"raw-user","display_name":"Raw User","avatar_url":""},
        "metadata":{"event_id":"evt-custom-001","room_id":"room-003","source_event_type":"control","timestamp_ms":1710000003000},
        "text":"custom payload",
        "gift":null,
        "viewer_count":0,
        "like_count":7,
        "raw_payload":{"foo":"bar","count":7}
    })";
    const auto decoded_custom_raw = codec.decode_json(custom_raw_payload);
    assert(decoded_custom_raw.has_value());
    assert(decoded_custom_raw->kind == nlp3::bridge::TikTokRawEventKind::custom_raw);
    assert(decoded_custom_raw->like_count == 7);
    assert(decoded_custom_raw->raw_payload.find("\"foo\":\"bar\"") != std::string::npos);

    return 0;
}
