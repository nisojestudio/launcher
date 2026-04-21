#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>

#include "bridge/tiktok_external_event_codec.hpp"
#include "platform/panel_app.hpp"
#include "platform/panel_config.hpp"
#include "platform/panel_config_storage.hpp"
#include "platform/panel_console.hpp"
#include "test_require.hpp"

int main() {
    const auto config_path =
        std::filesystem::temp_directory_path() / "nlp3_panel_external_storage_test_config.json";
    std::filesystem::remove(config_path);

    {
        std::ofstream config_output(config_path, std::ios::binary | std::ios::trunc);
        NLP3_TEST_REQUIRE(config_output.good());
        config_output
            << "{\n"
            << "  \"bridge_mode\": \"external\",\n"
            << "  \"external_target_user\": \"storage_user\",\n"
            << "  \"external_ws_port\": 8765,\n"
            << "  \"bridge\": {\n"
            << "    \"enabled\": true,\n"
            << "    \"stub_mode\": true,\n"
            << "    \"source_name\": \"tiktok-stub\"\n"
            << "  }\n"
            << "}\n";
    }

    nlp3::platform::PanelApp panel_app;
    NLP3_TEST_REQUIRE(panel_app.initialize(config_path.string()));
    NLP3_TEST_REQUIRE(panel_app.is_external_bridge_mode());
    NLP3_TEST_REQUIRE(!panel_app.config().bridge.stub_mode);
    NLP3_TEST_REQUIRE(panel_app.config().bridge.source_name == "tiktok-external");
    NLP3_TEST_REQUIRE(panel_app.save_config());

    nlp3::platform::PanelConfigStorage config_storage;
    nlp3::platform::PanelConfig persisted_config{};
    NLP3_TEST_REQUIRE(config_storage.load_from_file(config_path.string(), persisted_config));
    NLP3_TEST_REQUIRE(!persisted_config.bridge.stub_mode);
    NLP3_TEST_REQUIRE(persisted_config.bridge.source_name == "tiktok-external");
    {
        std::ifstream saved_input(config_path, std::ios::binary);
        NLP3_TEST_REQUIRE(saved_input.good());
        const std::string saved_payload{
            std::istreambuf_iterator<char>{saved_input},
            std::istreambuf_iterator<char>{},
        };
        NLP3_TEST_REQUIRE(saved_payload.find("\"stub_mode\": false") != std::string::npos);
        NLP3_TEST_REQUIRE(saved_payload.find("\"source_name\": \"tiktok-external\"") != std::string::npos);
    }

    std::istringstream console_input;
    std::ostringstream console_output;
    nlp3::platform::PanelConsole panel_console{
        &panel_app,
        &console_input,
        &console_output,
    };

    const auto codec = nlp3::bridge::TikTokExternalEventCodec{};

    const auto recording_path =
        std::filesystem::temp_directory_path() / "nlp3_panel_external_storage_recording.jsonl";
    std::filesystem::remove(recording_path);
    NLP3_TEST_REQUIRE(panel_console.execute_line(std::string{"bridge record start "} + recording_path.string()));
    NLP3_TEST_REQUIRE(panel_app.is_external_bridge_recording());
    NLP3_TEST_REQUIRE(panel_app.external_bridge_recording_path() == recording_path.string());

    NLP3_TEST_REQUIRE(panel_app.submit_external_bridge_event(nlp3::bridge::TikTokRawEvent{
        nlp3::bridge::TikTokRawEventKind::chat,
        nlp3::bridge::TikTokRawActor{
            "storage-user-01",
            "storage_alice",
            "Storage Alice",
            "",
        },
        nlp3::bridge::TikTokRawMetadata{
            "evt-storage-chat-001",
            "room-storage-001",
            "comment",
            1710000010000,
        },
        "Storage hello",
        std::nullopt,
        0,
    }));
    NLP3_TEST_REQUIRE(panel_app.tick_bridge(1) == 1);
    const auto recording_snapshot = panel_app.snapshot();
    NLP3_TEST_REQUIRE(recording_snapshot.total_events == 1);
    NLP3_TEST_REQUIRE(recording_snapshot.external_bridge.recording);
    NLP3_TEST_REQUIRE(recording_snapshot.external_bridge.recording_path == recording_path.string());
    NLP3_TEST_REQUIRE(std::filesystem::exists(recording_path));
    {
        std::ifstream recording_input(recording_path, std::ios::binary);
        NLP3_TEST_REQUIRE(recording_input.good());
        const std::string payload{
            std::istreambuf_iterator<char>{recording_input},
            std::istreambuf_iterator<char>{},
        };
        NLP3_TEST_REQUIRE(payload.find("evt-storage-chat-001") != std::string::npos);
    }

    NLP3_TEST_REQUIRE(panel_console.execute_line("bridge record stop"));
    NLP3_TEST_REQUIRE(!panel_app.is_external_bridge_recording());
    NLP3_TEST_REQUIRE(panel_app.external_bridge_recording_path().empty());

    NLP3_TEST_REQUIRE(panel_app.replay_external_bridge_file(recording_path.string()) == 1);
    const auto replay_snapshot = panel_app.snapshot();
    const auto replay_manifest = panel_app.external_bridge_manifest();
    NLP3_TEST_REQUIRE(replay_snapshot.total_events == 2);
    NLP3_TEST_REQUIRE(replay_manifest.last_replay_path == recording_path.string());
    NLP3_TEST_REQUIRE(replay_manifest.last_replay_accepted_events == 1);

    NLP3_TEST_REQUIRE(panel_console.execute_line(std::string{"bridge replay file "} + recording_path.string()));
    const auto replay_console_snapshot = panel_app.snapshot();
    NLP3_TEST_REQUIRE(replay_console_snapshot.total_events == 3);
    NLP3_TEST_REQUIRE(console_output.str().find("replayed 1 external events") != std::string::npos);

    const auto inbox_dir =
        std::filesystem::temp_directory_path() / "nlp3_panel_external_storage_inbox";
    std::filesystem::remove_all(inbox_dir);
    std::filesystem::create_directories(inbox_dir);
    {
        std::ofstream valid_output(inbox_dir / "valid-chat.json", std::ios::binary | std::ios::trunc);
        NLP3_TEST_REQUIRE(valid_output.good());
        valid_output << codec.encode_json(nlp3::bridge::TikTokRawEvent{
            nlp3::bridge::TikTokRawEventKind::chat,
            nlp3::bridge::TikTokRawActor{
                "storage-user-02",
                "storage_bob",
                "Storage Bob",
                "",
            },
            nlp3::bridge::TikTokRawMetadata{
                "evt-storage-chat-002",
                "room-storage-001",
                "comment",
                1710000010001,
            },
            "Inbox hello",
            std::nullopt,
            0,
        });
    }
    {
        std::ofstream invalid_output(inbox_dir / "invalid.json", std::ios::binary | std::ios::trunc);
        NLP3_TEST_REQUIRE(invalid_output.good());
        invalid_output << "{ invalid-json";
    }

    const auto inbox_before = panel_app.snapshot().total_events;
    const auto inbox_result = panel_app.process_external_inbox(inbox_dir.string());
    NLP3_TEST_REQUIRE(inbox_result.files_seen >= 2);
    NLP3_TEST_REQUIRE(inbox_result.files_processed >= 1);
    NLP3_TEST_REQUIRE(inbox_result.files_failed >= 1);
    NLP3_TEST_REQUIRE(inbox_result.events_accepted >= 1);
    const auto inbox_after = panel_app.snapshot();
    NLP3_TEST_REQUIRE(inbox_after.total_events >= inbox_before + 1);
    NLP3_TEST_REQUIRE(std::filesystem::exists(inbox_dir / "processed" / "valid-chat.json"));
    NLP3_TEST_REQUIRE(std::filesystem::exists(inbox_dir / "failed" / "invalid.json"));

    {
        std::ofstream demo_output(inbox_dir / "demo-chat.json", std::ios::binary | std::ios::trunc);
        NLP3_TEST_REQUIRE(demo_output.good());
        demo_output << codec.encode_json(nlp3::bridge::TikTokRawEvent{
            nlp3::bridge::TikTokRawEventKind::gift,
            nlp3::bridge::TikTokRawActor{
                "storage-user-03",
                "storage_carol",
                "Storage Carol",
                "",
            },
            nlp3::bridge::TikTokRawMetadata{
                "evt-storage-gift-001",
                "room-storage-001",
                "gift",
                1710000010002,
            },
            "",
            nlp3::bridge::TikTokRawGiftData{
                "gift-storage-rose",
                "Storage Rose",
                1,
                25,
            },
            0,
        });
    }

    const auto demo_before = panel_app.snapshot().total_events;
    NLP3_TEST_REQUIRE(panel_console.execute_line(
        std::string{"bridge demo inbox "} + inbox_dir.string() + " 2 500"));
    const auto demo_after = panel_app.snapshot();
    NLP3_TEST_REQUIRE(demo_after.total_events >= demo_before + 1);
    NLP3_TEST_REQUIRE(std::filesystem::exists(inbox_dir / "processed" / "demo-chat.json"));
    NLP3_TEST_REQUIRE(console_output.str().find("demo inbox processed: cycles=2") != std::string::npos);

    std::filesystem::remove(recording_path);
    std::filesystem::remove_all(inbox_dir);
    std::filesystem::remove(config_path);
    return 0;
}
