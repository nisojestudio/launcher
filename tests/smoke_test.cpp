#include <algorithm>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <cstdio>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "bridge/bridge_adapter.hpp"
#include "bridge/tiktok_bridge_controller.hpp"
#include "bridge/tiktok_external_event_codec.hpp"
#include "bridge/tiktok_bridge_external_session.hpp"
#include "bridge/tiktok_bridge_stub_session.hpp"
#include "bridge/tiktok_external_event_source.hpp"
#include "bridge/tiktok_stub_event_source.hpp"
#include "events/host_event.hpp"
#include "gamesdk/game_factory.hpp"
#include "gamesdk/game_module.hpp"
#include "gamesdk/game_registry.hpp"
#include "gamesdk/game_runtime_controller.hpp"
#include "host/host_runtime.hpp"
#include "platform/panel_config.hpp"
#include "platform/panel_activity.hpp"
#include "platform/panel_snapshot_builder.hpp"
#include "platform/panel_view_model_builder.hpp"
#include "platform/runtime.hpp"
#include "test_require.hpp"
#include "tts/mock_tts_backend.hpp"
#include "tts/tts_message.hpp"
#include "tts/tts_service.hpp"

#undef assert
#define assert(EXPR) NLP3_TEST_REQUIRE(EXPR)

namespace {
struct MockGameProbe {
    int activation_count = 0;
    int received_events = 0;
    nlp3::events::HostEventKind last_kind = nlp3::events::HostEventKind::chat_message;
    std::string last_actor;
    std::size_t total_events_seen = 0;
    std::vector<nlp3::events::HostEvent> event_log{};
    std::vector<nlp3::gamesdk::GameInputEvent> game_input_log{};
};

class MockGame final : public nlp3::gamesdk::IGameModule {
public:
    explicit MockGame(MockGameProbe* probe) noexcept
        : probe_(probe) {
    }

    std::string_view game_id() const noexcept override {
        return game_id_;
    }

    void on_activated() override {
        ++probe_->activation_count;
    }

    void on_host_event(
        const nlp3::events::HostEvent& event,
        const nlp3::host::HostSessionSnapshot& session_snapshot) override {
        ++probe_->received_events;
        probe_->last_kind = event.kind;
        probe_->last_actor = event.actor.display_name;
        probe_->total_events_seen = session_snapshot.total_events;
        probe_->event_log.push_back(event);
    }

    void on_game_input_event(
        const nlp3::gamesdk::GameInputEvent& event,
        const nlp3::host::HostSessionSnapshot&) override {
        probe_->game_input_log.push_back(event);
    }

private:
    MockGameProbe* probe_ = nullptr;
    std::string_view game_id_ = "mock-game";
};

class MockGameFactory final : public nlp3::gamesdk::IGameFactory {
public:
    explicit MockGameFactory(MockGameProbe* probe) noexcept
        : probe_(probe) {
    }

    const nlp3::gamesdk::GameManifest& manifest() const noexcept override {
        return manifest_;
    }

    std::unique_ptr<nlp3::gamesdk::IGameModule> create() const override {
        return std::make_unique<MockGame>(probe_);
    }

private:
    nlp3::gamesdk::GameManifest manifest_{
        "mock-game",
        "Mock Game",
        "1.0.0",
        nlp3::gamesdk::GameCompatibility{1, false, "windows"},
    };
    MockGameProbe* probe_ = nullptr;
};

} // namespace

