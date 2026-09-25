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
    // Emitidos por el bridge Python; deben decodificarse para que el monitor
    // del live pueda mostrar la reconexion y el cierre de la sesion.
    reconnecting,
    stopped,
    idle,
};

struct TikTokExternalSessionStatus {
    std::string target_user{};
    std::string room_id{};
    TikTokExternalSessionConnectionState connection_state =
        TikTokExternalSessionConnectionState::unknown;
    std::string message{};
    std::int64_t timestamp_ms = 0;
    // Diagnostico visible en el panel (opcionales: el bridge viejo no los manda).
    std::string phase{};        // starting | connecting | waiting | rate_limited | connected | error
    std::string severity{};     // info | warn | error
    std::string alert_code{};   // codigo del catalogo de errores del bridge
    double retry_in_sec = 0.0;  // segundos hasta el proximo intento
    // Presupuesto diario de aperturas contra el proveedor (0 = sin tope: no se
    // emite). El panel lo muestra para que el operador vea cuanto le queda hoy.
    std::int32_t daily_budget_total = 0;
    std::int32_t daily_budget_remaining = 0;
    std::int32_t daily_budget_manual_reserve = 0;
    std::int32_t daily_budget_remaining_auto = 0;
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
    case TikTokExternalSessionConnectionState::reconnecting:
        return "reconnecting";
    case TikTokExternalSessionConnectionState::stopped:
        return "stopped";
    case TikTokExternalSessionConnectionState::idle:
        return "idle";
    case TikTokExternalSessionConnectionState::unknown:
        break;
    }

    return "unknown";
}

std::optional<TikTokExternalSessionConnectionState> parse_external_session_connection_state(
    std::string_view state) noexcept;

} // namespace nlp3::bridge
