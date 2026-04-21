#pragma once

#include <string>

#include "platform/panel_http_server.hpp"

namespace nlp3::platform {

class PanelApp;

std::string build_panel_http_state_json(
    const PanelApp& app,
    const PanelHttpServerStatus& http_status);

std::string build_panel_http_events_json(const PanelApp& app);

std::string build_panel_http_metrics_json(const PanelApp& app);
std::string build_panel_http_tts_json(const PanelApp& app);

std::string build_panel_http_command_json(
    bool recognized,
    const std::string& output);

std::string build_panel_http_error_json(const std::string& message);

} // namespace nlp3::platform
