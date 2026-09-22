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
#include "platform/wall_clock.h"
#include "platform/port_zombie_detector.hpp"
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

using nlp3::platform::now_wall_clock_ms;

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
    std::string body,
    std::string_view extra_headers = {}) {
    std::ostringstream output;
    output << "HTTP/1.1 " << status_line << "\r\n"
           << "Content-Type: " << content_type << "\r\n"
           << "Content-Length: " << body.size() << "\r\n"
           << "Connection: close\r\n"
           << "Cache-Control: no-store\r\n";
    if (!extra_headers.empty()) {
        output << extra_headers;
        if (extra_headers.back() != '\n') {
            output << "\r\n";
        }
    }
    output << "\r\n"
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

// Respuesta del bridge con codigo de error estable para la UI (`error`) y texto
// humano ya listo para mostrar (`message`), mas el detalle del sondeo de entorno
// para que el panel pueda explicar el motivo real en el monitor del live.
std::string make_bridge_result(
    bool ok,
    std::string_view error_code,
    std::string_view message,
    const nlp3::platform::ExternalBridgeRunnerStatus* runner);

std::string make_bridge_keys_result(PanelApp* app) {
    if (app == nullptr) {
        return nlp3::platform::build_panel_http_error_json("panel unavailable");
    }

    const auto now_ms = static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    const auto& entries = app->bridge_key_vault().entries();

    std::string output = "{\"ok\":true,\"keys\":[";
    for (std::size_t index = 0; index < entries.size(); ++index) {
        if (index > 0) {
            output += ",";
        }
        const auto& entry = entries[index];
        const bool available = entry.cooldown_until_ms <= now_ms;
        output += "{";
        output += "\"index\":" + std::to_string(index) + ",";
        output += "\"label\":" + json_quote(entry.label) + ",";
        output += "\"fingerprint\":" + json_quote(nlp3::platform::BridgeKeyVault::fingerprint(entry.secret)) + ",";
        output += "\"available\":" + bool_json(available) + ",";
        output += "\"inUse\":" + bool_json(available && static_cast<int>(index) == app->bridge_key_vault().first_available(now_ms)) + ",";
        output += "\"cooldownUntilMs\":" + std::to_string(entry.cooldown_until_ms);
        output += "}";
    }
    output += "],\"count\":" + std::to_string(entries.size()) + "}";
    return output;
}

std::string handle_bridge_key_add(PanelApp* app, std::string_view body) {
    if (app == nullptr) {
        return nlp3::platform::build_panel_http_error_json("panel unavailable");
    }

    const auto api_key = parse_json_string(body, "api_key").value_or("");
    if (api_key.empty()) {
        return make_bridge_result(false, "invalid_api_key", "Escribi la API key antes de agregarla.", nullptr);
    }

    const auto label = parse_json_string(body, "label").value_or("");
    auto& vault = app->bridge_key_vault();
    const auto& entries = vault.entries();
    for (std::size_t index = 0; index < entries.size(); ++index) {
        if (entries[index].secret == api_key) {
            vault.replace(index, label.empty() ? entries[index].label : label, api_key);
            app->save_bridge_key_vault();
            return make_bridge_result(true, "bridge_key_updated", "La credencial ya estaba guardada: se actualizo su etiqueta.", nullptr);
        }
    }

    if (!vault.add(label, api_key)) {
        return make_bridge_result(false, "invalid_api_key", "No se pudo guardar esa API key.", nullptr);
    }
    if (!app->save_bridge_key_vault()) {
        return make_bridge_result(false, "key_vault_write_failed", "No se pudo cifrar y guardar la credencial en disco.", nullptr);
    }
    return make_bridge_result(true, "bridge_key_added", "Credencial guardada y cifrada correctamente.", nullptr);
}

std::string handle_bridge_key_remove(PanelApp* app, std::string_view body) {
    if (app == nullptr) {
        return nlp3::platform::build_panel_http_error_json("panel unavailable");
    }

    const auto index_text = parse_json_string(body, "fingerprint").value_or("");
    auto& vault = app->bridge_key_vault();
    const auto& entries = vault.entries();

    std::size_t target_index = entries.size();
    if (!index_text.empty()) {
        for (std::size_t index = 0; index < entries.size(); ++index) {
            if (nlp3::platform::BridgeKeyVault::fingerprint(entries[index].secret) == index_text) {
                target_index = index;
                break;
            }
        }
    } else {
        const auto parsed_index = parse_json_uint64(body, "index");
        if (parsed_index.has_value() && parsed_index.value() < entries.size()) {
            target_index = static_cast<std::size_t>(parsed_index.value());
        }
    }

    if (target_index >= entries.size()) {
        return make_bridge_result(false, "key_not_found", "No se encontro esa credencial.", nullptr);
    }
    if (vault.size() <= 1) {
        return make_bridge_result(false, "key_is_last", "No se puede borrar la unica credencial guardada.", nullptr);
    }

    vault.remove_at(target_index);
    app->save_bridge_key_vault();
    return make_bridge_result(true, "bridge_key_removed", "Credencial eliminada.", nullptr);
}

std::string make_bridge_result(
    bool ok,
    std::string_view error_code,
    std::string_view message,
    const nlp3::platform::ExternalBridgeRunnerStatus* runner) {
    std::string payload = "{"
        "\"ok\":" + bool_json(ok) + ","
        "\"error\":" + json_quote(error_code) + ","
        "\"message\":" + json_quote(message);

    if (runner != nullptr) {
        payload += ",\"runnerRunning\":" + bool_json(runner->running);
        payload += ",\"runnerError\":" + json_quote(runner->last_error);
        payload += ",\"runtimeChecked\":" + bool_json(runner->runtime_checked);
        payload += ",\"runtimeReady\":" + bool_json(runner->runtime_ready);
        payload += ",\"runtimeSummary\":" + json_quote(runner->runtime_summary);
        payload += ",\"runtimeAlerts\":[";
        for (std::size_t index = 0; index < runner->runtime_alerts.size(); ++index) {
            if (index > 0) {
                payload += ",";
            }
            payload += json_quote(runner->runtime_alerts[index]);
        }
        payload += "],\"runtimeWarnings\":[";
        for (std::size_t index = 0; index < runner->runtime_warnings.size(); ++index) {
            if (index > 0) {
                payload += ",";
            }
            payload += json_quote(runner->runtime_warnings[index]);
        }
        payload += "]";
    }

    payload += "}";
    return payload;
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
    event.metadata.source_timestamp_ms = now_wall_clock_ms();

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
    // Bloque A — reglas de eventos.
    add("like_use_magnitude"); out << ",";
    add("mult_subscriber"); out << ",";
    add("mult_follower"); out << ",";
    add("mult_moderator"); out << ",";
    add("cap_per_event_s"); out << ",";
    add("cap_per_user_per_minute_s"); out << ",";
    add("cap_total_per_minute_s"); out << ",";
    add("floor_time_s"); out << ",";
    add("gift_tiers"); out << ",";
    add("title_text"); out << ",";
    add("subtitle_text"); out << ",";
    add("on_complete_sound_path"); out << ",";
    add("on_complete_repeat"); out << ",";
    add("on_complete_volume"); out << ",";
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
    add("pulse_speed_s"); out << ",";
    add("digit_effect"); out << ",";
    add("color_preset"); out << ",";
    // Fase 5 — motor visual (escala, marco, adornos, contorno, tiempo, estados,
    // medidor y particulas). Estas claves tienen que aparecer AQUI y en
    // handle_timer_configure: el mapeo es explicito, asi que una clave que falte
    // en cualquiera de los dos lados se ignora en silencio.
    add("scale_mode"); out << ",";
    add("canvas_width"); out << ",";
    add("canvas_height"); out << ",";
    add("frame_style"); out << ",";
    add("frame_color"); out << ",";
    add("frame_opacity"); out << ",";
    add("frame_border_px"); out << ",";
    add("frame_radius_px"); out << ",";
    add("frame_padding_px"); out << ",";
    add("frame_brackets"); out << ",";
    add("frame_grid"); out << ",";
    add("frame_scanlines"); out << ",";
    add("text_outline_px"); out << ",";
    add("text_outline_color"); out << ",";
    add("time_separator"); out << ",";
    add("show_hours"); out << ",";
    add("warn_seconds"); out << ",";
    add("danger_seconds"); out << ",";
    add("danger_effect"); out << ",";
    add("progress_style"); out << ",";
    add("progress_thickness_px"); out << ",";
    add("progress_color"); out << ",";
    add("particles_enabled"); out << ",";
    add("particles_style"); out << ",";
    add("particles_budget"); out << ",";
    add("particles_density"); out << ",";
    add("particles_force");
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
    // V2: min 0.0 (antes 1.0) para que "sin tiempo configurado" sea un estado
    // valido y persistible: el timer arranca en cero hasta que el usuario
    // configure, en vez de forzar un minimo de 1 segundo.
    if (maybe_d.has_value()) config.set("initial_time_s", std::clamp(*maybe_d, 0.0, 31536000.0));
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

    // Bloque A — reglas de eventos. Los topes y multiplicadores no pueden ser
    // negativos (signos se gestionan en el propio time_per_*).
    {
        auto maybe_b = parse_json_bool(body, "like_use_magnitude");
        if (maybe_b.has_value()) config.set("like_use_magnitude", *maybe_b);
    }
    maybe_d = parse_json_double(body, "mult_subscriber");
    if (maybe_d.has_value()) config.set("mult_subscriber", std::clamp(*maybe_d, 0.0, 1000.0));
    maybe_d = parse_json_double(body, "mult_follower");
    if (maybe_d.has_value()) config.set("mult_follower", std::clamp(*maybe_d, 0.0, 1000.0));
    maybe_d = parse_json_double(body, "mult_moderator");
    if (maybe_d.has_value()) config.set("mult_moderator", std::clamp(*maybe_d, 0.0, 1000.0));
    maybe_d = parse_json_double(body, "cap_per_event_s");
    if (maybe_d.has_value()) config.set("cap_per_event_s", std::clamp(*maybe_d, 0.0, 86400.0));
    maybe_d = parse_json_double(body, "cap_per_user_per_minute_s");
    if (maybe_d.has_value()) config.set("cap_per_user_per_minute_s", std::clamp(*maybe_d, 0.0, 86400.0));
    maybe_d = parse_json_double(body, "cap_total_per_minute_s");
    if (maybe_d.has_value()) config.set("cap_total_per_minute_s", std::clamp(*maybe_d, 0.0, 86400.0));
    maybe_d = parse_json_double(body, "floor_time_s");
    if (maybe_d.has_value()) config.set("floor_time_s", std::clamp(*maybe_d, 0.0, 31536000.0));

    maybe_d = parse_json_double(body, "on_complete_volume");
    if (maybe_d.has_value()) config.set("on_complete_volume", std::clamp(*maybe_d, 0.0, 2.0));

    auto maybe_str = parse_json_string(body, "title_text");
    if (maybe_str.has_value()) config.set("title_text", maybe_str->substr(0, 256));
    maybe_str = parse_json_string(body, "subtitle_text");
    if (maybe_str.has_value()) config.set("subtitle_text", maybe_str->substr(0, 512));
    // Bloque A / M4: tramos de regalo (texto multilinea).
    maybe_str = parse_json_string(body, "gift_tiers");
    if (maybe_str.has_value()) config.set("gift_tiers", maybe_str->substr(0, 4096));
    maybe_str = parse_json_string(body, "on_complete_sound_path");
    if (maybe_str.has_value()) config.set("on_complete_sound_path", *maybe_str);
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
    if (maybe_str.has_value()) config.set("on_complete_text", maybe_str->substr(0, 128));
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
    maybe_d = parse_json_double(body, "pulse_speed_s");
    if (maybe_d.has_value()) config.set("pulse_speed_s", std::clamp(*maybe_d, 0.3, 5.0));
    // V3: digit transition + palette
    maybe_str = parse_json_string(body, "digit_effect");
    if (maybe_str.has_value()) config.set("digit_effect", *maybe_str);
    maybe_str = parse_json_string(body, "color_preset");
    if (maybe_str.has_value()) config.set("color_preset", *maybe_str);

    // === Fase 5: motor visual ================================================
    // Los limites se repiten aqui a proposito: el motor tambien los aplica, pero
    // el HTTP no puede ser la puerta por la que entra un lienzo de 0 o una
    // opacidad de 500. Un clamp en cada capa, no en una sola.
    maybe_str = parse_json_string(body, "scale_mode");
    if (maybe_str.has_value()) config.set("scale_mode", *maybe_str);
    maybe_i64 = parse_json_uint64(body, "canvas_width");
    if (maybe_i64.has_value()) config.set("canvas_width", static_cast<std::int64_t>(std::clamp<uint64_t>(*maybe_i64, 320, 7680)));
    maybe_i64 = parse_json_uint64(body, "canvas_height");
    if (maybe_i64.has_value()) config.set("canvas_height", static_cast<std::int64_t>(std::clamp<uint64_t>(*maybe_i64, 320, 7680)));

    maybe_str = parse_json_string(body, "frame_style");
    if (maybe_str.has_value()) config.set("frame_style", *maybe_str);
    maybe_str = parse_json_string(body, "frame_color");
    if (maybe_str.has_value()) config.set("frame_color", *maybe_str);
    maybe_i64 = parse_json_uint64(body, "frame_opacity");
    if (maybe_i64.has_value()) config.set("frame_opacity", static_cast<std::int64_t>(std::clamp<uint64_t>(*maybe_i64, 0, 100)));
    maybe_i64 = parse_json_uint64(body, "frame_border_px");
    if (maybe_i64.has_value()) config.set("frame_border_px", static_cast<std::int64_t>(std::clamp<uint64_t>(*maybe_i64, 0, 12)));
    maybe_i64 = parse_json_uint64(body, "frame_radius_px");
    if (maybe_i64.has_value()) config.set("frame_radius_px", static_cast<std::int64_t>(std::clamp<uint64_t>(*maybe_i64, 0, 64)));
    maybe_i64 = parse_json_uint64(body, "frame_padding_px");
    if (maybe_i64.has_value()) config.set("frame_padding_px", static_cast<std::int64_t>(std::clamp<uint64_t>(*maybe_i64, 0, 120)));
    maybe_bool = parse_json_bool(body, "frame_brackets");
    if (maybe_bool.has_value()) config.set("frame_brackets", *maybe_bool);
    maybe_bool = parse_json_bool(body, "frame_grid");
    if (maybe_bool.has_value()) config.set("frame_grid", *maybe_bool);
    maybe_bool = parse_json_bool(body, "frame_scanlines");
    if (maybe_bool.has_value()) config.set("frame_scanlines", *maybe_bool);

    maybe_i64 = parse_json_uint64(body, "text_outline_px");
    if (maybe_i64.has_value()) config.set("text_outline_px", static_cast<std::int64_t>(std::clamp<uint64_t>(*maybe_i64, 0, 8)));
    maybe_str = parse_json_string(body, "text_outline_color");
    if (maybe_str.has_value()) config.set("text_outline_color", *maybe_str);

    maybe_str = parse_json_string(body, "time_separator");
    if (maybe_str.has_value()) config.set("time_separator", maybe_str->substr(0, 4));
    maybe_bool = parse_json_bool(body, "show_hours");
    if (maybe_bool.has_value()) config.set("show_hours", *maybe_bool);

    maybe_i64 = parse_json_uint64(body, "warn_seconds");
    if (maybe_i64.has_value()) config.set("warn_seconds", static_cast<std::int64_t>(std::clamp<uint64_t>(*maybe_i64, 0, 3600)));
    maybe_i64 = parse_json_uint64(body, "danger_seconds");
    if (maybe_i64.has_value()) config.set("danger_seconds", static_cast<std::int64_t>(std::clamp<uint64_t>(*maybe_i64, 0, 3600)));
    maybe_str = parse_json_string(body, "danger_effect");
    if (maybe_str.has_value()) config.set("danger_effect", *maybe_str);

    maybe_str = parse_json_string(body, "progress_style");
    if (maybe_str.has_value()) config.set("progress_style", *maybe_str);
    maybe_i64 = parse_json_uint64(body, "progress_thickness_px");
    if (maybe_i64.has_value()) config.set("progress_thickness_px", static_cast<std::int64_t>(std::clamp<uint64_t>(*maybe_i64, 1, 40)));
    maybe_str = parse_json_string(body, "progress_color");
    if (maybe_str.has_value()) config.set("progress_color", *maybe_str);

    maybe_bool = parse_json_bool(body, "particles_enabled");
    if (maybe_bool.has_value()) config.set("particles_enabled", *maybe_bool);
    maybe_str = parse_json_string(body, "particles_style");
    if (maybe_str.has_value()) config.set("particles_style", *maybe_str);
    maybe_i64 = parse_json_uint64(body, "particles_budget");
    if (maybe_i64.has_value()) config.set("particles_budget", static_cast<std::int64_t>(std::clamp<uint64_t>(*maybe_i64, 10, 300)));
    maybe_d = parse_json_double(body, "particles_density");
    if (maybe_d.has_value()) config.set("particles_density", std::clamp(*maybe_d, 0.25, 2.0));
    maybe_bool = parse_json_bool(body, "particles_force");
    if (maybe_bool.has_value()) config.set("particles_force", *maybe_bool);

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

// Normalize TikTok username: @user, https://tiktok.com/@user, etc. -> "user"
std::string normalize_tiktok_user(std::string_view raw) {
    std::string s(raw);
    // Remove @ prefix
    if (!s.empty() && s[0] == '@') {
        s.erase(0, 1);
    }
    // Remove URL prefix
    const std::string prefixes[] = {
        "https://www.tiktok.com/@",
        "https://tiktok.com/@",
        "http://www.tiktok.com/@",
        "http://tiktok.com/@",
        "www.tiktok.com/@",
        "tiktok.com/@"
    };
    for (const auto& prefix : prefixes) {
        if (s.rfind(prefix, 0) == 0) {
            s = s.substr(prefix.size());
            break;
        }
    }
    // Remove trailing path/query
    const auto slash = s.find('/');
    if (slash != std::string::npos) {
        s = s.substr(0, slash);
    }
    const auto query = s.find('?');
    if (query != std::string::npos) {
        s = s.substr(0, query);
    }
    // Lowercase
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool is_valid_tiktok_user(std::string_view user) {
    if (user.empty() || user.size() > 24) return false;
    // TikTok username: alphanumeric + underscore + period, no consecutive periods
    bool prev_dot = false;
    for (char c : user) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.') {
            if (c == '.') {
                if (prev_dot) return false;
                prev_dot = true;
            } else {
                prev_dot = false;
            }
        } else {
            return false;
        }
    }
    return !user.empty() && user.front() != '.' && user.back() != '.';
}

std::string handle_bridge_connect(PanelApp* app, std::string_view body) {
    if (app == nullptr) {
        return nlp3::platform::build_panel_http_error_json("panel unavailable");
    }

    auto target_user = parse_json_string(body, "target_user").value_or("");
    target_user = normalize_tiktok_user(target_user);

    if (!is_valid_tiktok_user(target_user)) {
        return make_bridge_result(false, "invalid_tiktok_user", "Ese usuario de TikTok no es valido. Revisa el @ y volve a intentar.", nullptr);
    }

    auto api_key = parse_json_string(body, "api_key").value_or("");
    auto provider = to_lower_copy(parse_json_string(body, "provider").value_or("tiktools"));
    if (provider == "tiktok_live" || provider == "tiktoklive") {
        provider = "direct";
    }
    if (provider != "tiktools" && provider != "direct" && provider != "euler") {
        return make_bridge_result(false, "invalid_tiktok_provider", "Proveedor no valido. Opciones: tik.tools, Euler Stream o directo.", nullptr);
    }

    // 1. Guardar en config persistente (siempre, sin importar bridge mode)
    const auto previous_provider = app->config().tiktok_provider;
    app->config().external_target_user = target_user;

    // Credenciales: la key de la UI se guarda cifrada en la boveda (DPAPI) y el
    // pool se entrega al runner por archivo transitorio, nunca por argv.
    if (!api_key.empty() && (provider == "tiktools" || provider == "euler")) {
        auto& vault = app->bridge_key_vault();
        const auto key_label = parse_json_string(body, "key_label").value_or("");
        bool stored = false;
        const auto& entries = vault.entries();
        for (std::size_t index = 0; index < entries.size(); ++index) {
            if (entries[index].secret == api_key) {
                stored = vault.replace(index, key_label.empty() ? entries[index].label : key_label, api_key);
                break;
            }
        }
        if (!stored) {
            stored = vault.add(key_label, api_key);
        }
        if (stored) {
            app->save_bridge_key_vault();
        }
    }

    app->config().tiktok_provider = provider;

    // Si no está en modo external, forzarlo y pedir reinicio
    if (!app->is_external_bridge_mode()) {
        app->config().bridge_mode = "external";
        app->save_config();
        return make_bridge_result(
            false,
            "bridge_not_external_mode_saved",
            "Configuracion guardada. El panel necesita reiniciarse en modo external para conectar.",
            nullptr);
    }

    app->save_config();

    // 2. Asegurar WS server en el mejor puerto disponible (no depende de 8765)
    auto ws = app->external_ws_status();
    std::uint16_t bound_port = 0;
    if (!ws.running || ws.port == 0) {
        if (!app->start_external_ws_auto(bound_port)) {
            return make_bridge_result(
                false,
                "ws_start_failed",
                "No se pudo abrir el WebSocket interno del panel (probados los puertos 8765-8795 y uno efimero). Cerra otros paneles y volve a intentar.",
                nullptr);
        }
    } else {
        bound_port = ws.port;
    }

    // 3. Iniciar runner (lanza Python bridge)
    auto runner = app->external_runner_status();
    // Rotar de cuenta o de proveedor exige reiniciar el runner: si sigue vivo
    // con el usuario anterior, el panel mostraba "conectado" al usuario viejo.
    const auto provider_changed = previous_provider != provider;
    const auto target_changed = runner.running
        && !runner.target_user.empty()
        && runner.target_user != target_user;
    if (runner.running && (target_changed || provider_changed)) {
        app->stop_external_runner();
        runner = app->external_runner_status();
    }
    if (!runner.running) {
        if (!app->start_external_runner(target_user, 0)) {
            const auto failed_runner = app->external_runner_status();
            std::string reason = !failed_runner.runtime_summary.empty()
                ? failed_runner.runtime_summary
                : failed_runner.last_error;
            if (reason.empty()) {
                reason = "No se pudo iniciar el bridge de TikTok.";
            }
            return make_bridge_result(false, "runner_start_failed", reason, &failed_runner);
        }
    }

    return make_bridge_result(true, "bridge_connected", "Bridge conectado. Escuchando el live de TikTok.", nullptr);
}

std::string handle_bridge_disconnect(PanelApp* app) {
    if (app == nullptr) {
        return nlp3::platform::build_panel_http_error_json("panel unavailable");
    }
    if (!app->is_external_bridge_mode()) {
        return make_simple_result(false, "bridge_not_external_mode");
    }

    app->stop_external_runner();
    app->stop_external_ws();
    app->config().external_target_user.clear();
    app->save_config();

    return make_simple_result(true, "bridge_disconnected");
}

std::string handle_bridge_status(PanelApp* app) {
    if (app == nullptr) {
        return nlp3::platform::build_panel_http_error_json("panel unavailable");
    }

    auto ws = app->external_ws_status();
    auto runner = app->external_runner_status();
    const auto& cfg = app->config();

    // Build JSON manually for bridge status
    std::ostringstream out;
    out << "{"
        << "\"configured\":" << (cfg.external_target_user.empty() ? "false" : "true") << ","
        << "\"target_user\":" << json_quote(cfg.external_target_user) << ","
        << "\"api_key_configured\":" << (cfg.provider_api_key.empty() ? "false" : "true") << ","
        << "\"provider\":" << json_quote(cfg.tiktok_provider) << ","
        << "\"ws_port\":" << (cfg.external_ws_port == 0 ? 8765 : cfg.external_ws_port) << ","
        << "\"ws_running\":" << (ws.running ? "true" : "false") << ","
        << "\"ws_port_actual\":" << ws.port << ","
        << "\"ws_accepted_messages\":" << ws.accepted_messages << ","
        << "\"ws_rejected_messages\":" << ws.rejected_messages << ","
        << "\"runner_running\":" << (runner.running ? "true" : "false") << ","
        << "\"runner_pid\":" << runner.process_id << ","
        << "\"runner_ws_url\":" << json_quote(runner.ws_url) << ","
        << "\"runner_error\":" << json_quote(runner.last_error) << ","
        << "\"runtime_ready\":" << (runner.runtime_ready ? "true" : "false") << ","
        << "\"runtime_checked\":" << (runner.runtime_checked ? "true" : "false") << ","
        << "\"runtime_summary\":" << json_quote(runner.runtime_summary) << ","
        << "\"runtime_warnings\":[";
    for (std::size_t index = 0; index < runner.runtime_warnings.size(); ++index) {
        if (index > 0) {
            out << ",";
        }
        out << json_quote(runner.runtime_warnings[index]);
    }
    out << "],"
        << "\"connection_state\":" << json_quote(app->external_bridge_manifest().connection_state)
        << "}";
    return out.str();
}

std::string handle_diagnostics_ports(PanelApp* app) {
    if (app == nullptr) {
        return nlp3::platform::build_panel_http_error_json("panel unavailable");
    }

    // Usar PortZombieDetector real
    auto owned = nlp3::platform::PortZombieDetector::scan_owned_ports();
    auto zombies = nlp3::platform::PortZombieDetector::detect_zombies();

    std::ostringstream out;
    out << "{"
        << "\"timestamp_ms\":" << now_wall_clock_ms() << ","
        << "\"owned_ports\":[";
    for (size_t i = 0; i < owned.size(); ++i) {
        if (i > 0) out << ",";
        out << "{"
            << "\"port\":" << owned[i].port << ","
            << "\"in_use\":" << (owned[i].in_use ? "true" : "false") << ","
            << "\"pid\":" << owned[i].pid << ","
            << "\"process_name\":" << json_quote(owned[i].process_name) << ","
            << "\"state\":" << json_quote(owned[i].state) << ","
            << "\"is_zombie\":" << (owned[i].is_zombie ? "true" : "false")
            << "}";
    }
    out << "],"
        << "\"zombies\":[";
    for (size_t i = 0; i < zombies.size(); ++i) {
        if (i > 0) out << ",";
        out << "{"
            << "\"port\":" << zombies[i].port << ","
            << "\"pid\":" << zombies[i].pid << ","
            << "\"process_name\":" << json_quote(zombies[i].process_name) << ","
            << "\"state\":" << json_quote(zombies[i].state)
            << "}";
    }
    out << "],"
        << "\"clean\":" << (zombies.empty() ? "true" : "false") << ","
        << "\"bridge_ws_port_free\":" << (nlp3::platform::PortZombieDetector::is_port_free(nlp3::platform::PortZombieDetector::BRIDGE_WS_PORT) ? "true" : "false") << ","
        << "\"overlay_http_port_free\":" << (nlp3::platform::PortZombieDetector::is_port_free(nlp3::platform::PortZombieDetector::OVERLAY_HTTP_PORT) ? "true" : "false")
        << "}";
    return out.str();
}

/// Fase 3: rutas que si puede servir el listener expuesto por el tunel.
/// Todo lo demas (UI, /api/state, licencia, metricas, /status) queda fuera.
bool is_overlay_only_path(std::string_view path) {
    return path.rfind("/api/overlay/", 0) == 0 || path == "/health";
}

/// Cabeceras CORS del endpoint de estado del overlay, consumido por la pagina
/// estatica publica desde otro origen.
constexpr std::string_view kOverlayCorsHeaders =
    "Access-Control-Allow-Origin: *\r\nVary: Origin\r\n";

std::string build_route_response(
    PanelApp* app,
    const ParsedRequest& request,
    const PanelHttpServerStatus& status,
    bool overlay_only) {
    if (!request_origin_allowed(request, status)) {
        return make_http_response("403 Forbidden", "application/json; charset=utf-8", make_origin_forbidden_result());
    }

    if (overlay_only) {
        if (request.method == "OPTIONS" && is_overlay_only_path(request.path)) {
            return make_http_response(
                "204 No Content",
                "text/plain; charset=utf-8",
                {},
                "Access-Control-Allow-Origin: *\r\n"
                "Access-Control-Allow-Methods: GET, OPTIONS\r\n"
                "Access-Control-Max-Age: 600\r\n");
        }
        if (!is_overlay_only_path(request.path)) {
            return make_http_response(
                "404 Not Found",
                "application/json; charset=utf-8",
                "{\"error\":\"not_found\"}");
        }
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
        // Fase 3: esta respuesta la consume la pagina estatica publica
        // (https://nisoje.com/overlay/live-timer) desde otro origen, asi que
        // necesita CORS para que el fetch del navegador la pueda leer. Es un GET
        // de solo lectura con datos que ya se emiten en directo en el stream.
        if (app == nullptr) {
            return make_http_response(
                "200 OK",
                "application/json; charset=utf-8",
                nlp3::platform::build_live_timer_state_json(nullptr),
                kOverlayCorsHeaders);
        }
        return make_http_response(
            "200 OK",
            "application/json; charset=utf-8",
            nlp3::platform::build_live_timer_state_json(app->live_timer()),
            kOverlayCorsHeaders);
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
    if (request.method == "GET" && request.path == "/api/bridge/keys") {
        return make_http_response("200 OK", "application/json; charset=utf-8", make_bridge_keys_result(app));
    }
    if (request.method == "POST" && request.path == "/api/bridge/keys/add") {
        return make_http_response("200 OK", "application/json; charset=utf-8", handle_bridge_key_add(app, request.body));
    }
    if (request.method == "POST" && request.path == "/api/bridge/keys/remove") {
        return make_http_response("200 OK", "application/json; charset=utf-8", handle_bridge_key_remove(app, request.body));
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
    // Bridge API
    if (request.method == "POST" && request.path == "/api/bridge/connect") {
        return make_http_response("200 OK", "application/json; charset=utf-8", handle_bridge_connect(app, request.body));
    }
    if (request.method == "POST" && request.path == "/api/bridge/disconnect") {
        return make_http_response("200 OK", "application/json; charset=utf-8", handle_bridge_disconnect(app));
    }
    if (request.method == "GET" && request.path == "/api/bridge/status") {
        return make_http_response("200 OK", "application/json; charset=utf-8", handle_bridge_status(app));
    }
    // Diagnostics
    if (request.method == "GET" && request.path == "/api/diagnostics/ports") {
        return make_http_response("200 OK", "application/json; charset=utf-8", handle_diagnostics_ports(app));
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

PanelHttpServer::PanelHttpServer(PanelApp* app, bool overlay_only) noexcept
    : app_(app)
    , overlay_only_(overlay_only) {
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

    // Winsock crea sockets heredables y el panel lanza hijos con
    // bInheritHandles=TRUE: sin esto el listener queda en el hijo y el puerto
    // sobrevive como zombie cuando el panel termina.
    SetHandleInformation(
        reinterpret_cast<HANDLE>(listen_socket),
        HANDLE_FLAG_INHERIT,
        0);

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
    // Puerto efimero (port == 0): lo elige el SO y hay que leer cual es, porque
    // Fase 3 apunta el tunel al listener "solo overlay" en un puerto efimero.
    std::uint16_t bound_port = port;
    if (bound_port == 0) {
        sockaddr_in local_address{};
        int local_length = static_cast<int>(sizeof(local_address));
        if (getsockname(
                listen_socket,
                reinterpret_cast<sockaddr*>(&local_address),
                &local_length) == 0) {
            bound_port = ntohs(local_address.sin_port);
        }
    }
    status_.running = true;
    status_.port = bound_port;
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
            // R2: reject oversized requests before parsing to prevent OOM.
            constexpr std::size_t kMaxRequestBytes = 1 * 1024 * 1024;  // 1 MB
            if (request_buffer_.size() > kMaxRequestBytes) {
                pending_response_ = make_http_response(
                    "413 Payload Too Large", "text/plain; charset=utf-8",
                    "request body too large");
                request_buffer_.clear();
                close_client = true;
            } else {
                const auto parsed = parse_request_buffer(request_buffer_);
                if (parsed.ready) {
                    pending_response_ = build_route_response(app_, parsed, status_, overlay_only_);
                    request_buffer_.clear();
                }
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

bool PanelHttpServer::overlay_only() const noexcept {
    return overlay_only_;
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
