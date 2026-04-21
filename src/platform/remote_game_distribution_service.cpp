#include "platform/remote_game_distribution_service.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

#include "platform/win_http_client.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <ShlObj.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#endif

namespace {

using nlp3::platform::ExternalGameManifest;
using nlp3::platform::HttpResponse;
using nlp3::platform::RemoteGameCatalogParseResult;
using nlp3::platform::RemoteGameCatalogRecord;
using nlp3::platform::RemoteGameInstallRecord;
using nlp3::platform::RemoteGameOperationStatus;

std::string trim_copy(std::string_view value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) {
        return {};
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(begin, end - begin + 1));
}

std::string to_upper_copy(std::string_view value) {
    std::string upper(value);
    std::transform(
        upper.begin(),
        upper.end(),
        upper.begin(),
        [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    return upper;
}

std::string ensure_leading_slash(std::string_view path) {
    std::string normalized = trim_copy(path);
    if (normalized.empty()) {
        return "/";
    }
    if (normalized.front() != '/') {
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

std::string join_url(std::string_view base, std::string_view path) {
    return strip_trailing_slash(base) + ensure_leading_slash(path);
}

std::int64_t now_wall_clock_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::filesystem::path resolve_managed_root() {
#ifdef _WIN32
    PWSTR local_app_data = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &local_app_data))
        && local_app_data != nullptr) {
        std::filesystem::path root(local_app_data);
        CoTaskMemFree(local_app_data);
        return root / "NisojeStudio";
    }
    if (local_app_data != nullptr) {
        CoTaskMemFree(local_app_data);
    }
#endif
    const char* raw = std::getenv("LOCALAPPDATA");
    if (raw != nullptr && *raw != '\0') {
        return std::filesystem::path(raw) / "NisojeStudio";
    }
    return std::filesystem::temp_directory_path() / "NisojeStudio";
}

std::filesystem::path resolve_managed_downloads_root(const std::filesystem::path& managed_root) {
    return managed_root / "downloads";
}

std::filesystem::path resolve_managed_games_root(const std::filesystem::path& managed_root) {
    return managed_root / "games";
}

std::filesystem::path resolve_managed_state_root(const std::filesystem::path& managed_root) {
    return managed_root / "state";
}

std::wstring widen(std::string_view value) {
    return std::wstring(value.begin(), value.end());
}

#ifdef _WIN32

std::wstring quote_windows_argument(std::wstring_view argument) {
    if (argument.empty()) {
        return L"\"\"";
    }

    if (argument.find_first_of(L" \t\n\v\"") == std::wstring_view::npos) {
        return std::wstring(argument);
    }

    std::wstring quoted;
    quoted.push_back(L'"');
    std::size_t backslash_count = 0;
    for (wchar_t ch : argument) {
        if (ch == L'\\') {
            ++backslash_count;
            continue;
        }

        if (ch == L'"') {
            quoted.append(backslash_count * 2 + 1, L'\\');
            quoted.push_back(L'"');
            backslash_count = 0;
            continue;
        }

        quoted.append(backslash_count, L'\\');
        backslash_count = 0;
        quoted.push_back(ch);
    }

    quoted.append(backslash_count * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

std::wstring build_command_line(const std::filesystem::path& executable, const std::vector<std::wstring>& args) {
    std::wstring command = quote_windows_argument(executable.wstring());
    for (const auto& arg : args) {
        command.push_back(L' ');
        command += quote_windows_argument(arg);
    }
    return command;
}

std::filesystem::path resolve_powershell_executable() {
    wchar_t buffer[MAX_PATH] = {};
    const auto length = GetSystemDirectoryW(buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return std::filesystem::path(L"powershell.exe");
    }
    return std::filesystem::path(std::wstring(buffer, buffer + length))
        / "WindowsPowerShell" / "v1.0" / "powershell.exe";
}

bool run_hidden_process(const std::filesystem::path& executable, const std::vector<std::wstring>& args) {
    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    startup_info.dwFlags = STARTF_USESHOWWINDOW;
    startup_info.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION process_info{};
    auto command_line = build_command_line(executable, args);
    auto mutable_command_line = std::vector<wchar_t>(command_line.begin(), command_line.end());
    mutable_command_line.push_back(L'\0');

    const auto created = CreateProcessW(
        executable.wstring().c_str(),
        mutable_command_line.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startup_info,
        &process_info);
    if (!created) {
        return false;
    }

    WaitForSingleObject(process_info.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(process_info.hProcess, &exit_code);
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
    return exit_code == 0;
}

std::optional<std::string> compute_file_sha256(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.good()) {
        return std::nullopt;
    }

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD hash_object_size = 0;
    DWORD hash_object_size_result = 0;
    DWORD hash_length = 0;
    DWORD hash_length_result = 0;

    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) {
        return std::nullopt;
    }
    if (BCryptGetProperty(
            algorithm,
            BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&hash_object_size),
            sizeof(hash_object_size),
            &hash_object_size_result,
            0) < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return std::nullopt;
    }
    if (BCryptGetProperty(
            algorithm,
            BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&hash_length),
            sizeof(hash_length),
            &hash_length_result,
            0) < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return std::nullopt;
    }

    std::vector<unsigned char> hash_object(hash_object_size, 0);
    std::vector<unsigned char> hash_bytes(hash_length, 0);
    if (BCryptCreateHash(
            algorithm,
            &hash,
            hash_object.data(),
            static_cast<ULONG>(hash_object.size()),
            nullptr,
            0,
            0) < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return std::nullopt;
    }

    std::vector<char> buffer(64 * 1024, '\0');
    while (input.good()) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = static_cast<ULONG>(input.gcount());
        if (count == 0) {
            break;
        }
        if (BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), count, 0) < 0) {
            BCryptDestroyHash(hash);
            BCryptCloseAlgorithmProvider(algorithm, 0);
            return std::nullopt;
        }
    }

    if (BCryptFinishHash(hash, hash_bytes.data(), static_cast<ULONG>(hash_bytes.size()), 0) < 0) {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return std::nullopt;
    }

    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);

    constexpr char kHex[] = "0123456789ABCDEF";
    std::string hex;
    hex.reserve(hash_bytes.size() * 2);
    for (unsigned char byte : hash_bytes) {
        hex.push_back(kHex[(byte >> 4) & 0x0F]);
        hex.push_back(kHex[byte & 0x0F]);
    }
    return hex;
}

