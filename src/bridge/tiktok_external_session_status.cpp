#include "bridge/tiktok_external_session_status.hpp"

namespace nlp3::bridge {

std::optional<TikTokExternalSessionConnectionState> parse_external_session_connection_state(
    std::string_view state) noexcept {
    if (state == "preparing") {
        return TikTokExternalSessionConnectionState::preparing;
    }
    if (state == "resolving_room") {
        return TikTokExternalSessionConnectionState::resolving_room;
    }
    if (state == "connecting") {
        return TikTokExternalSessionConnectionState::connecting;
    }
    if (state == "connected") {
        return TikTokExternalSessionConnectionState::connected;
    }
    if (state == "disconnected") {
        return TikTokExternalSessionConnectionState::disconnected;
    }
    if (state == "faulted") {
        return TikTokExternalSessionConnectionState::faulted;
    }
    // Estados que el bridge Python ya emite. Sin estos, el panel descartaba el
    // mensaje completo: no se mostraba "Reintentando" ni las alertas asociadas.
    if (state == "reconnecting") {
        return TikTokExternalSessionConnectionState::reconnecting;
    }
    if (state == "stopped") {
        return TikTokExternalSessionConnectionState::stopped;
    }
    if (state == "idle") {
        return TikTokExternalSessionConnectionState::idle;
    }
    if (state == "unknown") {
        return TikTokExternalSessionConnectionState::unknown;
    }

    return std::nullopt;
}

} // namespace nlp3::bridge
