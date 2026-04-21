#include "audio/module.hpp"

namespace nlp3::audio {

ModuleDescriptor describe_module() noexcept {
    return ModuleDescriptor{
        "audio",
        "Defines audio policy and playback integration contracts",
        ModuleStage::contract_ready,
    };
}

} // namespace nlp3::audio
