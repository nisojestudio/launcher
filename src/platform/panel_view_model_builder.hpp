#pragma once

#include "platform/panel_view_model.hpp"

namespace nlp3::platform {

class PanelViewModelBuilder {
public:
    PanelViewModel build(const PanelSnapshot& snapshot) const;
};

} // namespace nlp3::platform
