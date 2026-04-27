#include "platform/server_license_service.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

#include "platform/win_http_client.hpp"

namespace {

using nlp3::platform::LicenseSnapshot;
using nlp3::platform::LicenseStatus;
using nlp3::platform::PanelAuthConfig;
using nlp3::platform::PanelAuthLoginRequest;
using nlp3::platform::PanelAuthLoginResult;
using nlp3::platform::PanelAuthStatus;
using nlp3::platform::HttpHeader;

std::string trim_copy(std::string_view value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) {
        return {};
    }

    const auto end = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(begin, end - begin + 1));
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

bool equals_ignore_case(std::string_view left, std::string_view right) {
    return to_lower_copy(trim_copy(left)) == to_lower_copy(trim_copy(right));
}

std::int64_t now_wall_clock_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string ensure_leading_slash(std::string_view path) {
    std::string normalized = trim_copy(path);
    if (normalized.empty()) {
        normalized = "/";
    } else if (normalized.front() != '/') {
        normalized.insert(normalized.begin(), '/');
    }
    return normalized;
}

std::string strip_trailing_slash(std::string_view value) {
    std::string normalized = trim_copy(value);
    while (!normalized.empty() && normalized.back() == '/') {
        normalized.pop_back();
    }
    return normalized;
}

std::string url_encode(std::string_view value) {
    constexpr char kHex[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(value.size() * 3);
    for (const auto ch : value) {
        const auto byte = static_cast<unsigned char>(ch);
        if ((byte >= 'A' && byte <= 'Z')
            || (byte >= 'a' && byte <= 'z')
            || (byte >= '0' && byte <= '9')
            || byte == '-'
            || byte == '_'
            || byte == '.'
            || byte == '~') {
            encoded.push_back(static_cast<char>(byte));
            continue;
        }

        encoded.push_back('%');
        encoded.push_back(kHex[(byte >> 4) & 0x0F]);
        encoded.push_back(kHex[byte & 0x0F]);
    }
    return encoded;
}

std::string join_url(std::string_view base, std::string_view path) {
    return strip_trailing_slash(base) + ensure_leading_slash(path);
}

PanelAuthLoginResult make_login_result(
    bool ok,
    std::string message,
    std::string error_code,
    const LicenseSnapshot& license,
    const PanelAuthStatus& auth) {
    PanelAuthLoginResult result{};
    result.ok = ok;
    result.message = std::move(message);
    result.error_code = std::move(error_code);
    result.license = license;
    result.auth = auth;
    return result;
}

#ifdef _WIN32
std::string firebase_error_code_from_body(const std::string& body) {
    const auto json = nlohmann::json::parse(body, nullptr, false);
    if (json.is_discarded()) {
        return {};
    }
    const auto* error_object = json.contains("error") && json["error"].is_object()
        ? &json["error"]
        : nullptr;
    if (error_object == nullptr) {
        return {};
    }
    if (error_object->contains("message") && (*error_object)["message"].is_string()) {
        return (*error_object)["message"].get<std::string>();
    }
    return {};
}

std::string humanize_firebase_error(std::string_view code) {
    const auto normalized = to_lower_copy(code);
    if (normalized == "invalid_login_credentials" || normalized == "email_not_found" || normalized == "invalid_password") {
        return "Usuario o contrasena incorrectos.";
    }
    if (normalized == "user_disabled") {
        return "La cuenta esta deshabilitada.";
    }
    if (normalized == "too_many_attempts_try_later") {
        return "Demasiados intentos. Intenta mas tarde.";
    }
    if (normalized == "network_request_failed") {
        return "No se pudo contactar el servidor de autenticacion.";
    }
    return "No se pudo validar la cuenta.";
}

std::string me_licenses_error_message(const std::string& body) {
    const auto json = nlohmann::json::parse(body, nullptr, false);
    if (json.is_discarded()) {
        return {};
    }
    if (json.contains("message") && json["message"].is_string()) {
        return json["message"].get<std::string>();
    }
    return {};
}

#endif

LicenseSnapshot local_dev_license_snapshot() {
    return LicenseSnapshot{
        LicenseStatus::active,
        "local development mode",
        "local-dev",
    };
}

PanelAuthStatus local_dev_auth_snapshot() {
    PanelAuthStatus auth{};
    auth.required = false;
    auth.authenticated = true;
    auth.message = "local development mode";
    return auth;
}

LicenseSnapshot locked_license_snapshot() {
    return LicenseSnapshot{
        LicenseStatus::inactive,
        "access validation required",
        "remote",
    };
}

PanelAuthStatus locked_auth_snapshot() {
    PanelAuthStatus auth{};
    auth.required = true;
    auth.authenticated = false;
    auth.message = "Ingresa tus credenciales para habilitar el panel.";
    return auth;
}

} // namespace

