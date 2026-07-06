#include "platform/panel_http_server.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>



#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "events/host_event.hpp"
#include "platform/panel_app.hpp"
#include "platform/panel_console.hpp"
#include "platform/panel_http_json.hpp"
#include "platform/overlay_assets.hpp"
#include "platform/panel_ui_assets.hpp"
#include "platform/server_license_service.hpp"
#include "platform/support_bundle_exporter.hpp"

namespace {

#ifdef _WIN32

using nlp3::events::GiftEventData;
using nlp3::events::HostActor;
using nlp3::events::HostEvent;
using nlp3::events::HostEventKind;
using nlp3::gamesdk::GameConfig;
using nlp3::gamesdk::GameConfigValue;
using nlp3::platform::PanelApp;
using nlp3::platform::PanelAuthLoginResult;
using nlp3::platform::PanelConsole;
using nlp3::platform::PanelHttpServerStatus;

// forward declarations
std::string handle_game_trigger_rest(PanelApp* app, std::string_view body, HostEvent& event);

bool ensure_winsock_initialized() {
    static const bool initialized = []() {
        WSADATA wsa_data{};
        return WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0;
    }();
    return initialized;
}

void close_socket(void*& socket_handle) {
    if (socket_handle != nullptr) {
        closesocket(reinterpret_cast<SOCKET>(socket_handle));
        socket_handle = nullptr;
    }
}

std::string to_lower_copy(std::string_view value) {
    std::string lowered(value);
    std::transform(
        lowered.begin(),
        lowered.end(),
        lowered.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return lowered;
}

std::string trim_copy(std::string_view value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) {
        return {};
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(begin, end - begin + 1));
}

std::uint64_t now_wall_clock_ms() {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

struct ParsedRequest {
    std::string method{};
    std::string path{};
    std::string body{};
    std::vector<std::pair<std::string, std::string>> headers{};
    bool ready = false;
};

ParsedRequest parse_request_buffer(std::string_view request_buffer) {
    ParsedRequest parsed{};
    const auto header_end = request_buffer.find("\r\n\r\n");
    if (header_end == std::string_view::npos) {
        return parsed;
    }

    std::size_t content_length = 0;
    const auto header_block = request_buffer.substr(0, header_end);
    const auto request_line_end = header_block.find("\r\n");
    const auto request_line = request_line_end == std::string_view::npos
        ? header_block
        : header_block.substr(0, request_line_end);

    std::istringstream request_line_stream{std::string(request_line)};
    request_line_stream >> parsed.method >> parsed.path;
    if (parsed.method.empty() || parsed.path.empty()) {
        return parsed;
    }
    // Strip query string from path so route exact-matches work
    const auto qmark = parsed.path.find('?');
    if (qmark != std::string::npos) {
        parsed.path = parsed.path.substr(0, qmark);
    }

    std::size_t cursor = request_line_end == std::string_view::npos ? header_block.size() : request_line_end + 2;
    while (cursor < header_block.size()) {
        const auto line_end = header_block.find("\r\n", cursor);
        const auto line = header_block.substr(
            cursor,
            line_end == std::string_view::npos ? header_block.size() - cursor : line_end - cursor);
        const auto separator = line.find(':');
        if (separator != std::string_view::npos) {
            const auto header_name = to_lower_copy(trim_copy(line.substr(0, separator)));
            const auto header_value = trim_copy(line.substr(separator + 1));
            parsed.headers.emplace_back(header_name, header_value);
            if (header_name == "content-length") {
                try {
                    content_length = static_cast<std::size_t>(std::stoull(header_value));
                } catch (...) {
                    content_length = 0;
                }
            }
        }

        if (line_end == std::string_view::npos) {
            break;
        }
        cursor = line_end + 2;
    }

    const auto body_offset = header_end + 4;
    if (request_buffer.size() < body_offset + content_length) {
        return parsed;
    }

    parsed.body = std::string(request_buffer.substr(body_offset, content_length));
    parsed.ready = true;
    return parsed;
}

std::string request_header(const ParsedRequest& request, std::string_view name) {
    const auto lowered_name = to_lower_copy(name);
    for (const auto& [header_name, header_value] : request.headers) {
        if (header_name == lowered_name) {
            return header_value;
        }
    }
    return {};
}

std::string make_http_response(
    std::string_view status_line,
    std::string_view content_type,
    std::string body) {
    std::ostringstream output;
    output << "HTTP/1.1 " << status_line << "\r\n"
           << "Content-Type: " << content_type << "\r\n"
           << "Content-Length: " << body.size() << "\r\n"
           << "Connection: close\r\n"
           << "Cache-Control: no-store\r\n"
           << "\r\n"
           << body;
    return output.str();
}

std::string json_escape(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (const auto ch : value) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }
    return escaped;
}

std::string json_quote(std::string_view value) {
    return "\"" + json_escape(value) + "\"";
}

std::size_t skip_json_ws(std::string_view body, std::size_t cursor) {
    while (cursor < body.size() && std::isspace(static_cast<unsigned char>(body[cursor])) != 0) {
        ++cursor;
    }
    return cursor;
}

std::optional<std::size_t> find_json_key(std::string_view body, std::string_view key) {
    const auto needle = "\"" + std::string(key) + "\"";
    const auto key_position = body.find(needle);
    if (key_position == std::string_view::npos) {
        return std::nullopt;
    }

    auto cursor = key_position + needle.size();
    cursor = skip_json_ws(body, cursor);
    if (cursor >= body.size() || body[cursor] != ':') {
        return std::nullopt;
    }
    ++cursor;
    cursor = skip_json_ws(body, cursor);
    return cursor;
}

std::optional<std::string> parse_json_string(std::string_view body, std::string_view key) {
    const auto value_cursor = find_json_key(body, key);
    if (!value_cursor.has_value() || *value_cursor >= body.size() || body[*value_cursor] != '"') {
        return std::nullopt;
    }

    std::string output;
    for (std::size_t cursor = *value_cursor + 1; cursor < body.size(); ++cursor) {
        const auto ch = body[cursor];
        if (ch == '\\') {
            if (cursor + 1 >= body.size()) {
                return std::nullopt;
            }
            const auto escaped = body[++cursor];
            switch (escaped) {
            case '\\':
                output.push_back('\\');
                break;
            case '"':
                output.push_back('"');
                break;
            case 'n':
                output.push_back('\n');
                break;
            case 'r':
                output.push_back('\r');
                break;
            case 't':
                output.push_back('\t');
                break;
            default:
                output.push_back(escaped);
                break;
            }
            continue;
        }

        if (ch == '"') {
            return output;
        }

        output.push_back(ch);
    }

    return std::nullopt;
}

std::optional<bool> parse_json_bool(std::string_view body, std::string_view key) {
    const auto value_cursor = find_json_key(body, key);
    if (!value_cursor.has_value()) {
        return std::nullopt;
    }
    if (body.substr(*value_cursor, 4) == "true") {
        return true;
    }
    if (body.substr(*value_cursor, 5) == "false") {
        return false;
    }
    return std::nullopt;
}

std::optional<double> parse_json_double(std::string_view body, std::string_view key) {
    const auto value_cursor = find_json_key(body, key);
    if (!value_cursor.has_value()) {
        return std::nullopt;
    }

    const auto cursor = *value_cursor;
    std::size_t end = cursor;
    bool has_dot = false;
    bool has_digit = false;
    if (end < body.size() && body[end] == '-') {
        ++end;
    }
    while (end < body.size()) {
        const auto ch = body[end];
        if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
            has_digit = true;
            ++end;
        } else if (ch == '.' && !has_dot) {
            has_dot = true;
            ++end;
        } else {
            break;
        }
    }
    if (!has_digit) {
        return std::nullopt;
    }

