#pragma once

#include <string>
#include <string_view>

#include "nlohmann/json_fwd.hpp"

namespace nlp3::platform {

nlohmann::json sanitize_support_bundle_json(nlohmann::json value);
std::string sanitize_support_bundle_log_line(std::string_view line);

} // namespace nlp3::platform
