#pragma once

#include <string>
#include <vector>

namespace nlp3::platform {

enum class PanelDiagnosticLevel {
    info,
    warning,
    error,
};

struct PanelDiagnosticEntry {
    PanelDiagnosticLevel level = PanelDiagnosticLevel::info;
    std::string code{};
    std::string message{};
};

struct PanelDiagnosticsReport {
    bool ok = true;
    std::vector<PanelDiagnosticEntry> entries{};
};

} // namespace nlp3::platform