bool expand_archive_with_powershell(
    const std::filesystem::path& archive_path,
    const std::filesystem::path& destination_path) {
    const auto powershell = resolve_powershell_executable();
    const std::wstring script =
        L"Expand-Archive -LiteralPath "
        + quote_windows_argument(archive_path.wstring())
        + L" -DestinationPath "
        + quote_windows_argument(destination_path.wstring())
        + L" -Force";
    return run_hidden_process(
        powershell,
        {
            L"-NoProfile",
            L"-NonInteractive",
            L"-ExecutionPolicy",
            L"Bypass",
            L"-Command",
            script,
        });
}

#endif

std::string extract_error_message(const std::string& body, std::string_view fallback) {
    const auto json = nlohmann::json::parse(body, nullptr, false);
    if (!json.is_discarded()) {
        if (json.contains("message") && json["message"].is_string()) {
            return json["message"].get<std::string>();
        }
        if (json.contains("error") && json["error"].is_string()) {
            return json["error"].get<std::string>();
        }
    }
    return std::string(fallback);
}

std::vector<RemoteGameOperationStatus>::iterator find_operation_status(
    std::vector<RemoteGameOperationStatus>& statuses,
    std::string_view game_id) {
    return std::find_if(
        statuses.begin(),
        statuses.end(),
        [&](const RemoteGameOperationStatus& item) { return item.game_id == game_id; });
}

std::vector<RemoteGameOperationStatus>::const_iterator find_operation_status(
    const std::vector<RemoteGameOperationStatus>& statuses,
    std::string_view game_id) {
    return std::find_if(
        statuses.begin(),
        statuses.end(),
        [&](const RemoteGameOperationStatus& item) { return item.game_id == game_id; });
}

