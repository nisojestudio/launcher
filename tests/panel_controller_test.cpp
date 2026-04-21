#include <cassert>
#include <memory>

#include "bridge/tiktok_bridge_controller.hpp"
#include "bridge/tiktok_bridge_stub_session.hpp"
#include "bridge/tiktok_event_mapper.hpp"
#include "games/event_counter_game.hpp"
#include "gamesdk/game_registry.hpp"
#include "gamesdk/game_runtime_controller.hpp"
#include "host/host_runtime.hpp"
#include "platform/panel_command.hpp"
#include "platform/panel_config.hpp"
#include "platform/panel_controller.hpp"
#include "tts/mock_tts_backend.hpp"
#include "tts/tts_service.hpp"

namespace {

nlp3::gamesdk::GameRegistry make_event_counter_registry(
    nlp3::gamesdk::GameFactoryRegistry& factory_registry) {
    nlp3::gamesdk::GameRegistry registry{&factory_registry};
    const nlp3::games::EventCounterGame manifest_probe{};
    assert(factory_registry.register_factory(
        std::make_unique<nlp3::games::EventCounterGameFactory>()));
    registry.catalog().add(nlp3::gamesdk::GameCatalogEntry{
        "event-counter",
        "Event Counter",
        "0.1.0",
        "local",
        true,
        true,
        false,
        {},
        {},
        0,
        manifest_probe.manifest(),
    });
    return registry;
}

} // namespace

int main() {
    nlp3::platform::PanelConfig config{};
    config.tts = nlp3::tts::TtsPolicy{
        true,
        true,
        true,
        true,
        3,
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
    nlp3::bridge::TikTokBridgeController bridge_controller{std::move(bridge_session)};
    assert(bridge_controller.start());

    nlp3::host::HostRuntime runtime{
        nullptr,
        &tts_service,
        nullptr,
        nullptr,
        nlp3::bridge::TikTokEventMapper{config.bridge},
        &bridge_controller,
        nlp3::host::HostAutomationEngine{config.automation},
        nlp3::host::HostPeriodicTtsEngine{},
    };

    nlp3::gamesdk::GameFactoryRegistry factory_registry;
    auto game_registry = make_event_counter_registry(factory_registry);
    nlp3::gamesdk::GameRuntimeController game_runtime_controller{&game_registry};

    nlp3::platform::PanelController panel_controller{
        &runtime,
        &bridge_controller,
        &game_runtime_controller,
    };

    const auto tts_result = panel_controller.execute({
        nlp3::platform::PanelCommandKind::tts_enqueue_announcement,
        "Command hello",
    });
    assert(tts_result.ok);
    assert(runtime.queued_tts_messages() == 1);

    const auto activate_result = panel_controller.execute({
        nlp3::platform::PanelCommandKind::game_activate,
        "event-counter",
    });
    assert(activate_result.ok);
    assert(game_runtime_controller.status().state == nlp3::gamesdk::GameRuntimeState::active);
    assert(game_runtime_controller.status().active_game_id == "event-counter");
    assert(runtime.has_active_game());
    assert(runtime.active_game_id() == "event-counter");

    const auto restart_result = panel_controller.execute({
        nlp3::platform::PanelCommandKind::game_restart,
        "",
    });
    assert(restart_result.ok);
    assert(game_runtime_controller.status().state == nlp3::gamesdk::GameRuntimeState::active);
    assert(runtime.has_active_game());
    assert(runtime.active_game_id() == "event-counter");

    const auto stop_result = panel_controller.execute({
        nlp3::platform::PanelCommandKind::bridge_stop,
        "",
    });
    assert(stop_result.ok);
    assert(bridge_controller.state() == nlp3::bridge::TikTokBridgeSessionState::stopped);

    const auto start_result = panel_controller.execute({
        nlp3::platform::PanelCommandKind::bridge_start,
        "",
    });
    assert(start_result.ok);
    assert(bridge_controller.state() == nlp3::bridge::TikTokBridgeSessionState::running);

    const auto deactivate_result = panel_controller.execute({
        nlp3::platform::PanelCommandKind::game_deactivate,
        "",
    });
    assert(deactivate_result.ok);
    assert(game_runtime_controller.status().state == nlp3::gamesdk::GameRuntimeState::idle);
    assert(!runtime.has_active_game());

    return 0;
}
