#include "platform/external_bridge_runner.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#endif

namespace {

std::wstring widen(std::string_view text) {
    return std::wstring(text.begin(), text.end());
}

std::string read_env_value(const char* name) {
    const char* raw = std::getenv(name);
    return raw != nullptr ? std::string(raw) : std::string{};
}

std::filesystem::path resolve_module_path() {
#ifdef _WIN32
    wchar_t buffer[MAX_PATH];
    const auto length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    return std::filesystem::path(std::wstring(buffer, buffer + length));
#else
    return {};
#endif
}

std::filesystem::path find_project_root() {
    auto cursor = resolve_module_path().parent_path();
    for (int depth = 0; depth < 8 && !cursor.empty(); ++depth) {
        if (std::filesystem::exists(cursor / "tools" / "bridge_py" / "run_tiktok_bridge.py")) {
            return cursor;
        }
        cursor = cursor.parent_path();
    }

    const auto current = std::filesystem::current_path();
    if (std::filesystem::exists(current / "tools" / "bridge_py" / "run_tiktok_bridge.py")) {
        return current;
    }

    return {};
}

std::filesystem::path resolve_runner_script_path() {
    const auto override_path = read_env_value("LIVEPANEL_TIKTOK_RUNNER_SCRIPT");
    if (!override_path.empty()) {
        return std::filesystem::path(override_path);
    }

    const auto project_root = find_project_root();
    if (!project_root.empty()) {
        return project_root / "tools" / "bridge_py" / "run_tiktok_bridge.py";
    }

    return {};
}

std::filesystem::path resolve_runtime_probe_script_path() {
    const auto project_root = find_project_root();
    if (!project_root.empty()) {
        return project_root / "tools" / "bridge_py" / "bridge_env_check.py";
    }

    return {};
}

std::filesystem::path resolve_python_executable_path() {
    const auto override_path = read_env_value("LIVEPANEL_TIKTOK_PYTHON_EXE");
    if (!override_path.empty()) {
        return std::filesystem::path(override_path);
    }

    const auto project_root = find_project_root();
    if (!project_root.empty()) {
        const auto packaged_runtime =
            project_root / "tools" / "bridge_py" / "python_runtime" / "python.exe";
        if (std::filesystem::exists(packaged_runtime)) {
            return packaged_runtime;
        }

        const auto local_bridge_venv =
            project_root / "tools" / "bridge_py" / ".venv" / "Scripts" / "python.exe";
        if (std::filesystem::exists(local_bridge_venv)) {
            return local_bridge_venv;
        }
    }

    return std::filesystem::path("python");
}

std::int64_t now_wall_clock_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch())
        .count();
}

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

        if (backslash_count > 0) {
            quoted.append(backslash_count, L'\\');
            backslash_count = 0;
        }
        quoted.push_back(ch);
    }

    if (backslash_count > 0) {
        quoted.append(backslash_count * 2, L'\\');
    }
    quoted.push_back(L'"');
    return quoted;
}

std::wstring build_command_line(
    const std::filesystem::path& python_executable,
    const std::filesystem::path& script_path,
    const nlp3::platform::ExternalBridgeRunnerStartRequest& request) {
    std::wstring command_line;
    command_line += quote_windows_argument(python_executable.wstring());
    command_line += L" ";
    command_line += quote_windows_argument(script_path.wstring());
    command_line += L" --user ";
    command_line += quote_windows_argument(widen(request.target_user));
    command_line += L" --ws ";
    command_line += quote_windows_argument(widen(request.ws_url));
    if (request.control_port > 0) {
        command_line += L" --status-port ";
        command_line += std::to_wstring(request.control_port);
    }
    command_line += L" --no-broadcast-ws";

    if (request.max_seconds > 0) {
        command_line += L" --max-seconds ";
        command_line += std::to_wstring(request.max_seconds);
    }

    return command_line;
}

