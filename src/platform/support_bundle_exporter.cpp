#include "platform/support_bundle_exporter.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string_view>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "nlohmann/json.hpp"

#include "platform/panel_app.hpp"
#include "platform/panel_http_json.hpp"

namespace {

std::uint64_t now_wall_clock_ms() {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

std::filesystem::path resolve_module_directory();

std::filesystem::path resolve_support_root() {
    const char* local_app_data = std::getenv("LOCALAPPDATA");
    if (local_app_data != nullptr && *local_app_data != '\0') {
        return std::filesystem::path(local_app_data) / "NisojeStudio" / "support";
    }
    return std::filesystem::temp_directory_path() / "NisojeStudio" / "support";
}

std::filesystem::path resolve_bridge_log_root() {
    const char* local_app_data = std::getenv("LOCALAPPDATA");
    if (local_app_data != nullptr && *local_app_data != '\0') {
        return std::filesystem::path(local_app_data) / "NisojeStudio" / "logs";
    }
    return std::filesystem::temp_directory_path() / "NisojeStudio" / "logs";
}

std::vector<std::filesystem::path> resolve_support_root_candidates() {
    std::vector<std::filesystem::path> candidates{};
    const auto current_directory = std::filesystem::current_path();
    const auto dev_workspace_mode =
        std::filesystem::exists(current_directory / "tools" / "bridge_py")
        || std::filesystem::exists(current_directory / "src" / "platform");

    const auto append_candidate = [&candidates](std::filesystem::path candidate) {
        if (candidate.empty()) {
            return;
        }

        candidate = candidate.lexically_normal();
        if (std::find(candidates.begin(), candidates.end(), candidate) == candidates.end()) {
            candidates.push_back(std::move(candidate));
        }
    };

    if (dev_workspace_mode) {
        append_candidate(current_directory / ".nisoje-support");
    }

    append_candidate(resolve_support_root());
    append_candidate(std::filesystem::temp_directory_path() / "NisojeStudio" / "support");

    if (!dev_workspace_mode) {
        append_candidate(current_directory / ".nisoje-support");
    }

    const auto module_directory = resolve_module_directory();
    if (!module_directory.empty()) {
        append_candidate(module_directory / "support");
    }

    return candidates;
}

std::filesystem::path resolve_module_directory() {
#ifdef _WIN32
    wchar_t buffer[MAX_PATH];
    const auto length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    return std::filesystem::path(std::wstring(buffer, buffer + length)).parent_path();
#else
    return {};
#endif
}

std::string trim_copy(std::string_view value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) {
        return {};
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(begin, end - begin + 1));
}

std::string sanitize_reason(std::string_view raw_reason) {
    std::string reason = trim_copy(raw_reason);
    if (reason.empty()) {
        return "manual";
    }

    std::string output;
    output.reserve(reason.size());
    for (const auto ch : reason) {
        const auto lowered = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if ((lowered >= 'a' && lowered <= 'z') || (lowered >= '0' && lowered <= '9')) {
            output.push_back(lowered);
        } else if (lowered == '-' || lowered == '_') {
            output.push_back(lowered);
        } else {
            output.push_back('_');
        }
    }

    while (!output.empty() && output.back() == '_') {
        output.pop_back();
    }
    return output.empty() ? "manual" : output;
}

std::filesystem::path resolve_bridge_log_path() {
    const char* override_path = std::getenv("LIVEPANEL_BRIDGE_LOG_PATH");
    if (override_path != nullptr && *override_path != '\0') {
        const auto path = std::filesystem::path(override_path);
        if (std::filesystem::exists(path)) {
            return path;
        }
    }

    const auto module_dir = resolve_module_directory();
    const auto current_dir = std::filesystem::current_path();
    const auto relative_log = std::filesystem::path("tools") / "bridge_py" / "logs" / "bridge.jsonl";
    const std::array<std::filesystem::path, 5> candidates{
        resolve_bridge_log_root() / "bridge.jsonl",
        current_dir / relative_log,
        module_dir / relative_log,
        module_dir.parent_path() / relative_log,
        module_dir.parent_path().parent_path() / relative_log,
    };

    for (const auto& candidate : candidates) {
        if (!candidate.empty() && std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    return candidates.front();
}

std::deque<std::string> read_log_tail(const std::filesystem::path& path, std::size_t max_lines = 80) {
    std::deque<std::string> lines{};
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return lines;
    }

    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(std::move(line));
        if (lines.size() > max_lines) {
            lines.pop_front();
        }
    }
    return lines;
}

nlohmann::json build_log_json(const std::filesystem::path& path, std::string_view label) {
    nlohmann::json output{
        {"label", std::string(label)},
        {"path", path.string()},
        {"exists", std::filesystem::exists(path)},
        {"lines", nlohmann::json::array()},
    };

    if (!std::filesystem::exists(path)) {
        return output;
    }

    for (const auto& line : read_log_tail(path)) {
        output["lines"].push_back(line);
    }
    return output;
}

nlohmann::json parse_json_or_wrap(std::string_view raw_json) {
    auto parsed = nlohmann::json::parse(raw_json, nullptr, false);
    if (parsed.is_discarded()) {
        return nlohmann::json{{"raw", std::string(raw_json)}};
    }
    return parsed;
}

} // namespace