std::vector<RemoteGameInstallRecord>::const_iterator find_install_record(
    const std::vector<RemoteGameInstallRecord>& entries,
    std::string_view game_id) {
    return std::find_if(
        entries.begin(),
        entries.end(),
        [&](const RemoteGameInstallRecord& item) { return item.game_id == game_id; });
}

std::string install_state_message(std::string_view state) {
    if (state == "queued") {
        return "Preparando descarga";
    }
    if (state == "downloading") {
        return "Descargando paquete";
    }
    if (state == "verifying") {
        return "Verificando integridad";
    }
    if (state == "installing") {
        return "Instalando juego";
    }
    if (state == "failed") {
        return "Error de instalacion";
    }
    return {};
}

} // namespace

namespace nlp3::platform {

RemoteGameCatalogParseResult parse_remote_game_catalog_response(std::string_view body) {
    RemoteGameCatalogParseResult result{};
    const auto json = nlohmann::json::parse(body, nullptr, false);
    if (json.is_discarded()) {
        result.message = "catalog_parse_failed";
        return result;
    }

    const auto games_json = json.contains("games") && json["games"].is_array()
        ? json["games"]
        : nlohmann::json::array();

    std::set<std::string> seen_ids{};
    for (const auto& entry : games_json) {
        if (!entry.is_object()) {
            continue;
        }

        RemoteGameCatalogRecord record{};
        record.game_id = entry.value("game_id", std::string{});
        record.display_name = entry.value("display_name", std::string{});
        record.version = entry.value("version", std::string{});
        record.source = entry.value("source", std::string{"remote"});
        record.licensed = entry.value("licensed", true);
        record.sha256 = to_upper_copy(trim_copy(entry.value("sha256", std::string{})));
        record.download_url = entry.value("download_url", std::string{});
        record.download_url_expires_at = entry.value("download_url_expires_at", std::string{});
        record.package_path = entry.value("package_path", std::string{});

        const auto manifest_json = entry.contains("manifest") && entry["manifest"].is_object()
            ? entry["manifest"]
            : nlohmann::json::object();
        record.manifest.game_id = !record.game_id.empty()
            ? record.game_id
            : manifest_json.value("gameId", std::string{});
        record.manifest.display_name = !record.display_name.empty()
            ? record.display_name
            : manifest_json.value("displayName", std::string{});
        record.manifest.version = !record.version.empty()
            ? record.version
            : manifest_json.value("version", std::string{});
        record.manifest.description = manifest_json.value("description", std::string{});
        record.manifest.author = manifest_json.value("author", std::string{});

        if (record.game_id.empty() || record.download_url.empty() || record.sha256.empty()) {
            continue;
        }
        if (!seen_ids.insert(record.game_id).second) {
            continue;
        }
        if (record.display_name.empty()) {
            record.display_name = record.game_id;
        }
        if (record.manifest.display_name.empty()) {
            record.manifest.display_name = record.display_name;
        }
        result.games.push_back(std::move(record));
    }

    result.ok = true;
    return result;
}

std::vector<RemoteGameInstallRecord> load_remote_game_install_registry(const std::filesystem::path& path) {
    std::vector<RemoteGameInstallRecord> entries{};
    std::ifstream input(path, std::ios::binary);
    if (!input.good()) {
        return entries;
    }

    const auto json = nlohmann::json::parse(input, nullptr, false);
    if (json.is_discarded() || !json.is_array()) {
        return entries;
    }

    for (const auto& item : json) {
        if (!item.is_object()) {
            continue;
        }
        RemoteGameInstallRecord record{};
        record.game_id = item.value("game_id", std::string{});
        record.display_name = item.value("display_name", std::string{});
        record.version = item.value("version", std::string{});
        record.sha256 = item.value("sha256", std::string{});
        record.install_root = std::filesystem::path(item.value("install_root", std::string{}));
        record.installed_at_ms = item.value("installed_at_ms", static_cast<std::int64_t>(0));
        if (record.game_id.empty() || record.install_root.empty()) {
            continue;
        }
        entries.push_back(std::move(record));
    }

    return entries;
}

bool save_remote_game_install_registry(
    const std::filesystem::path& path,
    const std::vector<RemoteGameInstallRecord>& entries) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return false;
    }

    nlohmann::json json = nlohmann::json::array();
    for (const auto& entry : entries) {
        json.push_back({
            {"game_id", entry.game_id},
            {"display_name", entry.display_name},
            {"version", entry.version},
            {"sha256", entry.sha256},
            {"install_root", entry.install_root.string()},
            {"installed_at_ms", entry.installed_at_ms},
        });
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.good()) {
        return false;
    }

    output << json.dump(2) << "\n";
    return output.good();
}

