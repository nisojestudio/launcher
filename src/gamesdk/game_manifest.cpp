#include "gamesdk/game_manifest.hpp"

namespace nlp3::gamesdk {

bool is_compatible(
    const GameCompatibility& compatibility,
    const HostCompatibilityProfile& host_profile) noexcept {
    if (host_profile.host_api_version < compatibility.min_host_api_version) {
        return false;
    }

    if (compatibility.requires_tts && !host_profile.tts_available) {
        return false;
    }

    return compatibility.target_platform == host_profile.platform;
}

} // namespace nlp3::gamesdk
