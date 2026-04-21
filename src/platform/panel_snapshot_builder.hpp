#pragma once

#include "gamesdk/game_runtime_controller.hpp"
#include "host/host_runtime.hpp"
#include "platform/external_bridge_manifest.hpp"
#include "platform/license_service.hpp"
#include "platform/panel_activity.hpp"
#include "platform/panel_config.hpp"
#include "platform/panel_snapshot.hpp"

namespace nlp3::platform {

PanelSnapshot build_panel_snapshot(
    const PanelConfig& config,
    const host::HostRuntime& runtime,
    const gamesdk::GameRuntimeController& game_runtime_controller,
    const PanelActivityLog* activity_log = nullptr,
    const ILicenseService* license_service = nullptr,
    PanelAuthStatus auth_status = {},
    ExternalBridgeManifest external_bridge_manifest = {},
    PanelExternalWsStatus external_ws_status = {},
    PanelExternalGameStatus external_game_status = {});

PanelSnapshot build_panel_snapshot(
    const PanelConfig& config,
    const host::HostRuntime& runtime,
    const gamesdk::GameRuntimeController& game_runtime_controller,
    const PanelActivityLog* activity_log,
    const ILicenseService* license_service,
    PanelAuthStatus auth_status,
    ExternalBridgeManifest external_bridge_manifest,
    PanelExternalWsStatus external_ws_status);

} // namespace nlp3::platform
