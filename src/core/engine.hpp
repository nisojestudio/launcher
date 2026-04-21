#pragma once
#include <string_view>

namespace nlp3 {

struct EngineInfo {
    std::string_view name;
    int version_major;
    int version_minor;
};

EngineInfo get_engine_info() noexcept;

} // namespace nlp3
