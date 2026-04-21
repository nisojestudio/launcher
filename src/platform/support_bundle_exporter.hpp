#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "platform/panel_http_server.hpp"

namespace nlp3::platform {

class PanelApp;

struct SupportBundleExportResult {
    bool ok = false;
    std::string message{};
    std::string bundle_path{};
    std::uint64_t exported_at_ms = 0;
    std::size_t included_logs = 0;
};

SupportBundleExportResult export_support_bundle(
    const PanelApp& app,
    const PanelHttpServerStatus& http_status,
    std::string_view reason = {});

} // namespace nlp3::platform
