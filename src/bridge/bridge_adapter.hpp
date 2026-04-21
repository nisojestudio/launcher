#pragma once

#include <string_view>

namespace nlp3::bridge {

class IBridgeAdapter {
public:
    virtual ~IBridgeAdapter() = default;

    virtual std::string_view adapter_name() const noexcept = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
};

} // namespace nlp3::bridge
