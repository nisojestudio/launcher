#include "render/module.hpp"

namespace nlp3::render {

ModuleDescriptor describe_module() noexcept {
    return ModuleDescriptor{
        "render",
        "Builds host-facing presentation data from core state",
        ModuleStage::contract_ready,
    };
}

} // namespace nlp3::render
