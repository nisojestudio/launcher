#include <cassert>
#include <cstdint>
#include <string>

#include "games/event_counter_game.hpp"
#include "gamesdk/game_config.hpp"
#include "gamesdk/game_input_event.hpp"
#include "host/session_state.hpp"

int main() {
    nlp3::games::EventCounterGame game;

    const auto manifest = game.manifest();
    assert(manifest.game_id == "event-counter");
    assert(manifest.display_name == "Event Counter");
    assert(manifest.capabilities.uses_chat_messages);
    assert(manifest.capabilities.uses_gifts);
    assert(manifest.capabilities.uses_follows);
    assert(manifest.capabilities.uses_viewer_joins);
    assert(manifest.capabilities.uses_avatar_data);

    const auto defaults = game.default_config();
    assert(defaults.get_bool("count_chat", false));
    assert(defaults.get_int("gift_points_multiplier", 0) == 1);
    assert(defaults.get_int("follow_points", 0) == 1);
    assert(defaults.get_bool("use_diamond_count", false));

    nlp3::gamesdk::GameConfig custom_config;
    custom_config.set("count_chat", false);
    custom_config.set("gift_points_multiplier", std::int64_t{2});
    custom_config.set("follow_points", std::int64_t{3});
    custom_config.set("use_diamond_count", false);
    game.apply_config(custom_config);
    game.on_activated();

    const nlp3::host::HostSessionSnapshot session_snapshot{};

    game.on_game_input_event(nlp3::gamesdk::GameInputEvent{
        nlp3::gamesdk::GameInputEventKind::chat_message,
        nlp3::gamesdk::GameInputActor{
            "game-user-01",
            "chat_alice",
            "Chat Alice",
            "https://cdn.example.com/avatar-chat-alice.png",
        },
        "Hello event counter",
        std::nullopt,
        {},
    }, session_snapshot);

    auto state = game.state();
    assert(state.chat_count == 0);
    assert(state.last_actor_name == "Chat Alice");
    assert(state.last_avatar_url == "https://cdn.example.com/avatar-chat-alice.png");
    assert(state.last_event_label == "chat_message");

    game.on_game_input_event(nlp3::gamesdk::GameInputEvent{
        nlp3::gamesdk::GameInputEventKind::follow,
        nlp3::gamesdk::GameInputActor{
            "game-user-02",
            "follow_bob",
            "",
            "https://cdn.example.com/avatar-follow-bob.png",
        },
        "",
        std::nullopt,
        {},
    }, session_snapshot);

    state = game.state();
    assert(state.follow_count == 1);
    assert(state.follow_points_total == 3);
    assert(state.last_actor_name == "follow_bob");
    assert(state.last_avatar_url == "https://cdn.example.com/avatar-follow-bob.png");
    assert(state.last_event_label == "follow");

    game.on_game_input_event(nlp3::gamesdk::GameInputEvent{
        nlp3::gamesdk::GameInputEventKind::gift,
        nlp3::gamesdk::GameInputActor{
            "game-user-03",
            "gift_carol",
            "",
            "https://cdn.example.com/avatar-gift-carol.png",
        },
        "",
        nlp3::gamesdk::GameInputGift{
            "gift-rose",
            "Rose",
            2,
            150,
        },
        {},
    }, session_snapshot);

    state = game.state();
    assert(state.gift_count == 1);
    assert(state.gift_points == 4);
    assert(state.last_actor_name == "gift_carol");
    assert(state.last_avatar_url == "https://cdn.example.com/avatar-gift-carol.png");
    assert(state.last_event_label == "gift");

    game.on_game_input_event(nlp3::gamesdk::GameInputEvent{
        nlp3::gamesdk::GameInputEventKind::viewer_join,
        nlp3::gamesdk::GameInputActor{
            "game-user-04",
            "viewer_dave",
            "Viewer Dave",
            "",
        },
        "",
        std::nullopt,
        {},
    }, session_snapshot);

    state = game.state();
    assert(state.chat_count == 0);
    assert(state.follow_count == 1);
    assert(state.gift_count == 1);
    assert(state.last_actor_name == "Viewer Dave");
    assert(state.last_event_label == "viewer_join");

    return 0;
}
