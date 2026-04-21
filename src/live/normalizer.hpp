#pragma once

#include <optional>

#include "live/events.hpp"

namespace nlp3::live {

std::optional<LiveEvent> normalize_external_event(const ExternalEvent& external_event);

} // namespace nlp3::live
