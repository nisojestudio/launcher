#include "platform/panel_console.hpp"

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "bridge/tiktok_external_event_codec.hpp"
#include "bridge/tiktok_raw_event.hpp"
#include "platform/panel_app.hpp"
#include "platform/panel_view_model_builder.hpp"

namespace {

std::string trim_copy(std::string_view value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) {
        return {};
    }

    const auto end = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(begin, end - begin + 1));
}

bool starts_with(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

void wait_step_ms(std::uint64_t step_ms) {
    if (step_ms == 0) {
        return;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(step_ms));
}

struct InboxCycleCommand {
    std::string path{};
    std::uint64_t ticks = 0;
    std::uint64_t step_ms = 0;
};

struct TickCycleCommand {
    std::uint64_t ticks = 0;
    std::uint64_t step_ms = 0;
};

struct WsAwaitCommand {
    std::uint64_t accepted_target = 0;
    std::uint64_t max_ticks = 0;
    std::uint64_t step_ms = 0;
};

struct DemoSessionCommand {
    std::uint64_t accepted_target = 0;
    std::uint64_t max_ticks = 0;
    std::uint64_t step_ms = 0;
    std::uint64_t port = 0;
};

std::vector<std::string> split_tokens(std::string_view value) {
    std::vector<std::string> tokens;
    std::size_t cursor = 0;
    while (cursor < value.size()) {
        const auto begin = value.find_first_not_of(" \t\r\n", cursor);
        if (begin == std::string_view::npos) {
            break;
        }

        const auto end = value.find_first_of(" \t\r\n", begin);
        if (end == std::string_view::npos) {
            tokens.emplace_back(value.substr(begin));
            break;
        }

        tokens.emplace_back(value.substr(begin, end - begin));
        cursor = end + 1;
    }

    return tokens;
}

std::optional<std::uint64_t> parse_uint64_value(std::string_view value) {
    const auto trimmed = trim_copy(value);
    if (trimmed.empty()) {
        return std::nullopt;
    }

    try {
        std::size_t consumed = 0;
        const auto parsed = std::stoull(trimmed, &consumed);
        if (consumed != trimmed.size()) {
            return std::nullopt;
        }
        return static_cast<std::uint64_t>(parsed);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<InboxCycleCommand> parse_inbox_cycle_command(std::string_view payload) {
    const auto trimmed = trim_copy(payload);
    const auto last_separator = trimmed.find_last_of(' ');
    if (last_separator == std::string::npos) {
        return std::nullopt;
    }

    InboxCycleCommand command{};
    std::string path_text = trim_copy(std::string_view(trimmed).substr(0, last_separator));
    std::string ticks_text = trim_copy(std::string_view(trimmed).substr(last_separator + 1));
    std::string step_text{};

    const auto previous_separator = path_text.find_last_of(' ');
    if (previous_separator != std::string::npos) {
        const auto candidate_ticks =
            trim_copy(std::string_view(path_text).substr(previous_separator + 1));
        const auto parsed_ticks = parse_uint64_value(candidate_ticks);
        const auto parsed_step = parse_uint64_value(ticks_text);
        if (parsed_ticks.has_value() && parsed_step.has_value()) {
            step_text = std::move(ticks_text);
            ticks_text = std::move(candidate_ticks);
            path_text = trim_copy(std::string_view(path_text).substr(0, previous_separator));
        }
    }

    command.path = std::move(path_text);
    const auto ticks = parse_uint64_value(ticks_text);
    if (command.path.empty() || !ticks.has_value()) {
        return std::nullopt;
    }

    command.ticks = *ticks;
    if (!step_text.empty()) {
        const auto parsed_step = parse_uint64_value(step_text);
        if (!parsed_step.has_value()) {
            return std::nullopt;
        }
        command.step_ms = *parsed_step;
    }

    return command;
}

std::optional<TickCycleCommand> parse_tick_cycle_command(std::string_view payload) {
    const auto trimmed = trim_copy(payload);
    if (trimmed.empty()) {
        return std::nullopt;
    }

    TickCycleCommand command{};
    const auto separator = trimmed.find(' ');
    std::string ticks_text = trimmed;
    std::string step_text{};
    if (separator != std::string::npos) {
        ticks_text = trim_copy(std::string_view(trimmed).substr(0, separator));
        step_text = trim_copy(std::string_view(trimmed).substr(separator + 1));
    }

    const auto ticks = parse_uint64_value(ticks_text);
    if (!ticks.has_value()) {
        return std::nullopt;
    }

    command.ticks = *ticks;
    if (!step_text.empty()) {
        const auto parsed_step = parse_uint64_value(step_text);
        if (!parsed_step.has_value()) {
            return std::nullopt;
        }
        command.step_ms = *parsed_step;
    }

    return command;
}

std::optional<WsAwaitCommand> parse_ws_await_command(std::string_view payload) {
    const auto trimmed = trim_copy(payload);
    if (trimmed.empty()) {
        return std::nullopt;
    }

    const auto first_separator = trimmed.find(' ');
    if (first_separator == std::string::npos) {
        return std::nullopt;
    }

    WsAwaitCommand command{};
    const auto accepted_text = trim_copy(std::string_view(trimmed).substr(0, first_separator));
    const auto remainder = trim_copy(std::string_view(trimmed).substr(first_separator + 1));
    const auto second_separator = remainder.find(' ');

    std::string max_ticks_text = remainder;
    std::string step_text{};
    if (second_separator != std::string::npos) {
        max_ticks_text = trim_copy(std::string_view(remainder).substr(0, second_separator));
        step_text = trim_copy(std::string_view(remainder).substr(second_separator + 1));
    }

    const auto accepted_target = parse_uint64_value(accepted_text);
    const auto max_ticks = parse_uint64_value(max_ticks_text);
    if (!accepted_target.has_value() || !max_ticks.has_value()) {
        return std::nullopt;
    }

    command.accepted_target = *accepted_target;
    command.max_ticks = *max_ticks;
    if (!step_text.empty()) {
        const auto parsed_step = parse_uint64_value(step_text);
        if (!parsed_step.has_value()) {
            return std::nullopt;
        }
        command.step_ms = *parsed_step;
    }

    return command;
}

std::optional<DemoSessionCommand> parse_demo_session_command(std::string_view payload) {
    const auto tokens = split_tokens(payload);
    if (tokens.size() < 2 || tokens.size() > 4) {
        return std::nullopt;
    }

    DemoSessionCommand command{};
    const auto accepted_target = parse_uint64_value(tokens[0]);
    const auto max_ticks = parse_uint64_value(tokens[1]);
    if (!accepted_target.has_value() || !max_ticks.has_value()) {
        return std::nullopt;
    }

    command.accepted_target = *accepted_target;
    command.max_ticks = *max_ticks;

    if (tokens.size() >= 3) {
        const auto step_ms = parse_uint64_value(tokens[2]);
        if (!step_ms.has_value()) {
            return std::nullopt;
        }
        command.step_ms = *step_ms;
    }

    if (tokens.size() >= 4) {
        const auto port = parse_uint64_value(tokens[3]);
        if (!port.has_value() || *port > 65535) {
            return std::nullopt;
        }
        command.port = *port;
    }

    return command;
}

std::string join_section_items(const nlp3::platform::PanelViewSection& section) {
    std::string line;

    for (std::size_t index = 0; index < section.items.size(); ++index) {
        if (index > 0) {
            line += ", ";
        }

        line += section.items[index].label + "=" + section.items[index].value;
    }

    return line;
}

void print_recent_activity_tail(
    std::ostream* output,
    const nlp3::platform::PanelViewModel& view_model,
    std::size_t max_lines = 3) {
    if (output == nullptr || view_model.recent_activity_lines.empty()) {
        return;
    }

    *output << "  activity_tail:\n";
    const auto start_index = view_model.recent_activity_lines.size() > max_lines
        ? view_model.recent_activity_lines.size() - max_lines
        : 0;
    for (std::size_t index = start_index; index < view_model.recent_activity_lines.size(); ++index) {
        *output << "    " << view_model.recent_activity_lines[index] << "\n";
    }
}

void print_command_result(
    std::ostream* output,
    const nlp3::platform::PanelCommandResult& result) {
    if (output == nullptr) {
        return;
    }

    *output << (result.ok ? "ok" : "error") << ": " << result.message << "\n";
}

std::string diagnostic_level_text(nlp3::platform::PanelDiagnosticLevel level) {
    switch (level) {
    case nlp3::platform::PanelDiagnosticLevel::info:
        return "info";
    case nlp3::platform::PanelDiagnosticLevel::warning:
        return "warning";
    case nlp3::platform::PanelDiagnosticLevel::error:
        return "error";
    }

    return "info";
}

std::string external_counts_text(const nlp3::platform::ExternalBridgeManifest& manifest) {
    return "chat=" + std::to_string(manifest.chat_events)
        + ", like=" + std::to_string(manifest.like_events)
        + ", gift=" + std::to_string(manifest.gift_events)
        + ", follow=" + std::to_string(manifest.follow_events)
        + ", share=" + std::to_string(manifest.share_events)
        + ", viewer_join=" + std::to_string(manifest.viewer_join_events)
        + ", viewer_count=" + std::to_string(manifest.viewer_count_events)
        + ", live_start=" + std::to_string(manifest.live_start_events)
        + ", live_end=" + std::to_string(manifest.live_end_events)
        + ", moderation=" + std::to_string(manifest.moderation_events)
        + ", custom_raw=" + std::to_string(manifest.custom_raw_events);
}

std::uint16_t resolve_configured_external_ws_port(const nlp3::platform::PanelApp* app) {
    if (app == nullptr) {
        return static_cast<std::uint16_t>(8765);
    }
    return app->config().external_ws_port == 0 ? static_cast<std::uint16_t>(8765) : app->config().external_ws_port;
}

std::uint16_t resolve_default_ui_port(const nlp3::platform::PanelApp* app) {
    if (app == nullptr) {
        return static_cast<std::uint16_t>(18913);
    }
    const auto status = app->http_ui_status();
    if (status.port != 0) {
        return status.port;
    }

    const auto embedded_url = app->config().embedded_ui_url;
    const auto scheme_separator = embedded_url.find("://");
    if (scheme_separator != std::string::npos) {
        const auto authority_start = scheme_separator + 3;
        const auto authority_end = embedded_url.find('/', authority_start);
        const auto authority = embedded_url.substr(
            authority_start,
            authority_end == std::string::npos
                ? std::string::npos
                : authority_end - authority_start);
        const auto port_separator = authority.rfind(':');
        if (port_separator != std::string::npos) {
            const auto parsed_port = parse_uint64_value(std::string_view(authority).substr(port_separator + 1));
            if (parsed_port.has_value() && *parsed_port <= 65535) {
                return static_cast<std::uint16_t>(*parsed_port);
            }
        }
    }

    return static_cast<std::uint16_t>(18913);
}

} // namespace

namespace nlp3::platform {

PanelConsole::PanelConsole(PanelApp* app, std::istream* input, std::ostream* output) noexcept
    : app_(app),
      input_(input),
      output_(output) {
}

void PanelConsole::print_overview() const {
    if (app_ == nullptr || output_ == nullptr) {
        return;
    }

    const auto snapshot = app_->snapshot();
    const PanelViewModelBuilder builder;
    const auto view_model = builder.build(snapshot);

    *output_ << view_model.title << "\n";
    for (const auto& section : view_model.sections) {
        *output_ << "- " << section.title << ": " << join_section_items(section) << "\n";
    }

    if (!view_model.recent_activity_lines.empty()) {
        *output_ << "Recent activity:\n";
        const auto start_index = view_model.recent_activity_lines.size() > 5
            ? view_model.recent_activity_lines.size() - 5
            : 0;

        for (std::size_t index = start_index; index < view_model.recent_activity_lines.size(); ++index) {
            *output_ << "  " << view_model.recent_activity_lines[index] << "\n";
        }
    }
}

void PanelConsole::print_help() const {
    if (output_ == nullptr) {
        return;
    }

    (void)input_;

    *output_ << "Commands:\n";
    *output_ << "  help\n";
    *output_ << "  status\n";
    *output_ << "  diagnostics\n";
    *output_ << "  tick [now_ms]\n";
    *output_ << "  run <ticks> [step_ms]\n";
    *output_ << "  activity\n";
    *output_ << "  games\n";
    *output_ << "  game list\n";
    *output_ << "  bridge mode\n";
    *output_ << "  bridge mode stub\n";
    *output_ << "  bridge mode external\n";
    *output_ << "  bridge target\n";
    *output_ << "  bridge target <user>\n";
    *output_ << "  bridge external\n";
    *output_ << "  bridge runner\n";
    *output_ << "  bridge runner status\n";
    *output_ << "  bridge runner start [user] [max_seconds]\n";
    *output_ << "  bridge runner stop\n";
    *output_ << "  bridge runner logs\n";
    *output_ << "  bridge ws\n";
    *output_ << "  bridge ws port\n";
    *output_ << "  bridge ws port <port>\n";
    *output_ << "  bridge ws start [port]\n";
    *output_ << "  bridge ws stop\n";
    *output_ << "  ui\n";
    *output_ << "  ui start [port]\n";
    *output_ << "  ui stop\n";
    *output_ << "  ui open\n";
    *output_ << "  bridge attach [user]\n";
    *output_ << "  bridge demo ws\n";
    *output_ << "  bridge demo ready\n";
    *output_ << "  bridge demo live [port]\n";
    *output_ << "  bridge demo session <accepted> <max_ticks> [step_ms] [port]\n";
    *output_ << "  bridge demo observe <accepted> <max_ticks> [step_ms] [port]\n";
    *output_ << "  bridge demo ws start [port]\n";
    *output_ << "  bridge demo ws run <ticks> [step_ms]\n";
    *output_ << "  bridge demo ws await <accepted> <max_ticks> [step_ms]\n";
    *output_ << "  bridge inject chat <actor> <message>\n";
    *output_ << "  bridge inject json <payload>\n";
    *output_ << "  bridge inbox <path>\n";
    *output_ << "  bridge inbox watch <path> <ticks> [step_ms]\n";
    *output_ << "  bridge demo inbox <path> <ticks> [step_ms]\n";
    *output_ << "  bridge record start <path>\n";
    *output_ << "  bridge record stop\n";
    *output_ << "  bridge record chat <path> <actor> <message>\n";
    *output_ << "  bridge replay file <path>\n";
    *output_ << "  bridge replay <path>\n";
    *output_ << "  bridge start\n";
    *output_ << "  bridge stop\n";
    *output_ << "  bridge reset\n";
    *output_ << "  game activate <id>\n";
    *output_ << "  game deactivate\n";
    *output_ << "  game restart\n";
    *output_ << "  config\n";
    *output_ << "  config save\n";
    *output_ << "  config reload\n";
    *output_ << "  license\n";
    *output_ << "  tts say <message>\n";
}

bool PanelConsole::execute_line(const std::string& line) {
    const auto trimmed = trim_copy(line);
    if (trimmed.empty()) {
        return false;
    }

    if (trimmed == "help") {
        print_help();
        return true;
    }

    if (trimmed == "status") {
        print_overview();
        return true;
    }

    if (trimmed == "diagnostics") {
        if (app_ == nullptr || output_ == nullptr) {
            return true;
        }

        const auto report = app_->diagnostics();
        *output_ << "Diagnostics: " << (report.ok ? "ok" : "issues") << "\n";
        for (const auto& entry : report.entries) {
            *output_ << "  ["
                     << diagnostic_level_text(entry.level)
                     << "] "
                     << entry.code
                     << ": "
                     << entry.message
                     << "\n";
        }
        return true;
    }

    if (trimmed == "tick" || starts_with(trimmed, "tick ")) {
        if (app_ == nullptr || output_ == nullptr) {
            return true;
        }

        std::uint64_t now_ms = 0;
        if (starts_with(trimmed, "tick ")) {
            const auto parsed = parse_uint64_value(
                std::string_view(trimmed).substr(std::string_view("tick ").size()));
            if (!parsed.has_value()) {
                *output_ << "error: invalid tick value\n";
                return true;
            }
            now_ms = *parsed;
        }

        const auto result = app_->tick(now_ms);
        *output_ << "ok: tick processed "
                 << result.bridge_events_processed
                 << " bridge events, periodic_tts="
                 << (result.periodic_tts_enqueued ? "yes" : "no")
                 << "\n";
        return true;
    }

    if (starts_with(trimmed, "run ")) {
        if (app_ == nullptr || output_ == nullptr) {
            return true;
        }

        const auto payload = trim_copy(
            std::string_view(trimmed).substr(std::string_view("run ").size()));
        const auto separator = payload.find(' ');

        std::string ticks_text = payload;
        std::string step_text{};
        if (separator != std::string::npos) {
            ticks_text = trim_copy(std::string_view(payload).substr(0, separator));
            step_text = trim_copy(std::string_view(payload).substr(separator + 1));
        }

        const auto ticks = parse_uint64_value(ticks_text);
        if (!ticks.has_value()) {
            *output_ << "error: invalid run tick count\n";
            return true;
        }

        std::uint64_t step_ms = 0;
        if (!step_text.empty()) {
            const auto parsed_step = parse_uint64_value(step_text);
            if (!parsed_step.has_value()) {
                *output_ << "error: invalid run step value\n";
                return true;
            }
            step_ms = *parsed_step;
        }

        const auto result = app_->run_ticks(
            static_cast<std::size_t>(*ticks),
            0,
            step_ms);
        *output_ << "ok: ran "
                 << result.ticks_executed
                 << " ticks, processed "
                 << result.total_bridge_events_processed
                 << " bridge events, periodic_tts="
                 << result.periodic_tts_enqueues
                 << "\n";
        return true;
    }

    if (trimmed == "activity") {
        if (app_ == nullptr || output_ == nullptr) {
            return true;
        }

        const PanelViewModelBuilder builder;
        const auto view_model = builder.build(app_->snapshot());
        *output_ << "Recent activity:\n";
        for (const auto& line_view : view_model.recent_activity_lines) {
            *output_ << "  " << line_view << "\n";
        }
        return true;
    }

    if (trimmed == "games" || trimmed == "game list") {
        if (output_ == nullptr) {
            return true;
        }

        const auto game_ids = app_ != nullptr ? app_->available_game_ids() : std::vector<std::string>{};
        *output_ << "Available games:\n";
        for (const auto& game_id : game_ids) {
            *output_ << "  " << game_id << "\n";
        }
        return true;
    }

    if (trimmed == "config") {
        if (app_ == nullptr || output_ == nullptr) {
            return true;
        }

        const auto& config = app_->config();
        *output_ << "Config:\n";
        *output_ << "  panel_name=" << config.panel_name << "\n";
        *output_ << "  default_game_id=" << config.default_game_id << "\n";
        *output_ << "  bridge_mode=" << config.bridge_mode << "\n";
        *output_ << "  external_target_user=" << (config.external_target_user.empty() ? "-" : config.external_target_user) << "\n";
        *output_ << "  external_ws_port=" << resolve_configured_external_ws_port(app_) << "\n";
        *output_ << "  embedded_ui_enabled=" << (config.embedded_ui_enabled ? "yes" : "no") << "\n";
        *output_ << "  embedded_ui_fallback_to_browser=" << (config.embedded_ui_fallback_to_browser ? "yes" : "no") << "\n";
        *output_ << "  embedded_ui_devtools=" << (config.embedded_ui_devtools ? "yes" : "no") << "\n";
        *output_ << "  embedded_ui_url=" << config.embedded_ui_url << "\n";
        *output_ << "  embedded_ui_startup_timeout_ms=" << config.embedded_ui_startup_timeout_ms << "\n";
        *output_ << "  bridge_enabled=" << (config.bridge.enabled ? "yes" : "no") << "\n";
        *output_ << "  periodic_tts_enabled=" << (config.periodic_tts.enabled ? "yes" : "no") << "\n";
        *output_ << "  periodic_messages=" << config.periodic_tts.messages.size() << "\n";
        return true;
    }

    if (trimmed == "config save") {
        if (app_ == nullptr) {
            print_command_result(output_, {false, "panel_app_unavailable"});
            return true;
        }

        const auto saved = app_->save_config();
        print_command_result(output_, {saved, saved ? "config_saved" : "config_save_failed"});
        return true;
    }

    if (trimmed == "config reload") {
        if (app_ == nullptr) {
            print_command_result(output_, {false, "panel_app_unavailable"});
            return true;
        }

        const auto reloaded = app_->reload_config();
        print_command_result(output_, {
            reloaded,
            reloaded ? "config reloaded and applied" : "config reload failed",
        });
        return true;
    }

    if (trimmed == "license") {
        if (app_ == nullptr || output_ == nullptr) {
            return true;
        }

        const auto snapshot = app_->snapshot();
        const PanelViewModelBuilder builder;
        const auto view_model = builder.build(snapshot);
        const auto license_section = std::find_if(
            view_model.sections.begin(),
            view_model.sections.end(),
            [](const PanelViewSection& section) {
                return section.title == "License";
            });
        *output_ << "License:\n";
        if (license_section != view_model.sections.end()) {
            for (const auto& item : license_section->items) {
                *output_ << "  " << item.label << "=" << item.value << "\n";
            }
        }
        return true;
    }

    if (trimmed == "ui") {
        if (app_ == nullptr || output_ == nullptr) {
            return true;
        }

        const auto status = app_->http_ui_status();
        *output_ << "UI server:\n";
        *output_ << "  running=" << (status.running ? "yes" : "no") << "\n";
        *output_ << "  port=" << (status.port == 0 ? std::to_string(resolve_default_ui_port(app_)) : std::to_string(status.port)) << "\n";
        *output_ << "  url=" << panel_http_ui_url(resolve_default_ui_port(app_)) << "\n";
        *output_ << "  embedded_enabled=" << (app_->config().embedded_ui_enabled ? "yes" : "no") << "\n";
        *output_ << "  fallback_to_browser=" << (app_->config().embedded_ui_fallback_to_browser ? "yes" : "no") << "\n";
        *output_ << "  devtools=" << (app_->config().embedded_ui_devtools ? "yes" : "no") << "\n";
        *output_ << "  requests=" << status.requests_served << "\n";
        *output_ << "  last_error=" << (status.last_error.empty() ? "-" : status.last_error) << "\n";
        return true;
    }

    if (trimmed == "ui open") {
        if (app_ == nullptr || output_ == nullptr) {
            return true;
        }

        auto status = app_->http_ui_status();
        auto port = resolve_default_ui_port(app_);
        if (!status.running) {
            if (!app_->start_http_ui(port)) {
                *output_ << "error: could not start ui server\n";
                return true;
            }
            status = app_->http_ui_status();
            port = status.port == 0 ? port : status.port;
        }

        const auto opened = open_panel_http_ui_in_browser(port);
        *output_ << (opened ? "ok: ui opened at " : "ok: ui available at ")
                 << panel_http_ui_url(port)
                 << "\n";
        return true;
    }

    if (trimmed == "ui start" || starts_with(trimmed, "ui start ")) {
        if (app_ == nullptr || output_ == nullptr) {
            return true;
        }

        auto port = resolve_default_ui_port(app_);
        if (starts_with(trimmed, "ui start ")) {
            const auto parsed = parse_uint64_value(
                std::string_view(trimmed).substr(std::string_view("ui start ").size()));
            if (!parsed.has_value() || *parsed > 65535) {
                *output_ << "error: invalid ui port\n";
                return true;
            }
            port = static_cast<std::uint16_t>(*parsed);
        }

        if (!app_->start_http_ui(port)) {
            const auto status = app_->http_ui_status();
            *output_ << "error: could not start ui server";
            if (!status.last_error.empty()) {
                *output_ << " (" << status.last_error << ")";
            }
            *output_ << "\n";
            return true;
        }

        *output_ << "ok: ui server started at " << panel_http_ui_url(port) << "\n";
        return true;
    }

    if (trimmed == "ui stop") {
        if (app_ == nullptr || output_ == nullptr) {
            return true;
        }

        app_->stop_http_ui();
        *output_ << "ok: ui server stopped\n";
        return true;
    }

    if (trimmed == "bridge mode") {
        if (app_ == nullptr || output_ == nullptr) {
            return true;
        }

        *output_ << "Bridge mode: " << app_->config().bridge_mode << "\n";
        return true;
    }

    if (trimmed == "bridge mode stub" || trimmed == "bridge mode external") {
        if (app_ == nullptr || output_ == nullptr) {
            return true;
        }

        app_->config().bridge_mode = trimmed == "bridge mode external" ? "external" : "stub";
        *output_ << "ok: bridge mode set to " << app_->config().bridge_mode
                 << " (applies on next start)\n";
        return true;
    }

    if (trimmed == "bridge target") {
        if (app_ == nullptr || output_ == nullptr) {
            return true;
        }

        *output_ << "Bridge target user: "
                 << (app_->config().external_target_user.empty() ? "-" : app_->config().external_target_user)
                 << "\n";
        return true;
    }

    if (starts_with(trimmed, "bridge target ")) {
        if (app_ == nullptr || output_ == nullptr) {
            return true;
        }

        const auto user = trim_copy(
            std::string_view(trimmed).substr(std::string_view("bridge target ").size()));
        if (user.empty()) {
            *output_ << "error: bridge target user required\n";
            return true;
        }

        app_->config().external_target_user = user;
        *output_ << "ok: bridge target user set to " << user << " (save config to persist)\n";
        return true;
    }

    if (trimmed == "bridge external") {
        if (app_ == nullptr || output_ == nullptr) {
            return true;
        }

        const auto manifest = app_->external_bridge_manifest();
        *output_ << "External bridge:\n";
        *output_ << "  external_mode=" << (manifest.external_mode ? "yes" : "no") << "\n";
        *output_ << "  recording=" << (manifest.recording ? "yes" : "no") << "\n";
        *output_ << "  recording_path=" << (manifest.recording_path.empty() ? "-" : manifest.recording_path) << "\n";
        *output_ << "  last_replay_path=" << (manifest.last_replay_path.empty() ? "-" : manifest.last_replay_path) << "\n";
        *output_ << "  last_replay_accepted_events=" << manifest.last_replay_accepted_events << "\n";
        *output_ << "  total_external_events_submitted=" << manifest.total_external_events_submitted << "\n";
        *output_ << "  target_user=" << (manifest.target_user.empty() ? "-" : manifest.target_user) << "\n";
        *output_ << "  connection_state=" << (manifest.connection_state.empty() ? "-" : manifest.connection_state) << "\n";
        *output_ << "  last_status_message="
                 << (manifest.last_status_message.empty() ? "-" : manifest.last_status_message) << "\n";
        *output_ << "  last_status_timestamp_ms="
                 << (manifest.last_status_timestamp_ms == 0
                         ? "-"
                         : std::to_string(manifest.last_status_timestamp_ms))
                 << "\n";
        *output_ << "  current_room_id=" << (manifest.current_room_id.empty() ? "-" : manifest.current_room_id) << "\n";
        *output_ << "  last_event_kind=" << (manifest.last_event_kind.empty() ? "-" : manifest.last_event_kind) << "\n";
        *output_ << "  last_event_actor=" << (manifest.last_event_actor.empty() ? "-" : manifest.last_event_actor) << "\n";
        *output_ << "  last_event_timestamp_ms="
                 << (manifest.last_event_timestamp_ms == 0 ? "-" : std::to_string(manifest.last_event_timestamp_ms))
                 << "\n";
        *output_ << "  event_counts=" << external_counts_text(manifest) << "\n";
        *output_ << "  runner_running=" << (manifest.runner_running ? "yes" : "no") << "\n";
        *output_ << "  runner_pid=" << (manifest.runner_process_id == 0 ? "-" : std::to_string(manifest.runner_process_id)) << "\n";
        *output_ << "  runner_ws=" << (manifest.runner_ws_url.empty() ? "-" : manifest.runner_ws_url) << "\n";
        *output_ << "  runner_exit=" << (manifest.runner_has_exit_code ? std::to_string(manifest.runner_last_exit_code) : "-") << "\n";
        *output_ << "  runner_error=" << (manifest.runner_last_error.empty() ? "-" : manifest.runner_last_error) << "\n";
        return true;
    }

    if (trimmed == "bridge runner" || trimmed == "bridge runner status") {
        if (app_ == nullptr || output_ == nullptr) {
            return true;
        }

        if (!app_->is_external_bridge_mode()) {
            *output_ << "error: bridge external mode is not active\n";
            return true;
        }

        const auto status = app_->external_runner_status();
        *output_ << "Bridge runner:\n";
        *output_ << "  running=" << (status.running ? "yes" : "no") << "\n";
        *output_ << "  pid=" << (status.process_id == 0 ? "-" : std::to_string(status.process_id)) << "\n";
        *output_ << "  target_user=" << (status.target_user.empty() ? "-" : status.target_user) << "\n";
        *output_ << "  ws=" << (status.ws_url.empty() ? "-" : status.ws_url) << "\n";
        *output_ << "  last_exit_code=" << (status.has_exit_code ? std::to_string(status.last_exit_code) : "-") << "\n";
        *output_ << "  last_error=" << (status.last_error.empty() ? "-" : status.last_error) << "\n";
        if (!status.recent_log_lines.empty()) {
            *output_ << "  recent_logs:\n";
            const auto start_index = status.recent_log_lines.size() > 8
                ? status.recent_log_lines.size() - 8
                : 0;
            for (std::size_t index = start_index; index < status.recent_log_lines.size(); ++index) {
                *output_ << "    " << status.recent_log_lines[index] << "\n";
            }
        }
        return true;
    }

    if (trimmed == "bridge runner logs") {
        if (app_ == nullptr || output_ == nullptr) {
            return true;
        }

        if (!app_->is_external_bridge_mode()) {
            *output_ << "error: bridge external mode is not active\n";
            return true;
        }

        const auto status = app_->external_runner_status();
        *output_ << "Bridge runner logs:\n";
        if (status.recent_log_lines.empty()) {
            *output_ << "  -\n";
            return true;
        }

        const auto start_index = status.recent_log_lines.size() > 20
            ? status.recent_log_lines.size() - 20
            : 0;
        for (std::size_t index = start_index; index < status.recent_log_lines.size(); ++index) {
            *output_ << "  " << status.recent_log_lines[index] << "\n";
        }
        return true;
    }

    if (trimmed == "bridge runner start" || starts_with(trimmed, "bridge runner start ")) {
        if (app_ == nullptr || output_ == nullptr) {
            return true;
        }

        if (!app_->is_external_bridge_mode()) {
            *output_ << "error: bridge external mode is not active\n";
            return true;
        }

        std::string target_user = app_->config().external_target_user;
        std::uint64_t max_seconds = 0;
        if (starts_with(trimmed, "bridge runner start ")) {
            const auto payload = trim_copy(
                std::string_view(trimmed).substr(std::string_view("bridge runner start ").size()));
            const auto tokens = split_tokens(payload);
            if (tokens.empty() || tokens.size() > 2) {
                *output_ << "error: invalid bridge runner start arguments\n";
                return true;
            }

            if (tokens.size() == 1) {
                const auto parsed_max_seconds = parse_uint64_value(tokens[0]);
                if (parsed_max_seconds.has_value()) {
                    max_seconds = *parsed_max_seconds;
                } else {
                    target_user = tokens[0];
                }
            } else {
                target_user = tokens[0];
                const auto parsed_max_seconds = parse_uint64_value(tokens[1]);
                if (!parsed_max_seconds.has_value()) {
                    *output_ << "error: invalid bridge runner max_seconds\n";
                    return true;
                }
                max_seconds = *parsed_max_seconds;
            }

            if (!target_user.empty()) {
                app_->config().external_target_user = target_user;
            }
        }

        if (target_user.empty()) {
            *output_ << "error: bridge target user is not configured\n";
            return true;
        }

        if (!app_->start_external_runner(target_user, max_seconds)) {
            const auto status = app_->external_runner_status();
            *output_ << "error: could not start bridge runner";
            if (!status.last_error.empty()) {
                *output_ << " (" << status.last_error << ")";
            }
            *output_ << "\n";
            return true;
        }

        const auto status = app_->external_runner_status();
        *output_ << "ok: bridge runner started for " << target_user << "\n";
        *output_ << "  pid=" << (status.process_id == 0 ? "-" : std::to_string(status.process_id)) << "\n";
        *output_ << "  ws=" << (status.ws_url.empty() ? "-" : status.ws_url) << "\n";
        if (max_seconds > 0) {
            *output_ << "  max_seconds=" << max_seconds << "\n";
        }
        return true;
    }

    if (trimmed == "bridge runner stop") {
        if (app_ == nullptr || output_ == nullptr) {
            return true;
        }

        if (!app_->is_external_bridge_mode()) {
            *output_ << "error: bridge external mode is not active\n";
            return true;
        }

        app_->stop_external_runner();
        const auto status = app_->external_runner_status();
        *output_ << "ok: bridge runner stopped";
        if (status.has_exit_code) {
            *output_ << " (exit=" << status.last_exit_code << ")";
        }
        if (!status.last_error.empty()) {
            *output_ << " error=" << status.last_error;
        }
        *output_ << "\n";
        return true;
    }

    if (trimmed == "bridge ws port") {
        if (app_ == nullptr || output_ == nullptr) {
            return true;
        }

        *output_ << "Bridge WS port: configured=" << resolve_configured_external_ws_port(app_);
        const auto status = app_->external_ws_status();
        if (status.running) {
            *output_ << ", running=" << status.port;
        }
        *output_ << "\n";
        return true;
    }

    if (starts_with(trimmed, "bridge ws port ")) {
        if (app_ == nullptr || output_ == nullptr) {
            return true;
        }

        const auto parsed = parse_uint64_value(
            std::string_view(trimmed).substr(std::string_view("bridge ws port ").size()));
        if (!parsed.has_value() || *parsed == 0 || *parsed > 65535) {
            *output_ << "error: invalid ws port\n";
            return true;
        }

        const auto port = static_cast<std::uint16_t>(*parsed);
        app_->config().external_ws_port = port;
        const auto status = app_->external_ws_status();
        if (app_->is_external_bridge_mode() && status.running) {
            app_->stop_external_ws();
            if (!app_->start_external_ws(port)) {
                *output_ << "error: could not restart bridge ws on port " << port << "\n";
                return true;
            }
            *output_ << "ok: bridge ws port set to " << port << " and restarted\n";
            return true;
        }

        *output_ << "ok: bridge ws port set to " << port << " (save config to persist)\n";
        return true;
    }

    if (trimmed == "bridge ws") {
        if (app_ == nullptr || output_ == nullptr) {
            return true;
        }

        if (!app_->is_external_bridge_mode()) {
            *output_ << "error: bridge external mode is not active\n";
            return true;
        }

        const auto status = app_->external_ws_status();
        *output_ << "Bridge WS:\n";
        *output_ << "  running=" << (status.running ? "yes" : "no") << "\n";
        *output_ << "  port=" << status.port << "\n";
        *output_ << "  accepted_messages=" << status.accepted_messages << "\n";
        *output_ << "  rejected_messages=" << status.rejected_messages << "\n";
        return true;
    }

    if (trimmed == "bridge attach" || starts_with(trimmed, "bridge attach ")) {
        if (app_ == nullptr || output_ == nullptr) {
            return true;
        }

        if (!app_->is_external_bridge_mode()) {
            *output_ << "error: bridge external mode is not active\n";
            return true;
        }

        auto target_user = app_->config().external_target_user;
        if (starts_with(trimmed, "bridge attach ")) {
            target_user = trim_copy(
                std::string_view(trimmed).substr(std::string_view("bridge attach ").size()));
            if (!target_user.empty()) {
                app_->config().external_target_user = target_user;
            }
        }

        if (target_user.empty()) {
            *output_ << "error: bridge target user is not configured\n";
            return true;
        }

        const auto configured_port = resolve_configured_external_ws_port(app_);
        auto ws_status = app_->external_ws_status();
        if (!ws_status.running || ws_status.port != configured_port) {
            if (!app_->start_external_ws(configured_port)) {
                *output_ << "error: could not start bridge ws\n";
                return true;
            }
            ws_status = app_->external_ws_status();
        }

        *output_ << "ok: bridge attach ready for " << target_user << "\n";
        *output_ << "  ws=ws://127.0.0.1:" << ws_status.port << "\n";
        *output_ << "  runner=python tools/bridge_py/run_tiktok_bridge.py --user "
                 << target_user
                 << " --ws ws://127.0.0.1:"
                 << ws_status.port
                 << "\n";
        *output_ << "  persist=config save\n";
        return true;
    }

    if (trimmed == "bridge demo ws") {
        if (app_ == nullptr || output_ == nullptr) {
            return true;
        }

        if (!app_->is_external_bridge_mode()) {
            *output_ << "error: bridge external mode is not active\n";
            return true;
        }

        const auto status = app_->external_ws_status();
        const auto port = status.port == 0 ? resolve_configured_external_ws_port(app_) : status.port;
        const auto target_user = app_->config().external_target_user.empty()
            ? std::string{"alice"}
            : app_->config().external_target_user;
        *output_ << "Bridge WS demo:\n";
        *output_ << "  running=" << (status.running ? "yes" : "no") << "\n";
        *output_ << "  port=" << port << "\n";
        *output_ << "  accepted_messages=" << status.accepted_messages << "\n";
        *output_ << "  rejected_messages=" << status.rejected_messages << "\n";
        *output_ << "  sample_command=python tools/bridge_py/sample_events.py --ws ws://127.0.0.1:" << port << "\n";
        *output_ << "  real_command=python tools/bridge_py/run_tiktok_bridge.py --user " << target_user << " --ws ws://127.0.0.1:" << port << "\n";
        *output_ << "  pump_command=bridge demo ws run 20 1000\n";
        return true;
    }

    if (trimmed == "bridge demo ready") {
        if (app_ == nullptr || output_ == nullptr) {
            return true;
        }

        const auto snapshot = app_->snapshot();
        const auto diagnostics = app_->diagnostics();
        const auto bridge_running =
            snapshot.bridge.state == nlp3::bridge::TikTokBridgeSessionState::running;
        const auto license_active = snapshot.license.status == LicenseStatus::active;
        const auto demo_ready =
            app_->is_external_bridge_mode()
            && snapshot.bridge.integrated
            && bridge_running
            && snapshot.game.has_active_game
            && snapshot.external_ws.running
            && license_active;

        *output_ << "Demo ready: " << (demo_ready ? "yes" : "no") << "\n";
        *output_ << "  bridge_mode=" << snapshot.bridge_mode << "\n";
        *output_ << "  bridge_integrated=" << (snapshot.bridge.integrated ? "yes" : "no") << "\n";
        *output_ << "  bridge_running=" << (bridge_running ? "yes" : "no") << "\n";
        *output_ << "  game_active=" << (snapshot.game.has_active_game ? "yes" : "no");
        if (snapshot.game.has_active_game && !snapshot.game.active_game_id.empty()) {
            *output_ << " (" << snapshot.game.active_game_id << ")";
        }
        *output_ << "\n";
        *output_ << "  ws_running=" << (snapshot.external_ws.running ? "yes" : "no");
        if (snapshot.external_ws.port != 0) {
            *output_ << " (" << snapshot.external_ws.port << ")";
        }
        *output_ << "\n";
        *output_ << "  license_active=" << (license_active ? "yes" : "no") << "\n";
        *output_ << "  diagnostics=" << (diagnostics.ok ? "ok" : "issues") << "\n";

        if (!demo_ready) {
            *output_ << "Hints:\n";
            if (!app_->is_external_bridge_mode()) {
                *output_ << "  set bridge_mode=external and restart the panel\n";
            }
            if (!snapshot.bridge.integrated || !bridge_running) {
                *output_ << "  run: bridge start\n";
            }
            if (!snapshot.game.has_active_game) {
                *output_ << "  run: game activate event-counter\n";
            }
            if (!snapshot.external_ws.running) {
                *output_ << "  run: bridge ws start " << resolve_configured_external_ws_port(app_) << "\n";
            }
            if (!license_active) {
                *output_ << "  review local license status\n";
            }
        }

        return true;
    }

    if (trimmed == "bridge demo live" || starts_with(trimmed, "bridge demo live ")) {
        if (app_ == nullptr || output_ == nullptr) {
            return true;
        }

        if (!app_->is_external_bridge_mode()) {
            *output_ << "error: bridge external mode is not active\n";
            return true;
        }

        auto ws_status = app_->external_ws_status();
        std::uint64_t port = ws_status.port == 0 ? resolve_configured_external_ws_port(app_) : ws_status.port;
        if (starts_with(trimmed, "bridge demo live ")) {
            const auto parsed = parse_uint64_value(
                std::string_view(trimmed).substr(std::string_view("bridge demo live ").size()));
            if (!parsed.has_value() || *parsed > 65535) {
                *output_ << "error: invalid ws port\n";
                return true;
            }
            port = *parsed;
        }

        if (!ws_status.running || ws_status.port != port) {
            if (!app_->start_external_ws(static_cast<std::uint16_t>(port))) {
                *output_ << "error: could not start bridge ws\n";
                return true;
            }
            ws_status = app_->external_ws_status();
        }

        const auto snapshot = app_->snapshot();
        const auto bridge_running =
            snapshot.bridge.state == nlp3::bridge::TikTokBridgeSessionState::running;
        const auto license_active = snapshot.license.status == LicenseStatus::active;
        const auto demo_ready =
            snapshot.bridge.integrated
            && bridge_running
            && snapshot.game.has_active_game
            && ws_status.running
            && license_active;

        *output_ << "ok: bridge live demo ready on port " << ws_status.port << "\n";
        const auto target_user = app_->config().external_target_user.empty()
            ? std::string{"alice"}
            : app_->config().external_target_user;
        *output_ << "  sample: python tools/bridge_py/sample_events.py --ws ws://127.0.0.1:" << ws_status.port << "\n";
        *output_ << "  real: python tools/bridge_py/run_tiktok_bridge.py --user " << target_user << " --ws ws://127.0.0.1:" << ws_status.port << "\n";
        *output_ << "  panel await: bridge demo ws await 1 200 0\n";
        *output_ << "  ready=" << (demo_ready ? "yes" : "no") << "\n";
        *output_ << "  active_game=" << (snapshot.game.has_active_game ? snapshot.game.active_game_id : "-") << "\n";
        *output_ << "  bridge_running=" << (bridge_running ? "yes" : "no") << "\n";
        *output_ << "  license_active=" << (license_active ? "yes" : "no") << "\n";
        return true;
    }

    if (starts_with(trimmed, "bridge demo session ")) {
        if (app_ == nullptr || output_ == nullptr) {
            return true;
        }

        if (!app_->is_external_bridge_mode()) {
            *output_ << "error: bridge external mode is not active\n";
            return true;
        }

        const auto parsed = parse_demo_session_command(
            std::string_view(trimmed).substr(std::string_view("bridge demo session ").size()));
        if (!parsed.has_value()) {
            *output_ << "error: accepted target and max_ticks required\n";
            return true;
        }

        auto ws_status = app_->external_ws_status();
        const auto port = parsed->port != 0
            ? parsed->port
            : static_cast<std::uint64_t>(ws_status.port == 0 ? resolve_configured_external_ws_port(app_) : ws_status.port);
        if (!ws_status.running || ws_status.port != port) {
            if (!app_->start_external_ws(static_cast<std::uint16_t>(port))) {
                *output_ << "error: could not start bridge ws\n";
                return true;
            }
            ws_status = app_->external_ws_status();
        }

        const auto status_before = app_->external_ws_status();
        const auto snapshot_before = app_->snapshot();
        nlp3::platform::PanelRunResult result{};
        std::size_t accepted_delta = 0;
        for (std::uint64_t index = 0; index < parsed->max_ticks; ++index) {
            const auto tick_result = app_->tick(index * parsed->step_ms);
            ++result.ticks_executed;
            result.total_bridge_events_processed += tick_result.bridge_events_processed;
            if (tick_result.periodic_tts_enqueued) {
                ++result.periodic_tts_enqueues;
            }
            result.last_now_ms = tick_result.now_ms;

            const auto status_now = app_->external_ws_status();
            accepted_delta = status_now.accepted_messages - status_before.accepted_messages;
            if (accepted_delta >= parsed->accepted_target) {
                break;
            }
            wait_step_ms(parsed->step_ms);
        }

        const auto status_after = app_->external_ws_status();
        const auto snapshot_after = app_->snapshot();
        *output_ << "ok: bridge demo session: ticks=" << result.ticks_executed
                 << " target_met=" << (accepted_delta >= parsed->accepted_target ? "yes" : "no")
                 << " ws_accepted_delta=" << (status_after.accepted_messages - status_before.accepted_messages)
                 << " ws_rejected_delta=" << (status_after.rejected_messages - status_before.rejected_messages)
                 << " total_events_delta=" << (snapshot_after.total_events - snapshot_before.total_events)
                 << " tts_queue=" << snapshot_after.tts.queued_messages
                 << " active_game=" << (snapshot_after.game.has_active_game ? snapshot_after.game.active_game_id : "-")
                 << "\n";

        const PanelViewModelBuilder builder;
        const auto view_model = builder.build(snapshot_after);
        print_recent_activity_tail(output_, view_model);

        return true;
    }

    if (starts_with(trimmed, "bridge demo observe ")) {
        if (app_ == nullptr || output_ == nullptr) {
            return true;
        }

        if (!app_->is_external_bridge_mode()) {
            *output_ << "error: bridge external mode is not active\n";
            return true;
        }

        const auto parsed = parse_demo_session_command(
            std::string_view(trimmed).substr(std::string_view("bridge demo observe ").size()));
        if (!parsed.has_value()) {
            *output_ << "error: accepted target and max_ticks required\n";
            return true;
        }

        auto ws_status = app_->external_ws_status();
        const auto port = parsed->port != 0
            ? parsed->port
            : static_cast<std::uint64_t>(ws_status.port == 0 ? resolve_configured_external_ws_port(app_) : ws_status.port);
        if (!ws_status.running || ws_status.port != port) {
            if (!app_->start_external_ws(static_cast<std::uint16_t>(port))) {
                *output_ << "error: could not start bridge ws\n";
                return true;
            }
            ws_status = app_->external_ws_status();
        }

        const auto status_before = app_->external_ws_status();
        const auto snapshot_before = app_->snapshot();
        nlp3::platform::PanelRunResult result{};
        std::size_t accepted_delta = 0;
        for (std::uint64_t index = 0; index < parsed->max_ticks; ++index) {
            const auto tick_result = app_->tick(index * parsed->step_ms);
            ++result.ticks_executed;
            result.total_bridge_events_processed += tick_result.bridge_events_processed;
            if (tick_result.periodic_tts_enqueued) {
                ++result.periodic_tts_enqueues;
            }
            result.last_now_ms = tick_result.now_ms;

            const auto status_now = app_->external_ws_status();
            accepted_delta = status_now.accepted_messages - status_before.accepted_messages;
            if (accepted_delta >= parsed->accepted_target) {
                break;
            }
            wait_step_ms(parsed->step_ms);
        }

        const auto status_after = app_->external_ws_status();
        const auto snapshot_after = app_->snapshot();
        const auto diagnostics = app_->diagnostics();
        const PanelViewModelBuilder builder;
        const auto view_model = builder.build(snapshot_after);

        *output_ << "ok: bridge demo observe: ticks=" << result.ticks_executed
                 << " target_met=" << (accepted_delta >= parsed->accepted_target ? "yes" : "no")
                 << " ws_accepted_delta=" << (status_after.accepted_messages - status_before.accepted_messages)
                 << " ws_rejected_delta=" << (status_after.rejected_messages - status_before.rejected_messages)
                 << " total_events_delta=" << (snapshot_after.total_events - snapshot_before.total_events)
                 << "\n";
        *output_ << "  diagnostics=" << (diagnostics.ok ? "ok" : "issues") << "\n";
        *output_ << "  panel_total_events=" << snapshot_after.total_events << "\n";
        *output_ << "  tts_queue=" << snapshot_after.tts.queued_messages << "\n";
        *output_ << "  active_game="
                 << (snapshot_after.game.has_active_game ? snapshot_after.game.active_game_id : "-")
                 << "\n";
        *output_ << "  ws_running=" << (snapshot_after.external_ws.running ? "yes" : "no");
        if (snapshot_after.external_ws.port != 0) {
            *output_ << " (" << snapshot_after.external_ws.port << ")";
        }
        *output_ << "\n";
        print_recent_activity_tail(output_, view_model);

        return true;
    }

    if (trimmed == "bridge ws stop") {
        if (app_ == nullptr || output_ == nullptr) {
            return true;
        }

        if (!app_->is_external_bridge_mode()) {
            *output_ << "error: bridge external mode is not active\n";
            return true;
        }

        app_->stop_external_ws();
        *output_ << "ok: bridge ws stopped\n";
        return true;
    }

    if (trimmed == "bridge demo ws start" || starts_with(trimmed, "bridge demo ws start ")) {
        if (app_ == nullptr || output_ == nullptr) {
            return true;
        }

        if (!app_->is_external_bridge_mode()) {
            *output_ << "error: bridge external mode is not active\n";
            return true;
        }

        std::uint64_t port = resolve_configured_external_ws_port(app_);
        if (starts_with(trimmed, "bridge demo ws start ")) {
            const auto parsed = parse_uint64_value(
                std::string_view(trimmed).substr(std::string_view("bridge demo ws start ").size()));
            if (!parsed.has_value() || *parsed > 65535) {
                *output_ << "error: invalid ws port\n";
                return true;
            }
            port = *parsed;
        }

        if (!app_->start_external_ws(static_cast<std::uint16_t>(port))) {
            *output_ << "error: could not start bridge ws\n";
            return true;
        }

        *output_ << "ok: bridge ws demo ready on port " << port << "\n";
        *output_ << "  sample: python tools/bridge_py/sample_events.py --ws ws://127.0.0.1:" << port << "\n";
        const auto target_user = app_->config().external_target_user.empty()
            ? std::string{"alice"}
            : app_->config().external_target_user;
        *output_ << "  real: python tools/bridge_py/run_tiktok_bridge.py --user " << target_user << " --ws ws://127.0.0.1:" << port << "\n";
        *output_ << "  panel: bridge demo ws run 20 1000\n";
        return true;
    }

    if (trimmed == "bridge ws start" || starts_with(trimmed, "bridge ws start ")) {
        if (app_ == nullptr || output_ == nullptr) {
            return true;
        }

        if (!app_->is_external_bridge_mode()) {
            *output_ << "error: bridge external mode is not active\n";
            return true;
        }

        std::uint64_t port = resolve_configured_external_ws_port(app_);
        if (starts_with(trimmed, "bridge ws start ")) {
            const auto parsed = parse_uint64_value(
                std::string_view(trimmed).substr(std::string_view("bridge ws start ").size()));
            if (!parsed.has_value() || *parsed > 65535) {
                *output_ << "error: invalid ws port\n";
                return true;
            }
            port = *parsed;
        }

        if (!app_->start_external_ws(static_cast<std::uint16_t>(port))) {
            *output_ << "error: could not start bridge ws\n";
            return true;
        }

        *output_ << "ok: bridge ws started on port " << port << "\n";
        return true;
    }

    if (starts_with(trimmed, "bridge demo ws run ")) {
        if (app_ == nullptr || output_ == nullptr) {
            return true;
        }

        if (!app_->is_external_bridge_mode()) {
            *output_ << "error: bridge external mode is not active\n";
            return true;
        }

        const auto status_before = app_->external_ws_status();
        if (!status_before.running) {
            *output_ << "error: bridge ws is not running\n";
            return true;
        }

        const auto parsed = parse_tick_cycle_command(
            std::string_view(trimmed).substr(std::string_view("bridge demo ws run ").size()));
        if (!parsed.has_value()) {
            *output_ << "error: tick count required\n";
            return true;
        }

        const auto snapshot_before = app_->snapshot();
        nlp3::platform::PanelRunResult result{};
        for (std::uint64_t index = 0; index < parsed->ticks; ++index) {
            const auto tick_result = app_->tick(index * parsed->step_ms);
            ++result.ticks_executed;
            result.total_bridge_events_processed += tick_result.bridge_events_processed;
            if (tick_result.periodic_tts_enqueued) {
                ++result.periodic_tts_enqueues;
            }
            result.last_now_ms = tick_result.now_ms;
            if (index + 1 < parsed->ticks) {
                wait_step_ms(parsed->step_ms);
            }
        }
        const auto status_after = app_->external_ws_status();
        const auto snapshot_after = app_->snapshot();

        *output_ << "ok: bridge ws demo run: ticks=" << result.ticks_executed
                 << " processed=" << result.total_bridge_events_processed
                 << " ws_accepted_delta=" << (status_after.accepted_messages - status_before.accepted_messages)
                 << " ws_rejected_delta=" << (status_after.rejected_messages - status_before.rejected_messages)
                 << " total_events_delta=" << (snapshot_after.total_events - snapshot_before.total_events)
                 << "\n";
        return true;
    }

    if (starts_with(trimmed, "bridge demo ws await ")) {
        if (app_ == nullptr || output_ == nullptr) {
            return true;
        }

        if (!app_->is_external_bridge_mode()) {
            *output_ << "error: bridge external mode is not active\n";
            return true;
        }

        const auto status_before = app_->external_ws_status();
        if (!status_before.running) {
            *output_ << "error: bridge ws is not running\n";
            return true;
        }

        const auto parsed = parse_ws_await_command(
            std::string_view(trimmed).substr(std::string_view("bridge demo ws await ").size()));
        if (!parsed.has_value()) {
            *output_ << "error: accepted target and max_ticks required\n";
            return true;
        }

        const auto snapshot_before = app_->snapshot();
        nlp3::platform::PanelRunResult result{};
        std::size_t accepted_delta = 0;
        for (std::uint64_t index = 0; index < parsed->max_ticks; ++index) {
            const auto tick_result = app_->tick(index * parsed->step_ms);
            ++result.ticks_executed;
            result.total_bridge_events_processed += tick_result.bridge_events_processed;
            if (tick_result.periodic_tts_enqueued) {
                ++result.periodic_tts_enqueues;
            }
            result.last_now_ms = tick_result.now_ms;

            const auto status_now = app_->external_ws_status();
            accepted_delta = status_now.accepted_messages - status_before.accepted_messages;
            if (accepted_delta >= parsed->accepted_target) {
                break;
            }
            wait_step_ms(parsed->step_ms);
        }

        const auto status_after = app_->external_ws_status();
        const auto snapshot_after = app_->snapshot();
        *output_ << "ok: bridge ws demo await: ticks=" << result.ticks_executed
                 << " target_met=" << (accepted_delta >= parsed->accepted_target ? "yes" : "no")
                 << " ws_accepted_delta=" << (status_after.accepted_messages - status_before.accepted_messages)
                 << " ws_rejected_delta=" << (status_after.rejected_messages - status_before.rejected_messages)
                 << " total_events_delta=" << (snapshot_after.total_events - snapshot_before.total_events)
                 << "\n";
        return true;
    }

    if (starts_with(trimmed, "bridge inject chat ")) {
        if (app_ == nullptr || output_ == nullptr) {
            return true;
        }

        if (!app_->is_external_bridge_mode()) {
            *output_ << "error: bridge external mode is not active\n";
            return true;
        }

        const auto payload = trim_copy(
            std::string_view(trimmed).substr(std::string_view("bridge inject chat ").size()));
        const auto separator = payload.find(' ');
        if (separator == std::string::npos) {
            *output_ << "error: actor and message required\n";
            return true;
        }

        const auto actor = trim_copy(std::string_view(payload).substr(0, separator));
        const auto message = trim_copy(std::string_view(payload).substr(separator + 1));
        if (actor.empty() || message.empty()) {
            *output_ << "error: actor and message required\n";
            return true;
        }

        const bridge::TikTokRawEvent raw_event{
            bridge::TikTokRawEventKind::chat,
            bridge::TikTokRawActor{
                actor,
                actor,
                actor,
                "",
            },
            bridge::TikTokRawMetadata{
                "console-chat",
                "local-room",
                "comment",
                0,
            },
            message,
            std::nullopt,
            0,
        };

        if (!app_->submit_external_bridge_event(raw_event)) {
            *output_ << "error: bridge external session is unavailable\n";
            return true;
        }

        const auto processed = app_->tick_bridge(1);
        *output_ << "ok: bridge chat injected";
        if (processed > 0) {
            *output_ << " and processed";
        }
        *output_ << "\n";
        return true;
    }

    if (starts_with(trimmed, "bridge inject json ")) {
        if (app_ == nullptr || output_ == nullptr) {
            return true;
        }

        if (!app_->is_external_bridge_mode()) {
            *output_ << "error: bridge external mode is not active\n";
            return true;
        }

        const auto payload = trim_copy(
            std::string_view(trimmed).substr(std::string_view("bridge inject json ").size()));
        if (payload.empty()) {
            *output_ << "error: invalid external event payload\n";
            return true;
        }

        const bridge::TikTokExternalEventCodec codec{};
        const auto decoded = codec.decode_json(payload);
        if (!decoded.has_value()) {
            *output_ << "error: invalid external event payload\n";
            return true;
        }

        if (!app_->submit_external_bridge_event(*decoded)) {
            *output_ << "error: bridge external session is unavailable\n";
            return true;
        }

        const auto processed = app_->tick_bridge(1);
        *output_ << "ok: bridge json event injected";
        if (processed > 0) {
            *output_ << " and processed";
        }
        *output_ << "\n";
        return true;
    }

    if (starts_with(trimmed, "bridge inbox watch ")
        || starts_with(trimmed, "bridge demo inbox ")) {
        if (app_ == nullptr || output_ == nullptr) {
            return true;
        }

        if (!app_->is_external_bridge_mode()) {
            *output_ << "error: bridge external mode is not active\n";
            return true;
        }

        const auto command_prefix = starts_with(trimmed, "bridge demo inbox ")
            ? std::string_view("bridge demo inbox ")
            : std::string_view("bridge inbox watch ");
        const auto parsed_command = parse_inbox_cycle_command(
            std::string_view(trimmed).substr(command_prefix.size()));
        if (!parsed_command.has_value()) {
            *output_ << "error: inbox path and tick count required\n";
            return true;
        }

        nlp3::bridge::TikTokExternalInboxResult total_result{};
        for (std::uint64_t index = 0; index < parsed_command->ticks; ++index) {
            const auto cycle_result = app_->process_external_inbox_and_tick(
                parsed_command->path,
                index * parsed_command->step_ms);
            total_result.files_seen += cycle_result.files_seen;
            total_result.files_processed += cycle_result.files_processed;
            total_result.files_failed += cycle_result.files_failed;
            total_result.events_accepted += cycle_result.events_accepted;
        }

        *output_ << "ok: "
                 << (starts_with(trimmed, "bridge demo inbox ")
                        ? "demo inbox processed: cycles="
                        : "inbox watch processed: cycles=")
                 << parsed_command->ticks
                 << " seen="
                 << total_result.files_seen
                 << " processed="
                 << total_result.files_processed
                 << " failed="
                 << total_result.files_failed
                 << " accepted="
                 << total_result.events_accepted
                 << "\n";
        return true;
    }

    if (starts_with(trimmed, "bridge inbox ")) {
        if (app_ == nullptr || output_ == nullptr) {
            return true;
        }

        if (!app_->is_external_bridge_mode()) {
            *output_ << "error: bridge external mode is not active\n";
            return true;
        }

        const auto path = trim_copy(
            std::string_view(trimmed).substr(std::string_view("bridge inbox ").size()));
        if (path.empty()) {
            *output_ << "error: inbox path required\n";
            return true;
        }

        const auto result = app_->process_external_inbox(path);
        *output_ << "ok: inbox processed: seen="
                 << result.files_seen
                 << " processed="
                 << result.files_processed
                 << " failed="
                 << result.files_failed
                 << " accepted="
                 << result.events_accepted
                 << "\n";
        return true;
    }

    if (starts_with(trimmed, "bridge record chat ")) {
        if (app_ == nullptr || output_ == nullptr) {
            return true;
        }

        if (!app_->is_external_bridge_mode()) {
            *output_ << "error: bridge external mode is not active\n";
            return true;
        }

        const auto payload = trim_copy(
            std::string_view(trimmed).substr(std::string_view("bridge record chat ").size()));
        const auto path_separator = payload.find(' ');
        if (path_separator == std::string::npos) {
            *output_ << "error: path, actor and message required\n";
            return true;
        }

        const auto path = trim_copy(std::string_view(payload).substr(0, path_separator));
        const auto remainder = trim_copy(std::string_view(payload).substr(path_separator + 1));
        const auto actor_separator = remainder.find(' ');
        if (path.empty() || actor_separator == std::string::npos) {
            *output_ << "error: path, actor and message required\n";
            return true;
        }

        const auto actor = trim_copy(std::string_view(remainder).substr(0, actor_separator));
        const auto message = trim_copy(std::string_view(remainder).substr(actor_separator + 1));
        if (actor.empty() || message.empty()) {
            *output_ << "error: path, actor and message required\n";
            return true;
        }

        const bridge::TikTokRawEvent raw_event{
            bridge::TikTokRawEventKind::chat,
            bridge::TikTokRawActor{
                actor,
                actor,
                actor,
                "",
            },
            bridge::TikTokRawMetadata{
                "console-record-chat",
                "local-room",
                "comment",
                0,
            },
            message,
            std::nullopt,
            0,
        };

        if (!app_->record_external_bridge_event(raw_event, path)) {
            *output_ << "error: could not record external event\n";
            return true;
        }

        *output_ << "ok: bridge chat recorded\n";
        return true;
    }

    if (starts_with(trimmed, "bridge record start ")) {
        if (app_ == nullptr || output_ == nullptr) {
            return true;
        }

        if (!app_->is_external_bridge_mode()) {
            *output_ << "error: bridge external mode is not active\n";
            return true;
        }

        const auto path = trim_copy(
            std::string_view(trimmed).substr(std::string_view("bridge record start ").size()));
        if (!app_->start_external_bridge_recording(path)) {
            *output_ << "error: could not start bridge recording\n";
            return true;
        }

        *output_ << "ok: bridge recording started\n";
        return true;
    }

    if (trimmed == "bridge record stop") {
        if (app_ == nullptr || output_ == nullptr) {
            return true;
        }

        app_->stop_external_bridge_recording();
        *output_ << "ok: bridge recording stopped\n";
        return true;
    }

    if (starts_with(trimmed, "bridge replay file ")) {
        if (app_ == nullptr || output_ == nullptr) {
            return true;
        }

        if (!app_->is_external_bridge_mode()) {
            *output_ << "error: bridge external mode is not active\n";
            return true;
        }

        const auto path = trim_copy(
            std::string_view(trimmed).substr(std::string_view("bridge replay file ").size()));
        if (path.empty()) {
            *output_ << "error: could not replay file\n";
            return true;
        }

        const auto replayed = app_->replay_external_bridge_file(path);
        if (replayed == 0) {
            *output_ << "error: could not replay file\n";
            return true;
        }

        *output_ << "ok: replayed " << replayed << " external events\n";
        return true;
    }

    if (starts_with(trimmed, "bridge replay ")) {
        if (app_ == nullptr || output_ == nullptr) {
            return true;
        }

        if (!app_->is_external_bridge_mode()) {
            *output_ << "error: bridge external mode is not active\n";
            return true;
        }

        const auto path = trim_copy(
            std::string_view(trimmed).substr(std::string_view("bridge replay ").size()));
        if (path.empty()) {
            *output_ << "error: could not replay file\n";
            return true;
        }

        const auto replayed = app_->replay_external_bridge_file(path);
        if (replayed == 0) {
            *output_ << "error: could not replay file\n";
            return true;
        }

        *output_ << "ok: replayed " << replayed << " external events\n";
        return true;
    }

    if (trimmed == "bridge start") {
        print_command_result(output_, app_ != nullptr
            ? app_->execute_command({PanelCommandKind::bridge_start, {}})
            : PanelCommandResult{false, "panel_app_unavailable"});
        return true;
    }

    if (trimmed == "bridge stop") {
        print_command_result(output_, app_ != nullptr
            ? app_->execute_command({PanelCommandKind::bridge_stop, {}})
            : PanelCommandResult{false, "panel_app_unavailable"});
        return true;
    }

    if (trimmed == "bridge reset") {
        print_command_result(output_, app_ != nullptr
            ? app_->execute_command({PanelCommandKind::bridge_reset, {}})
            : PanelCommandResult{false, "panel_app_unavailable"});
        return true;
    }

    if (starts_with(trimmed, "game activate ")) {
        const auto argument = trim_copy(std::string_view(trimmed).substr(std::string_view("game activate ").size()));
        print_command_result(output_, app_ != nullptr
            ? app_->execute_command({PanelCommandKind::game_activate, argument})
            : PanelCommandResult{false, "panel_app_unavailable"});
        return true;
    }

    if (trimmed == "game deactivate") {
        print_command_result(output_, app_ != nullptr
            ? app_->execute_command({PanelCommandKind::game_deactivate, {}})
            : PanelCommandResult{false, "panel_app_unavailable"});
        return true;
    }

    if (trimmed == "game restart") {
        print_command_result(output_, app_ != nullptr
            ? app_->execute_command({PanelCommandKind::game_restart, {}})
            : PanelCommandResult{false, "panel_app_unavailable"});
        return true;
    }

    if (starts_with(trimmed, "tts say ")) {
        const auto argument = trim_copy(std::string_view(trimmed).substr(std::string_view("tts say ").size()));
        print_command_result(output_, app_ != nullptr
            ? app_->execute_command({PanelCommandKind::tts_enqueue_announcement, argument})
            : PanelCommandResult{false, "panel_app_unavailable"});
        return true;
    }

    return false;
}

} // namespace nlp3::platform
