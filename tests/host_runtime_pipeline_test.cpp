#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "bridge/tiktok_bridge_controller.hpp"
#include "bridge/tiktok_bridge_stub_session.hpp"
#include "events/host_event.hpp"
#include "gamesdk/game_input_event.hpp"
#include "gamesdk/game_module.hpp"
#include "host/host_runtime.hpp"
#include "platform/panel_activity.hpp"
#include "platform/panel_config.hpp"
#include "tts/mock_tts_backend.hpp"
#include "tts/tts_message.hpp"
#include "tts/tts_service.hpp"

namespace {

struct MockGameProbe {
    int activation_count = 0;
    std::vector<nlp3::events::HostEvent> event_log{};
    std::vector<nlp3::gamesdk::GameInputEvent> game_input_log{};
};

class MockGame final : public nlp3::gamesdk::IGameModule {
public:
    explicit MockGame(MockGameProbe* probe) noexcept
        : probe_(probe) {
    }

    std::string_view game_id() const noexcept override {
        return "mock-runtime-game";
    }

    void on_activated() override {
        ++probe_->activation_count;
    }

    void on_host_event(
        const nlp3::events::HostEvent& event,
        const nlp3::host::HostSessionSnapshot& snapshot) override {
        (void)snapshot;
        probe_->event_log.push_back(event);
    }

    void on_game_input_event(
        const nlp3::gamesdk::GameInputEvent& event,
        const nlp3::host::HostSessionSnapshot& snapshot) override {
        (void)snapshot;
        probe_->game_input_log.push_back(event);
    }

private:
    MockGameProbe* probe_ = nullptr;
};

} // namespace

