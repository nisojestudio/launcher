#pragma once

#include <string_view>

namespace nlp3 {

enum class ModuleStage {
    scaffolded,
    contract_ready,
    integrated,
};

struct ModuleDescriptor {
    std::string_view name;
    std::string_view responsibility;
    ModuleStage stage;
};

constexpr std::string_view to_string(ModuleStage stage) noexcept {
    switch (stage) {
    case ModuleStage::scaffolded:
        return "scaffolded";
    case ModuleStage::contract_ready:
        return "contract_ready";
    case ModuleStage::integrated:
        return "integrated";
    }

    return "unknown";
}

} // namespace nlp3