    try {
        return std::stod(std::string(body.substr(cursor, end - cursor)));
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::uint64_t> parse_json_uint64(std::string_view body, std::string_view key) {
    const auto value_cursor = find_json_key(body, key);
    if (!value_cursor.has_value()) {
        return std::nullopt;
    }

    const auto cursor = *value_cursor;
    std::size_t end = cursor;
    while (end < body.size() && std::isdigit(static_cast<unsigned char>(body[end])) != 0) {
        ++end;
    }
    if (end == cursor) {
        return std::nullopt;
    }

    try {
        return static_cast<std::uint64_t>(std::stoull(std::string(body.substr(cursor, end - cursor))));
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::vector<std::string>> parse_json_string_array(std::string_view body, std::string_view key) {
    const auto value_cursor = find_json_key(body, key);
    if (!value_cursor.has_value()) {
        return std::nullopt;
    }

    auto cursor = skip_json_ws(body, *value_cursor);
    if (cursor >= body.size() || body[cursor] != '[') {
        return std::nullopt;
    }
    ++cursor;

    std::vector<std::string> values;
    while (cursor < body.size()) {
        cursor = skip_json_ws(body, cursor);
        if (cursor >= body.size()) {
            return std::nullopt;
        }
        if (body[cursor] == ']') {
            return values;
        }
        if (body[cursor] != '"') {
            return std::nullopt;
        }

        std::string current;
        ++cursor;
        while (cursor < body.size()) {
            const auto ch = body[cursor++];
            if (ch == '\\') {
                if (cursor >= body.size()) {
                    return std::nullopt;
                }
                const auto escaped = body[cursor++];
                switch (escaped) {
                case '\\': current.push_back('\\'); break;
                case '"': current.push_back('"'); break;
                case 'n': current.push_back('\n'); break;
                case 'r': current.push_back('\r'); break;
                case 't': current.push_back('\t'); break;
                default: current.push_back(escaped); break;
                }
                continue;
            }
            if (ch == '"') {
                break;
            }
            current.push_back(ch);
        }

        values.push_back(std::move(current));
        cursor = skip_json_ws(body, cursor);
        if (cursor >= body.size()) {
            return std::nullopt;
        }
        if (body[cursor] == ',') {
            ++cursor;
            continue;
        }
        if (body[cursor] == ']') {
            return values;
        }
        return std::nullopt;
    }

    return std::nullopt;
}

std::string bool_json(bool value) {
    return value ? "true" : "false";
}

std::string make_simple_result(bool ok, std::string_view message) {
    return "{"
        "\"ok\":" + bool_json(ok) + ","
        "\"message\":" + json_quote(message)
        + "}";
}

std::string license_status_text(nlp3::platform::LicenseStatus status) {
    switch (status) {
    case nlp3::platform::LicenseStatus::active:
        return "active";
    case nlp3::platform::LicenseStatus::inactive:
        return "inactive";
    case nlp3::platform::LicenseStatus::unknown:
        break;
    }
    return "unknown";
}

std::string make_auth_login_result(const PanelAuthLoginResult& result) {
    return "{"
        "\"ok\":" + bool_json(result.ok) + ","
        "\"message\":" + json_quote(result.message) + ","
        "\"errorCode\":" + json_quote(result.error_code) + ","
        "\"license\":{"
        "\"status\":" + json_quote(license_status_text(result.license.status)) + ","
        "\"tier\":" + json_quote(result.license.tier) + ","
        "\"message\":" + json_quote(result.license.message)
        + "},"
        "\"auth\":{"
        "\"required\":" + bool_json(result.auth.required) + ","
        "\"authenticated\":" + bool_json(result.auth.authenticated) + ","
        "\"email\":" + json_quote(result.auth.email) + ","
        "\"firebaseUid\":" + json_quote(result.auth.firebase_uid) + ","
        "\"licenseKey\":" + json_quote(result.auth.license_key) + ","
        "\"message\":" + json_quote(result.auth.message) + ","
        "\"lastErrorCode\":" + json_quote(result.auth.last_error_code) + ","
        "\"lastValidatedTimestampMs\":" + std::to_string(result.auth.last_validated_timestamp_ms)
        + "},"
        "\"remoteCatalogError\":" + json_quote(result.remote_catalog_error) + ","
        "\"deviceActivationError\":" + json_quote(result.device_activation_error) + ","
        "\"registeredDeviceId\":" + json_quote(result.registered_device_id) + ","
        "\"registeredDeviceName\":" + json_quote(result.registered_device_name)
        + "}";
}

std::string make_auth_required_result() {
    return std::string{"{"
        "\"ok\":false,"
        "\"message\":\"auth_required\","
        "\"errorCode\":\"auth_required\""
        "}"}; 
}

std::string make_origin_forbidden_result() {
    return std::string{"{"
        "\"ok\":false,"
        "\"message\":\"origin_not_allowed\","
        "\"errorCode\":\"origin_not_allowed\""
        "}"};
}

bool is_allowed_loopback_origin(std::string_view origin, std::uint16_t port) {
    const auto normalized = to_lower_copy(trim_copy(origin));
    constexpr std::string_view prefix = "http://";
    if (normalized.rfind(prefix, 0) != 0) {
        return false;
    }

    auto authority = std::string_view(normalized).substr(prefix.size());
    const auto path_start = authority.find('/');
    if (path_start != std::string_view::npos) {
        authority = authority.substr(0, path_start);
    }

    const auto port_separator = authority.rfind(':');
    if (port_separator == std::string_view::npos) {
        return false;
    }

    const auto host = authority.substr(0, port_separator);
    const auto port_text = authority.substr(port_separator + 1);
    if (host != "127.0.0.1" && host != "localhost") {
        return false;
    }

    try {
        return std::stoul(std::string(port_text)) == port;
    } catch (...) {
        return false;
    }
}

bool request_origin_allowed(const ParsedRequest& request, const PanelHttpServerStatus& status) {
    if (request.method != "POST") {
        return true;
    }

    const auto origin = request_header(request, "origin");
    if (origin.empty()) {
        return true;
    }

    return is_allowed_loopback_origin(origin, status.port);
}

bool request_requires_access(const ParsedRequest& request) {
    return request.method == "POST"
        && request.path != "/api/auth/login"
        && request.path != "/api/auth/logout"
        && request.path != "/api/support/export";
}

std::string make_support_export_result(const nlp3::platform::SupportBundleExportResult& result) {
    return "{"
        "\"ok\":" + bool_json(result.ok) + ","
        "\"message\":" + json_quote(result.message) + ","
        "\"path\":" + json_quote(result.bundle_path) + ","
        "\"exportedAtMs\":" + std::to_string(result.exported_at_ms) + ","
        "\"includedLogs\":" + std::to_string(result.included_logs)
        + "}";
}

std::string normalize_energy_level(std::string_view raw) {
    const auto lowered = to_lower_copy(trim_copy(raw));
    if (lowered == "calm" || lowered == "balanced" || lowered == "hype") {
        return lowered;
    }
    return "balanced";
}

std::string normalize_tone_style(std::string_view raw) {
    const auto lowered = to_lower_copy(trim_copy(raw));
    if (lowered == "warm" || lowered == "neutral" || lowered == "electric") {
        return lowered;
    }
    return "neutral";
}

std::string gift_template_for(std::string_view energy, std::string_view tone) {
    if (energy == "calm" && tone == "warm") {
        return "Gracias por tu regalo, lo valoramos mucho";
    }
    if (energy == "hype" && tone == "electric") {
        return "Tremendo regalo, vamos con todo";
    }
    if (energy == "hype") {
        return "Gracias por el regalo, subimos la energia";
    }
    if (tone == "warm") {
        return "Gracias por tu apoyo con ese regalo";
    }
    return "Gracias por el regalo";
}

std::string follow_template_for(std::string_view energy, std::string_view tone) {
    if (energy == "calm" && tone == "warm") {
        return "Gracias por quedarte con nosotros";
    }
    if (energy == "hype" && tone == "electric") {
        return "Se suma una estrella mas a la sala";
    }
    if (energy == "hype") {
        return "Gracias por seguir la cuenta, seguimos arriba";
    }
    if (tone == "warm") {
        return "Gracias por seguir y acompaniar este directo";
    }
    return "Gracias por seguir la cuenta";
}

std::string like_template_for(std::string_view energy, std::string_view tone) {
    if (tone == "electric" || energy == "hype") {
        return "Gracias {user} por subir el live con {count} likes";
    }
    if (tone == "warm" || energy == "calm") {
        return "Gracias {user} por regalar {count} likes al directo";
    }
    return "Gracias {user} por enviar {count} likes";
}

std::string share_template_for(std::string_view energy, std::string_view tone) {
    if (energy == "calm" && tone == "warm") {
        return "Gracias por compartir este momento";
    }
    if (energy == "hype" && tone == "electric") {
        return "Gracias por compartir el live, seguimos creciendo";
    }
    if (energy == "hype") {
        return "Gracias por compartir el directo";
    }
    if (tone == "warm") {
        return "Gracias por compartir la transmision";
    }
    return "Gracias por compartir el directo";
}

std::string subscriber_template_for(std::string_view energy, std::string_view tone) {
    if (energy == "calm" && tone == "warm") {
        return "Gracias por suscribirte y acompanar este directo";
    }
    if (energy == "hype" && tone == "electric") {
        return "Nueva suscripcion en el live, muchisimas gracias";
    }
    if (energy == "hype") {
        return "Gracias por suscribirte, seguimos con toda la energia";
    }
    if (tone == "warm") {
        return "Gracias por tu suscripcion y por estar aqui";
    }
    return "Gracias por suscribirte";
}

std::vector<std::string> periodic_messages_for(std::string_view energy, std::string_view tone) {
    if (energy == "calm") {
        return {
            "Gracias por estar en el directo.",
            "Disfruta la sesion y comparte tu energia.",
        };
    }
    if (energy == "hype" && tone == "electric") {
        return {
            "Seguimos arriba en el live.",
            "Si te esta gustando, deja tu reaccion en el chat.",
        };
    }
    return {
        "Gracias por acompanarnos en vivo.",
        "Comparte tu mensaje y sigue la partida en tiempo real.",
    };
}

void sync_host_persona(
    PanelApp* app,
    bool replace_periodic_messages,
    bool replace_automation_templates) {
    auto& config = app->config();
    if (replace_automation_templates || config.automation.gift_thanks_template.empty()) {
        config.automation.gift_thanks_template =
            gift_template_for(config.host_energy_level, config.host_tone_style);
    }
    if (replace_automation_templates || config.automation.follow_thanks_template.empty()) {
        config.automation.follow_thanks_template =
            follow_template_for(config.host_energy_level, config.host_tone_style);
    }
    if (replace_automation_templates || config.automation.like_thanks_template.empty()) {
        config.automation.like_thanks_template =
            like_template_for(config.host_energy_level, config.host_tone_style);
    }
    if (replace_automation_templates || config.automation.share_thanks_template.empty()) {
        config.automation.share_thanks_template =
            share_template_for(config.host_energy_level, config.host_tone_style);
    }
    if (replace_automation_templates || config.automation.subscriber_thanks_template.empty()) {
        config.automation.subscriber_thanks_template =
            subscriber_template_for(config.host_energy_level, config.host_tone_style);
    }
    if (replace_periodic_messages || config.periodic_tts.messages.empty()) {
        config.periodic_tts.messages = periodic_messages_for(config.host_energy_level, config.host_tone_style);
    }
}

HostEventKind parse_host_event_kind(std::string_view raw_kind) {
    const auto kind = to_lower_copy(trim_copy(raw_kind));
    if (kind == "chat" || kind == "chat_message") {
        return HostEventKind::chat_message;
    }
    if (kind == "like") {
        return HostEventKind::like;
    }
    if (kind == "gift") {
        return HostEventKind::gift;
    }
    if (kind == "follow") {
        return HostEventKind::follow;
    }
    if (kind == "share") {
        return HostEventKind::share;
    }
    if (kind == "viewer_join" || kind == "join") {
        return HostEventKind::viewer_join;
    }
    if (kind == "viewer_count") {
        return HostEventKind::viewer_count;
    }
    if (kind == "live_start") {
        return HostEventKind::live_start;
    }
    if (kind == "live_end") {
        return HostEventKind::live_end;
    }
    if (kind == "moderation") {
        return HostEventKind::moderation;
    }
    return HostEventKind::custom_raw;
}

std::string handle_command(PanelApp* app, std::string_view body) {
    if (app == nullptr) {
        return nlp3::platform::build_panel_http_error_json("panel unavailable");
    }

    std::istringstream input;
    std::ostringstream output;
    PanelConsole console{app, &input, &output};
    const auto command = trim_copy(body);
    const auto recognized = !command.empty() && console.execute_line(command);
    return nlp3::platform::build_panel_http_command_json(recognized, output.str());
}

std::string handle_game_start(PanelApp* app, std::string_view body) {
    if (app == nullptr) {
        return nlp3::platform::build_panel_http_error_json("panel unavailable");
    }

    const auto requested_game_id = parse_json_string(body, "gameId").value_or("");
    const auto snapshot = app->snapshot();
    const auto target_game_id = !requested_game_id.empty()
        ? requested_game_id
        : (!snapshot.game.active_game_id.empty() ? snapshot.game.active_game_id : app->config().default_game_id);

    bool ok = false;
    std::string message = "game_start_failed";
    if (snapshot.game.runtime_state == nlp3::gamesdk::GameRuntimeState::paused
        && (requested_game_id.empty() || requested_game_id == snapshot.game.active_game_id)) {
        ok = app->resume_active_game();
        message = ok ? "game_resumed" : "game_resume_failed";
    } else {
        ok = app->activate_game_by_id(target_game_id);
        message = ok ? "game_started" : "game_start_failed";
    }

    return make_simple_result(ok, message);
}

std::string handle_game_pause(PanelApp* app) {
    if (app == nullptr) {
        return nlp3::platform::build_panel_http_error_json("panel unavailable");
    }
    const auto ok = app->pause_active_game();
    return make_simple_result(ok, ok ? "game_paused" : "game_pause_failed");
}

std::string handle_game_download(PanelApp* app, std::string_view body) {
    if (app == nullptr) {
        return nlp3::platform::build_panel_http_error_json("panel unavailable");
    }

    const auto requested_game_id = parse_json_string(body, "gameId").value_or("");
    const auto result = app->start_remote_game_download(requested_game_id);
    return make_simple_result(result.ok, result.message);
}

std::string handle_game_reset(PanelApp* app) {
    if (app == nullptr) {
        return nlp3::platform::build_panel_http_error_json("panel unavailable");
    }
    const auto ok = app->restart_active_game();
    return make_simple_result(ok, ok ? "game_reset" : "game_reset_failed");
}

std::string handle_metrics_reset(PanelApp* app) {
    if (app == nullptr) {
        return nlp3::platform::build_panel_http_error_json("panel unavailable");
    }
    const auto ok = app->reset_metrics();
    return make_simple_result(ok, ok ? "metrics_reset" : "metrics_reset_failed");
}

std::string handle_game_trigger(PanelApp* app, std::string_view body) {
    if (app == nullptr) {
        return nlp3::platform::build_panel_http_error_json("panel unavailable");
    }

    HostEvent event{};
    event.kind = parse_host_event_kind(parse_json_string(body, "kind").value_or("chat"));
    const auto actor_name = parse_json_string(body, "actorName")
        .value_or(parse_json_string(body, "username").value_or("UI Operator"));
    const auto actor_id = parse_json_string(body, "actorId")
        .value_or(parse_json_string(body, "userId").value_or("ui-operator"));
    const auto avatar_url = parse_json_string(body, "avatarUrl").value_or("");
    event.actor = HostActor{actor_id, actor_name, avatar_url};
    event.metadata.source = "panel_ui";
    event.metadata.source_event_type = std::string(nlp3::events::to_string(event.kind));
    event.metadata.source_event_id = "panel-ui-" + std::to_string(now_wall_clock_ms());
    event.metadata.source_room_id = app->snapshot().external_bridge.current_room_id;
    event.metadata.source_timestamp_ms = static_cast<std::int64_t>(now_wall_clock_ms());

    return handle_game_trigger_rest(app, body, event);
}

std::string build_live_timer_config_json(const nlp3::games::LiveTimerGame* game) {
    if (game == nullptr) {
        return "null";
    }
    const auto& cfg = game->config();
    const auto q = [](std::string_view s) { return "\"" + json_escape(s) + "\""; };
    std::ostringstream out;
    out << "{";
    auto add = [&](std::string_view key) {
        const auto* val = cfg.find(key);
        if (val == nullptr) return;
        if (std::holds_alternative<double>(*val)) {
            out << q(key) << ":" << std::get<double>(*val);
        } else if (std::holds_alternative<std::int64_t>(*val)) {
            out << q(key) << ":" << std::get<std::int64_t>(*val);
        } else if (std::holds_alternative<bool>(*val)) {
            out << q(key) << ":" << (std::get<bool>(*val) ? "true" : "false");
        } else if (std::holds_alternative<std::string>(*val)) {
            out << q(key) << ":" << q(std::get<std::string>(*val));
        }
    };
    add("initial_time_s"); out << ",";
    add("max_time_s"); out << ",";
    add("time_per_like_s"); out << ",";
    add("time_per_share_s"); out << ",";
    add("time_per_follow_s"); out << ",";
    add("time_per_gift_coin_s"); out << ",";
    add("time_per_chat_s"); out << ",";
    add("title_text"); out << ",";
    add("subtitle_text"); out << ",";
    add("on_complete_sound_path"); out << ",";
    add("on_complete_repeat"); out << ",";
    add("on_complete_volume"); out << ",";
    add("on_complete_video_url"); out << ",";
    add("title_font_size"); out << ",";
    add("title_font_color"); out << ",";
    add("title_font_family"); out << ",";
    add("title_bold"); out << ",";
    add("counter_font_size"); out << ",";
    add("counter_font_color"); out << ",";
    add("counter_font_family"); out << ",";
    add("counter_bold"); out << ",";
    add("subtitle_font_size"); out << ",";
    add("subtitle_font_color"); out << ",";
    add("subtitle_font_family"); out << ",";
    add("subtitle_bold"); out << ",";
    add("popup_add_color"); out << ",";
    add("popup_subtract_color"); out << ",";
    add("on_complete_text"); out << ",";
    add("on_complete_text_color"); out << ",";
    add("on_complete_text_size"); out << ",";
    add("tick_sound_path"); out << ",";
    add("tick_sound_volume"); out << ",";
    add("add_sound_path"); out << ",";
    add("add_sound_volume"); out << ",";
    // Visual effects
    add("title_effect"); out << ",";
    add("counter_effect"); out << ",";
    add("subtitle_effect"); out << ",";
    add("title_glow_enabled"); out << ",";
    add("counter_glow_enabled"); out << ",";
    add("subtitle_glow_enabled"); out << ",";
    add("glow_color"); out << ",";
    add("glow_intensity_px"); out << ",";
    add("wave_colors"); out << ",";
    add("pulse_speed_s"); out << ",";
    add("shake_intensity"); out << ",";
    add("particles_enabled"); out << ",";
    add("particle_count"); out << ",";
    add("particle_color");
    out << "}";
    return out.str();
}

std::string handle_timer_get_config(PanelApp* app) {
    if (app == nullptr) {
        return nlp3::platform::build_panel_http_error_json("panel unavailable");
    }
    const auto* timer = app->live_timer();
    if (timer == nullptr) {
        return make_simple_result(false, "timer unavailable");
    }
    return "{"
        "\"ok\":true,"
        "\"config\":" + build_live_timer_config_json(timer) +
    "}";
}

std::string handle_timer_configure(PanelApp* app, std::string_view body) {
    if (app == nullptr) {
        return nlp3::platform::build_panel_http_error_json("panel unavailable");
    }
    auto* timer = app->live_timer();
    if (timer == nullptr) {
        return make_simple_result(false, "timer unavailable");
    }
    GameConfig config;

    auto maybe_bool = parse_json_bool(body, "on_complete_repeat");
    if (maybe_bool.has_value()) config.set("on_complete_repeat", *maybe_bool);

    auto maybe_d = parse_json_double(body, "initial_time_s");
    if (maybe_d.has_value()) config.set("initial_time_s", std::clamp(*maybe_d, 1.0, 31536000.0));
    maybe_d = parse_json_double(body, "max_time_s");
    if (maybe_d.has_value()) config.set("max_time_s", std::clamp(*maybe_d, 0.0, 31536000.0));
    maybe_d = parse_json_double(body, "time_per_like_s");
    if (maybe_d.has_value()) config.set("time_per_like_s", std::clamp(*maybe_d, -3600.0, 3600.0));
    maybe_d = parse_json_double(body, "time_per_share_s");
    if (maybe_d.has_value()) config.set("time_per_share_s", std::clamp(*maybe_d, -3600.0, 3600.0));
    maybe_d = parse_json_double(body, "time_per_follow_s");
    if (maybe_d.has_value()) config.set("time_per_follow_s", std::clamp(*maybe_d, -3600.0, 3600.0));
    maybe_d = parse_json_double(body, "time_per_gift_coin_s");
    if (maybe_d.has_value()) config.set("time_per_gift_coin_s", std::clamp(*maybe_d, -3600.0, 3600.0));
    maybe_d = parse_json_double(body, "time_per_chat_s");
    if (maybe_d.has_value()) config.set("time_per_chat_s", std::clamp(*maybe_d, -3600.0, 3600.0));
    maybe_d = parse_json_double(body, "on_complete_volume");
    if (maybe_d.has_value()) config.set("on_complete_volume", std::clamp(*maybe_d, 0.0, 2.0));

    auto maybe_str = parse_json_string(body, "title_text");
    if (maybe_str.has_value()) config.set("title_text", *maybe_str);
    maybe_str = parse_json_string(body, "subtitle_text");
    if (maybe_str.has_value()) config.set("subtitle_text", *maybe_str);
    maybe_str = parse_json_string(body, "on_complete_sound_path");
    if (maybe_str.has_value()) config.set("on_complete_sound_path", *maybe_str);
    maybe_str = parse_json_string(body, "on_complete_video_url");
    if (maybe_str.has_value()) config.set("on_complete_video_url", *maybe_str);
    maybe_str = parse_json_string(body, "title_font_color");
    if (maybe_str.has_value()) config.set("title_font_color", *maybe_str);
    maybe_str = parse_json_string(body, "title_font_family");
    if (maybe_str.has_value()) config.set("title_font_family", *maybe_str);
    maybe_str = parse_json_string(body, "counter_font_color");
    if (maybe_str.has_value()) config.set("counter_font_color", *maybe_str);
    maybe_str = parse_json_string(body, "counter_font_family");
    if (maybe_str.has_value()) config.set("counter_font_family", *maybe_str);
    maybe_str = parse_json_string(body, "subtitle_font_color");
    if (maybe_str.has_value()) config.set("subtitle_font_color", *maybe_str);
    maybe_str = parse_json_string(body, "subtitle_font_family");
    if (maybe_str.has_value()) config.set("subtitle_font_family", *maybe_str);

    auto maybe_i64 = parse_json_uint64(body, "title_font_size");
    if (maybe_i64.has_value()) config.set("title_font_size", static_cast<std::int64_t>(std::clamp<uint64_t>(*maybe_i64, 8, 200)));
    maybe_i64 = parse_json_uint64(body, "counter_font_size");
    if (maybe_i64.has_value()) config.set("counter_font_size", static_cast<std::int64_t>(std::clamp<uint64_t>(*maybe_i64, 8, 400)));
    maybe_i64 = parse_json_uint64(body, "subtitle_font_size");
    if (maybe_i64.has_value()) config.set("subtitle_font_size", static_cast<std::int64_t>(std::clamp<uint64_t>(*maybe_i64, 8, 200)));

    maybe_bool = parse_json_bool(body, "title_bold");
    if (maybe_bool.has_value()) config.set("title_bold", *maybe_bool);
    maybe_bool = parse_json_bool(body, "counter_bold");
    if (maybe_bool.has_value()) config.set("counter_bold", *maybe_bool);
    maybe_bool = parse_json_bool(body, "subtitle_bold");
    if (maybe_bool.has_value()) config.set("subtitle_bold", *maybe_bool);

    maybe_str = parse_json_string(body, "popup_add_color");
    if (maybe_str.has_value()) config.set("popup_add_color", *maybe_str);
    maybe_str = parse_json_string(body, "popup_subtract_color");
    if (maybe_str.has_value()) config.set("popup_subtract_color", *maybe_str);
    maybe_str = parse_json_string(body, "on_complete_text");
    if (maybe_str.has_value()) config.set("on_complete_text", *maybe_str);
    maybe_str = parse_json_string(body, "on_complete_text_color");
    if (maybe_str.has_value()) config.set("on_complete_text_color", *maybe_str);
    maybe_i64 = parse_json_uint64(body, "on_complete_text_size");
    if (maybe_i64.has_value()) config.set("on_complete_text_size", static_cast<std::int64_t>(std::clamp<uint64_t>(*maybe_i64, 8, 200)));

    maybe_str = parse_json_string(body, "tick_sound_path");
    if (maybe_str.has_value()) config.set("tick_sound_path", *maybe_str);
    maybe_d = parse_json_double(body, "tick_sound_volume");
    if (maybe_d.has_value()) config.set("tick_sound_volume", std::clamp(*maybe_d, 0.0, 2.0));
    maybe_str = parse_json_string(body, "add_sound_path");
    if (maybe_str.has_value()) config.set("add_sound_path", *maybe_str);
    maybe_d = parse_json_double(body, "add_sound_volume");
    if (maybe_d.has_value()) config.set("add_sound_volume", std::clamp(*maybe_d, 0.0, 2.0));

    // Visual effects
    maybe_str = parse_json_string(body, "title_effect");
    if (maybe_str.has_value()) config.set("title_effect", *maybe_str);
    maybe_str = parse_json_string(body, "counter_effect");
    if (maybe_str.has_value()) config.set("counter_effect", *maybe_str);
    maybe_str = parse_json_string(body, "subtitle_effect");
    if (maybe_str.has_value()) config.set("subtitle_effect", *maybe_str);

    maybe_bool = parse_json_bool(body, "title_glow_enabled");
    if (maybe_bool.has_value()) config.set("title_glow_enabled", *maybe_bool);
    maybe_bool = parse_json_bool(body, "counter_glow_enabled");
    if (maybe_bool.has_value()) config.set("counter_glow_enabled", *maybe_bool);
    maybe_bool = parse_json_bool(body, "subtitle_glow_enabled");
    if (maybe_bool.has_value()) config.set("subtitle_glow_enabled", *maybe_bool);

    maybe_str = parse_json_string(body, "glow_color");
    if (maybe_str.has_value()) config.set("glow_color", *maybe_str);
    maybe_i64 = parse_json_uint64(body, "glow_intensity_px");
    if (maybe_i64.has_value()) config.set("glow_intensity_px", static_cast<std::int64_t>(std::clamp<uint64_t>(*maybe_i64, 1, 60)));
    maybe_str = parse_json_string(body, "wave_colors");
    if (maybe_str.has_value()) config.set("wave_colors", *maybe_str);
    maybe_d = parse_json_double(body, "pulse_speed_s");
    if (maybe_d.has_value()) config.set("pulse_speed_s", std::clamp(*maybe_d, 0.3, 5.0));
    maybe_str = parse_json_string(body, "shake_intensity");
    if (maybe_str.has_value()) config.set("shake_intensity", *maybe_str);
    maybe_bool = parse_json_bool(body, "particles_enabled");
    if (maybe_bool.has_value()) config.set("particles_enabled", *maybe_bool);
    maybe_i64 = parse_json_uint64(body, "particle_count");
    if (maybe_i64.has_value()) config.set("particle_count", static_cast<std::int64_t>(std::clamp<uint64_t>(*maybe_i64, 0, 60)));
    maybe_str = parse_json_string(body, "particle_color");
    if (maybe_str.has_value()) config.set("particle_color", *maybe_str);

    // T3.1: capture the requested config snapshot so we can compare against the
    // effective post-apply config and report normalization/clamps to the caller.
    const GameConfig requested = config;

    timer->apply_config(config);
    app->save_timer_state();

    // Build a stringified side-by-side comparison of the keys the client set.
    // A5: normalize numeric values so that int64_t(2) and double(2.0) compare
    // equal and we don't emit spurious "normalized" warnings for harmless
    // type-only differences. Doubles are rendered with trailing zeros and
    // decimal point stripped when the value is integral, matching the
    // canonical JSON number form.
    auto value_to_string = [](const GameConfigValue& v) -> std::string {
        if (std::holds_alternative<bool>(v)) return std::get<bool>(v) ? "true" : "false";
        if (std::holds_alternative<std::int64_t>(v)) return std::to_string(std::get<std::int64_t>(v));
        if (std::holds_alternative<double>(v)) {
            const double d = std::get<double>(v);
            if (std::isfinite(d) && d == std::floor(d) && std::abs(d) < 1e15) {
                return std::to_string(static_cast<std::int64_t>(d));
            }
            std::ostringstream oss;
            oss << d;
            return oss.str();
        }
        return std::get<std::string>(v);
    };

    std::vector<std::string> warnings;
    for (const auto& [key, req_val] : requested.values()) {
        const auto* eff = timer->config().find(key);
        if (eff == nullptr) continue;
        std::string req_str = value_to_string(req_val);
        std::string eff_str = value_to_string(*eff);
        if (req_str != eff_str) {
            warnings.push_back(
                std::string(key) + " normalized from '" + req_str + "' to '" + eff_str + "'");
        }
    }

    std::ostringstream out;
    out << "{\"ok\":true,\"message\":\"config_applied\",\"warnings\":[";
    for (std::size_t i = 0; i < warnings.size(); ++i) {
        if (i > 0) out << ",";
        out << "\"" << json_escape(warnings[i]) << "\"";
    }
    out << "]}";
    return out.str();
}

std::string handle_game_trigger_rest(PanelApp* app, std::string_view body, HostEvent& event) {
    event.message = parse_json_string(body, "message").value_or(parse_json_string(body, "text").value_or(""));
    event.magnitude = static_cast<int>(parse_json_uint64(body, "magnitude").value_or(1));
    event.viewer_count = static_cast<int>(parse_json_uint64(body, "viewerCount").value_or(0));
    event.raw_payload = std::string(body);

    if (event.kind == HostEventKind::gift) {
        event.gift = GiftEventData{
            parse_json_string(body, "giftName").value_or("Rose"),
            static_cast<int>(parse_json_uint64(body, "quantity").value_or(1)),
            static_cast<int>(parse_json_uint64(body, "value").value_or(event.magnitude)),
        };
    }

    const auto ok = app->inject_host_event(event);
    return make_simple_result(ok, ok ? "event_injected" : "event_injection_failed");
}

std::string handle_host_tts(PanelApp* app, std::string_view body) {
    if (app == nullptr) {
        return nlp3::platform::build_panel_http_error_json("panel unavailable");
    }

    auto& config = app->config();
    const auto tts_enabled = parse_json_bool(body, "ttsEnabled");
    const auto voice_id = parse_json_string(body, "voiceId");
    const auto voice_language = parse_json_string(body, "voiceLanguage");
    const auto voice_frequency = parse_json_string(body, "voiceFrequency");
    const auto energy = parse_json_string(body, "energyLevel");
    const auto tone = parse_json_string(body, "toneStyle");
    const auto action = to_lower_copy(parse_json_string(body, "action").value_or(""));
    const auto message = parse_json_string(body, "message").value_or("");
    const auto replace_periodic_messages = parse_json_bool(body, "replacePeriodicMessages").value_or(false);
    const auto allow_chat_messages = parse_json_bool(body, "allowChatMessages");
    const auto chat_filter_mode = parse_json_string(body, "chatFilterMode");
    const auto chat_template = parse_json_string(body, "chatMessageTemplate");
    const auto gift_template = parse_json_string(body, "giftThanksTemplate");
    const auto follow_template = parse_json_string(body, "followThanksTemplate");
    const auto like_template = parse_json_string(body, "likeThanksTemplate");
    const auto subscriber_template = parse_json_string(body, "subscriberThanksTemplate");
    const auto share_template = parse_json_string(body, "shareThanksTemplate");
    const auto periodic_messages = parse_json_string_array(body, "periodicMessages");

    if (tts_enabled.has_value()) {
        config.tts_runtime.enabled = *tts_enabled;
    }
    if (voice_id.has_value()) {
        config.tts_runtime.selected_voice_id = *voice_id;
    }
    if (voice_language.has_value()) {
        config.tts_runtime.selected_language = *voice_language;
    }
    if (voice_frequency.has_value()) {
        config.tts_runtime.frequency = *voice_frequency;
    }
    if (energy.has_value()) {
        config.host_energy_level = normalize_energy_level(*energy);
    }
    if (tone.has_value()) {
        config.host_tone_style = normalize_tone_style(*tone);
    }
    if (allow_chat_messages.has_value()) {
        config.tts.allow_chat_messages = *allow_chat_messages;
    }
    if (chat_filter_mode.has_value()) {
        config.tts.chat_filter_mode = nlp3::tts::parse_tts_chat_filter_mode(*chat_filter_mode);
    }
    if (chat_template.has_value()) {
        config.tts.chat_message_template = *chat_template;
    }

    if (const auto value = parse_json_bool(body, "giftThanksEnabled"); value.has_value()) {
        config.automation.enable_gift_thanks_tts = *value;
    }
    if (const auto value = parse_json_bool(body, "followThanksEnabled"); value.has_value()) {
        config.automation.enable_follow_thanks_tts = *value;
    }
    if (const auto value = parse_json_bool(body, "likeThanksEnabled"); value.has_value()) {
        config.automation.enable_like_thanks_tts = *value;
    }
    if (const auto value = parse_json_bool(body, "subscriberThanksEnabled"); value.has_value()) {
        config.automation.enable_subscriber_thanks_tts = *value;
    }
    if (const auto value = parse_json_bool(body, "shareThanksEnabled"); value.has_value()) {
        config.automation.enable_share_thanks_tts = *value;
    }
    if (const auto value = parse_json_bool(body, "periodicEnabled"); value.has_value()) {
        config.periodic_tts.enabled = *value;
    }
    if (const auto value = parse_json_uint64(body, "periodicIntervalMs"); value.has_value()) {
        config.periodic_tts.interval_ms = *value;
    }

    if (action == "boost_hype") {
        config.host_energy_level = "hype";
        config.host_tone_style = "electric";
        config.periodic_tts.enabled = true;
    } else if (action == "calm_mode") {
        config.host_energy_level = "calm";
        config.host_tone_style = "warm";
        config.periodic_tts.enabled = true;
    } else if (action == "silence") {
        config.tts_runtime.enabled = false;
        config.periodic_tts.enabled = false;
        config.automation.enable_gift_thanks_tts = false;
        config.automation.enable_follow_thanks_tts = false;
        config.automation.enable_like_thanks_tts = false;
        config.automation.enable_subscriber_thanks_tts = false;
        config.automation.enable_share_thanks_tts = false;
        config.tts.allow_chat_messages = false;
    }

    sync_host_persona(app, replace_periodic_messages || !action.empty(), !action.empty());

    if (gift_template.has_value()) {
        config.automation.gift_thanks_template = *gift_template;
    }
    if (follow_template.has_value()) {
        config.automation.follow_thanks_template = *follow_template;
    }
    if (like_template.has_value()) {
        config.automation.like_thanks_template = *like_template;
    }
    if (subscriber_template.has_value()) {
        config.automation.subscriber_thanks_template = *subscriber_template;
    }
    if (share_template.has_value()) {
        config.automation.share_thanks_template = *share_template;
    }
    if (periodic_messages.has_value()) {
        config.periodic_tts.messages = *periodic_messages;
    }

    const auto applied = app->apply_live_config();
    const auto saved = applied && app->save_config();

    bool spoke = false;
    if (!message.empty()) {
        const auto result = app->execute_command({
            nlp3::platform::PanelCommandKind::tts_enqueue_announcement,
            message,
        });
        spoke = result.ok;
    } else if (action == "speak_now") {
        const auto result = app->execute_command({
            nlp3::platform::PanelCommandKind::tts_enqueue_announcement,
            "Seguimos en vivo desde el panel",
        });
        spoke = result.ok;
    }

    return "{"
        "\"ok\":" + bool_json(applied && saved) + ","
        "\"message\":" + json_quote(applied && saved ? "host_tts_updated" : "host_tts_update_failed") + ","
        "\"spoke\":" + bool_json(spoke)
        + "}";
}

std::string handle_tts_test(PanelApp* app, std::string_view body) {
    if (app == nullptr) {
        return nlp3::platform::build_panel_http_error_json("panel unavailable");
    }

    const auto message = parse_json_string(body, "message").value_or("Nisoje Studio voice test");
    const auto result = app->execute_command({
        nlp3::platform::PanelCommandKind::tts_enqueue_announcement,
        message,
    });
    return "{"
        "\"ok\":" + bool_json(result.ok) + ","
        "\"message\":" + json_quote(result.message)
        + "}";
}

std::string handle_system_reconnect(PanelApp* app) {
    if (app == nullptr) {
        return nlp3::platform::build_panel_http_error_json("panel unavailable");
    }
    const auto ok = app->reconnect_external_pipeline();
    return make_simple_result(ok, ok ? "system_reconnected" : "system_reconnect_failed");
}

std::string handle_update_trigger(PanelApp* app) {
    if (app == nullptr) {
        return nlp3::platform::build_panel_http_error_json("panel unavailable");
    }
    const auto ok = app->trigger_panel_update();
    return make_simple_result(ok, ok ? "update_triggered" : "update_trigger_failed");
}

std::string handle_auth_login(PanelApp* app, std::string_view body) {
    if (app == nullptr) {
        return nlp3::platform::build_panel_http_error_json("panel unavailable");
    }

    nlp3::platform::PanelAuthLoginRequest request{};
    request.email = parse_json_string(body, "email").value_or("");
    request.password = parse_json_string(body, "password").value_or("");
    request.license_key = parse_json_string(body, "licenseKey")
        .value_or(parse_json_string(body, "license_key").value_or(""));

    return make_auth_login_result(app->authenticate_access(request));
}

std::string handle_auth_logout(PanelApp* app) {
    if (app == nullptr) {
        return nlp3::platform::build_panel_http_error_json("panel unavailable");
    }

    app->logout_access();
    return make_simple_result(true, "auth_logged_out");
}

std::string handle_support_export(
    PanelApp* app,
    const PanelHttpServerStatus& status,
    std::string_view body) {
    if (app == nullptr) {
        return nlp3::platform::build_panel_http_error_json("panel unavailable");
    }

    const auto reason = parse_json_string(body, "reason").value_or("manual");
    return make_support_export_result(nlp3::platform::export_support_bundle(*app, status, reason));
}

std::string build_route_response(
    PanelApp* app,
    const ParsedRequest& request,
    const PanelHttpServerStatus& status) {
    if (!request_origin_allowed(request, status)) {
        return make_http_response("403 Forbidden", "application/json; charset=utf-8", make_origin_forbidden_result());
    }

    if (request.method == "GET" && request.path == "/") {
        return make_http_response("200 OK", "text/html; charset=utf-8", std::string(nlp3::platform::panel_ui_index_html()));
    }
    if (request.method == "GET" && request.path == "/app.css") {
        return make_http_response("200 OK", "text/css; charset=utf-8", std::string(nlp3::platform::panel_ui_styles_css()));
    }
    if (request.method == "GET" && request.path == "/app.js") {
        return make_http_response("200 OK", "application/javascript; charset=utf-8", std::string(nlp3::platform::panel_ui_app_js()));
    }
    if (request.method == "GET" && request.path == "/game-previews.js") {
        return make_http_response(
            "200 OK",
            "application/javascript; charset=utf-8",
            std::string(nlp3::platform::panel_ui_game_previews_js()));
    }
    if (request.method == "GET" && request.path == "/api/state") {
        return make_http_response(
            "200 OK",
            "application/json; charset=utf-8",
            nlp3::platform::build_panel_http_state_json(*app, status));
    }
    if (request.method == "GET" && request.path == "/api/events") {
        return make_http_response(
            "200 OK",
            "application/json; charset=utf-8",
            nlp3::platform::build_panel_http_events_json(*app));
    }
    if (request.method == "GET" && request.path == "/api/metrics") {
        return make_http_response(
            "200 OK",
            "application/json; charset=utf-8",
            nlp3::platform::build_panel_http_metrics_json(*app));
    }
    if (request.method == "GET" && request.path == "/api/realtime") {
        return make_http_response(
            "200 OK",
            "application/json; charset=utf-8",
            nlp3::platform::build_panel_http_realtime_json(*app));
    }
    if (request.method == "GET" && request.path == "/api/tts/config") {
        return make_http_response(
            "200 OK",
            "application/json; charset=utf-8",
            nlp3::platform::build_panel_http_tts_json(*app));
    }
    if (request.method == "GET" && request.path == "/overlay/live-timer") {
        return make_http_response("200 OK", "text/html; charset=utf-8", std::string(nlp3::platform::panel_overlay_live_timer_html()));
    }
    if (request.method == "GET" && request.path == "/api/overlay/live-timer/state") {
        if (app == nullptr) {
            return make_http_response("200 OK", "application/json; charset=utf-8", nlp3::platform::build_live_timer_state_json(nullptr));
        }
        return make_http_response("200 OK", "application/json; charset=utf-8", nlp3::platform::build_live_timer_state_json(app->live_timer()));
    }
    if (request.method == "GET" && request.path == "/health") {
        return make_http_response("200 OK", "application/json; charset=utf-8", "{\"ok\":true}");
    }
    if (request.method == "GET" && request.path == "/status") {
        return make_http_response(
            "200 OK",
            "application/json; charset=utf-8",
            nlp3::platform::build_panel_http_state_json(*app, status));
    }
    if (request.method == "POST" && request.path == "/api/auth/login") {
        return make_http_response("200 OK", "application/json; charset=utf-8", handle_auth_login(app, request.body));
    }
    if (request.method == "POST" && request.path == "/api/auth/logout") {
        return make_http_response("200 OK", "application/json; charset=utf-8", handle_auth_logout(app));
    }
    if (request.method == "POST" && request.path == "/api/support/export") {
        return make_http_response("200 OK", "application/json; charset=utf-8", handle_support_export(app, status, request.body));
    }
    if (app != nullptr && app->auth_required() && !app->access_granted() && request_requires_access(request)) {
        return make_http_response("403 Forbidden", "application/json; charset=utf-8", make_auth_required_result());
    }
    if (request.method == "POST" && request.path == "/api/metrics/reset") {
        return make_http_response("200 OK", "application/json; charset=utf-8", handle_metrics_reset(app));
    }
    if (request.method == "POST" && request.path == "/api/command") {
        return make_http_response("200 OK", "application/json; charset=utf-8", handle_command(app, request.body));
    }
    if (request.method == "POST" && request.path == "/api/game/start") {
        return make_http_response("200 OK", "application/json; charset=utf-8", handle_game_start(app, request.body));
    }
    if (request.method == "POST" && request.path == "/api/game/pause") {
        return make_http_response("200 OK", "application/json; charset=utf-8", handle_game_pause(app));
    }
    if (request.method == "POST" && request.path == "/api/game/download") {
        return make_http_response("200 OK", "application/json; charset=utf-8", handle_game_download(app, request.body));
    }
    if (request.method == "POST" && request.path == "/api/game/reset") {
        return make_http_response("200 OK", "application/json; charset=utf-8", handle_game_reset(app));
    }
    if (request.method == "POST" && request.path == "/api/game/trigger") {
        return make_http_response("200 OK", "application/json; charset=utf-8", handle_game_trigger(app, request.body));
    }
    if (request.method == "GET" && request.path == "/api/timer/config") {
        return make_http_response("200 OK", "application/json; charset=utf-8", handle_timer_get_config(app));
    }
    if (request.method == "POST" && request.path == "/api/timer/configure") {
        return make_http_response("200 OK", "application/json; charset=utf-8", handle_timer_configure(app, request.body));
    }
    if (request.method == "POST" && request.path == "/api/timer/start") {
        auto* timer = app->live_timer();
        if (timer != nullptr) { timer->on_activated(); app->save_timer_state(); }
        return make_http_response("200 OK", "application/json; charset=utf-8", make_simple_result(timer != nullptr, timer ? "started" : "unavailable"));
    }
    if (request.method == "POST" && request.path == "/api/timer/pause") {
        auto* timer = app->live_timer();
        if (timer != nullptr) { timer->pause(); app->save_timer_state(); }
        return make_http_response("200 OK", "application/json; charset=utf-8", make_simple_result(timer != nullptr, timer ? "paused" : "unavailable"));
    }
    if (request.method == "POST" && request.path == "/api/timer/resume") {
        auto* timer = app->live_timer();
        if (timer != nullptr) { timer->resume(); app->save_timer_state(); }
        return make_http_response("200 OK", "application/json; charset=utf-8", make_simple_result(timer != nullptr, timer ? "resumed" : "unavailable"));
    }
    if (request.method == "POST" && request.path == "/api/timer/reset") {
        auto* timer = app->live_timer();
        if (timer != nullptr) { timer->reset(); app->save_timer_state(); }
        return make_http_response("200 OK", "application/json; charset=utf-8", make_simple_result(timer != nullptr, timer ? "reset" : "unavailable"));
    }
    if (request.method == "POST" && request.path == "/api/timer/stop") {
        auto* timer = app->live_timer();
        if (timer != nullptr) { timer->stop(); app->save_timer_state(); }
        return make_http_response("200 OK", "application/json; charset=utf-8", make_simple_result(timer != nullptr, timer ? "stopped" : "unavailable"));
    }
    if (request.method == "POST" && request.path == "/api/timer/adjust") {
        auto* timer = app->live_timer();
        if (timer == nullptr) {
            return make_http_response("200 OK", "application/json; charset=utf-8", make_simple_result(false, "unavailable"));
        }
        auto delta = parse_json_double(request.body, "delta").value_or(0.0);
        if (delta == 0.0) {
            return make_http_response("200 OK", "application/json; charset=utf-8", make_simple_result(false, "delta_zero"));
        }
        timer->adjust_time(delta);
        app->save_timer_state();
        return make_http_response("200 OK", "application/json; charset=utf-8", make_simple_result(true, "adjusted"));
    }
    if (request.method == "POST" && request.path == "/api/timer/reset-config") {
        auto* timer = app->live_timer();
        if (timer != nullptr) { timer->reset_config_to_defaults(); app->save_timer_state(); }
        return make_http_response("200 OK", "application/json; charset=utf-8", make_simple_result(timer != nullptr, timer ? "config_reset" : "unavailable"));
    }
    if (request.method == "POST" && request.path == "/api/timer/toggle") {
        auto* timer = app->live_timer();
        if (timer != nullptr) { timer->set_enabled(!timer->is_enabled()); app->save_timer_state(); }
        return make_http_response("200 OK", "application/json; charset=utf-8", make_simple_result(timer != nullptr, timer ? (timer->is_enabled() ? "enabled" : "disabled") : "unavailable"));
    }
    if (request.method == "POST" && request.path == "/api/host/tts") {
        return make_http_response("200 OK", "application/json; charset=utf-8", handle_host_tts(app, request.body));
    }
    if (request.method == "POST" && request.path == "/api/tts/config") {
        return make_http_response("200 OK", "application/json; charset=utf-8", handle_host_tts(app, request.body));
    }
    if (request.method == "POST" && request.path == "/api/tts/test") {
        return make_http_response("200 OK", "application/json; charset=utf-8", handle_tts_test(app, request.body));
    }
    if (request.method == "POST" && request.path == "/api/system/reconnect") {
        return make_http_response("200 OK", "application/json; charset=utf-8", handle_system_reconnect(app));
    }
    if (request.method == "POST" && request.path == "/api/update/trigger") {
        return make_http_response("200 OK", "application/json; charset=utf-8", handle_update_trigger(app));
    }

    return make_http_response(
        "404 Not Found",
        "application/json; charset=utf-8",
        nlp3::platform::build_panel_http_error_json("not_found"));
}

#endif

} // namespace

namespace nlp3::platform {

PanelHttpServer::PanelHttpServer(PanelApp* app) noexcept
    : app_(app) {
}

PanelHttpServer::~PanelHttpServer() {
    stop();
}

bool PanelHttpServer::start(std::uint16_t port) {
#ifdef _WIN32
    stop();

    if (app_ == nullptr) {
        status_.last_error = "panel unavailable";
        return false;
    }
    if (!ensure_winsock_initialized()) {
        status_.last_error = "winsock startup failed";
        return false;
    }

    SOCKET listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket == INVALID_SOCKET) {
        status_.last_error = "socket create failed";
        return false;
    }

    u_long nonblocking = 1;
    ioctlsocket(listen_socket, FIONBIO, &nonblocking);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(listen_socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
        closesocket(listen_socket);
        status_.last_error = "bind failed";
        return false;
    }
    if (listen(listen_socket, 8) == SOCKET_ERROR) {
        closesocket(listen_socket);
        status_.last_error = "listen failed";
        return false;
    }

    listen_socket_ = reinterpret_cast<void*>(listen_socket);
    status_.running = true;
    status_.port = port;
    status_.last_error.clear();
    client_socket_ = nullptr;
    request_buffer_.clear();
    pending_response_.clear();
    return true;
#else
    (void)port;
    status_.last_error = "http ui is only available on windows in this build";
    return false;
#endif
}

void PanelHttpServer::stop() {
#ifdef _WIN32
    close_socket(client_socket_);
    close_socket(listen_socket_);
#endif
    request_buffer_.clear();
    pending_response_.clear();
    status_.running = false;
    status_.port = 0;
}

void PanelHttpServer::poll() {
#ifdef _WIN32
    if (!status_.running || listen_socket_ == nullptr) {
        return;
    }

    if (client_socket_ == nullptr) {
        sockaddr_in client_address{};
        int client_length = sizeof(client_address);
        const auto accepted = accept(
            reinterpret_cast<SOCKET>(listen_socket_),
            reinterpret_cast<sockaddr*>(&client_address),
            &client_length);
        if (accepted != INVALID_SOCKET) {
            u_long nonblocking = 1;
            ioctlsocket(accepted, FIONBIO, &nonblocking);
            client_socket_ = reinterpret_cast<void*>(accepted);
            request_buffer_.clear();
            pending_response_.clear();
        } else {
            const auto error_code = WSAGetLastError();
            if (error_code != WSAEWOULDBLOCK) {
                status_.last_error = "accept failed";
            }
        }
    }

    if (client_socket_ == nullptr) {
        return;
    }

    bool close_client = false;
    const auto client_socket = reinterpret_cast<SOCKET>(client_socket_);
    if (pending_response_.empty()) {
        std::array<char, 4096> buffer{};
        const auto received = recv(
            client_socket,
            buffer.data(),
            static_cast<int>(buffer.size()),
            0);
        if (received > 0) {
            request_buffer_.append(buffer.data(), static_cast<std::size_t>(received));
            const auto parsed = parse_request_buffer(request_buffer_);
            if (parsed.ready) {
                pending_response_ = build_route_response(app_, parsed, status_);
                request_buffer_.clear();
            }
        } else if (received == 0) {
            close_client = true;
        } else {
            const auto error_code = WSAGetLastError();
            if (error_code != WSAEWOULDBLOCK) {
                status_.last_error = "recv failed";
                close_client = true;
            }
        }
    }

    if (!close_client && !pending_response_.empty()) {
        const auto sent = send(
            client_socket,
            pending_response_.data(),
            static_cast<int>(pending_response_.size()),
            0);
        if (sent > 0) {
            pending_response_.erase(0, static_cast<std::size_t>(sent));
            if (pending_response_.empty()) {
                ++status_.requests_served;
                close_client = true;
            }
        } else if (sent < 0) {
            const auto error_code = WSAGetLastError();
            if (error_code != WSAEWOULDBLOCK) {
                status_.last_error = "send failed";
                close_client = true;
            }
        }
    }

    if (close_client) {
        close_socket(client_socket_);
        request_buffer_.clear();
        pending_response_.clear();
    }
#endif
}

bool PanelHttpServer::running() const noexcept {
    return status_.running;
}

PanelHttpServerStatus PanelHttpServer::status() const noexcept {
    return status_;
}

std::string panel_http_ui_url(std::uint16_t port) {
    return "http://127.0.0.1:" + std::to_string(port);
}

bool open_panel_http_ui_in_browser(std::uint16_t port) {
    return open_panel_http_ui_in_browser(panel_http_ui_url(port));
}

bool open_panel_http_ui_in_browser(std::string_view url) {
#ifdef _WIN32
    const auto resolved_url = std::string(url);
    const auto result = reinterpret_cast<std::intptr_t>(
        ShellExecuteA(nullptr, "open", resolved_url.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    return result > 32;
#else
    (void)url;
    return false;
#endif
}

} // namespace nlp3::platform
