#include <cassert>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "events/host_event.hpp"
#include "gamesdk/game_factory.hpp"
#include "gamesdk/game_module.hpp"
#include "gamesdk/game_registry.hpp"
#include "gamesdk/game_runtime_controller.hpp"
#include "host/host_runtime.hpp"

namespace {

struct MockGameProbe {
    int activation_count = 0;
};

class MockGame final : public nlp3::gamesdk::IGameModule {
public:
    explicit MockGame(MockGameProbe* probe) noexcept
        : probe_(probe) {
    }

    std::string_view game_id() const noexcept override {
        return "mock-game";
    }

    void on_activated() override {
        ++probe_->activation_count;
    }

    void on_host_event(
        const nlp3::events::HostEvent&,
        const nlp3::host::HostSessionSnapshot&) override {
    }

private:
    MockGameProbe* probe_ = nullptr;
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
    MockGameProbe probe;
    nlp3::gamesdk::GameFactoryRegistry factory_registry;
    nlp3::gamesdk::GameRegistry game_registry{&factory_registry};

    assert(factory_registry.register_factory(std::make_unique<MockGameFactory>(&probe)));
    game_registry.catalog().add(nlp3::gamesdk::GameCatalogEntry{
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
    game_registry.catalog().add(nlp3::gamesdk::GameCatalogEntry{
        "future-game",
        "Future Game",
        "9.0.0",
        "local",
        true,
        true,
        false,
        {},
        {},
        0,
        {},
    });

    assert(game_registry.catalog().contains("mock-game"));
    const auto* catalog_entry = game_registry.catalog().find_by_id("mock-game");
    assert(catalog_entry != nullptr);
    assert(catalog_entry->display_name == "Mock Game");
    assert(catalog_entry->source == "local");
    assert(game_registry.can_activate("mock-game"));
    assert(!game_registry.can_activate("future-game"));

    nlp3::host::HostRuntime runtime{nullptr, nullptr, nullptr};
    nlp3::gamesdk::GameRuntimeController game_runtime_controller{&game_registry};

    const auto initial_status = game_runtime_controller.status();
    assert(initial_status.state == nlp3::gamesdk::GameRuntimeState::idle);
    assert(!initial_status.has_game);

    assert(game_runtime_controller.activate("mock-game"));
    const auto active_status = game_runtime_controller.status();
    assert(active_status.state == nlp3::gamesdk::GameRuntimeState::active);
    assert(active_status.active_game_id == "mock-game");
    assert(active_status.has_game);
    assert(game_runtime_controller.active_game() != nullptr);

    runtime.attach_game(game_runtime_controller.active_game());
    assert(runtime.has_active_game());
    assert(runtime.active_game_id() == "mock-game");
    assert(probe.activation_count == 1);

    assert(game_runtime_controller.restart());
    const auto restarted_status = game_runtime_controller.status();
    assert(restarted_status.state == nlp3::gamesdk::GameRuntimeState::active);
    assert(restarted_status.active_game_id == "mock-game");

    runtime.attach_game(game_runtime_controller.active_game());
    assert(runtime.has_active_game());
    assert(runtime.active_game_id() == "mock-game");
    assert(probe.activation_count == 2);

    game_runtime_controller.deactivate();
    const auto idle_status = game_runtime_controller.status();
    assert(idle_status.state == nlp3::gamesdk::GameRuntimeState::idle);
    assert(idle_status.active_game_id.empty());
    assert(!idle_status.has_game);

    runtime.attach_game(nullptr);
    assert(!runtime.has_active_game());

    return 0;
}
