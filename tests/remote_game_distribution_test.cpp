#include <filesystem>
#include <string>
#include <vector>

#include "platform/remote_game_distribution_service.hpp"
#include "test_require.hpp"

int main() {
    using nlp3::platform::RemoteGameInstallRecord;
    using nlp3::platform::load_remote_game_install_registry;
    using nlp3::platform::parse_remote_game_catalog_response;
    using nlp3::platform::save_remote_game_install_registry;

    const auto parsed = parse_remote_game_catalog_response(R"({
        "games": [
            {
                "game_id": "arena_live",
                "display_name": "Arena Live",
                "version": "20260411-030455",
                "sha256": "abc123",
                "download_url": "https://example.invalid/download/arena",
                "download_url_expires_at": "2026-04-11T03:35:00Z",
                "package_path": "catalog/games/arena_live/20260411-030455/arena_live-20260411-030455.zip",
                "manifest": {
                    "displayName": "Arena Live",
                    "description": "Juego live"
                }
            },
            {
                "game_id": "arena_live",
                "display_name": "Duplicated Arena",
                "sha256": "fff",
                "download_url": "https://example.invalid/download/arena-dup"
            },
            {
                "game_id": "missing_download",
                "sha256": "fff"
            }
        ]
    })");

    NLP3_TEST_REQUIRE(parsed.ok);
    NLP3_TEST_REQUIRE(parsed.games.size() == 1);
    NLP3_TEST_REQUIRE(parsed.games.front().game_id == "arena_live");
    NLP3_TEST_REQUIRE(parsed.games.front().display_name == "Arena Live");
    NLP3_TEST_REQUIRE(parsed.games.front().version == "20260411-030455");
    NLP3_TEST_REQUIRE(parsed.games.front().sha256 == "ABC123");
    NLP3_TEST_REQUIRE(parsed.games.front().download_url == "https://example.invalid/download/arena");
    NLP3_TEST_REQUIRE(parsed.games.front().manifest.display_name == "Arena Live");
    NLP3_TEST_REQUIRE(parsed.games.front().manifest.description == "Juego live");

    const auto temp_root = std::filesystem::temp_directory_path() / "nlp3_remote_game_distribution_test";
    const auto registry_path = temp_root / "state" / "remote_game_installs.json";
    std::error_code error;
    std::filesystem::remove_all(temp_root, error);

    const std::vector<RemoteGameInstallRecord> expected{
        RemoteGameInstallRecord{
            "arena_live",
            "Arena Live",
            "20260411-030455",
            "ABC123",
            temp_root / "games" / "arena_live" / "20260411-030455",
            1712806200000,
        },
        RemoteGameInstallRecord{
            "conquista",
            "Conquista",
            "20260411-030455",
            "DEF456",
            temp_root / "games" / "conquista" / "20260411-030455",
            1712806201000,
        },
    };

    NLP3_TEST_REQUIRE(save_remote_game_install_registry(registry_path, expected));

    const auto loaded = load_remote_game_install_registry(registry_path);
    NLP3_TEST_REQUIRE(loaded.size() == expected.size());
    NLP3_TEST_REQUIRE(loaded[0].game_id == expected[0].game_id);
    NLP3_TEST_REQUIRE(loaded[0].display_name == expected[0].display_name);
    NLP3_TEST_REQUIRE(loaded[0].version == expected[0].version);
    NLP3_TEST_REQUIRE(loaded[0].sha256 == expected[0].sha256);
    NLP3_TEST_REQUIRE(loaded[0].install_root == expected[0].install_root);
    NLP3_TEST_REQUIRE(loaded[0].installed_at_ms == expected[0].installed_at_ms);
    NLP3_TEST_REQUIRE(loaded[1].game_id == expected[1].game_id);
    NLP3_TEST_REQUIRE(loaded[1].install_root == expected[1].install_root);

    std::filesystem::remove_all(temp_root, error);
    return 0;
}
