#pragma once

#include "platform/panel_diagnostics.hpp"
#include "platform/panel_snapshot.hpp"

namespace nlp3::platform {

class PanelDiagnosticsBuilder {
public:
    PanelDiagnosticsReport build(const PanelSnapshot& snapshot) const;
};

} // namespace nlp3::platform
