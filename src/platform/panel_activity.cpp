#include "platform/panel_activity.hpp"

#include <utility>

namespace nlp3::platform {

PanelActivityLog::PanelActivityLog(std::size_t capacity) noexcept
    : capacity_(capacity > 0 ? capacity : 1) {
}

void PanelActivityLog::push(PanelActivityEntry entry) {
    entries_.push_back(std::move(entry));

    while (entries_.size() > capacity_) {
        entries_.pop_front();
    }
}

std::vector<PanelActivityEntry> PanelActivityLog::entries() const {
    return {entries_.begin(), entries_.end()};
}

std::size_t PanelActivityLog::size() const noexcept {
    return entries_.size();
}

std::size_t PanelActivityLog::capacity() const noexcept {
    return capacity_;
}

void PanelActivityLog::clear() noexcept {
    entries_.clear();
}

} // namespace nlp3::platform
