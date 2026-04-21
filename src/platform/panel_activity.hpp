#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace nlp3::platform {

enum class PanelActivityKind {
    unknown,
    host_event,
    tts_chat_enqueued,
    tts_automation_enqueued,
    tts_periodic_enqueued,
};

struct PanelActivityEntry {
    PanelActivityKind kind = PanelActivityKind::unknown;
    std::string label{};
    std::string source{};
    std::string actor_name{};
    std::string details{};
    std::int64_t timestamp_ms = 0;
};

class PanelActivityLog {
public:
    explicit PanelActivityLog(std::size_t capacity = 20) noexcept;

    void push(PanelActivityEntry entry);
    std::vector<PanelActivityEntry> entries() const;
    std::size_t size() const noexcept;
    std::size_t capacity() const noexcept;
    void clear() noexcept;

private:
    std::size_t capacity_ = 20;
    std::deque<PanelActivityEntry> entries_{};
};

} // namespace nlp3::platform
