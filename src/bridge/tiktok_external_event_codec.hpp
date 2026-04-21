#pragma once

#include <optional>
#include <string>

#include "bridge/tiktok_raw_event.hpp"

namespace nlp3::bridge {

class TikTokExternalEventCodec {
public:
    std::string encode_json(const TikTokRawEvent& event) const;
    std::optional<TikTokRawEvent> decode_json(const std::string& payload) const;
};

} // namespace nlp3::bridge
