#include "platform/runtime.hpp"

#include "bridge/bridge_adapter.hpp"
#include "events/host_event.hpp"
#include "gamesdk/game_module.hpp"
#include "host/host_runtime.hpp"
#include "live/module.hpp"
#include "tts/tts_service.hpp"

namespace nlp3::platform {

RuntimeManifest build_runtime_manifest() noexcept {
    return RuntimeManifest{
        get_engine_info(),
        {
            ModuleDescriptor{
                "host",
                "Owns the installable panel runtime and routes normalized events to the active game",
                ModuleStage::integrated,
            },
            ModuleDescriptor{
                "events",
                "Defines the normalized host event contract shared by host, bridge, TTS and games",
                ModuleStage::integrated,
            },
            ModuleDescriptor{
                "gamesdk",
                "Defines the standard contract for games loaded by the host",
                ModuleStage::integrated,
            },
            ModuleDescriptor{
                "bridge",
                "Defines external adapter contracts plus the TikTok raw event model, mapper and local stub",
                ModuleStage::integrated,
            },
            live::describe_module(),
        },
    };
}

} // namespace nlp3::platform
