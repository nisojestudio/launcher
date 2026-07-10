#include "platform/panel_diagnostics_builder.hpp"

#include <string>
#include <utility>

#include "bridge/tiktok_bridge_session.hpp"

namespace {

using nlp3::platform::PanelDiagnosticEntry;
using nlp3::platform::PanelDiagnosticLevel;

PanelDiagnosticEntry make_entry(
    PanelDiagnosticLevel level,
    std::string code,
    std::string message) {
    return PanelDiagnosticEntry{
        level,
        std::move(code),
        std::move(message),
    };
}

} // namespace

namespace nlp3::platform {

PanelDiagnosticsReport PanelDiagnosticsBuilder::build(const PanelSnapshot& snapshot) const {
    PanelDiagnosticsReport report{};

    if (snapshot.bridge.integrated) {
        report.entries.push_back(make_entry(
            PanelDiagnosticLevel::info,
            "bridge.integrated",
            "Bridge integrado"));
    } else {
        report.entries.push_back(make_entry(
            PanelDiagnosticLevel::warning,
            "bridge.not_integrated",
            "Bridge no integrado"));
    }

    if (snapshot.bridge.state == nlp3::bridge::TikTokBridgeSessionState::faulted) {
        report.entries.push_back(make_entry(
            PanelDiagnosticLevel::error,
            "bridge.faulted",
            "Bridge en estado faulted"));
    } else if (snapshot.bridge.state != nlp3::bridge::TikTokBridgeSessionState::running) {
        report.entries.push_back(make_entry(
            PanelDiagnosticLevel::warning,
            "bridge.not_running",
            "Bridge no esta running"));
    }

    if (snapshot.bridge.last_fault.has_value()) {
        const auto& fault = *snapshot.bridge.last_fault;
        report.entries.push_back(make_entry(
            PanelDiagnosticLevel::error,
            fault.code.empty() ? "bridge.last_fault" : fault.code,
            fault.message.empty() ? "Bridge reporto un fault" : fault.message));
    }

    if (snapshot.tts.available) {
        report.entries.push_back(make_entry(
            PanelDiagnosticLevel::info,
            "tts.available",
            "TTS disponible"));
    } else {
        report.entries.push_back(make_entry(
            PanelDiagnosticLevel::warning,
            "tts.unavailable",
            "TTS no disponible"));
    }

    if (snapshot.game.has_active_game) {
        report.entries.push_back(make_entry(
            PanelDiagnosticLevel::info,
            "game.active",
            "Juego activo: " + snapshot.game.active_game_id));
    } else {
        report.entries.push_back(make_entry(
            PanelDiagnosticLevel::warning,
            "game.inactive",
            "No hay juego activo"));
    }

    switch (snapshot.license.status) {
    case LicenseStatus::active:
        report.entries.push_back(make_entry(
            PanelDiagnosticLevel::info,
            "license.active",
            "Licencia activa"));
        break;
    case LicenseStatus::inactive:
        report.entries.push_back(make_entry(
            PanelDiagnosticLevel::warning,
            "license.inactive",
            "Licencia inactiva"));
        break;
    case LicenseStatus::unknown:
        report.entries.push_back(make_entry(
            PanelDiagnosticLevel::warning,
            "license.unknown",
            "Licencia en estado desconocido"));
        break;
    }

    if (!snapshot.recent_activity.empty()) {
        report.entries.push_back(make_entry(
            PanelDiagnosticLevel::info,
            "activity.present",
            "Actividad reciente disponible"));
    } else {
        report.entries.push_back(make_entry(
            PanelDiagnosticLevel::info,
            "activity.empty",
            "Sin actividad reciente"));
    }

    if (snapshot.external_bridge.external_mode) {
        // En auto-demo (sin target_user) no hay bridge TikTok activo;
        // el check de runtime no debe reportarse como error.
        if (snapshot.external_bridge.runtime_checked && !snapshot.external_bridge.target_user.empty()) {
            if (snapshot.external_bridge.runtime_ready) {
                report.entries.push_back(make_entry(
                    PanelDiagnosticLevel::info,
                    "external.runtime.ready",
                    snapshot.external_bridge.runtime_summary.empty()
                        ? "Runtime TikTok listo"
                        : snapshot.external_bridge.runtime_summary));
            } else {
                report.entries.push_back(make_entry(
                    PanelDiagnosticLevel::error,
                    "external.runtime.missing",
                    snapshot.external_bridge.runtime_summary.empty()
                        ? "Faltan dependencias para TikTok"
                        : snapshot.external_bridge.runtime_summary));
                for (const auto& alert : snapshot.external_bridge.runtime_alerts) {
                    report.entries.push_back(make_entry(
                        PanelDiagnosticLevel::error,
                        "external.runtime.alert",
                        alert));
                }
            }
        }

        if (snapshot.external_ws.running) {
            report.entries.push_back(make_entry(
                PanelDiagnosticLevel::info,
                "external.ws.running",
                "WS external local activo en puerto " + std::to_string(snapshot.external_ws.port)));
        } else {
            report.entries.push_back(make_entry(
                PanelDiagnosticLevel::warning,
                "external.ws.not_running",
                "WS external local no esta activo"));
        }

        if (!snapshot.external_bridge.target_user.empty()) {
            report.entries.push_back(make_entry(
                PanelDiagnosticLevel::info,
                "external.target_user",
                "Target TikTok: " + snapshot.external_bridge.target_user));
        }

        if (snapshot.external_bridge.runner_running) {
            report.entries.push_back(make_entry(
                PanelDiagnosticLevel::info,
                "external.runner.running",
                "Runner Python activo"));
        } else if (!snapshot.external_bridge.target_user.empty()) {
            report.entries.push_back(make_entry(
                PanelDiagnosticLevel::warning,
                "external.runner.not_running",
                "Runner Python detenido"));
        }

        if (snapshot.external_bridge.runner_has_exit_code && snapshot.external_bridge.runner_last_exit_code != 0) {
            report.entries.push_back(make_entry(
                PanelDiagnosticLevel::error,
                "external.runner.exit",
                "Runner Python termino con exit code " + std::to_string(snapshot.external_bridge.runner_last_exit_code)));
        }

        if (!snapshot.external_bridge.runner_last_error.empty()
            && (!snapshot.external_bridge.runner_has_exit_code || snapshot.external_bridge.runner_last_exit_code != 0)) {
            report.entries.push_back(make_entry(
                PanelDiagnosticLevel::warning,
                "external.runner.last_error",
                snapshot.external_bridge.runner_last_error));
        }

        if (snapshot.external_bridge.connection_state == "connected") {
            report.entries.push_back(make_entry(
                PanelDiagnosticLevel::info,
                "external.connection.connected",
                "Sesion TikTok external conectada"));
        } else if (!snapshot.external_bridge.connection_state.empty()) {
            report.entries.push_back(make_entry(
                PanelDiagnosticLevel::warning,
                "external.connection.state",
                "Sesion TikTok external en estado " + snapshot.external_bridge.connection_state));
        }
    }

    for (const auto& entry : report.entries) {
        if (entry.level == PanelDiagnosticLevel::error) {
            report.ok = false;
            break;
        }
    }

    return report;
}

} // namespace nlp3::platform
