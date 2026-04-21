#pragma once

#include <optional>

#include "bridge/tiktok_bridge_config.hpp"
#include "bridge/tiktok_raw_event.hpp"
#include "events/host_event.hpp"

namespace nlp3::bridge {

class TikTokEventMapper {
public:
    explicit TikTokEventMapper(TikTokBridgeConfig config = {}) noexcept;

    std::optional<events::HostEvent> map(const TikTokRawEvent& raw_event) const;

private:
    bool is_enabled(TikTokRawEventKind kind) const noexcept;
    std::string normalize_avatar_url(std::string_view value) const;

    TikTokBridgeConfig config_;
};

} // namespace nlp3::bridge