std::wstring build_runtime_probe_command_line(
    const std::filesystem::path& python_executable,
    const std::filesystem::path& script_path,
    const std::filesystem::path& report_path) {
    std::wstring command_line;
    command_line += quote_windows_argument(python_executable.wstring());
    command_line += L" ";
    command_line += quote_windows_argument(script_path.wstring());
    command_line += L" --format json";
    if (!report_path.empty()) {
        command_line += L" --report-path ";
        command_line += quote_windows_argument(report_path.wstring());
        command_line += L" --no-stdout";
    }
    return command_line;
}

std::string exit_message(std::int32_t exit_code) {
    return "runner exited with code " + std::to_string(exit_code);
}

#ifdef _WIN32

struct BridgeRuntimeProbeResult {
    bool checked = false;
    bool ok = false;
    std::int64_t checked_timestamp_ms = 0;
    std::string summary{};
    std::vector<std::string> alerts{};
};
bool ensure_winsock_initialized() {
    static bool initialized = false;
    static bool failed = false;
    if (initialized) {
        return true;
    }
    if (failed) {
        return false;
    }

    WSADATA winsock_data{};
    if (WSAStartup(MAKEWORD(2, 2), &winsock_data) != 0) {
        failed = true;
        return false;
    }

    initialized = true;
    return true;
}

void close_handle(void*& handle) {
    if (handle != nullptr) {
        CloseHandle(reinterpret_cast<HANDLE>(handle));
        handle = nullptr;
    }
}

void append_log_line(
    nlp3::platform::ExternalBridgeRunnerStatus& status,
    std::string line) {
    if (line.empty()) {
        return;
    }

    status.recent_log_lines.push_back(std::move(line));
    constexpr std::size_t kMaxLogLines = 24;
    if (status.recent_log_lines.size() > kMaxLogLines) {
        status.recent_log_lines.erase(status.recent_log_lines.begin());
    }
}

void consume_log_buffer(
    nlp3::platform::ExternalBridgeRunnerStatus& status,
    std::string& partial_buffer,
    std::string_view chunk,
    bool error_stream) {
    partial_buffer.append(chunk.data(), chunk.size());

    std::size_t cursor = 0;
    while (cursor < partial_buffer.size()) {
        const auto newline = partial_buffer.find('\n', cursor);
        if (newline == std::string::npos) {
            break;
        }

        auto line = partial_buffer.substr(cursor, newline - cursor);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (error_stream) {
            append_log_line(status, "stderr: " + line);
            if (status.last_error.empty()) {
                status.last_error = line;
            }
        } else {
            append_log_line(status, line);
        }
        cursor = newline + 1;
    }

    partial_buffer.erase(0, cursor);
}

void flush_partial_log_buffer(
    nlp3::platform::ExternalBridgeRunnerStatus& status,
    std::string& partial_buffer,
    bool error_stream) {
    if (partial_buffer.empty()) {
        return;
    }

    if (error_stream) {
        append_log_line(status, "stderr: " + partial_buffer);
        if (status.last_error.empty()) {
            status.last_error = partial_buffer;
        }
    } else {
        append_log_line(status, partial_buffer);
    }
    partial_buffer.clear();
}

