#include <cassert>
#include <memory>
#include <utility>

#include "events/host_event.hpp"
#include "gamesdk/game_factory.hpp"
#include "gamesdk/game_input_event.hpp"
#include "gamesdk/game_module.hpp"
#include "host/host_runtime.hpp"
#include "host/local_game_registry.hpp"

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

    void on_game_input_event(
        const nlp3::gamesdk::GameInputEvent&,
        const nlp3::host::HostSessionSnapshot&) override {
    }

private:
    MockGameProbe* probe_ = nullptr;
};

class MockGameFactory final : public nlp3::gamesdk::IGameFactory {
public:
    MockGameFactory(nlp3::gamesdk::GameManifest manifest, MockGameProbe* probe) noexcept
        : manifest_(std::move(manifest)),
          probe_(probe) {
    }

    const nlp3::gamesdk::GameManifest& manifest() const noexcept override {
        return manifest_;
    }

    std::unique_ptr<nlp3::gamesdk::IGameModule> create() const override {
        return std::make_unique<MockGame>(probe_);
    }

private:
    nlp3::gamesdk::GameManifest manifest_;
    MockGameProbe* probe_ = nullptr;
};

} // namespace

int main() {
    MockGameProbe compatible_probe;
    MockGameProbe second_probe;

    nlp3::host::LocalGameRegistry registry;
    assert(registry.register_factory(std::make_unique<MockGameFactory>(
        nlp3::gamesdk::GameManifest{
            "mock-game",
            "Mock Game",
            "1.0.0",
            nlp3::gamesdk::GameCompatibility{1, false, "windows"},
        },
        &compatible_probe)));
    assert(registry.register_factory(std::make_unique<MockGameFactory>(
        nlp3::gamesdk::GameManifest{
            "future-game",
            "Future Game",
            "9.0.0",
            nlp3::gamesdk::GameCompatibility{99, false, "windows"},
        },
        &second_probe)));
    assert(!registry.register_factory(std::make_unique<MockGameFactory>(
        nlp3::gamesdk::GameManifest{
            "mock-game",
            "Duplicate Game",
            "1.0.1",
            nlp3::gamesdk::GameCompatibility{1, false, "windows"},
        },
        &second_probe)));

    assert(registry.registered_count() == 2);
    const auto manifests = registry.list_manifests();
    assert(manifests.size() == 2);
    assert(manifests[0].game_id == "mock-game");
    assert(manifests[1].game_id == "future-game");

    const auto* compatible_manifest = registry.find_manifest("mock-game");
    assert(compatible_manifest != nullptr);
    assert(compatible_manifest->display_name == "Mock Game");
    assert(compatible_manifest->version == "1.0.0");

    const auto* incompatible_manifest = registry.find_manifest("future-game");
    assert(incompatible_manifest != nullptr);
    assert(incompatible_manifest->compatibility.min_host_api_version == 99);

    nlp3::host::HostRuntime runtime{nullptr, nullptr, nullptr};
    assert(registry.is_compatible("mock-game", runtime));
    assert(!registry.is_compatible("future-game", runtime));
    assert(!registry.is_compatible("missing-game", runtime));

    assert(!registry.activate_game("future-game", runtime));
    assert(registry.activate_game("mock-game", runtime));
    assert(runtime.has_active_game());
    assert(runtime.active_game_id() == "mock-game");
    assert(compatible_probe.activation_count == 1);

    return 0;
}
