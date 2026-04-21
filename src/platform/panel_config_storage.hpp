#pragma once

#include <string>

#include "platform/panel_config.hpp"

namespace nlp3::platform {

class PanelConfigStorage {
public:
    bool save_to_file(const PanelConfig& config, const std::string& path) const;
    bool load_from_file(const std::string& path, PanelConfig& out_config) const;
};

} // namespace nlp3::platform