int main() {
    nlp3::platform::PanelConfig config{};
    config.bridge.emit_viewer_join_events = true;
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
        {"Tick message"},
    };

    nlp3::tts::MockTtsBackend tts_backend;
    nlp3::tts::HostTtsService tts_service{
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

    auto bridge_session = std::make_unique<nlp3::bridge::TikTokBridgeStubSession>(config.bridge);
    auto* bridge_session_ptr = bridge_session.get();
    nlp3::bridge::TikTokBridgeController bridge_controller{std::move(bridge_session)};
    assert(bridge_controller.start());

    nlp3::platform::PanelActivityLog activity_log{};
    nlp3::host::HostRuntime runtime{
        nullptr,
        &tts_service,
        nullptr,
        nullptr,
        nlp3::bridge::TikTokEventMapper{config.bridge},
        &bridge_controller,
        nlp3::host::HostAutomationEngine{config.automation},
        nlp3::host::HostPeriodicTtsEngine{config.periodic_tts},
        &activity_log,
    };

    MockGameProbe probe;
    runtime.activate_game(std::make_unique<MockGame>(&probe));
    assert(probe.activation_count == 1);
    assert(runtime.has_active_game());
    assert(runtime.active_game_id() == "mock-runtime-game");

    assert(bridge_session_ptr->inject_event(nlp3::bridge::TikTokRawEvent{
        nlp3::bridge::TikTokRawEventKind::chat,
        nlp3::bridge::TikTokRawActor{
            "pipeline-user-01",
            "pipeline_alice",
            "Pipeline Alice",
            "https://cdn.example.com/avatar-pipeline-alice.png",
        },
        nlp3::bridge::TikTokRawMetadata{
            "evt-pipeline-chat-001",
            "room-pipeline-001",
            "comment",
            1710000020000,
        },
        "Hello runtime",
        std::nullopt,
        0,
    }));
    assert(runtime.tick_bridge() == 1);
    assert(runtime.snapshot().total_events == 1);
    assert(runtime.snapshot().chat_messages == 1);
    assert(runtime.queued_tts_messages() == 1);
    assert(probe.event_log.size() == 1);
    assert(probe.game_input_log.size() == 1);
    assert(probe.game_input_log[0].kind == nlp3::gamesdk::GameInputEventKind::chat_message);
    assert(probe.game_input_log[0].actor.avatar_url == "https://cdn.example.com/avatar-pipeline-alice.png");

    assert(bridge_session_ptr->inject_event(nlp3::bridge::TikTokRawEvent{
        nlp3::bridge::TikTokRawEventKind::gift,
        nlp3::bridge::TikTokRawActor{
            "pipeline-user-02",
            "pipeline_bob",
            "Pipeline Bob",
            "https://cdn.example.com/avatar-pipeline-bob.png",
        },
        nlp3::bridge::TikTokRawMetadata{
            "evt-pipeline-gift-001",
            "room-pipeline-001",
            "gift",
            1710000020001,
        },
        "",
        nlp3::bridge::TikTokRawGiftData{
            "gift-pipeline-rose",
            "Pipeline Rose",
            2,
            150,
        },
        0,
    }));
    assert(runtime.tick_bridge() == 1);
    assert(runtime.snapshot().total_events == 2);
    assert(runtime.snapshot().gifts == 1);
    assert(runtime.queued_tts_messages() == 2);
    assert(probe.event_log.size() == 2);
    assert(probe.game_input_log.size() == 2);
    assert(probe.game_input_log[1].kind == nlp3::gamesdk::GameInputEventKind::gift);
    assert(probe.game_input_log[1].gift.has_value());
    assert(probe.game_input_log[1].gift->gift_name == "Pipeline Rose");

    assert(runtime.flush_tts() == 1);
    assert(runtime.flush_tts() == 1);
    assert(tts_backend.spoken_messages().size() == 2);
    assert(tts_backend.spoken_messages()[0].category == nlp3::tts::TtsMessageCategory::gift);
    assert(tts_backend.spoken_messages()[0].text.find("Pipeline Bob") != std::string::npos);
    assert(tts_backend.spoken_messages()[1].trigger == nlp3::tts::TtsTrigger::chat_event);
    assert(tts_backend.spoken_messages()[1].text == "Pipeline Alice: Hello runtime");

    assert(!runtime.tick_periodic_tts(500));
    assert(runtime.tick_periodic_tts(1000));
    assert(runtime.queued_tts_messages() == 1);
    assert(runtime.flush_tts() == 1);
    assert(tts_backend.spoken_messages().size() == 3);
    assert(tts_backend.spoken_messages()[2].text == "Tick message");

    assert(bridge_session_ptr->inject_event(nlp3::bridge::TikTokRawEvent{
        nlp3::bridge::TikTokRawEventKind::viewer_join,
        nlp3::bridge::TikTokRawActor{
            "pipeline-user-03",
            "pipeline_carol",
            "Pipeline Carol",
            "https://cdn.example.com/avatar-pipeline-carol.png",
        },
        nlp3::bridge::TikTokRawMetadata{
            "evt-pipeline-join-001",
            "room-pipeline-001",
            "join",
            1710000020002,
        },
        "",
        std::nullopt,
        0,
    }));
    assert(runtime.tick_bridge() == 1);
    assert(runtime.snapshot().total_events == 3);
    assert(runtime.snapshot().viewer_joins == 1);
    assert(probe.event_log.size() == 3);
    assert(probe.game_input_log.size() == 3);
    assert(probe.event_log.back().kind == nlp3::events::HostEventKind::viewer_join);
    assert(probe.game_input_log.back().kind == nlp3::gamesdk::GameInputEventKind::viewer_join);
    assert(runtime.queued_tts_messages() == 0);

    const auto bridge_status = runtime.bridge_status();
    assert(bridge_status.integrated);
    assert(bridge_status.metrics.raw_events_received == 3);
    assert(bridge_status.metrics.raw_events_emitted == 3);
    assert(bridge_status.metrics.last_event_timestamp_ms == 1710000020002);

    const auto activity_entries = activity_log.entries();
    assert(!activity_entries.empty());
    assert(std::find_if(
        activity_entries.begin(),
        activity_entries.end(),
        [](const nlp3::platform::PanelActivityEntry& entry) {
            return entry.kind == nlp3::platform::PanelActivityKind::host_event;
        }) != activity_entries.end());
    assert(std::find_if(
        activity_entries.begin(),
        activity_entries.end(),
        [](const nlp3::platform::PanelActivityEntry& entry) {
            return entry.kind == nlp3::platform::PanelActivityKind::tts_chat_enqueued
                || entry.kind == nlp3::platform::PanelActivityKind::tts_automation_enqueued
                || entry.kind == nlp3::platform::PanelActivityKind::tts_periodic_enqueued;
        }) != activity_entries.end());

    nlp3::tts::MockTtsBackend like_tts_backend;
    nlp3::tts::HostTtsService like_tts_service{
        nlp3::tts::TtsConfig{
            true,
            4,
            1,
            64,
            false,
        },
        config.tts,
        like_tts_backend,
    };
    auto like_automation_config = config.automation;
    like_automation_config.enable_like_thanks_tts = true;

    nlp3::platform::PanelActivityLog like_activity_log{};
    nlp3::host::HostRuntime like_runtime{
        nullptr,
        &like_tts_service,
        nullptr,
        nullptr,
        nlp3::bridge::TikTokEventMapper{config.bridge},
        nullptr,
        nlp3::host::HostAutomationEngine{like_automation_config},
        nlp3::host::HostPeriodicTtsEngine{},
        &like_activity_log,
    };

    const auto make_like_event = [](std::string_view event_id, std::int64_t source_timestamp_ms, int magnitude) {
        return nlp3::events::HostEvent{
            nlp3::events::HostEventKind::like,
            nlp3::events::HostActor{
                "like-user-01",
                "Like Burst",
                "https://cdn.example.com/avatar-like-burst.png",
            },
            nlp3::events::HostEventMetadata{
                "tiktok-external",
                "like",
                std::string(event_id),
                "room-like-burst",
                source_timestamp_ms,
            },
            {},
            std::nullopt,
            magnitude,
            0,
            {},
        };
    };

    like_runtime.receive_event(make_like_event("evt-like-burst-001", 1710000030000, 10), 1000);
    like_runtime.receive_event(make_like_event("evt-like-burst-002", 1710000031000, 10), 3000);
    assert(like_runtime.snapshot().likes == 0);
    assert(!like_runtime.tick_like_batches(5999));
    assert(like_runtime.snapshot().likes == 0);
    assert(like_runtime.tick_like_batches(6000));
    assert(like_runtime.snapshot().likes == 20);
    assert(like_runtime.flush_tts() == 1);
    assert(like_tts_backend.spoken_messages().size() == 1);
    assert(like_tts_backend.spoken_messages().back().text.find("20 likes") != std::string::npos);

    like_runtime.receive_event(make_like_event("evt-like-burst-003", 1710000032000, 10), 7000);
    like_runtime.receive_event(make_like_event("evt-like-burst-004", 1710000033000, 10), 8000);
    assert(like_runtime.snapshot().likes == 20);
    like_runtime.receive_event(make_like_event("evt-like-burst-005", 1710000034000, 10), 8500);
    assert(like_runtime.snapshot().likes == 50);
    assert(like_runtime.flush_tts() == 1);
    assert(like_tts_backend.spoken_messages().size() == 2);
    assert(like_tts_backend.spoken_messages().back().text.find("30 likes") != std::string::npos);

    const auto like_activity_entries = like_activity_log.entries();
    assert(std::find_if(
        like_activity_entries.begin(),
        like_activity_entries.end(),
        [](const nlp3::platform::PanelActivityEntry& entry) {
            return entry.kind == nlp3::platform::PanelActivityKind::host_event
                && entry.label == "like"
                && entry.details == "30 likes";
        }) != like_activity_entries.end());

    nlp3::tts::MockTtsBackend cleared_tts_backend;
    nlp3::tts::HostTtsService cleared_tts_service{
        nlp3::tts::TtsConfig{
            true,
            4,
            1,
            64,
            false,
        },
        config.tts,
        cleared_tts_backend,
    };
    nlp3::host::HostRuntime cleared_runtime{
        nullptr,
        &cleared_tts_service,
        nullptr,
        nullptr,
        nlp3::bridge::TikTokEventMapper{config.bridge},
        nullptr,
        nlp3::host::HostAutomationEngine{config.automation},
        nlp3::host::HostPeriodicTtsEngine{config.periodic_tts},
        nullptr,
    };

    assert(cleared_runtime.queue_tts_announcement("Mensaje pendiente"));
    cleared_runtime.receive_event(make_like_event("evt-like-pending-001", 1710000040000, 10), 1000);
    assert(cleared_runtime.queued_tts_messages() == 1);
    cleared_runtime.clear_pending_live_backlog();
    assert(cleared_runtime.queued_tts_messages() == 0);
    assert(!cleared_runtime.tick_like_batches(7000));
    assert(cleared_runtime.flush_tts() == 0);
    assert(cleared_tts_backend.spoken_messages().empty());

    return 0;
}
