#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include "bridge/tiktok_bridge_controller.hpp"
#include "bridge/tiktok_bridge_stub_session.hpp"
#include "bridge/tiktok_event_mapper.hpp"
#include "games/event_counter_game.hpp"
#include "gamesdk/game_registry.hpp"
#include "gamesdk/game_runtime_controller.hpp"
#include "host/host_runtime.hpp"
#include "platform/panel_config.hpp"
#include "test_support.hpp"

namespace {

[[noreturn]] void fail_check(int line, const char* expression) {
    std::fprintf(stderr, "event_counter_runtime_test failure at line %d: %s\n", line, expression);
    std::fflush(stderr);
    std::abort();
}

#define REQUIRE(EXPR) \
    do { \
        if (!(EXPR)) { \
            fail_check(__LINE__, #EXPR); \
        } \
    } while (false)

nlp3::gamesdk::GameRegistry make_event_counter_registry(
    nlp3::gamesdk::GameFactoryRegistry& factory_registry) {
    nlp3::gamesdk::GameRegistry registry{&factory_registry};
    const nlp3::games::EventCounterGame manifest_probe{};
    REQUIRE(factory_registry.register_factory(
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

    nlp3::gamesdk::GameFactoryRegistry factory_registry;
    auto game_registry = make_event_counter_registry(factory_registry);
    const auto* catalog_entry = game_registry.catalog().find_by_id("event-counter");
    REQUIRE(catalog_entry != nullptr);
    REQUIRE(catalog_entry->manifest.game_id == "event-counter");
    REQUIRE(catalog_entry->manifest.capabilities.uses_avatar_data);

    auto bridge_session = std::make_unique<nlp3::bridge::TikTokBridgeStubSession>(config.bridge);
    auto* bridge_session_ptr = bridge_session.get();
    nlp3::bridge::TikTokBridgeController bridge_controller{std::move(bridge_session)};
    REQUIRE(bridge_controller.start());

    nlp3::host::HostRuntime runtime{
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nlp3::bridge::TikTokEventMapper{config.bridge},
        &bridge_controller,
        nlp3::host::HostAutomationEngine{config.automation},
        nlp3::host::HostPeriodicTtsEngine{},
    };

    nlp3::gamesdk::GameRuntimeController game_runtime_controller{&game_registry};
    REQUIRE(game_runtime_controller.activate("event-counter"));
    runtime.attach_game(game_runtime_controller.active_game());

    auto* event_counter_game = dynamic_cast<nlp3::games::EventCounterGame*>(
        game_runtime_controller.active_game());
    REQUIRE(event_counter_game != nullptr);

    REQUIRE(bridge_session_ptr->inject_event(nlp3::testsupport::make_chat_event(
        "arena-user-01",
        "arena_alice",
        "Arena Alice",
        "evt-arena-chat-001",
        "room-arena-001",
        "Arena hello",
        1710000010000,
        "https://cdn.example.com/avatar-arena-alice.png")));
    REQUIRE(runtime.tick_bridge() == 1);

    REQUIRE(bridge_session_ptr->inject_event(nlp3::testsupport::make_gift_event(
        "arena-user-02",
        "arena_bob",
        "Arena Bob",
        "evt-arena-gift-001",
        "room-arena-001",
        "gift-star",
        "Star",
        3,
        200,
        1710000011000,
        "https://cdn.example.com/avatar-arena-bob.png")));
    REQUIRE(bridge_session_ptr->inject_event(nlp3::testsupport::make_follow_event(
        "arena-user-03",
        "arena_carol",
        "Arena Carol",
        "evt-arena-follow-001",
        "room-arena-001",
        1710000012000,
        "https://cdn.example.com/avatar-arena-carol.png")));
    REQUIRE(runtime.tick_bridge() == 2);

    const auto& state = event_counter_game->state();
    REQUIRE(state.chat_count == 1);
    REQUIRE(state.gift_count == 1);
    REQUIRE(state.follow_count == 1);
    REQUIRE(state.gift_points == 200);
    REQUIRE(state.last_actor_name == "Arena Carol");
    REQUIRE(state.last_avatar_url == "https://cdn.example.com/avatar-arena-carol.png");
    REQUIRE(state.last_event_label == "follow");

    auto configured_bridge_session =
        std::make_unique<nlp3::bridge::TikTokBridgeStubSession>(config.bridge);
    auto* configured_bridge_session_ptr = configured_bridge_session.get();
    nlp3::bridge::TikTokBridgeController configured_bridge_controller{
        std::move(configured_bridge_session)};
    REQUIRE(configured_bridge_controller.start());

    nlp3::host::HostRuntime configured_runtime{
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nlp3::bridge::TikTokEventMapper{config.bridge},
        &configured_bridge_controller,
        nlp3::host::HostAutomationEngine{config.automation},
        nlp3::host::HostPeriodicTtsEngine{},
    };

    nlp3::gamesdk::GameRuntimeController configured_runtime_controller{&game_registry};
    REQUIRE(configured_runtime_controller.activate("event-counter"));
    auto* configured_game = dynamic_cast<nlp3::games::EventCounterGame*>(
        configured_runtime_controller.active_game());
    REQUIRE(configured_game != nullptr);

    auto custom_config = configured_game->default_config();
    custom_config.set("count_chat", false);
    custom_config.set("gift_points_multiplier", std::int64_t{2});
    custom_config.set("follow_points", std::int64_t{3});
    custom_config.set("use_diamond_count", false);
    configured_game->apply_config(custom_config);
    configured_runtime.attach_game(configured_game);

    REQUIRE(configured_bridge_session_ptr->inject_event(nlp3::testsupport::make_chat_event(
        "arena-user-11",
        "arena_config_alice",
        "Arena Config Alice",
        "evt-arena-config-chat-001",
        "room-arena-config-001",
        "Config hello",
        1710000013000,
        "https://cdn.example.com/avatar-arena-config-alice.png")));
    REQUIRE(configured_runtime.tick_bridge() == 1);

    REQUIRE(configured_bridge_session_ptr->inject_event(nlp3::testsupport::make_gift_event(
        "arena-user-12",
        "arena_config_bob",
        "Arena Config Bob",
        "evt-arena-config-gift-001",
        "room-arena-config-001",
        "gift-coin",
        "Coin",
        3,
        200,
        1710000014000,
        "https://cdn.example.com/avatar-arena-config-bob.png")));
    REQUIRE(configured_bridge_session_ptr->inject_event(nlp3::testsupport::make_follow_event(
        "arena-user-13",
        "arena_config_carol",
        "Arena Config Carol",
        "evt-arena-config-follow-001",
        "room-arena-config-001",
        1710000015000,
        "https://cdn.example.com/avatar-arena-config-carol.png")));
    REQUIRE(configured_runtime.tick_bridge() == 2);

    const auto& configured_state = configured_game->state();
    REQUIRE(configured_state.chat_count == 0);
    REQUIRE(configured_state.gift_count == 1);
    REQUIRE(configured_state.gift_points == 6);
    REQUIRE(configured_state.follow_count == 1);
    REQUIRE(configured_state.follow_points_total == 3);
    REQUIRE(configured_state.last_actor_name == "Arena Config Carol");
    REQUIRE(configured_state.last_avatar_url ==
        "https://cdn.example.com/avatar-arena-config-carol.png");
    REQUIRE(configured_state.last_event_label == "follow");

    return 0;
}
