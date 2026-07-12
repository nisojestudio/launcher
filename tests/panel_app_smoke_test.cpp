#include <filesystem>
#include <cstdio>
#include <sstream>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "platform/panel_app.hpp"
#include "platform/panel_console.hpp"
#include "platform/panel_config_storage.hpp"
#include "test_require.hpp"
#include "test_support.hpp"

namespace {

std::filesystem::path resolve_module_directory() {
#ifdef _WIN32
        std::wstring buffer(MAX_PATH, L'\0');
        while (true) {
            const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        NLP3_TEST_REQUIRE(length != 0);
            if (length < buffer.size() - 1) {
                buffer.resize(length);
                return std::filesystem::path(buffer).parent_path();
            }
        buffer.resize(buffer.size() * 2, L'\0');
    }
#else
    return std::filesystem::current_path();
#endif
}

class ScopedCurrentPath {
public:
    explicit ScopedCurrentPath(const std::filesystem::path& target_path)
        : previous_path_(std::filesystem::current_path()) {
        std::filesystem::current_path(target_path);
    }

    ~ScopedCurrentPath() {
        std::filesystem::current_path(previous_path_);
    }

private:
    std::filesystem::path previous_path_;
};

} // namespace

int main() {
    nlp3::bridge::TikTokExternalWsServer::set_test_mode(true);
    std::puts("panel_app_smoke cp1");
    std::fflush(stdout);
    const auto config_path = nlp3::testsupport::write_temp_panel_config(
        "nlp3_panel_app_smoke_test_config.json",
        []() {
            nlp3::platform::PanelConfig config{};
            config.bridge_mode = "stub";
            config.bridge.stub_mode = true;
            config.bridge.source_name = "tiktok-stub";
            return config;
        }());

    nlp3::platform::PanelApp uninitialized_panel_app;
    const auto uninitialized_diagnostics = uninitialized_panel_app.diagnostics();
    NLP3_TEST_REQUIRE(!uninitialized_diagnostics.ok);
    NLP3_TEST_REQUIRE(!uninitialized_diagnostics.entries.empty());
    NLP3_TEST_REQUIRE(uninitialized_diagnostics.entries.front().code == "panel.not_initialized");

    nlp3::platform::PanelApp panel_app;
    NLP3_TEST_REQUIRE(panel_app.initialize(config_path.string()));
    std::puts("panel_app_smoke cp2");
    std::fflush(stdout);

    const auto snapshot = panel_app.snapshot();
    NLP3_TEST_REQUIRE(snapshot.panel_name == "Nisoje Studio");
    NLP3_TEST_REQUIRE(snapshot.bridge_mode == "stub");
    NLP3_TEST_REQUIRE(snapshot.bridge.integrated);
    NLP3_TEST_REQUIRE(snapshot.tts.available);
    NLP3_TEST_REQUIRE(snapshot.game.has_active_game);
    NLP3_TEST_REQUIRE(snapshot.game.active_game_id == "event-counter");
    NLP3_TEST_REQUIRE(snapshot.license.status == nlp3::platform::LicenseStatus::active);
    NLP3_TEST_REQUIRE(!panel_app.start_external_ws());
    NLP3_TEST_REQUIRE(!panel_app.external_ws_status().running);
    std::puts("panel_app_smoke cp3");
    std::fflush(stdout);

    std::istringstream console_input;
    std::ostringstream console_output;
    nlp3::platform::PanelConsole panel_console{
        &panel_app,
        &console_input,
        &console_output,
    };
    NLP3_TEST_REQUIRE(panel_console.execute_line("status"));
    NLP3_TEST_REQUIRE(panel_console.execute_line("diagnostics"));
    NLP3_TEST_REQUIRE(panel_console.execute_line("bridge demo ready"));

    const auto output = console_output.str();
    NLP3_TEST_REQUIRE(output.find("Nisoje Studio") != std::string::npos);
    NLP3_TEST_REQUIRE(output.find("Diagnostics: ok") != std::string::npos);
    NLP3_TEST_REQUIRE(output.find("Demo ready: no") != std::string::npos);
    NLP3_TEST_REQUIRE(output.find("bridge_mode=stub") != std::string::npos);
    std::puts("panel_app_smoke cp4");
    std::fflush(stdout);

    const auto module_directory = resolve_module_directory();
    const auto module_config_path = module_directory / "panel_config.json";
    const auto module_config_backup_path = module_directory / "panel_config.json.nlp3_backup";
    const auto foreign_cwd = std::filesystem::temp_directory_path() / "nlp3_panel_app_foreign_cwd";
    std::filesystem::create_directories(foreign_cwd);

    const bool had_module_config = std::filesystem::exists(module_config_path);
    if (had_module_config) {
        std::filesystem::remove(module_config_backup_path);
        std::filesystem::rename(module_config_path, module_config_backup_path);
    }

    nlp3::platform::PanelConfig module_config{};
    module_config.bridge_mode = "external";
    module_config.external_target_user = "module-path-user";
    module_config.bridge.stub_mode = false;
    module_config.bridge.source_name = "tiktok-external";
    nlp3::platform::PanelConfigStorage storage{};
    NLP3_TEST_REQUIRE(storage.save_to_file(module_config, module_config_path.string()));

    {
        ScopedCurrentPath foreign_path{foreign_cwd};
        nlp3::platform::PanelApp module_path_panel_app;
        NLP3_TEST_REQUIRE(module_path_panel_app.initialize());
        NLP3_TEST_REQUIRE(module_path_panel_app.config().bridge_mode == "external");
        NLP3_TEST_REQUIRE(module_path_panel_app.is_external_bridge_mode());
        NLP3_TEST_REQUIRE(module_path_panel_app.config().bridge.source_name == "tiktok-external");
    }
    std::puts("panel_app_smoke cp5");
    std::fflush(stdout);

    const auto reconnect_config_path = nlp3::testsupport::write_temp_panel_config(
        "nlp3_panel_app_reconnect_test_config.json",
        []() {
            nlp3::platform::PanelConfig config{};
            config.bridge_mode = "external";
            config.external_ws_port = 8765;
            config.external_target_user.clear();
            config.bridge.stub_mode = false;
            config.bridge.source_name = "tiktok-external";
            return config;
        }());
    nlp3::platform::PanelApp reconnect_panel_app;
    NLP3_TEST_REQUIRE(reconnect_panel_app.initialize(reconnect_config_path.string()));
    NLP3_TEST_REQUIRE(!reconnect_panel_app.reconnect_external_pipeline());
    const auto reconnect_snapshot = reconnect_panel_app.snapshot();
    NLP3_TEST_REQUIRE(reconnect_snapshot.external_bridge.connection_state == "config_error");
    NLP3_TEST_REQUIRE(
        reconnect_snapshot.external_bridge.last_status_message.find("external_target_user")
        != std::string::npos);
    std::filesystem::remove(reconnect_config_path);

    std::filesystem::remove(module_config_path);
    if (had_module_config) {
        std::filesystem::rename(module_config_backup_path, module_config_path);
    }

    std::filesystem::remove(config_path);
    std::puts("panel_app_smoke cp6");
    std::fflush(stdout);
    return 0;
}
