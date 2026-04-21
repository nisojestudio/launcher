#pragma once

#include <array>

#include "core/engine.hpp"
#include "core/module_contract.hpp"

namespace nlp3::platform {

struct RuntimeManifest {
    EngineInfo engine;
    std::array<ModuleDescriptor, 5> modules;
};

RuntimeManifest build_runtime_manifest() noexcept;

} // namespace nlp3::platform
