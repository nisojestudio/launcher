#pragma once

#include <cstddef>
#include <string>

namespace nlp3 {
namespace platform {
class PanelApp;
}
}

namespace nlp3::bridge {

class TikTokExternalEventReplay {
public:
    explicit TikTokExternalEventReplay(platform::PanelApp* app) noexcept;

    std::size_t replay_jsonl_file(const std::string& path);

private:
    platform::PanelApp* app_ = nullptr;
};

} // namespace nlp3::bridge