std::string format_windows_error_message(DWORD error_code) {
    wchar_t* buffer = nullptr;
    const auto size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error_code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr);
    if (size == 0 || buffer == nullptr) {
        return "windows error " + std::to_string(error_code);
    }

    std::wstring message(buffer, size);
    LocalFree(buffer);

    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ')) {
        message.pop_back();
    }

    if (message.empty()) {
        return "windows error " + std::to_string(error_code);
    }

    const auto required_size = WideCharToMultiByte(
        CP_UTF8,
        0,
        message.c_str(),
        static_cast<int>(message.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (required_size <= 0) {
        return "windows error " + std::to_string(error_code);
    }

    std::string utf8_message(static_cast<std::size_t>(required_size), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        message.c_str(),
        static_cast<int>(message.size()),
        utf8_message.data(),
        required_size,
        nullptr,
        nullptr);
    return utf8_message;
}

std::string read_pipe_to_string(HANDLE handle) {
    std::string output;
    if (handle == nullptr) {
        return output;
    }

    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(handle, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
        output.append(buffer, buffer + read);
    }
    return output;
}

std::filesystem::path build_runtime_probe_report_path() {
    std::error_code error;
    auto root = std::filesystem::temp_directory_path(error);
    if (error || root.empty()) {
        root = find_project_root();
    }
    if (root.empty()) {
        root = std::filesystem::current_path(error);
    }
    if (error || root.empty()) {
        return {};
    }

    return root / (
        "nlp3-bridge-env-check-"
        + std::to_string(GetCurrentProcessId())
        + "-"
        + std::to_string(now_wall_clock_ms())
        + ".json");
}

std::optional<std::string> read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return std::nullopt;
    }

    std::string contents;
    input.seekg(0, std::ios::end);
    contents.resize(static_cast<std::size_t>(input.tellg()));
    input.seekg(0, std::ios::beg);
    if (!contents.empty()) {
        input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
    }
    return contents;
}

BridgeRuntimeProbeResult run_bridge_runtime_probe() {
    BridgeRuntimeProbeResult result{};
    result.checked = true;
    result.checked_timestamp_ms = now_wall_clock_ms();

    const auto python_executable = resolve_python_executable_path();
    const auto probe_script = resolve_runtime_probe_script_path();

    if (find_project_root().empty()) {
        result.summary = "No se encontro la carpeta tools/bridge_py de esta instalacion.";
        result.alerts.push_back(result.summary);
        return result;
    }

    if (python_executable.empty() || python_executable == std::filesystem::path("python") || !std::filesystem::exists(python_executable)) {
        result.summary = "No se encontro el runtime Python instalado por Panel Live para TikTok.";
        result.alerts.push_back(result.summary);
        return result;
    }

    if (probe_script.empty() || !std::filesystem::exists(probe_script)) {
        result.summary = "No se encontro bridge_env_check.py para validar TikTok.";
        result.alerts.push_back(result.summary);
        return result;
    }

    SECURITY_ATTRIBUTES security_attributes{};
    security_attributes.nLength = sizeof(security_attributes);
    security_attributes.bInheritHandle = TRUE;

    HANDLE stdout_read = nullptr;
    HANDLE stdout_write = nullptr;
    HANDLE stderr_read = nullptr;
    HANDLE stderr_write = nullptr;
    if (!CreatePipe(&stdout_read, &stdout_write, &security_attributes, 0)
        || !CreatePipe(&stderr_read, &stderr_write, &security_attributes, 0)) {
        close_handle(reinterpret_cast<void*&>(stdout_read));
        close_handle(reinterpret_cast<void*&>(stdout_write));
        close_handle(reinterpret_cast<void*&>(stderr_read));
        close_handle(reinterpret_cast<void*&>(stderr_write));
        result.summary = "No se pudieron crear los canales internos para verificar TikTok.";
        result.alerts.push_back(result.summary);
        return result;
    }

    SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    startup_info.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    startup_info.wShowWindow = SW_HIDE;
    startup_info.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup_info.hStdOutput = stdout_write;
    startup_info.hStdError = stderr_write;

    PROCESS_INFORMATION process_info{};
    const auto report_path = build_runtime_probe_report_path();
    auto command_line = build_runtime_probe_command_line(python_executable, probe_script, report_path);
    auto mutable_command_line = std::vector<wchar_t>(command_line.begin(), command_line.end());
    mutable_command_line.push_back(L'\0');

    const auto working_directory_path = find_project_root().empty()
        ? probe_script.parent_path()
        : find_project_root();
    const auto created = CreateProcessW(
        python_executable.wstring().c_str(),
        mutable_command_line.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        working_directory_path.wstring().c_str(),
        &startup_info,
        &process_info);

    close_handle(reinterpret_cast<void*&>(stdout_write));
    close_handle(reinterpret_cast<void*&>(stderr_write));

    if (!created) {
        close_handle(reinterpret_cast<void*&>(stdout_read));
        close_handle(reinterpret_cast<void*&>(stderr_read));
        result.summary = "No se pudo iniciar la verificacion TikTok: "
            + format_windows_error_message(GetLastError());
        result.alerts.push_back(result.summary);
        return result;
    }

    const auto wait_result = WaitForSingleObject(process_info.hProcess, 15000);
    if (wait_result == WAIT_TIMEOUT) {
        TerminateProcess(process_info.hProcess, 1);
        WaitForSingleObject(process_info.hProcess, 2000);
        if (!report_path.empty()) {
            std::error_code remove_error;
            std::filesystem::remove(report_path, remove_error);
        }
        result.summary = "La verificacion TikTok tardo demasiado y fue cancelada.";
        result.alerts.push_back(result.summary);
    }

    const auto stdout_text = read_pipe_to_string(stdout_read);
    const auto stderr_text = read_pipe_to_string(stderr_read);
    close_handle(reinterpret_cast<void*&>(stdout_read));
    close_handle(reinterpret_cast<void*&>(stderr_read));

    DWORD exit_code = 1;
    GetExitCodeProcess(process_info.hProcess, &exit_code);
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);

    if (wait_result == WAIT_TIMEOUT) {
        return result;
    }

    std::string payload_text = stdout_text;
    if (!report_path.empty()) {
        if (const auto report_text = read_text_file(report_path); report_text.has_value() && !report_text->empty()) {
            payload_text = *report_text;
        }
        std::error_code remove_error;
        std::filesystem::remove(report_path, remove_error);
    }

    auto payload = nlohmann::json::parse(payload_text, nullptr, false);
    if (!payload.is_object()) {
        result.summary = !stderr_text.empty()
            ? stderr_text
            : "La verificacion TikTok no devolvio un reporte valido.";
        result.alerts.push_back(result.summary);
        return result;
    }

    result.ok = payload.value("ok", false);
    result.summary = payload.value(
        "summary",
        result.ok ? "TikTok listo en este equipo." : "La verificacion TikTok fallo.");

    if (payload.contains("alerts") && payload["alerts"].is_array()) {
        for (const auto& item : payload["alerts"]) {
            if (item.is_string()) {
                result.alerts.push_back(item.get<std::string>());
            }
        }
    }

    if (result.alerts.empty() && !result.ok && !stderr_text.empty()) {
        result.alerts.push_back(stderr_text);
    }

    if (exit_code != 0 && result.alerts.empty()) {
        result.alerts.push_back(result.summary);
    }

    return result;
}