namespace nlp3::platform {

SupportBundleExportResult export_support_bundle(
    const PanelApp& app,
    const PanelHttpServerStatus& http_status,
    std::string_view reason) {
    SupportBundleExportResult result{};
    result.exported_at_ms = now_wall_clock_ms();

    try {
        const auto safe_reason = sanitize_reason(reason);
        const auto runtime_log_root = std::filesystem::temp_directory_path() / "NisojeStudio";
        nlohmann::json logs = nlohmann::json::array();
        logs.push_back(build_log_json(resolve_bridge_log_path(), "bridge"));
        logs.push_back(build_log_json(runtime_log_root / "embedded_ui.log", "embedded_ui"));
        logs.push_back(build_log_json(runtime_log_root / "desktop_launcher.log", "desktop_launcher"));

        result.included_logs = std::count_if(
            logs.begin(),
            logs.end(),
            [](const nlohmann::json& item) { return item.value("exists", false); });

        const auto bundle = nlohmann::json{
            {"bundleVersion", 1},
            {"exportedAtMs", result.exported_at_ms},
            {"reason", std::string(reason)},
            {"includedLogs", result.included_logs},
            {"state", parse_json_or_wrap(build_panel_http_state_json(app, http_status))},
            {"metrics", parse_json_or_wrap(build_panel_http_metrics_json(app))},
            {"events", parse_json_or_wrap(build_panel_http_events_json(app))},
            {"logs", logs},
        };

        for (const auto& support_root : resolve_support_root_candidates()) {
            std::error_code directory_error;
            std::filesystem::create_directories(support_root, directory_error);
            if (directory_error) {
                continue;
            }

            const auto bundle_path = support_root / (
                "support-bundle-" + std::to_string(result.exported_at_ms) + "-" + safe_reason + ".json");

            auto writable_bundle = bundle;
            writable_bundle["bundlePath"] = bundle_path.string();

            std::ofstream output(bundle_path, std::ios::binary | std::ios::trunc);
            if (!output.is_open()) {
                continue;
            }

            output << writable_bundle.dump(2);
            output.flush();
            const auto write_ok = output.good();
            output.close();

            if (!write_ok) {
                std::error_code remove_error;
                std::filesystem::remove(bundle_path, remove_error);
                continue;
            }

            std::error_code exists_error;
            if (!std::filesystem::exists(bundle_path, exists_error) || exists_error) {
                continue;
            }

            result.ok = true;
            result.message = "support_bundle_exported";
            result.bundle_path = bundle_path.string();
            return result;
        }

        result.message = "support_bundle_export_failed";
        return result;
    } catch (const std::exception& exception) {
        result.message = exception.what();
        return result;
    } catch (...) {
        result.message = "support_bundle_export_failed";
        return result;
    }
}

} // namespace nlp3::platform
