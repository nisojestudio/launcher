#include "live/module.hpp"

namespace nlp3::live {

ModuleDescriptor describe_module() noexcept {
    return ModuleDescriptor{
        "live",
        "Normalizes external live events into stable contracts for platform routing",
        ModuleStage::integrated,
    };
}

} // namespace nlp3::live