BridgeRuntimeProbeResult cached_bridge_runtime_probe(bool force_refresh) {
    static std::mutex cache_mutex;
    static BridgeRuntimeProbeResult cached_result{};

    std::lock_guard<std::mutex> lock(cache_mutex);
    const auto stale = !cached_result.checked
        || (now_wall_clock_ms() - cached_result.checked_timestamp_ms) > 30000;
    if (force_refresh || stale) {
        cached_result = run_bridge_runtime_probe();
    }
    return cached_result;
}

void apply_runtime_probe_to_status(
    nlp3::platform::ExternalBridgeRunnerStatus& status,
    const BridgeRuntimeProbeResult& probe) {
    status.runtime_checked = probe.checked;
    status.runtime_ready = probe.ok;
    status.runtime_checked_timestamp_ms = probe.checked_timestamp_ms;
    status.runtime_summary = probe.summary;
    status.runtime_alerts = probe.alerts;
}

bool post_local_shutdown_request(std::uint16_t port) {
    if (port == 0 || !ensure_winsock_initialized()) {
        return false;
    }

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* result = nullptr;
    const auto service = std::to_string(port);
    if (getaddrinfo("127.0.0.1", service.c_str(), &hints, &result) != 0 || result == nullptr) {
        return false;
    }

    SOCKET socket_handle = INVALID_SOCKET;
    bool connected = false;
    for (auto* candidate = result; candidate != nullptr; candidate = candidate->ai_next) {
        socket_handle = socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
        if (socket_handle == INVALID_SOCKET) {
            continue;
        }

        DWORD timeout_ms = 750;
        setsockopt(
            socket_handle,
            SOL_SOCKET,
            SO_RCVTIMEO,
            reinterpret_cast<const char*>(&timeout_ms),
            sizeof(timeout_ms));
        setsockopt(
            socket_handle,
            SOL_SOCKET,
            SO_SNDTIMEO,
            reinterpret_cast<const char*>(&timeout_ms),
            sizeof(timeout_ms));

        if (connect(socket_handle, candidate->ai_addr, static_cast<int>(candidate->ai_addrlen)) == 0) {
            connected = true;
            break;
        }

        closesocket(socket_handle);
        socket_handle = INVALID_SOCKET;
    }
    freeaddrinfo(result);

    if (!connected || socket_handle == INVALID_SOCKET) {
        if (socket_handle != INVALID_SOCKET) {
            closesocket(socket_handle);
        }
        return false;
    }

    constexpr char request[] =
        "POST /shutdown HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Connection: close\r\n"
        "Content-Length: 0\r\n\r\n";

    const auto sent = send(socket_handle, request, static_cast<int>(sizeof(request) - 1), 0);
    if (sent <= 0) {
        closesocket(socket_handle);
        return false;
    }

    char response_buffer[256];
    const auto received = recv(socket_handle, response_buffer, static_cast<int>(sizeof(response_buffer)), 0);
    closesocket(socket_handle);
    if (received <= 0) {
        return false;
    }

    const std::string_view response(response_buffer, static_cast<std::size_t>(received));
    return response.find("200 OK") != std::string_view::npos;
}
#endif

} // namespace

