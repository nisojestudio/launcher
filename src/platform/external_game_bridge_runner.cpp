#include "platform/external_game_bridge_runner.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "platform/external_game_manifest.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

std::wstring widen(std::string_view text) {
    if (text.empty()) {
        return {};
    }
#ifdef _WIN32
    const auto required = MultiByteToWideChar(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (required <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        result.data(), required);
    return result;
#else
    return std::wstring(text.begin(), text.end());
#endif
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
        if (std::filesystem::exists(cursor / "tools" / "game_bridge_py" / "run_local_game_bridge.py")) {
            return cursor;
        }
        cursor = cursor.parent_path();
    }

    const auto current = std::filesystem::current_path();
    if (std::filesystem::exists(current / "tools" / "game_bridge_py" / "run_local_game_bridge.py")) {
        return current;
    }

    return {};
}

std::filesystem::path resolve_bridge_script_path() {
    const auto override_path = read_env_value("NLP3_LOCAL_GAME_BRIDGE_SCRIPT");
    if (!override_path.empty()) {
        return std::filesystem::path(override_path);
    }

    const auto project_root = find_project_root();
    if (project_root.empty()) {
        return {};
    }

    return project_root / "tools" / "game_bridge_py" / "run_local_game_bridge.py";
}

std::filesystem::path resolve_python_executable_path() {
    const auto override_path = read_env_value("NLP3_LOCAL_GAME_BRIDGE_PYTHON_EXE");
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
    const nlp3::platform::ExternalGameBridgeStartRequest& request) {
    std::wstring command_line;
    command_line += quote_windows_argument(python_executable.wstring());
    command_line += L" ";
    command_line += quote_windows_argument(script_path.wstring());
    command_line += L" --game-root ";
    command_line += quote_windows_argument(widen(request.game_root));
    return command_line;
}

std::string exit_message(std::int32_t exit_code) {
    return "external game bridge exited with code " + std::to_string(exit_code);
}

#ifdef _WIN32
void close_handle(void*& handle) {
    if (handle != nullptr) {
        CloseHandle(reinterpret_cast<HANDLE>(handle));
        handle = nullptr;
    }
}

void append_log_line(
    nlp3::platform::ExternalGameBridgeRunnerStatus& status,
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
    nlp3::platform::ExternalGameBridgeRunnerStatus& status,
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
    nlp3::platform::ExternalGameBridgeRunnerStatus& status,
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
#endif

} // namespace

namespace nlp3::platform {

ExternalGameBridgeRunner::ExternalGameBridgeRunner() noexcept = default;

ExternalGameBridgeRunner::~ExternalGameBridgeRunner() {
    stop();
}

bool ExternalGameBridgeRunner::start(const ExternalGameBridgeStartRequest& request) {
    stop();
    status_ = {};
    stop_requested_by_panel_ = false;
    status_.game_id = request.game_id;
    status_.game_root = request.game_root;

    if (request.game_id.empty() || request.game_root.empty()) {
        status_.last_error = "external game bridge launch missing game id or game root";
        return false;
    }

#ifndef _WIN32
    status_.last_error = "external game bridge process management is only implemented on Windows";
    return false;
#else
    const auto python_executable = resolve_python_executable_path();
    const auto runner_script = resolve_bridge_script_path();
    if (runner_script.empty() || !std::filesystem::exists(runner_script)) {
        status_.last_error = "external game bridge script not found";
        append_log_line(status_, "external game bridge launch failed: script not found");
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
        status_.last_error = "could not create external game bridge pipes";
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
        status_.last_error =
            "could not start external game bridge: " + format_windows_error_message(GetLastError());
        append_log_line(status_, "external game bridge launch failed: " + status_.last_error);
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
    append_log_line(status_, "external game bridge launch: python=" + python_executable.string());
    append_log_line(status_, "external game bridge launch: script=" + runner_script.string());
    append_log_line(status_, "external game bridge launch: cwd=" + working_directory_path.string());
    append_log_line(status_, "external game bridge launch: game_id=" + request.game_id);
    append_log_line(status_, "external game bridge launch: game_root=" + request.game_root);
    return true;
#endif
}

void ExternalGameBridgeRunner::stop() {
#ifdef _WIN32
    poll();
    if (process_handle_ != nullptr) {
        DWORD exit_code = STILL_ACTIVE;
        bool terminated_by_panel = false;
        GetExitCodeProcess(reinterpret_cast<HANDLE>(process_handle_), &exit_code);
        if (exit_code == STILL_ACTIVE) {
            stop_requested_by_panel_ = true;
            const auto stop_file =
                std::filesystem::path(status_.game_root) / "runtime" / "panel_bridge" / "control" / "stop.flag";
            std::error_code error;
            std::filesystem::create_directories(stop_file.parent_path(), error);
            std::ofstream output(stop_file, std::ios::binary | std::ios::trunc);
            output << "stop\n";
            output.close();
            append_log_line(status_, "external game bridge stop flag written");
            WaitForSingleObject(reinterpret_cast<HANDLE>(process_handle_), 5000);
            GetExitCodeProcess(reinterpret_cast<HANDLE>(process_handle_), &exit_code);
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
            append_log_line(status_, "external game bridge stopped by panel");
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

void ExternalGameBridgeRunner::poll() {
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
        status_.last_error = "could not query external game bridge exit code";
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

ExternalGameBridgeRunnerStatus ExternalGameBridgeRunner::status() const noexcept {
    return status_;
}

void ExternalGameBridgeRunner::reset_process_handles() noexcept {
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
#else
    process_handle_ = nullptr;
    thread_handle_ = nullptr;
#endif
}

} // namespace nlp3::platform
