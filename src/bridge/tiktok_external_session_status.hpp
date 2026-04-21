#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace nlp3::bridge {

enum class TikTokExternalSessionConnectionState {
    unknown,
    preparing,
    resolving_room,
    connecting,
    connected,
    disconnected,
    faulted,
};

struct TikTokExternalSessionStatus {
    std::string target_user{};
    std::string room_id{};
    TikTokExternalSessionConnectionState connection_state =
        TikTokExternalSessionConnectionState::unknown;
    std::string message{};
    std::int64_t timestamp_ms = 0;
};

constexpr std::string_view to_string(TikTokExternalSessionConnectionState state) noexcept {
    switch (state) {
    case TikTokExternalSessionConnectionState::preparing:
        return "preparing";
    case TikTokExternalSessionConnectionState::resolving_room:
        return "resolving_room";
    case TikTokExternalSessionConnectionState::connecting:
        return "connecting";
    case TikTokExternalSessionConnectionState::connected:
        return "connected";
    case TikTokExternalSessionConnectionState::disconnected:
        return "disconnected";
    case TikTokExternalSessionConnectionState::faulted:
        return "faulted";
    case TikTokExternalSessionConnectionState::unknown:
        break;
    }

    return "unknown";
}

std::optional<TikTokExternalSessionConnectionState> parse_external_session_connection_state(
    std::string_view state) noexcept;

} // namespace nlp3::bridge