namespace nlp3::platform {

ExternalBridgeRunner::ExternalBridgeRunner() noexcept {
    refresh_runtime_status();
}

ExternalBridgeRunner::~ExternalBridgeRunner() {
    stop();
}

bool ExternalBridgeRunner::start(const ExternalBridgeRunnerStartRequest& request) {
    stop();
    status_ = {};
    stop_requested_by_panel_ = false;
    control_port_ = request.control_port;
    status_.target_user = request.target_user;
    status_.ws_url = request.ws_url;
    refresh_runtime_status(true);

    if (request.target_user.empty() || request.ws_url.empty()) {
        status_.last_error = "runner target or ws url missing";
        return false;
    }

    if (status_.runtime_checked && !status_.runtime_ready) {
        status_.last_error = status_.runtime_summary.empty()
            ? "Faltan dependencias para TikTok."
            : status_.runtime_summary;
        append_log_line(status_, "runner launch failed: " + status_.last_error);
        for (const auto& alert : status_.runtime_alerts) {
            append_log_line(status_, "runtime alert: " + alert);
        }
        return false;
    }

#ifndef _WIN32
    status_.last_error = "runner process management is only implemented on Windows";
    return false;
#else
    const auto python_executable = resolve_python_executable_path();
    const auto runner_script = resolve_runner_script_path();
    if (runner_script.empty() || !std::filesystem::exists(runner_script)) {
        status_.last_error = "runner script not found";
        append_log_line(status_, "runner launch failed: script not found");
        return false;
    }

    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    startup_info.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    startup_info.wShowWindow = SW_HIDE;

    SECURITY_ATTRIBUTES security_attributes{};
    security_attributes.nLength = sizeof(security_attributes);
    security_attributes.bInheritHandle = TRUE;

    HANDLE stdout_read = nullptr;
    HANDLE stdout_write = nullptr;
    HANDLE stderr_read = nullptr;
    HANDLE stderr_write = nullptr;
    if (!CreatePipe(&stdout_read, &stdout_write, &security_attributes, 0)
        || !CreatePipe(&stderr_read, &stderr_write, &security_attributes, 0)) {
        close_handle(reinterpret_cast<void*&>(stdout_read));
        close_handle(reinterpret_cast<void*&>(stdout_write));
        close_handle(reinterpret_cast<void*&>(stderr_read));
        close_handle(reinterpret_cast<void*&>(stderr_write));
        status_.last_error = "could not create runner pipes";
        return false;
    }

    SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0);
    startup_info.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup_info.hStdOutput = stdout_write;
    startup_info.hStdError = stderr_write;

