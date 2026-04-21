#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

#include "platform/panel_app.hpp"
#include "platform/panel_console.hpp"
#include "test_require.hpp"
#include "test_support.hpp"

namespace {

class ScopedEnvironmentOverride {
public:
    ScopedEnvironmentOverride(const char* name, std::string value)
        : name_(name) {
        const auto* previous_value = std::getenv(name);
        had_previous_ = previous_value != nullptr;
        if (previous_value != nullptr) {
            previous_value_ = previous_value;
        }
#ifdef _WIN32
        _putenv_s(name_, value.c_str());
#endif
    }

    ~ScopedEnvironmentOverride() {
#ifdef _WIN32
        if (had_previous_) {
            _putenv_s(name_, previous_value_.c_str());
        } else {
            _putenv_s(name_, "");
        }
#endif
    }

private:
    const char* name_;
    std::string previous_value_{};
    bool had_previous_ = false;
};

std::filesystem::path find_project_root_for_runner_test() {
    auto cursor = std::filesystem::current_path();
    for (int depth = 0; depth < 8 && !cursor.empty(); ++depth) {
        if (std::filesystem::exists(cursor / "tools" / "bridge_py" / "run_tiktok_bridge.py")) {
            return cursor;
        }
        cursor = cursor.parent_path();
    }

    return {};
}

std::string resolve_python_for_runner_test() {
    const auto override_value = std::getenv("LIVEPANEL_TIKTOK_PYTHON_EXE");
    if (override_value != nullptr && *override_value != '\0') {
        return override_value;
    }

    const auto project_root = find_project_root_for_runner_test();
    if (!project_root.empty()) {
        const auto packaged_python =
            project_root / "tools" / "bridge_py" / "python_runtime" / "python.exe";
        if (std::filesystem::exists(packaged_python)) {
            return packaged_python.string();
        }

        const auto local_python =
            project_root / "tools" / "bridge_py" / ".venv" / "Scripts" / "python.exe";
        if (std::filesystem::exists(local_python)) {
            return local_python.string();
        }
    }

    return "python";
}

std::filesystem::path write_runner_probe_script() {
    const auto script_path =
        std::filesystem::temp_directory_path() / "nlp3_panel_external_runner_test_probe.py";
    std::ofstream output(script_path, std::ios::binary | std::ios::trunc);
    output
        << "import argparse\n"
        << "import json\n"
        << "import sys\n"
        << "import threading\n"
        << "import time\n"
        << "from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer\n"
        << "parser = argparse.ArgumentParser(add_help=False)\n"
        << "parser.add_argument('--user', default='')\n"
        << "parser.add_argument('--ws', default='')\n"
        << "parser.add_argument('--status-port', type=int, default=0)\n"
        << "parser.add_argument('--max-seconds', type=float, default=0)\n"
        << "args, _ = parser.parse_known_args()\n"
        << "stop_event = threading.Event()\n"
        << "class Handler(BaseHTTPRequestHandler):\n"
        << "    def do_POST(self):\n"
        << "        if self.path != '/shutdown':\n"
        << "            self.send_response(404)\n"
        << "            self.end_headers()\n"
        << "            return\n"
        << "        payload = json.dumps({'ok': True, 'stopping': True}).encode('utf-8')\n"
        << "        self.send_response(200)\n"
        << "        self.send_header('Content-Type', 'application/json; charset=utf-8')\n"
        << "        self.send_header('Content-Length', str(len(payload)))\n"
        << "        self.end_headers()\n"
        << "        self.wfile.write(payload)\n"
        << "        stop_event.set()\n"
        << "    def log_message(self, format, *args):\n"
        << "        return\n"
        << "server = None\n"
        << "server_thread = None\n"
        << "if args.status_port > 0:\n"
        << "    server = ThreadingHTTPServer(('127.0.0.1', args.status_port), Handler)\n"
        << "    server_thread = threading.Thread(target=server.serve_forever, daemon=True)\n"
        << "    server_thread.start()\n"
        << "print('probe stdout ready', flush=True)\n"
        << "print('probe stderr ready', file=sys.stderr, flush=True)\n"
        << "deadline = time.monotonic() + (args.max_seconds if args.max_seconds > 0 else 5.0)\n"
        << "while time.monotonic() < deadline and not stop_event.is_set():\n"
        << "    time.sleep(0.05)\n"
        << "if server is not None:\n"
        << "    server.shutdown()\n"
        << "    server.server_close()\n"
        << "print('probe stdout done', flush=True)\n";
    output.close();
    return script_path;
}

bool wait_until_runner_stops(
    nlp3::platform::PanelApp& panel_app,
    std::chrono::milliseconds timeout) {
    const auto started_at = std::chrono::steady_clock::now();
    while ((std::chrono::steady_clock::now() - started_at) < timeout) {
        panel_app.tick(0);
        if (!panel_app.external_runner_status().running) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    panel_app.tick(0);
    return !panel_app.external_runner_status().running;
}

} // namespace

int main() {
    const auto config_path = nlp3::testsupport::write_temp_panel_config(
        "nlp3_panel_external_runner_test_config.json",
        []() {
            nlp3::platform::PanelConfig config{};
            config.bridge_mode = "external";
            config.external_target_user = "runner_test_user";
            config.external_ws_port = 28777;
            return config;
        }());

    const auto script_path = write_runner_probe_script();
    const auto python_path = resolve_python_for_runner_test();
    ScopedEnvironmentOverride python_override("LIVEPANEL_TIKTOK_PYTHON_EXE", python_path);
    ScopedEnvironmentOverride script_override("LIVEPANEL_TIKTOK_RUNNER_SCRIPT", script_path.string());

    nlp3::platform::PanelApp panel_app;
    NLP3_TEST_REQUIRE(panel_app.initialize(config_path.string()));
    NLP3_TEST_REQUIRE(panel_app.is_external_bridge_mode());

    NLP3_TEST_REQUIRE(panel_app.start_external_runner({}, 1));
    auto runner_status = panel_app.external_runner_status();
    NLP3_TEST_REQUIRE(runner_status.running);
    NLP3_TEST_REQUIRE(runner_status.process_id != 0);
    NLP3_TEST_REQUIRE(runner_status.target_user == "runner_test_user");
    NLP3_TEST_REQUIRE(runner_status.ws_url == "ws://127.0.0.1:28777");
    NLP3_TEST_REQUIRE(!runner_status.recent_log_lines.empty());
    NLP3_TEST_REQUIRE(std::find_if(
        runner_status.recent_log_lines.begin(),
        runner_status.recent_log_lines.end(),
        [](const std::string& line) {
            return line.find("runner launch: python=") != std::string::npos;
        }) != runner_status.recent_log_lines.end());
    NLP3_TEST_REQUIRE(std::find_if(
        runner_status.recent_log_lines.begin(),
        runner_status.recent_log_lines.end(),
        [](const std::string& line) {
            return line.find("runner launch: control_port=") != std::string::npos;
        }) != runner_status.recent_log_lines.end());

    auto snapshot_running = panel_app.snapshot();
    NLP3_TEST_REQUIRE(snapshot_running.external_bridge.runner_running);
    NLP3_TEST_REQUIRE(snapshot_running.external_bridge.runner_process_id != 0);
    NLP3_TEST_REQUIRE(snapshot_running.external_bridge.runner_ws_url == "ws://127.0.0.1:28777");

    NLP3_TEST_REQUIRE(wait_until_runner_stops(panel_app, std::chrono::milliseconds(3000)));

    runner_status = panel_app.external_runner_status();
    NLP3_TEST_REQUIRE(!runner_status.running);
    NLP3_TEST_REQUIRE(runner_status.has_exit_code);
    NLP3_TEST_REQUIRE(!runner_status.recent_log_lines.empty());
    NLP3_TEST_REQUIRE(std::find_if(
        runner_status.recent_log_lines.begin(),
        runner_status.recent_log_lines.end(),
        [](const std::string& line) {
            return line.find("probe stdout ready") != std::string::npos;
        }) != runner_status.recent_log_lines.end());
    NLP3_TEST_REQUIRE(std::find_if(
        runner_status.recent_log_lines.begin(),
        runner_status.recent_log_lines.end(),
        [](const std::string& line) {
            return line.find("probe stderr ready") != std::string::npos;
        }) != runner_status.recent_log_lines.end());
    auto snapshot_after_exit = panel_app.snapshot();
    NLP3_TEST_REQUIRE(!snapshot_after_exit.external_bridge.runner_running);
    NLP3_TEST_REQUIRE(snapshot_after_exit.external_bridge.runner_has_exit_code);
    NLP3_TEST_REQUIRE(!snapshot_after_exit.external_bridge.runner_recent_log_lines.empty());

    std::istringstream console_input;
    std::ostringstream console_output;
    nlp3::platform::PanelConsole panel_console{&panel_app, &console_input, &console_output};
    NLP3_TEST_REQUIRE(panel_console.execute_line("bridge runner status"));
    NLP3_TEST_REQUIRE(panel_console.execute_line("bridge runner start runner_test_user_2 1"));
    auto second_status = panel_app.external_runner_status();
    NLP3_TEST_REQUIRE(second_status.running);
    NLP3_TEST_REQUIRE(second_status.target_user == "runner_test_user_2");
    NLP3_TEST_REQUIRE(panel_console.execute_line("bridge runner"));
    NLP3_TEST_REQUIRE(panel_console.execute_line("bridge runner logs"));
    NLP3_TEST_REQUIRE(panel_console.execute_line("bridge runner stop"));
    NLP3_TEST_REQUIRE(wait_until_runner_stops(panel_app, std::chrono::milliseconds(3000)));
    second_status = panel_app.external_runner_status();
    NLP3_TEST_REQUIRE(!second_status.running);
    NLP3_TEST_REQUIRE(second_status.has_exit_code);
    NLP3_TEST_REQUIRE(second_status.last_exit_code == 0);
    NLP3_TEST_REQUIRE(second_status.last_error.empty());
    NLP3_TEST_REQUIRE(std::find_if(
        second_status.recent_log_lines.begin(),
        second_status.recent_log_lines.end(),
        [](const std::string& line) {
            return line.find("runner shutdown requested via control port") != std::string::npos;
        }) != second_status.recent_log_lines.end());
    NLP3_TEST_REQUIRE(std::find_if(
        second_status.recent_log_lines.begin(),
        second_status.recent_log_lines.end(),
        [](const std::string& line) {
            return line.find("runner stopped by panel") != std::string::npos;
        }) != second_status.recent_log_lines.end());
    const auto snapshot_after_manual_stop = panel_app.snapshot();
    NLP3_TEST_REQUIRE(snapshot_after_manual_stop.external_bridge.connection_state == "disconnected");
    NLP3_TEST_REQUIRE(snapshot_after_manual_stop.external_bridge.last_status_message == "Runner stopped by panel");

    const auto output = console_output.str();
    NLP3_TEST_REQUIRE(output.find("Bridge runner:") != std::string::npos);
    NLP3_TEST_REQUIRE(output.find("Bridge runner logs:") != std::string::npos);
    NLP3_TEST_REQUIRE(output.find("bridge runner started for runner_test_user_2") != std::string::npos);
    NLP3_TEST_REQUIRE(output.find("runner launch: python=") != std::string::npos);
    NLP3_TEST_REQUIRE(output.find("max_seconds=1") != std::string::npos);
    NLP3_TEST_REQUIRE(output.find("bridge runner stopped (exit=0)") != std::string::npos);

    NLP3_TEST_REQUIRE(panel_app.submit_external_bridge_event({
        nlp3::bridge::TikTokRawEventKind::chat,
        {
            "runner-backlog-user",
            "runner_backlog_user",
            "Runner Backlog User",
            {},
        },
        {
            "evt-runner-backlog-001",
            "room-runner-backlog-001",
            "comment",
            1710000050000,
        },
        "esto no se debe reproducir tras desconectar",
        std::nullopt,
        0,
    }));
    NLP3_TEST_REQUIRE(panel_app.snapshot().total_events == 0);
    panel_app.stop_external_runner();
    panel_app.tick(0);
    NLP3_TEST_REQUIRE(panel_app.snapshot().total_events == 0);

    std::filesystem::remove(config_path);
    std::filesystem::remove(script_path);
    return 0;
}