namespace nlp3::platform {

ServerLicenseService::ServerLicenseService(PanelAuthConfig config) noexcept
    : config_(std::move(config)) {
    reset_for_current_mode();
}

LicenseSnapshot ServerLicenseService::snapshot() const {
    return license_snapshot_;
}

PanelAuthStatus ServerLicenseService::auth_snapshot() const {
    return auth_snapshot_;
}

std::string ServerLicenseService::id_token() const {
    return id_token_;
}

bool ServerLicenseService::access_granted() const noexcept {
    return !config_.required || auth_snapshot_.authenticated;
}

bool ServerLicenseService::access_required() const noexcept {
    return config_.required;
}

PanelAuthLoginResult ServerLicenseService::authenticate(const PanelAuthLoginRequest& request) {
    const auto email = trim_copy(request.email);
    const auto password = request.password;
    const auto license_key = trim_copy(request.license_key);

    if (!config_.required) {
        reset_for_current_mode();
        return make_login_result(true, "auth_not_required", {}, license_snapshot_, auth_snapshot_);
    }

    if (email.empty() || password.empty() || license_key.empty()) {
        auth_snapshot_ = locked_auth_snapshot();
        auth_snapshot_.last_error_code = "missing_fields";
        auth_snapshot_.message = "Completa usuario, contrasena y licencia.";
        license_snapshot_ = locked_license_snapshot();
        license_snapshot_.message = auth_snapshot_.message;
        return make_login_result(false, auth_snapshot_.message, auth_snapshot_.last_error_code, license_snapshot_, auth_snapshot_);
    }

    if (trim_copy(config_.firebase_api_key).empty()
        || trim_copy(config_.nisoje_api_base).empty()
        || trim_copy(config_.me_licenses_path).empty()) {
        auth_snapshot_ = locked_auth_snapshot();
        auth_snapshot_.last_error_code = "auth_config_missing";
        auth_snapshot_.message = "La configuracion remota de acceso esta incompleta.";
        license_snapshot_ = locked_license_snapshot();
        license_snapshot_.message = auth_snapshot_.message;
        return make_login_result(false, auth_snapshot_.message, auth_snapshot_.last_error_code, license_snapshot_, auth_snapshot_);
    }

#ifdef _WIN32
    const auto firebase_url =
        "https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key="
        + url_encode(config_.firebase_api_key);
    const auto firebase_request = nlohmann::json{
        {"email", email},
        {"password", password},
        {"returnSecureToken", true},
    };
    const auto firebase_response = http_request(
        "POST",
        firebase_url,
        firebase_request.dump(),
        "application/json; charset=utf-8");
    if (!firebase_response.error.empty()) {
        auth_snapshot_ = locked_auth_snapshot();
        auth_snapshot_.last_error_code = firebase_response.error;
        auth_snapshot_.message = "No se pudo contactar el servidor de autenticacion.";
        license_snapshot_ = locked_license_snapshot();
        license_snapshot_.message = auth_snapshot_.message;
        return make_login_result(false, auth_snapshot_.message, auth_snapshot_.last_error_code, license_snapshot_, auth_snapshot_);
    }
    if (firebase_response.status_code < 200 || firebase_response.status_code >= 300) {
        const auto firebase_code = firebase_error_code_from_body(firebase_response.body);
        auth_snapshot_ = locked_auth_snapshot();
        auth_snapshot_.last_error_code = firebase_code.empty() ? "firebase_auth_failed" : firebase_code;
        auth_snapshot_.message = humanize_firebase_error(auth_snapshot_.last_error_code);
        license_snapshot_ = locked_license_snapshot();
        license_snapshot_.message = auth_snapshot_.message;
        return make_login_result(false, auth_snapshot_.message, auth_snapshot_.last_error_code, license_snapshot_, auth_snapshot_);
    }

    const auto firebase_json = nlohmann::json::parse(firebase_response.body, nullptr, false);
    if (firebase_json.is_discarded()) {
        auth_snapshot_ = locked_auth_snapshot();
        auth_snapshot_.last_error_code = "firebase_parse_failed";
        auth_snapshot_.message = "La respuesta de autenticacion no se pudo interpretar.";
        license_snapshot_ = locked_license_snapshot();
        license_snapshot_.message = auth_snapshot_.message;
        return make_login_result(false, auth_snapshot_.message, auth_snapshot_.last_error_code, license_snapshot_, auth_snapshot_);
    }

    const auto firebase_uid = firebase_json.value("localId", std::string{});
    const auto id_token = firebase_json.value("idToken", std::string{});
    const auto resolved_email = firebase_json.value("email", email);
    if (firebase_uid.empty()) {
        auth_snapshot_ = locked_auth_snapshot();
        auth_snapshot_.last_error_code = "firebase_uid_missing";
        auth_snapshot_.message = "La autenticacion no devolvio una cuenta valida.";
        license_snapshot_ = locked_license_snapshot();
        license_snapshot_.message = auth_snapshot_.message;
        return make_login_result(false, auth_snapshot_.message, auth_snapshot_.last_error_code, license_snapshot_, auth_snapshot_);
    }

    const auto licenses_url = join_url(config_.nisoje_api_base, config_.me_licenses_path);
    std::vector<HttpHeader> license_headers{};
    if (!id_token.empty()) {
        license_headers.push_back(HttpHeader{"Authorization", "Bearer " + id_token});
    }
    const auto licenses_response = http_request("GET", licenses_url, {}, {}, license_headers);
    if (!licenses_response.error.empty()) {
        auth_snapshot_ = locked_auth_snapshot();
        auth_snapshot_.last_error_code = licenses_response.error;
        auth_snapshot_.message = "No se pudo consultar la licencia en el servidor.";
        license_snapshot_ = locked_license_snapshot();
        license_snapshot_.message = auth_snapshot_.message;
        return make_login_result(false, auth_snapshot_.message, auth_snapshot_.last_error_code, license_snapshot_, auth_snapshot_);
    }
    if (licenses_response.status_code < 200 || licenses_response.status_code >= 300) {
        auth_snapshot_ = locked_auth_snapshot();
        auth_snapshot_.last_error_code = "license_lookup_failed";
        auth_snapshot_.message = "No se pudo validar la licencia en el servidor.";
        const auto api_message = me_licenses_error_message(licenses_response.body);
        if (!api_message.empty()) {
            auth_snapshot_.message = api_message;
        }
        license_snapshot_ = locked_license_snapshot();
        license_snapshot_.message = auth_snapshot_.message;
        return make_login_result(false, auth_snapshot_.message, auth_snapshot_.last_error_code, license_snapshot_, auth_snapshot_);
    }

    const auto licenses_json = nlohmann::json::parse(licenses_response.body, nullptr, false);
    if (licenses_json.is_discarded()) {
        auth_snapshot_ = locked_auth_snapshot();
        auth_snapshot_.last_error_code = "license_parse_failed";
        auth_snapshot_.message = "La respuesta de licencias no se pudo interpretar.";
        license_snapshot_ = locked_license_snapshot();
        license_snapshot_.message = auth_snapshot_.message;
        return make_login_result(false, auth_snapshot_.message, auth_snapshot_.last_error_code, license_snapshot_, auth_snapshot_);
    }

    if (!licenses_json.value("valid", false)) {
        auth_snapshot_ = locked_auth_snapshot();
        auth_snapshot_.last_error_code = "license_account_invalid";
        auth_snapshot_.message = licenses_json.value("message", std::string{"La cuenta no tiene licencias validas."});
        license_snapshot_ = locked_license_snapshot();
        license_snapshot_.message = auth_snapshot_.message;
        return make_login_result(false, auth_snapshot_.message, auth_snapshot_.last_error_code, license_snapshot_, auth_snapshot_);
    }

    const auto licenses = licenses_json.contains("licenses") && licenses_json["licenses"].is_array()
        ? licenses_json["licenses"]
        : nlohmann::json::array();

    nlohmann::json matched_license;
    for (const auto& entry : licenses) {
        if (!entry.is_object()) {
            continue;
        }
        if (equals_ignore_case(entry.value("license_key", std::string{}), license_key)) {
            matched_license = entry;
            break;
        }
    }

    if (matched_license.is_null()) {
        auth_snapshot_ = locked_auth_snapshot();
        auth_snapshot_.last_error_code = "license_not_found";
        auth_snapshot_.message = "La licencia ingresada no pertenece a esta cuenta.";
        license_snapshot_ = locked_license_snapshot();
        license_snapshot_.message = auth_snapshot_.message;
        return make_login_result(false, auth_snapshot_.message, auth_snapshot_.last_error_code, license_snapshot_, auth_snapshot_);
    }

    if (!equals_ignore_case(matched_license.value("status", std::string{}), "active")) {
        auth_snapshot_ = locked_auth_snapshot();
        auth_snapshot_.last_error_code = "license_inactive";
        auth_snapshot_.message = "La licencia ingresada no esta activa.";
        license_snapshot_ = locked_license_snapshot();
        license_snapshot_.message = auth_snapshot_.message;
        return make_login_result(false, auth_snapshot_.message, auth_snapshot_.last_error_code, license_snapshot_, auth_snapshot_);
    }

    auth_snapshot_ = {};
    auth_snapshot_.required = true;
    auth_snapshot_.authenticated = true;
    auth_snapshot_.email = resolved_email;
    auth_snapshot_.firebase_uid = firebase_uid;
    auth_snapshot_.license_key = license_key;
    auth_snapshot_.message = "Acceso validado correctamente.";
    auth_snapshot_.last_validated_timestamp_ms = now_wall_clock_ms();

    license_snapshot_ = {};
    license_snapshot_.status = LicenseStatus::active;
    license_snapshot_.message = "remote license validated";
    license_snapshot_.tier = matched_license.value("plan_name", std::string{});
    if (license_snapshot_.tier.empty()) {
        license_snapshot_.tier = matched_license.value("tier", std::string{});
    }
    if (license_snapshot_.tier.empty()) {
        license_snapshot_.tier = "licensed";
    }
    id_token_ = id_token;

    return make_login_result(true, "auth_success", {}, license_snapshot_, auth_snapshot_);
#else
    auth_snapshot_ = locked_auth_snapshot();
    auth_snapshot_.last_error_code = "windows_only_auth_runtime";
    auth_snapshot_.message = "La validacion remota solo esta disponible en Windows.";
    license_snapshot_ = locked_license_snapshot();
    license_snapshot_.message = auth_snapshot_.message;
    return make_login_result(false, auth_snapshot_.message, auth_snapshot_.last_error_code, license_snapshot_, auth_snapshot_);
#endif
}

void ServerLicenseService::logout() noexcept {
    id_token_.clear();
    reset_for_current_mode();
}

void ServerLicenseService::update_config(PanelAuthConfig config) noexcept {
    config_ = std::move(config);
    reset_for_current_mode();
}

void ServerLicenseService::reset_for_current_mode() noexcept {
    id_token_.clear();
    if (config_.required) {
        license_snapshot_ = locked_license_snapshot();
        auth_snapshot_ = locked_auth_snapshot();
        return;
    }

    license_snapshot_ = local_dev_license_snapshot();
    auth_snapshot_ = local_dev_auth_snapshot();
}

} // namespace nlp3::platform