RemoteGameDistributionService::RemoteGameDistributionService(PanelAuthConfig config) noexcept
    : config_(std::move(config)),
      managed_root_(resolve_managed_root()),
      registry_path_(resolve_managed_state_root(managed_root_) / "remote_game_installs.json"),
      install_registry_(load_remote_game_install_registry(registry_path_)) {
}

RemoteGameDistributionService::~RemoteGameDistributionService() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_requested_ = true;
    }
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

void RemoteGameDistributionService::update_config(PanelAuthConfig config) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = std::move(config);
}

void RemoteGameDistributionService::set_access_context(
    std::string firebase_uid,
    std::string email,
    std::string id_token,
    std::string license_key) {
    std::lock_guard<std::mutex> lock(mutex_);
    firebase_uid_ = std::move(firebase_uid);
    email_ = std::move(email);
    id_token_ = std::move(id_token);
    license_key_ = std::move(license_key);
}

void RemoteGameDistributionService::clear_access_context() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    firebase_uid_.clear();
    email_.clear();
    id_token_.clear();
    license_key_.clear();
    catalog_.clear();
}

bool RemoteGameDistributionService::refresh_catalog(std::string* error_message) {
    PanelAuthConfig config_copy{};
    std::string token{};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        config_copy = config_;
        token = id_token_;
    }

    if (trim_copy(config_copy.nisoje_api_base).empty() || trim_copy(config_copy.me_games_catalog_path).empty()) {
        if (error_message != nullptr) {
            *error_message = "remote_catalog_config_missing";
        }
        return false;
    }

    if (trim_copy(token).empty()) {
        if (error_message != nullptr) {
            *error_message = "remote_catalog_auth_missing";
        }
        return false;
    }

    std::vector<HttpHeader> headers{
        HttpHeader{"Authorization", "Bearer " + token},
    };
    const auto response = http_request(
        "GET",
        join_url(config_copy.nisoje_api_base, config_copy.me_games_catalog_path),
        {},
        {},
        headers);
    if (!response.error.empty()) {
        if (error_message != nullptr) {
            *error_message = response.error;
        }
        return false;
    }
    if (response.status_code < 200 || response.status_code >= 300) {
        if (error_message != nullptr) {
            *error_message = extract_error_message(response.body, "remote_catalog_request_failed");
        }
        return false;
    }

    const auto parsed = parse_remote_game_catalog_response(response.body);
    if (!parsed.ok) {
        if (error_message != nullptr) {
            *error_message = parsed.message.empty() ? "remote_catalog_parse_failed" : parsed.message;
        }
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    catalog_ = parsed.games;
    return true;
}

std::vector<gamesdk::GameCatalogEntry> RemoteGameDistributionService::catalog_entries() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<gamesdk::GameCatalogEntry> entries{};
    entries.reserve(catalog_.size());

    for (const auto& item : catalog_) {
        const auto installed_it = find_install_record(install_registry_, item.game_id);
        const auto installed = installed_it != install_registry_.end();
        const auto installed_version = installed ? installed_it->version : std::string{};
        const auto update_available = installed && !item.version.empty() && item.version != installed_version;

        gamesdk::GameCatalogEntry entry{};
        entry.game_id = item.game_id;
        entry.display_name = item.display_name;
        entry.version = !item.version.empty() ? item.version : "remote";
        entry.source = installed ? "remote-installed" : item.source;
        entry.installed = installed;
        entry.enabled = item.licensed;
        entry.update_available = update_available;
        entry.manifest = item.manifest;
        if (entry.manifest.game_id.empty()) {
            entry.manifest.game_id = entry.game_id;
        }
        if (entry.manifest.display_name.empty()) {
            entry.manifest.display_name = entry.display_name;
        }
        if (entry.manifest.version.empty()) {
            entry.manifest.version = entry.version;
        }

        const auto operation_it = find_operation_status(operation_statuses_, item.game_id);
        if (operation_it != operation_statuses_.end()) {
            entry.install_state = operation_it->state;
            entry.install_message = operation_it->message;
            entry.install_progress_percent = operation_it->progress_percent;
        } else if (update_available) {
            entry.install_message = "Nueva version disponible";
        }

        entries.push_back(std::move(entry));
    }

    return entries;
}