    PROCESS_INFORMATION process_info{};
    auto command_line = build_command_line(python_executable, runner_script, request);
    auto mutable_command_line = std::vector<wchar_t>(command_line.begin(), command_line.end());
    mutable_command_line.push_back(L'\0');

    const auto working_directory_path = find_project_root().empty()
        ? runner_script.parent_path()
        : find_project_root();
    const auto working_directory = working_directory_path.wstring();
    const auto created = CreateProcessW(
        python_executable.wstring().c_str(),
        mutable_command_line.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        working_directory.c_str(),
        &startup_info,
        &process_info);

    close_handle(reinterpret_cast<void*&>(stdout_write));
    close_handle(reinterpret_cast<void*&>(stderr_write));

    if (!created) {
        close_handle(reinterpret_cast<void*&>(stdout_read));
        close_handle(reinterpret_cast<void*&>(stderr_read));
        status_.last_error = "could not start external runner process: " + format_windows_error_message(GetLastError());
        append_log_line(status_, "runner launch failed: " + status_.last_error);
        reset_process_handles();
        return false;
    }

    process_handle_ = process_info.hProcess;
    thread_handle_ = process_info.hThread;
    stdout_read_handle_ = stdout_read;
    stderr_read_handle_ = stderr_read;
    stdout_partial_buffer_.clear();
    stderr_partial_buffer_.clear();
    status_.running = true;
    status_.process_id = process_info.dwProcessId;
    status_.last_error.clear();
    status_.recent_log_lines.clear();
    append_log_line(status_, "runner launch: python=" + python_executable.string());
    append_log_line(status_, "runner launch: script=" + runner_script.string());
    append_log_line(status_, "runner launch: cwd=" + working_directory_path.string());
    append_log_line(status_, "runner launch: target=" + request.target_user + ", ws=" + request.ws_url);
    if (request.control_port > 0) {
        append_log_line(status_, "runner launch: control_port=" + std::to_string(request.control_port));
    }
    return true;
#endif
}

void ExternalBridgeRunner::stop() {
#ifdef _WIN32
    poll();
    if (process_handle_ != nullptr) {
        DWORD exit_code = STILL_ACTIVE;
        bool terminated_by_panel = false;
        GetExitCodeProcess(reinterpret_cast<HANDLE>(process_handle_), &exit_code);
        if (exit_code == STILL_ACTIVE) {
            stop_requested_by_panel_ = true;
            const auto shutdown_requested = post_local_shutdown_request(control_port_);
            if (shutdown_requested) {
                append_log_line(status_, "runner shutdown requested via control port");
                WaitForSingleObject(reinterpret_cast<HANDLE>(process_handle_), 3000);
                GetExitCodeProcess(reinterpret_cast<HANDLE>(process_handle_), &exit_code);
            }
            if (exit_code == STILL_ACTIVE) {
                TerminateProcess(reinterpret_cast<HANDLE>(process_handle_), 1);
                WaitForSingleObject(reinterpret_cast<HANDLE>(process_handle_), 2000);
                terminated_by_panel = true;
            }
            GetExitCodeProcess(reinterpret_cast<HANDLE>(process_handle_), &exit_code);
        }

        status_.running = false;
        if (terminated_by_panel || stop_requested_by_panel_) {
            status_.has_exit_code = true;
            status_.last_exit_code = 0;
            status_.last_error.clear();
            append_log_line(status_, "runner stopped by panel");
        } else {
            status_.has_exit_code = exit_code != STILL_ACTIVE;
            status_.last_exit_code = exit_code == STILL_ACTIVE ? 1 : static_cast<std::int32_t>(exit_code);
            if (status_.last_exit_code != 0 && status_.last_error.empty()) {
                status_.last_error = exit_message(status_.last_exit_code);
            }
        }
    }
    flush_partial_log_buffer(status_, stdout_partial_buffer_, false);
    flush_partial_log_buffer(status_, stderr_partial_buffer_, true);
#endif
    reset_process_handles();
}

