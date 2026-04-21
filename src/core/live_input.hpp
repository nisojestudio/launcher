#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace nlp3::core {

class RuntimeState;
struct RuntimeSnapshot;

enum class CoreInputEventType {
    chat_message,
    reaction,
    gift,
    follow,
    share,
};

struct CoreInputEvent {
    CoreInputEventType type;
    std::string actor_id;
    std::string actor_name;
    std::string label;
    std::string text;
    int magnitude = 0;
};

struct LiveInputState {
    std::size_t accepted_events = 0;
    std::optional<CoreInputEvent> last_event;
};

class LiveInputPort {
public:
    explicit LiveInputPort(RuntimeState& runtime_state) noexcept;

    void submit(CoreInputEvent event);
    const LiveInputState& snapshot() const noexcept;
    const RuntimeSnapshot& runtime_snapshot() const noexcept;

private:
    RuntimeState& runtime_state_;
    LiveInputState state_{};
};

constexpr std::string_view to_string(CoreInputEventType type) noexcept {
    switch (type) {
    case CoreInputEventType::chat_message:
        return "chat_message";
    case CoreInputEventType::reaction:
        return "reaction";
    case CoreInputEventType::gift:
        return "gift";
    case CoreInputEventType::follow:
        return "follow";
    case CoreInputEventType::share:
        return "share";
    }

    return "unknown";
}

} // namespace nlp3::core