int main() {
    std::puts("smoke cp1");
    std::fflush(stdout);
    nlp3::platform::PanelConfig config{};
    config.tts = nlp3::tts::TtsPolicy{
        true,
        true,
        true,
        true,
        3,
    };
    config.periodic_tts = nlp3::host::HostPeriodicTtsConfig{
        true,
        1000,
        {"Mensaje A", "Mensaje B"},
    };
    nlp3::tts::MockTtsBackend tts_backend;
    nlp3::tts::HostTtsService tts{
        nlp3::tts::TtsConfig{
            true,
            4,
            1,
            64,
            false,
        },
        config.tts,
        tts_backend,
    };
    const nlp3::bridge::TikTokRawEvent codec_probe_event{
        nlp3::bridge::TikTokRawEventKind::chat,
        nlp3::bridge::TikTokRawActor{
            "codec-user-01",
            "codec_alice",
            "Codec Alice",
            "https://cdn.example.com/avatar-codec-alice.png",
        },
        nlp3::bridge::TikTokRawMetadata{
            "evt-codec-chat-001",
            "room-codec-001",
            "comment",
            1710000000004,
        },
        "Codec hello",
        std::nullopt,
        0,
    };
    const nlp3::bridge::TikTokExternalEventCodec external_event_codec{};
    const auto encoded_external_payload = external_event_codec.encode_json(codec_probe_event);
    const auto decoded_external_event = external_event_codec.decode_json(encoded_external_payload);
    std::puts("smoke cp2");
    std::fflush(stdout);
    assert(decoded_external_event.has_value());
    assert(decoded_external_event->kind == nlp3::bridge::TikTokRawEventKind::chat);
    assert(decoded_external_event->actor.display_name == "Codec Alice");
    assert(decoded_external_event->metadata.event_id == "evt-codec-chat-001");
    assert(decoded_external_event->message == "Codec hello");
    nlp3::bridge::TikTokStubEventSource raw_event_source{config.bridge};
    assert(raw_event_source.start());
    assert(raw_event_source.inject_event(nlp3::bridge::TikTokRawEvent{
        nlp3::bridge::TikTokRawEventKind::chat,
        nlp3::bridge::TikTokRawActor{
            "source-user-01",
            "source_alice",
            "Source Alice",
            "https://cdn.example.com/avatar-source-alice.png",
        },
        nlp3::bridge::TikTokRawMetadata{
            "evt-source-chat-001",
            "room-source-001",
            "comment",
            1710000000000,
        },
        "Source hello",
        std::nullopt,
        0,
    }));
    assert(raw_event_source.queued_raw_event_count() == 1);
    const auto raw_source_events = raw_event_source.poll();
    assert(raw_source_events.size() == 1);
    assert(raw_source_events[0].kind == nlp3::bridge::TikTokRawEventKind::chat);
    assert(raw_source_events[0].actor.display_name == "Source Alice");
    assert(raw_source_events[0].metadata.event_id == "evt-source-chat-001");
    assert(raw_event_source.queued_raw_event_count() == 0);
    nlp3::bridge::TikTokExternalEventSource external_event_source{};
    assert(!external_event_source.running());
    assert(external_event_source.start());
    assert(external_event_source.running());
    assert(external_event_source.submit_external_event(nlp3::bridge::TikTokRawEvent{
        nlp3::bridge::TikTokRawEventKind::follow,
        nlp3::bridge::TikTokRawActor{
            "external-user-01",
            "external_bob",
            "External Bob",
            "https://cdn.example.com/avatar-external-bob.png",
        },
        nlp3::bridge::TikTokRawMetadata{
            "evt-external-follow-001",
            "room-external-001",
            "follow",
            1710000000002,
        },
        "",
        std::nullopt,
        0,
    }));
    assert(external_event_source.queued_raw_event_count() == 1);
    const auto external_source_events = external_event_source.poll();
    assert(external_source_events.size() == 1);
    assert(external_source_events[0].kind == nlp3::bridge::TikTokRawEventKind::follow);
    assert(external_source_events[0].actor.display_name == "External Bob");
    assert(external_source_events[0].metadata.event_id == "evt-external-follow-001");
    assert(external_event_source.queued_raw_event_count() == 0);
    external_event_source.stop();
    assert(!external_event_source.running());
    nlp3::bridge::TikTokBridgeExternalSession external_bridge_session{config.bridge};
    std::puts("smoke cp3");
    std::fflush(stdout);
    assert(external_bridge_session.state() == nlp3::bridge::TikTokBridgeSessionState::stopped);
    assert(external_bridge_session.start());
    assert(external_bridge_session.state() == nlp3::bridge::TikTokBridgeSessionState::running);
    assert(external_bridge_session.submit_external_event(nlp3::bridge::TikTokRawEvent{
        nlp3::bridge::TikTokRawEventKind::gift,
        nlp3::bridge::TikTokRawActor{
            "external-session-user-01",
            "external_session_bob",
            "External Session Bob",
            "https://cdn.example.com/avatar-external-session-bob.png",
        },
        nlp3::bridge::TikTokRawMetadata{
            "evt-external-session-gift-001",
            "room-external-session-001",
            "gift",
            1710000000003,
        },
        "",
        nlp3::bridge::TikTokRawGiftData{
            "gift-external-rose",
            "External Rose",
            1,
            25,
        },
        0,
    }));
    assert(external_bridge_session.queued_raw_event_count() == 1);
    const auto external_session_events = external_bridge_session.poll();
    assert(external_session_events.size() == 1);
    assert(external_session_events[0].kind == nlp3::bridge::TikTokRawEventKind::gift);
    assert(external_session_events[0].gift.has_value());
    assert(external_session_events[0].gift->gift_name == "External Rose");
    assert(external_bridge_session.queued_raw_event_count() == 0);
    assert(external_bridge_session.metrics().raw_events_received == 1);
    assert(external_bridge_session.metrics().raw_events_emitted == 1);
    assert(external_bridge_session.metrics().last_event_timestamp_ms == 1710000000003);
    external_bridge_session.stop();
    assert(external_bridge_session.state() == nlp3::bridge::TikTokBridgeSessionState::stopped);
    auto bridge_session = std::make_unique<nlp3::bridge::TikTokBridgeStubSession>(config.bridge);
    auto* bridge_session_ptr = bridge_session.get();
    assert(bridge_session_ptr->state() == nlp3::bridge::TikTokBridgeSessionState::stopped);
    nlp3::bridge::TikTokBridgeController bridge_controller{std::move(bridge_session)};
    std::puts("smoke cp4");
    std::fflush(stdout);
    assert(bridge_controller.available());
    assert(bridge_controller.state() == nlp3::bridge::TikTokBridgeSessionState::stopped);
    assert(bridge_controller.start());
    assert(bridge_controller.state() == nlp3::bridge::TikTokBridgeSessionState::running);
    MockGameProbe compatible_probe;
    nlp3::platform::PanelActivityLog activity_log{};

    nlp3::host::HostRuntime runtime{
        nullptr,
        &tts,
        nullptr,
        nullptr,
        nlp3::bridge::TikTokEventMapper{config.bridge},
        &bridge_controller,
        nlp3::host::HostAutomationEngine{config.automation},
        nlp3::host::HostPeriodicTtsEngine{config.periodic_tts},
        &activity_log,
    };
    std::puts("smoke cp5");
    std::fflush(stdout);
    assert(!runtime.has_active_game());

    const auto initial_bridge_status = runtime.bridge_status();
    assert(initial_bridge_status.integrated);
    assert(initial_bridge_status.state == nlp3::bridge::TikTokBridgeSessionState::running);
    assert(initial_bridge_status.metrics.raw_events_received == 0);
    assert(initial_bridge_status.metrics.raw_events_emitted == 0);

    nlp3::platform::PanelViewModelBuilder view_model_builder;

    nlp3::gamesdk::GameFactoryRegistry smoke_factory_registry;
    nlp3::gamesdk::GameRegistry smoke_game_registry{&smoke_factory_registry};
    assert(smoke_factory_registry.register_factory(
        std::make_unique<MockGameFactory>(&compatible_probe)));
    smoke_game_registry.catalog().add(nlp3::gamesdk::GameCatalogEntry{
        "mock-game",
        "Mock Game",
        "1.0.0",
        "local",
        true,
        true,
        false,
        {},
        {},
        0,
        {},
    });
    nlp3::gamesdk::GameRuntimeController game_runtime_controller{&smoke_game_registry};
    assert(game_runtime_controller.activate("mock-game"));
    runtime.attach_game(game_runtime_controller.active_game());
    assert(runtime.has_active_game());
    assert(runtime.active_game_id() == "mock-game");
    assert(compatible_probe.activation_count == 1);

    const nlp3::events::HostEvent direct_event{
        nlp3::events::HostEventKind::follow,
        nlp3::events::HostActor{"user-00", "Starter", ""},
        nlp3::events::HostEventMetadata{"smoke", "follow"},
        "",
        std::nullopt,
        1,
    };

    runtime.receive_event(direct_event);
    assert(runtime.snapshot().total_events == 1);
    assert(runtime.snapshot().follows == 1);
    assert(compatible_probe.received_events == 1);
    assert(compatible_probe.last_kind == nlp3::events::HostEventKind::follow);
    assert(compatible_probe.last_actor == "Starter");
    assert(runtime.queued_tts_messages() == 0);

    assert(bridge_session_ptr->inject_event(nlp3::bridge::TikTokRawEvent{
        nlp3::bridge::TikTokRawEventKind::chat,
        nlp3::bridge::TikTokRawActor{
            "user-00",
            "tiny_user",
            "",
            "https://cdn.example.com/avatar-tiny.png",
        },
        nlp3::bridge::TikTokRawMetadata{
            "evt-short-chat",
            "room-001",
            "comment",
            1710000000001,
        },
        "Hi",
        std::nullopt,
        0,
    }));
    assert(runtime.tick_bridge() == 1);
    assert(runtime.snapshot().total_events == 2);
    assert(runtime.snapshot().chat_messages == 1);
    assert(compatible_probe.received_events == 2);
    assert(compatible_probe.event_log.size() == 2);
    assert(compatible_probe.event_log[1].kind == nlp3::events::HostEventKind::chat_message);
    assert(compatible_probe.event_log[1].actor.avatar_url == "https://cdn.example.com/avatar-tiny.png");
    assert(compatible_probe.event_log[1].metadata.source_event_id == "evt-short-chat");
    assert(compatible_probe.event_log[1].metadata.source_room_id == "room-001");
    assert(compatible_probe.event_log[1].metadata.source_timestamp_ms == 1710000000001);
    assert(runtime.queued_tts_messages() == 0);
    assert(tts_backend.spoken_messages().empty());

    assert(bridge_session_ptr->inject_event(nlp3::bridge::TikTokRawEvent{
        nlp3::bridge::TikTokRawEventKind::chat,
        nlp3::bridge::TikTokRawActor{
            "user-01",
            "alice",
            "Alice",
            "https://cdn.example.com/avatar-alice.png",
        },
        nlp3::bridge::TikTokRawMetadata{
            "evt-chat-001",
            "room-001",
            "comment",
            1710000001000,
        },
        "Hello host",
        std::nullopt,
        0,
    }));
    assert(runtime.tick_bridge() == 1);
    assert(runtime.snapshot().total_events == 3);
    assert(runtime.snapshot().chat_messages == 2);
    assert(runtime.snapshot().last_actor.has_value());
    assert(runtime.snapshot().last_actor->display_name == "Alice");
    assert(compatible_probe.received_events == 3);
    assert(compatible_probe.total_events_seen == 3);
    assert(compatible_probe.event_log.size() == 3);
    assert(compatible_probe.game_input_log.size() == 3);
    assert(compatible_probe.event_log[2].kind == nlp3::events::HostEventKind::chat_message);
    assert(compatible_probe.event_log[2].actor.avatar_url == "https://cdn.example.com/avatar-alice.png");
    assert(compatible_probe.event_log[2].metadata.source == "tiktok-stub");
    assert(compatible_probe.event_log[2].metadata.source_event_type == "chat");
    assert(compatible_probe.event_log[2].metadata.source_event_id == "evt-chat-001");
    assert(compatible_probe.event_log[2].metadata.source_room_id == "room-001");
    assert(compatible_probe.event_log[2].metadata.source_timestamp_ms == 1710000001000);
    assert(compatible_probe.game_input_log[2].kind == nlp3::gamesdk::GameInputEventKind::chat_message);
    assert(compatible_probe.game_input_log[2].text == "Hello host");
    assert(compatible_probe.game_input_log[2].actor.display_name == "Alice");
    assert(compatible_probe.game_input_log[2].actor.avatar_url == "https://cdn.example.com/avatar-alice.png");
    assert(compatible_probe.game_input_log[2].metadata.source_event_id == "evt-chat-001");
    assert(compatible_probe.game_input_log[2].metadata.source_room_id == "room-001");
    assert(compatible_probe.game_input_log[2].metadata.source_timestamp_ms == 1710000001000);
    assert(runtime.queued_tts_messages() == 1);

    const auto running_bridge_status = runtime.bridge_status();
    assert(running_bridge_status.state == nlp3::bridge::TikTokBridgeSessionState::running);
    assert(running_bridge_status.metrics.raw_events_received == 2);
    assert(running_bridge_status.metrics.raw_events_emitted == 2);
    assert(running_bridge_status.metrics.poll_calls >= 2);
    assert(running_bridge_status.metrics.last_event_timestamp_ms == 1710000001000);

    assert(runtime.queue_tts_announcement("Panel ready"));
    assert(runtime.queued_tts_messages() == 2);
    assert(!runtime.queue_tts_announcement(" "));
    assert(runtime.queued_tts_messages() == 2);

    assert(runtime.flush_tts() == 1);
    assert(runtime.queued_tts_messages() == 1);
    assert(tts_backend.spoken_messages().size() == 1);
    assert(tts_backend.spoken_messages()[0].trigger == nlp3::tts::TtsTrigger::manual_message);
    assert(tts_backend.spoken_messages()[0].text == "Panel ready");

    assert(runtime.flush_tts(4) == 1);
    assert(runtime.queued_tts_messages() == 0);
    assert(tts_backend.spoken_messages().size() == 2);
    assert(tts_backend.spoken_messages()[1].trigger == nlp3::tts::TtsTrigger::chat_event);
    assert(tts_backend.spoken_messages()[1].text == "Alice: Hello host");

    assert(!runtime.tick_periodic_tts(500));
    assert(runtime.queued_tts_messages() == 0);
    assert(runtime.tick_periodic_tts(1000));
    assert(runtime.queued_tts_messages() == 1);
    assert(runtime.flush_tts() == 1);
    assert(runtime.queued_tts_messages() == 0);
    assert(tts_backend.spoken_messages().size() == 3);
    assert(tts_backend.spoken_messages()[2].text == "Mensaje A");
    assert(!runtime.tick_periodic_tts(1500));
    assert(runtime.queued_tts_messages() == 0);
    assert(runtime.tick_periodic_tts(2000));
    assert(runtime.queued_tts_messages() == 1);
    assert(runtime.flush_tts() == 1);
    assert(runtime.queued_tts_messages() == 0);
    assert(tts_backend.spoken_messages().size() == 4);
    assert(tts_backend.spoken_messages()[3].text == "Mensaje B");

    assert(bridge_session_ptr->inject_event(nlp3::bridge::TikTokRawEvent{
        nlp3::bridge::TikTokRawEventKind::gift,
        nlp3::bridge::TikTokRawActor{
            "user-02",
            "bob",
            "Bob",
            "https://cdn.example.com/avatar-bob.png",
        },
        nlp3::bridge::TikTokRawMetadata{
            "evt-gift-001",
            "room-001",
            "gift",
            1710000002000,
        },
        "",
        nlp3::bridge::TikTokRawGiftData{
            "gift-rose",
            "Rose",
            2,
            150,
        },
        0,
    }));
    assert(bridge_session_ptr->inject_event(nlp3::bridge::TikTokRawEvent{
        nlp3::bridge::TikTokRawEventKind::follow,
        nlp3::bridge::TikTokRawActor{
            "user-03",
            "carol",
            "Carol",
            "https://cdn.example.com/avatar-carol.png",
        },
        nlp3::bridge::TikTokRawMetadata{
            "evt-follow-001",
            "room-001",
            "follow",
            1710000003000,
        },
        "",
        std::nullopt,
        0,
    }));
    assert(runtime.tick_bridge() == 2);
    assert(runtime.snapshot().total_events == 5);
    assert(runtime.snapshot().gifts == 1);
    assert(runtime.snapshot().follows == 2);
    assert(runtime.snapshot().last_gift.has_value());
    assert(runtime.snapshot().last_gift->gift_name == "Rose");
    assert(runtime.snapshot().last_gift->quantity == 2);
    assert(compatible_probe.received_events == 5);
    assert(compatible_probe.event_log.size() == 5);
    assert(compatible_probe.game_input_log.size() == 5);
    assert(compatible_probe.event_log[3].kind == nlp3::events::HostEventKind::gift);
    assert(compatible_probe.event_log[3].gift.has_value());
    assert(compatible_probe.event_log[3].gift->gift_name == "Rose");
    assert(compatible_probe.event_log[3].gift->quantity == 2);
    assert(compatible_probe.event_log[3].actor.avatar_url == "https://cdn.example.com/avatar-bob.png");
    assert(compatible_probe.event_log[3].metadata.source_event_id == "evt-gift-001");
    assert(compatible_probe.game_input_log[3].kind == nlp3::gamesdk::GameInputEventKind::gift);
    assert(compatible_probe.game_input_log[3].gift.has_value());
    assert(compatible_probe.game_input_log[3].gift->gift_name == "Rose");
    assert(compatible_probe.game_input_log[3].gift->quantity == 2);
    assert(compatible_probe.game_input_log[3].gift->diamond_count == 150);
    assert(compatible_probe.game_input_log[3].actor.avatar_url == "https://cdn.example.com/avatar-bob.png");
    assert(compatible_probe.game_input_log[3].metadata.source_event_id == "evt-gift-001");
    assert(compatible_probe.event_log[4].kind == nlp3::events::HostEventKind::follow);
    assert(compatible_probe.event_log[4].actor.avatar_url == "https://cdn.example.com/avatar-carol.png");
    assert(compatible_probe.event_log[4].metadata.source_event_id == "evt-follow-001");
    assert(compatible_probe.game_input_log[4].kind == nlp3::gamesdk::GameInputEventKind::follow);
    assert(compatible_probe.game_input_log[4].actor.avatar_url == "https://cdn.example.com/avatar-carol.png");
    assert(compatible_probe.game_input_log[4].metadata.source_event_id == "evt-follow-001");
    assert(runtime.queued_tts_messages() == 1);
    assert(compatible_probe.last_kind == nlp3::events::HostEventKind::follow);
    assert(compatible_probe.last_actor == "Carol");
    const auto runtime_panel_snapshot =
        nlp3::platform::build_panel_snapshot(
            config,
            runtime,
            game_runtime_controller,
            &activity_log,
            nullptr,
            {},
            {},
            {},
            {});
    std::puts("smoke cp6");
    std::fflush(stdout);
    assert(runtime_panel_snapshot.panel_name == "Nisoje Studio");
    assert(runtime_panel_snapshot.total_events == 5);
    assert(runtime_panel_snapshot.bridge.integrated);
    assert(runtime_panel_snapshot.bridge.state == nlp3::bridge::TikTokBridgeSessionState::running);
    assert(runtime_panel_snapshot.tts.available);
    assert(runtime_panel_snapshot.tts.queued_messages == 1);
    assert(runtime_panel_snapshot.game.has_active_game);
    assert(runtime_panel_snapshot.game.active_game_id == "mock-game");
    assert(runtime_panel_snapshot.game.runtime_state == nlp3::gamesdk::GameRuntimeState::active);
    assert(runtime_panel_snapshot.game.last_error.empty());
    assert(!runtime_panel_snapshot.recent_activity.empty());
    assert(runtime_panel_snapshot.recent_activity.size() <= activity_log.capacity());
    assert(std::find_if(
        runtime_panel_snapshot.recent_activity.begin(),
        runtime_panel_snapshot.recent_activity.end(),
        [](const nlp3::platform::PanelActivityEntry& entry) {
            return entry.kind == nlp3::platform::PanelActivityKind::host_event;
        }) != runtime_panel_snapshot.recent_activity.end());
    assert(std::find_if(
        runtime_panel_snapshot.recent_activity.begin(),
        runtime_panel_snapshot.recent_activity.end(),
        [](const nlp3::platform::PanelActivityEntry& entry) {
            return entry.kind == nlp3::platform::PanelActivityKind::tts_chat_enqueued
                || entry.kind == nlp3::platform::PanelActivityKind::tts_automation_enqueued
                || entry.kind == nlp3::platform::PanelActivityKind::tts_periodic_enqueued;
        }) != runtime_panel_snapshot.recent_activity.end());
    const auto runtime_view_model = view_model_builder.build(runtime_panel_snapshot);
    std::puts("smoke cp7");
    std::fflush(stdout);
    assert(!runtime_view_model.recent_activity_lines.empty());
    assert(std::find_if(
        runtime_view_model.recent_activity_lines.begin(),
        runtime_view_model.recent_activity_lines.end(),
        [](const std::string& line) {
            return line.find('|') != std::string::npos;
        }) != runtime_view_model.recent_activity_lines.end());

    assert(runtime.flush_tts() == 1);
    assert(runtime.queued_tts_messages() == 0);
    assert(tts_backend.spoken_messages().size() == 5);
    assert(tts_backend.spoken_messages()[4].trigger == nlp3::tts::TtsTrigger::scheduled_message);
    assert(tts_backend.spoken_messages()[4].text.find("Bob") != std::string::npos);
    assert(tts_backend.spoken_messages()[4].text.find("regalo") != std::string::npos);

    auto disabled_share_config = config;
    std::puts("smoke cp8");
    std::fflush(stdout);
    disabled_share_config.bridge.emit_share_events = false;
    nlp3::bridge::TikTokBridgeStubSession disabled_share_bridge{disabled_share_config.bridge};
    assert(disabled_share_bridge.start());
    nlp3::host::HostRuntime disabled_share_runtime{
        nullptr,
        nullptr,
        nullptr,
        &disabled_share_bridge,
        nlp3::bridge::TikTokEventMapper{disabled_share_config.bridge},
        nullptr,
        nlp3::host::HostAutomationEngine{disabled_share_config.automation},
        nlp3::host::HostPeriodicTtsEngine{disabled_share_config.periodic_tts},
    };
    assert(disabled_share_bridge.inject_event(nlp3::bridge::TikTokRawEvent{
        nlp3::bridge::TikTokRawEventKind::share,
        nlp3::bridge::TikTokRawActor{},
        nlp3::bridge::TikTokRawMetadata{
            "evt-share-001",
            "room-001",
            "share",
            1710000004000,
        },
        "",
        std::nullopt,
        0,
    }));
    assert(disabled_share_runtime.tick_bridge() == 0);
    assert(disabled_share_runtime.snapshot().total_events == 0);
    const auto disabled_share_status = disabled_share_runtime.bridge_status();
    assert(disabled_share_status.integrated);
    assert(disabled_share_status.metrics.raw_events_received == 1);
    assert(disabled_share_status.metrics.raw_events_emitted == 1);

    auto viewer_join_config = config;
    std::puts("smoke cp9");
    std::fflush(stdout);
    viewer_join_config.bridge.emit_viewer_join_events = true;
    auto viewer_join_bridge_session =
        std::make_unique<nlp3::bridge::TikTokBridgeStubSession>(viewer_join_config.bridge);
    auto* viewer_join_bridge_session_ptr = viewer_join_bridge_session.get();
    nlp3::bridge::TikTokBridgeController viewer_join_bridge_controller{
        std::move(viewer_join_bridge_session)};
    assert(viewer_join_bridge_controller.start());
    MockGameProbe viewer_join_probe;
    nlp3::host::HostRuntime viewer_join_runtime{
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nlp3::bridge::TikTokEventMapper{viewer_join_config.bridge},
        &viewer_join_bridge_controller,
        nlp3::host::HostAutomationEngine{viewer_join_config.automation},
        nlp3::host::HostPeriodicTtsEngine{viewer_join_config.periodic_tts},
    };
    viewer_join_runtime.activate_game(std::make_unique<MockGame>(&viewer_join_probe));
    assert(viewer_join_probe.activation_count == 1);
    assert(viewer_join_bridge_session_ptr->inject_event(nlp3::bridge::TikTokRawEvent{
        nlp3::bridge::TikTokRawEventKind::viewer_join,
        nlp3::bridge::TikTokRawActor{
            "join-user-01",
            "join_alice",
            "Join Alice",
            "https://cdn.example.com/avatar-join-alice.png",
        },
        nlp3::bridge::TikTokRawMetadata{
            "evt-join-001",
            "room-join-001",
            "join",
            1710000006000,
        },
        "",
        std::nullopt,
        0,
    }));
    assert(viewer_join_runtime.tick_bridge() == 1);
    assert(viewer_join_runtime.snapshot().total_events == 1);
    assert(viewer_join_runtime.snapshot().viewer_joins == 1);
    assert(viewer_join_probe.received_events == 1);
    assert(viewer_join_probe.event_log.size() == 1);
    assert(viewer_join_probe.game_input_log.size() == 1);
    assert(viewer_join_probe.event_log[0].kind == nlp3::events::HostEventKind::viewer_join);
    assert(viewer_join_probe.event_log[0].actor.display_name == "Join Alice");
    assert(viewer_join_probe.event_log[0].metadata.source_event_id == "evt-join-001");
    assert(viewer_join_probe.game_input_log[0].kind == nlp3::gamesdk::GameInputEventKind::viewer_join);
    assert(viewer_join_probe.game_input_log[0].actor.avatar_url == "https://cdn.example.com/avatar-join-alice.png");
    assert(viewer_join_probe.game_input_log[0].metadata.source_event_type == "viewer_join");
    assert(viewer_join_runtime.queued_tts_messages() == 0);

    std::puts("smoke cp10");
    std::fflush(stdout);
    return 0;
}
