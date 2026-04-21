#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

#include "platform/panel_config.hpp"
#include "platform/panel_config_storage.hpp"

int main() {
    const auto config_path =
        std::filesystem::temp_directory_path() / "nlp3_panel_config_storage_test.json";
    std::filesystem::remove(config_path);

    {
        std::ofstream output(config_path, std::ios::binary | std::ios::trunc);
        assert(output.good());
        output
            << "{\n"
            << "  \"panel_name\": \"Partial Config\",\n"
            << "  \"bridge\": {\n"
            << "    \"enabled\": false\n"
            << "  }\n"
            << "}\n";
    }

    nlp3::platform::PanelConfigStorage storage;
    nlp3::platform::PanelConfig partial{};
    assert(storage.load_from_file(config_path.string(), partial));
    assert(partial.panel_name == "Partial Config");
    assert(partial.default_game_id == "event-counter");
    assert(partial.bridge_mode == "stub");
    assert(!partial.bridge.enabled);
    assert(partial.bridge.stub_mode);
    assert(partial.bridge.source_name == "tiktok-stub");

    {
        std::ofstream output(config_path, std::ios::binary | std::ios::trunc);
        assert(output.good());
        output
            << "{\n"
            << "  \"bridge_mode\": \"external\",\n"
            << "  \"bridge\": {\n"
            << "    \"stub_mode\": true,\n"
            << "    \"source_name\": \"tiktok-stub\"\n"
            << "  }\n"
            << "}\n";
    }

    nlp3::platform::PanelConfig external{};
    assert(storage.load_from_file(config_path.string(), external));
    assert(external.bridge_mode == "external");
    assert(!external.bridge.stub_mode);
    assert(external.bridge.source_name == "tiktok-external");

    nlp3::platform::PanelConfig auth_roundtrip{};
    auth_roundtrip.panel_name = "Auth Config";
    auth_roundtrip.auth.required = true;
    auth_roundtrip.auth.firebase_api_key = "test-api-key";
    auth_roundtrip.auth.firebase_project_id = "test-project";
    auth_roundtrip.auth.firebase_auth_domain = "test-project.firebaseapp.com";
    auth_roundtrip.auth.nisoje_api_base = "https://example.invalid";
    auth_roundtrip.auth.me_licenses_path = "/api/licenses";
    auth_roundtrip.auth.me_games_catalog_path = "/api/games/catalog";
    assert(storage.save_to_file(auth_roundtrip, config_path.string()));

    nlp3::platform::PanelConfig loaded_auth{};
    assert(storage.load_from_file(config_path.string(), loaded_auth));
    assert(loaded_auth.panel_name == "Auth Config");
    assert(loaded_auth.auth.required);
    assert(loaded_auth.auth.firebase_api_key == "test-api-key");
    assert(loaded_auth.auth.firebase_project_id == "test-project");
    assert(loaded_auth.auth.firebase_auth_domain == "test-project.firebaseapp.com");
    assert(loaded_auth.auth.nisoje_api_base == "https://example.invalid");
    assert(loaded_auth.auth.me_licenses_path == "/api/licenses");
    assert(loaded_auth.auth.me_games_catalog_path == "/api/games/catalog");

    {
        std::ofstream output(config_path, std::ios::binary | std::ios::trunc);
        assert(output.good());
        output
            << "{\n"
            << "  \"panel_name\": 42,\n"
            << "  \"external_ws_port\": \"oops\"\n"
            << "}\n";
    }

    nlp3::platform::PanelConfig sentinel{};
    sentinel.panel_name = "Sentinel";
    sentinel.external_ws_port = 34567;
    assert(!storage.load_from_file(config_path.string(), sentinel));
    assert(sentinel.panel_name == "Sentinel");
    assert(sentinel.external_ws_port == 34567);

    std::filesystem::remove(config_path);
    return 0;
}
