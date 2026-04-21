#include "bridge/tiktok_external_event_recorder.hpp"

#include <fstream>

#include "bridge/tiktok_external_event_codec.hpp"

namespace nlp3::bridge {

bool TikTokExternalEventRecorder::append_jsonl(
    const std::string& path,
    const TikTokRawEvent& event) const {
    std::ofstream output(path, std::ios::binary | std::ios::app);
    if (!output) {
        return false;
    }

    const TikTokExternalEventCodec codec{};
    output << codec.encode_json(event) << "\n";
    return output.good();
}

} // namespace nlp3::bridge
