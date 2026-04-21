#pragma once

#include <string>
#include <string_view>

namespace nlp3::gamesdk {

struct GameCapabilities {
    bool uses_chat_messages = false;
    bool uses_gifts = false;
    bool uses_follows = false;
    bool uses_shares = false;
    bool uses_viewer_joins = false;
    bool uses_avatar_data = false;
    bool uses_tts = false;
};

struct HostCompatibilityProfile {
    int host_api_version = 1;
    bool tts_available = false;
    std::string_view platform = "windows";
};

struct GameCompatibility {
    int min_host_api_version = 1;
    bool requires_tts = false;
    std::string target_platform = "windows";
};

struct GameManifest {
    std::string game_id;
    std::string display_name;
    std::string version = "0.1.0";
    GameCompatibility compatibility{};
    std::string description;
    std::string author = "nlp3";
    GameCapabilities capabilities{};
};

bool is_compatible(
    const GameCompatibility& compatibility,
    const HostCompatibilityProfile& host_profile) noexcept;

} // namespace nlp3::gamesdk