std::vector<ExternalGameManifest> RemoteGameDistributionService::installed_game_manifests() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ExternalGameManifest> manifests{};
    manifests.reserve(install_registry_.size());
    for (const auto& entry : install_registry_) {
        auto manifest = load_external_game_manifest(entry.install_root);
        if (!manifest.has_value()) {
            continue;
        }
        manifests.push_back(std::move(*manifest));
    }
    return manifests;
}

std::vector<RemoteGameOperationStatus> RemoteGameDistributionService::operation_statuses() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return operation_statuses_;
}

PanelCommandResult RemoteGameDistributionService::start_download(const std::string& game_id) {
    join_worker_if_idle();

    std::lock_guard<std::mutex> lock(mutex_);
    if (worker_running_) {
        return {false, "game_download_busy"};
    }

    const auto catalog_it = std::find_if(
        catalog_.begin(),
        catalog_.end(),
        [&](const RemoteGameCatalogRecord& item) { return item.game_id == game_id; });
    if (catalog_it == catalog_.end()) {
        return {false, "game_not_found_in_remote_catalog"};
    }

    const auto installed_it = find_install_record(install_registry_, game_id);
    if (installed_it != install_registry_.end()
        && installed_it->version == catalog_it->version
        && !catalog_it->version.empty()) {
        return {false, "game_already_installed"};
    }

    worker_running_ = true;
    stop_requested_ = false;
    const auto operation_it = find_operation_status(operation_statuses_, game_id);
    if (operation_it == operation_statuses_.end()) {
        operation_statuses_.push_back(RemoteGameOperationStatus{
            game_id,
            "queued",
            install_state_message("queued"),
            0,
        });
    } else {
        operation_it->state = "queued";
        operation_it->message = install_state_message("queued");
        operation_it->progress_percent = 0;
    }

    auto record = *catalog_it;
    worker_thread_ = std::thread([this, record]() mutable {
        run_install_job(std::move(record));
    });
    return {true, "game_download_started"};
}

std::filesystem::path RemoteGameDistributionService::managed_root() const {
    return managed_root_;
}

std::filesystem::path RemoteGameDistributionService::managed_games_root() const {
    return resolve_managed_games_root(managed_root_);
}

std::filesystem::path RemoteGameDistributionService::registry_path() const {
    return registry_path_;
}

void RemoteGameDistributionService::join_worker_if_idle() {
    std::thread thread_to_join{};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!worker_thread_.joinable() || worker_running_) {
            return;
        }
        thread_to_join = std::move(worker_thread_);
    }

    if (thread_to_join.joinable()) {
        thread_to_join.join();
    }
}

void RemoteGameDistributionService::run_install_job(RemoteGameCatalogRecord record) {
    auto set_status = [&](std::string state, std::string message, std::uint64_t progress) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto operation_it = find_operation_status(operation_statuses_, record.game_id);
        if (operation_it == operation_statuses_.end()) {
            operation_statuses_.push_back(RemoteGameOperationStatus{
                record.game_id,
                std::move(state),
                std::move(message),
                progress,
            });
            return;
        }
        operation_it->state = std::move(state);
        operation_it->message = std::move(message);
        operation_it->progress_percent = progress;
    };

    auto finish = [&](bool success, std::string state, std::string message, std::uint64_t progress) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto operation_it = find_operation_status(operation_statuses_, record.game_id);
            if (operation_it != operation_statuses_.end()) {
                operation_it->state = std::move(state);
                operation_it->message = std::move(message);
                operation_it->progress_percent = progress;
            }
            worker_running_ = false;
        }
        if (success) {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto operation_it = find_operation_status(operation_statuses_, record.game_id);
            if (operation_it != operation_statuses_.end()) {
                operation_it->state.clear();
                operation_it->message.clear();
                operation_it->progress_percent = 0;
            }
        }
    };

