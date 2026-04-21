#pragma once

#include <string>

namespace nlp3::bridge {

struct TikTokBridgeConfig {
    bool enabled = true;
    bool stub_mode = true;
    bool emit_chat_events = true;
    bool emit_like_events = true;
    bool emit_gift_events = true;
    bool emit_follow_events = true;
    bool emit_share_events = true;
    bool emit_viewer_join_events = true;
    bool emit_viewer_count_events = true;
    bool emit_live_start_events = true;
    bool emit_live_end_events = true;
    bool emit_moderation_events = true;
    bool emit_custom_raw_events = true;
    bool passthrough_avatar_url = true;
    std::string source_name = "tiktok-stub";
};

} // namespace nlp3::bridge
