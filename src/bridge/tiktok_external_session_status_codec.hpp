#pragma once

#include <optional>
#include <string>

#include "bridge/tiktok_external_session_status.hpp"

namespace nlp3::bridge {

class TikTokExternalSessionStatusCodec {
public:
    std::string encode_json(const TikTokExternalSessionStatus& status) const;
    std::optional<TikTokExternalSessionStatus> decode_json(const std::string& payload) const;
};

} // namespace nlp3::bridge
