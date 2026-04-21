#pragma once

#include <cstddef>
#include <string>

namespace nlp3 {
namespace platform {
class PanelApp;
}
} // namespace nlp3

namespace nlp3::bridge {

struct TikTokExternalInboxResult {
    std::size_t files_seen = 0;
    std::size_t files_processed = 0;
    std::size_t files_failed = 0;
    std::size_t events_accepted = 0;
};

class TikTokExternalInboxAdapter {
public:
    explicit TikTokExternalInboxAdapter(platform::PanelApp* app) noexcept;

    TikTokExternalInboxResult process_inbox(const std::string& inbox_dir);

private:
    platform::PanelApp* app_ = nullptr;
};

} // namespace nlp3::bridge
