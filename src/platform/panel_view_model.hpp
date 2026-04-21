#pragma once

#include <string>
#include <vector>

#include "platform/panel_command.hpp"
#include "platform/panel_snapshot.hpp"

namespace nlp3::platform {

struct PanelViewSectionItem {
    std::string label{};
    std::string value{};
};

struct PanelViewSection {
    std::string title{};
    std::vector<PanelViewSectionItem> items{};
};

struct PanelViewAction {
    PanelCommandKind command = PanelCommandKind::unknown;
    std::string label{};
    std::string argument_hint{};
};

struct PanelViewModel {
    std::string title{};
    std::vector<PanelViewSection> sections{};
    std::vector<PanelViewAction> actions{};
    std::vector<std::string> recent_activity_lines{};
};

} // namespace nlp3::platform
