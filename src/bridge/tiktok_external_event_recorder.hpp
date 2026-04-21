#pragma once

#include <string>

#include "bridge/tiktok_raw_event.hpp"

namespace nlp3::bridge {

class TikTokExternalEventRecorder {
public:
    bool append_jsonl(const std::string& path, const TikTokRawEvent& event) const;
};

} // namespace nlp3::bridge