void ExternalBridgeRunner::poll() {
#ifdef _WIN32
    auto read_pipe = [&](void* handle, std::string& partial_buffer, bool error_stream) {
        if (handle == nullptr) {
            return;
        }

        DWORD available = 0;
        if (!PeekNamedPipe(reinterpret_cast<HANDLE>(handle), nullptr, 0, nullptr, &available, nullptr)) {
            return;
        }

        while (available > 0) {
            std::string chunk;
            chunk.resize(available);
            DWORD read = 0;
            if (!ReadFile(
                    reinterpret_cast<HANDLE>(handle),
                    chunk.data(),
                    available,
                    &read,
                    nullptr)) {
                break;
            }
            if (read == 0) {
                break;
            }
            chunk.resize(read);
            consume_log_buffer(status_, partial_buffer, chunk, error_stream);
            if (!PeekNamedPipe(reinterpret_cast<HANDLE>(handle), nullptr, 0, nullptr, &available, nullptr)) {
                break;
            }
        }
    };

    read_pipe(stdout_read_handle_, stdout_partial_buffer_, false);
    read_pipe(stderr_read_handle_, stderr_partial_buffer_, true);

    if (process_handle_ == nullptr) {
        return;
    }

    DWORD exit_code = STILL_ACTIVE;
    if (!GetExitCodeProcess(reinterpret_cast<HANDLE>(process_handle_), &exit_code)) {
        status_.running = false;
        status_.last_error = "could not query runner exit code";
        reset_process_handles();
        return;
    }

    if (exit_code == STILL_ACTIVE) {
        status_.running = true;
        return;
    }

    status_.running = false;
    status_.has_exit_code = true;
    status_.last_exit_code = stop_requested_by_panel_ ? 0 : static_cast<std::int32_t>(exit_code);
    if (stop_requested_by_panel_) {
        status_.last_error.clear();
    } else if (status_.last_exit_code != 0 && status_.last_error.empty()) {
        status_.last_error = exit_message(status_.last_exit_code);
    }
    flush_partial_log_buffer(status_, stdout_partial_buffer_, false);
    flush_partial_log_buffer(status_, stderr_partial_buffer_, true);
    reset_process_handles();
#endif
}

void ExternalBridgeRunner::refresh_runtime_status(bool force) {
#ifdef _WIN32
    apply_runtime_probe_to_status(status_, cached_bridge_runtime_probe(force));
#else
    (void)force;
    status_.runtime_checked = true;
    status_.runtime_ready = false;
    status_.runtime_checked_timestamp_ms = now_wall_clock_ms();
    status_.runtime_summary = "TikTok solo esta disponible en Windows.";
    status_.runtime_alerts = {status_.runtime_summary};
#endif
}

ExternalBridgeRunnerStatus ExternalBridgeRunner::status() const noexcept {
    return status_;
}

void ExternalBridgeRunner::reset_process_handles() noexcept {
#ifdef _WIN32
    if (thread_handle_ != nullptr) {
        CloseHandle(reinterpret_cast<HANDLE>(thread_handle_));
        thread_handle_ = nullptr;
    }
    if (process_handle_ != nullptr) {
        CloseHandle(reinterpret_cast<HANDLE>(process_handle_));
        process_handle_ = nullptr;
    }
    close_handle(stdout_read_handle_);
    close_handle(stderr_read_handle_);
    stop_requested_by_panel_ = false;
    control_port_ = 0;
#else
    process_handle_ = nullptr;
    thread_handle_ = nullptr;
#endif
}

} // namespace nlp3::platform
