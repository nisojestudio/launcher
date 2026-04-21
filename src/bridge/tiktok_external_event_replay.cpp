#include "bridge/tiktok_external_event_replay.hpp"

#include <fstream>
#include <string>

#include "bridge/tiktok_external_event_codec.hpp"
#include "platform/panel_app.hpp"

namespace nlp3::bridge {

TikTokExternalEventReplay::TikTokExternalEventReplay(platform::PanelApp* app) noexcept
    : app_(app) {
}

std::size_t TikTokExternalEventReplay::replay_jsonl_file(const std::string& path) {
    if (app_ == nullptr) {
        return 0;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return 0;
    }

    const TikTokExternalEventCodec codec{};
    std::size_t accepted = 0;
    std::string line{};

    while (std::getline(input, line)) {
        if (line.find_first_not_of(" \t\r\n") == std::string::npos) {
            continue;
        }

        const auto decoded = codec.decode_json(line);
        if (!decoded.has_value()) {
            continue;
        }

        if (!app_->submit_external_bridge_event(*decoded)) {
            continue;
        }

        app_->tick_bridge(1);
        ++accepted;
    }

    return accepted;
}

} // namespace nlp3::bridge
