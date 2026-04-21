#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "bridge/tiktok_raw_event.hpp"
#include "platform/panel_config.hpp"
#include "platform/panel_config_storage.hpp"
#include "test_require.hpp"

namespace nlp3::testsupport {

inline std::filesystem::path write_temp_panel_config(
    std::string_view filename,
    const platform::PanelConfig& config) {
    const auto path = std::filesystem::temp_directory_path() / std::filesystem::path(filename);
    std::filesystem::remove(path);

    platform::PanelConfigStorage storage;
    const bool saved = storage.save_to_file(config, path.string());
    NLP3_TEST_REQUIRE(saved);
    return path;
}

inline bridge::TikTokRawEvent make_chat_event(
    std::string actor_id,
    std::string username,
    std::string display_name,
    std::string event_id,
    std::string room_id,
    std::string text,
    std::int64_t timestamp_ms = 0,
    std::string avatar_url = {}) {
    return bridge::TikTokRawEvent{
        bridge::TikTokRawEventKind::chat,
        bridge::TikTokRawActor{
            std::move(actor_id),
            std::move(username),
            std::move(display_name),
            std::move(avatar_url),
        },
        bridge::TikTokRawMetadata{
            std::move(event_id),
            std::move(room_id),
            "comment",
            timestamp_ms,
        },
        std::move(text),
        std::nullopt,
        0,
    };
}

inline bridge::TikTokRawEvent make_follow_event(
    std::string actor_id,
    std::string username,
    std::string display_name,
    std::string event_id,
    std::string room_id,
    std::int64_t timestamp_ms = 0,
    std::string avatar_url = {}) {
    return bridge::TikTokRawEvent{
        bridge::TikTokRawEventKind::follow,
        bridge::TikTokRawActor{
            std::move(actor_id),
            std::move(username),
            std::move(display_name),
            std::move(avatar_url),
        },
        bridge::TikTokRawMetadata{
            std::move(event_id),
            std::move(room_id),
            "follow",
            timestamp_ms,
        },
        "",
        std::nullopt,
        0,
    };
}

inline bridge::TikTokRawEvent make_gift_event(
    std::string actor_id,
    std::string username,
    std::string display_name,
    std::string event_id,
    std::string room_id,
    std::string gift_id,
    std::string gift_name,
    std::int64_t quantity,
    std::int64_t diamond_count,
    std::int64_t timestamp_ms = 0,
    std::string avatar_url = {}) {
    return bridge::TikTokRawEvent{
        bridge::TikTokRawEventKind::gift,
        bridge::TikTokRawActor{
            std::move(actor_id),
            std::move(username),
            std::move(display_name),
            std::move(avatar_url),
        },
        bridge::TikTokRawMetadata{
            std::move(event_id),
            std::move(room_id),
            "gift",
            timestamp_ms,
        },
        "",
        bridge::TikTokRawGiftData{
            std::move(gift_id),
            std::move(gift_name),
            static_cast<int>(quantity),
            static_cast<int>(diamond_count),
        },
        0,
    };
}

} // namespace nlp3::testsupport
