#pragma once

#include <cstddef>
#include <vector>

#include "bridge/tiktok_raw_event.hpp"

namespace nlp3::bridge {

class ITikTokEventSource {
public:
    virtual ~ITikTokEventSource() = default;

    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual void reset() = 0;

    virtual std::vector<TikTokRawEvent> poll(std::size_t max_events = 0) = 0;
};

} // namespace nlp3::bridge