#ifdef _WIN32
    const auto downloads_root = resolve_managed_downloads_root(managed_root_);
    const auto games_root = resolve_managed_games_root(managed_root_);
    const auto archive_path = downloads_root / (record.game_id + "-" + record.version + ".zip");
    const auto install_root = games_root / record.game_id / record.version;
    std::vector<HttpHeader> download_headers{};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto token = trim_copy(id_token_);
        if (!token.empty()) {
            download_headers.push_back(HttpHeader{"Authorization", "Bearer " + token});
        }
    }
    if (download_headers.empty()) {
        finish(false, "failed", "download_auth_missing", 0);
        return;
    }

    set_status("downloading", install_state_message("downloading"), 1);
    const auto download_response = download_to_file(
        record.download_url,
        archive_path,
        download_headers,
        [&](std::uint64_t bytes_downloaded, std::uint64_t total_bytes) {
            std::uint64_t progress = 50;
            if (total_bytes > 0) {
                progress = std::min<std::uint64_t>(90, (bytes_downloaded * 90) / total_bytes);
            }
            set_status("downloading", install_state_message("downloading"), progress);
        });
    if (!download_response.error.empty()) {
        finish(false, "failed", download_response.error, 0);
        return;
    }
    if (download_response.status_code < 200 || download_response.status_code >= 300) {
        finish(false, "failed", "package_download_failed", 0);
        return;
    }

    set_status("verifying", install_state_message("verifying"), 92);
    const auto computed_hash = compute_file_sha256(archive_path);
    if (!computed_hash.has_value()) {
        finish(false, "failed", "package_hash_failed", 0);
        return;
    }
    if (to_upper_copy(trim_copy(*computed_hash)) != to_upper_copy(trim_copy(record.sha256))) {
        finish(false, "failed", "package_hash_mismatch", 0);
        return;
    }

    std::error_code error;
    std::filesystem::remove_all(install_root, error);
    error.clear();
    std::filesystem::create_directories(install_root, error);
    if (error) {
        finish(false, "failed", "game_install_directory_create_failed", 0);
        return;
    }

    set_status("installing", install_state_message("installing"), 96);
    if (!expand_archive_with_powershell(archive_path, install_root)) {
        finish(false, "failed", "package_extract_failed", 0);
        return;
    }

    auto manifest = load_external_game_manifest(install_root);
    if (!manifest.has_value()) {
        finish(false, "failed", "installed_manifest_missing", 0);
        return;
    }
    if (manifest->game_id != record.game_id) {
        finish(false, "failed", "installed_manifest_game_mismatch", 0);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::filesystem::path> stale_roots{};
        install_registry_.erase(
            std::remove_if(
                install_registry_.begin(),
                install_registry_.end(),
                [&](const RemoteGameInstallRecord& item) {
                    if (item.game_id != record.game_id) {
                        return false;
                    }
                    if (item.install_root != install_root) {
                        stale_roots.push_back(item.install_root);
                    }
                    return true;
                }),
            install_registry_.end());
        install_registry_.push_back(RemoteGameInstallRecord{
            record.game_id,
            record.display_name,
            record.version,
            record.sha256,
            install_root,
            now_wall_clock_ms(),
        });
        save_remote_game_install_registry(registry_path_, install_registry_);
        for (const auto& stale_root : stale_roots) {
            std::error_code remove_error;
            std::filesystem::remove_all(stale_root, remove_error);
        }
    }

    finish(true, "installed", "game_installed", 100);
#else
    (void)record;
    finish(false, "failed", "windows_only_remote_distribution", 0);
#endif
}

} // namespace nlp3::platform
