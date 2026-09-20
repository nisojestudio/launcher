#pragma once

#include <cstdint>
#include <string>

#include "bridge/tiktok_bridge_config.hpp"
#include "host/host_automation.hpp"
#include "host/host_periodic_tts.hpp"
#include "tts/tts_config.hpp"
#include "tts/tts_policy.hpp"

namespace nlp3::platform {

struct PanelAuthConfig {
    bool required = false;
    std::string firebase_api_key = "AIzaSyBWRMoHbPNkOw0zvflcPb_dv9G1Bgg1uLc";
    std::string firebase_project_id = "nisoje-studio";
    std::string firebase_auth_domain = "nisoje-studio.firebaseapp.com";
    std::string nisoje_api_base = "https://nisoje.com";
    std::string me_licenses_path = "/api/me/licenses";
    // Used by the panel to fetch the licensed remote catalog.
    std::string me_games_catalog_path = "/api/me/games/catalog";
    // Called after login to register this device against the activated license.
    std::string license_activate_path = "/api/license/activate";
};

struct PanelConfig {
    std::string panel_name = "Nisoje Studio";
    std::string default_game_id = "event-counter";
    std::string bridge_mode = "stub";
    std::string external_target_user{};
    std::string provider_api_key{};
    std::string tiktok_provider = "tiktools";
    // 0 = automatico: el panel busca el primer puerto libre del rango 8765-8795
    // y, si hace falta, deja que Windows asigne uno efimero.
    std::uint16_t external_ws_port = 0;
    bool embedded_ui_enabled = true;
    bool embedded_ui_fallback_to_browser = true;
    bool embedded_ui_devtools = false;
    std::string embedded_ui_url = "http://127.0.0.1:18913/";
    // Fase 3: el puerto local de la UI embebida vive en su propio campo porque
    // antes se derivaba de `embedded_ui_url`, y la URL efimera del quick tunnel
    // que se escribia ahi hacia que el panel olvidara su puerto real en el
    // siguiente arranque (caia siempre a 18913).
    std::uint16_t embedded_ui_port = 18913;
    std::uint64_t embedded_ui_startup_timeout_ms = 8000;
    std::string host_energy_level = "balanced";
    std::string host_tone_style = "neutral";
    std::string overlay_host = "localhost";

    PanelAuthConfig auth{};
    bridge::TikTokBridgeConfig bridge{};
    tts::TtsConfig tts_runtime{};
    tts::TtsPolicy tts{};
    host::HostAutomationConfig automation{};
    host::HostPeriodicTtsConfig periodic_tts{};
};

} // namespace nlp3::platform
