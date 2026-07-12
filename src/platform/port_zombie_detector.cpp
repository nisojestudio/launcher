#include "platform/port_zombie_detector.hpp"

#include <algorithm>
#include <chrono>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <iphlpapi.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <ws2tcpip.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "psapi.lib")
#endif

namespace nlp3::platform {

namespace {

// Puertos que el panel "posee" - duplicados aquí para uso en namespace anónimo
constexpr std::uint16_t kBridgeWsPort = 8765;
constexpr std::uint16_t kBridgeControlPort = 8770;
constexpr std::uint16_t kOverlayHttpPort = 18913;
constexpr std::uint16_t kPanelHttpPort = 8080;

#ifdef _WIN32
std::string get_process_name_by_pid(DWORD pid) {
    std::string name;
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hProcess) {
        wchar_t buffer[MAX_PATH];
        DWORD size = MAX_PATH;
        if (GetModuleBaseNameW(hProcess, nullptr, buffer, size)) {
            int required = WideCharToMultiByte(CP_UTF8, 0, buffer, -1, nullptr, 0, nullptr, nullptr);
            if (required > 0) {
                name.resize(required - 1);
                WideCharToMultiByte(CP_UTF8, 0, buffer, -1, &name[0], required, nullptr, nullptr);
            }
        }
        CloseHandle(hProcess);
    }
    if (name.empty()) {
        name = "pid:" + std::to_string(pid);
    }
    return name;
}

const char* tcp_state_to_string(DWORD state) {
    switch (state) {
        case MIB_TCP_STATE_CLOSED: return "CLOSED";
        case MIB_TCP_STATE_LISTEN: return "LISTENING";
        case MIB_TCP_STATE_SYN_SENT: return "SYN_SENT";
        case MIB_TCP_STATE_SYN_RCVD: return "SYN_RCVD";
        case MIB_TCP_STATE_ESTAB: return "ESTABLISHED";
        case MIB_TCP_STATE_FIN_WAIT1: return "FIN_WAIT1";
        case MIB_TCP_STATE_FIN_WAIT2: return "FIN_WAIT2";
        case MIB_TCP_STATE_CLOSE_WAIT: return "CLOSE_WAIT";
        case MIB_TCP_STATE_CLOSING: return "CLOSING";
        case MIB_TCP_STATE_LAST_ACK: return "LAST_ACK";
        case MIB_TCP_STATE_TIME_WAIT: return "TIME_WAIT";
        case MIB_TCP_STATE_DELETE_TCB: return "DELETE_TCB";
        default: return "UNKNOWN";
    }
}

bool is_our_port(uint16_t port) {
    return port == kBridgeWsPort || port == kBridgeControlPort ||
           port == kOverlayHttpPort || port == kPanelHttpPort;
}

bool is_zombie_state(DWORD state) {
    // TIME_WAIT y CLOSE_WAIT son estados "zombie" que indican limpieza pendiente
    return state == MIB_TCP_STATE_TIME_WAIT || state == MIB_TCP_STATE_CLOSE_WAIT;
}

#endif

} // namespace

std::vector<PortStatus> PortZombieDetector::scan_owned_ports() {
    std::vector<PortStatus> results;

#ifdef _WIN32
    // Obtener tabla TCP extendida con información de PID
    ULONG size = 0;
    DWORD ret = GetExtendedTcpTable(nullptr, &size, TRUE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
    if (ret != ERROR_INSUFFICIENT_BUFFER) {
        return results;
    }

    std::vector<BYTE> buffer(size);
    auto* table = reinterpret_cast<MIB_TCPTABLE_OWNER_PID*>(buffer.data());
    ret = GetExtendedTcpTable(table, &size, TRUE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
    if (ret != NO_ERROR) {
        return results;
    }

    DWORD current_pid = GetCurrentProcessId();

    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const auto& row = table->table[i];
        uint16_t local_port = ntohs(static_cast<uint16_t>(row.dwLocalPort));
        uint16_t remote_port = ntohs(static_cast<uint16_t>(row.dwRemotePort));

        // Solo nos interesan puertos propios en localhost
        if (!is_our_port(local_port)) {
            continue;
        }

        PortStatus status;
        status.port = local_port;
        status.in_use = true;
        status.pid = row.dwOwningPid;
        status.process_name = get_process_name_by_pid(row.dwOwningPid);
        status.state = tcp_state_to_string(row.dwState);
        status.is_zombie = is_zombie_state(row.dwState);

        results.push_back(status);
    }
#endif

    return results;
}

std::vector<PortStatus> PortZombieDetector::detect_zombies() {
    auto all = scan_owned_ports();
    std::vector<PortStatus> zombies;
    for (const auto& p : all) {
        if (p.is_zombie) {
            zombies.push_back(p);
        }
    }
    return zombies;
}

bool PortZombieDetector::cleanup_zombies(const std::vector<PortStatus>& zombies) {
    bool any_cleaned = false;
    for (const auto& z : zombies) {
        if (z.pid != 0 && z.pid != GetCurrentProcessId()) {
            // Intentar cerrar handle duplicado o terminar proceso nuestro
            HANDLE hProcess = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, z.pid);
            if (hProcess) {
                // Solo matar si es un proceso relacionado con nuestro panel
                // (evitar matar procesos ajenos que por casualidad usan el puerto)
                char exe_name[MAX_PATH] = {0};
                if (GetModuleFileNameExA(hProcess, nullptr, exe_name, MAX_PATH)) {
                    std::string exe(exe_name);
                    // Solo procesos nuestros: python (bridge), cloudflared, panel_app
                    if (exe.find("python") != std::string::npos ||
                        exe.find("cloudflared") != std::string::npos ||
                        exe.find("panel") != std::string::npos ||
                        exe.find("Nisoje") != std::string::npos) {
                        TerminateProcess(hProcess, 0);
                        any_cleaned = true;
                    }
                }
                CloseHandle(hProcess);
            }
        }
    }
    return any_cleaned;
}

// Limpieza agresiva: mata cualquier proceso que tenga LISTENING en nuestros puertos
bool PortZombieDetector::force_cleanup_owned_ports() {
    bool any_cleaned = false;
    auto all = scan_owned_ports();
    for (const auto& p : all) {
        if (p.in_use && p.state == "LISTENING" && p.pid != 0 && p.pid != GetCurrentProcessId()) {
            HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, p.pid);
            if (hProcess) {
                TerminateProcess(hProcess, 0);
                CloseHandle(hProcess);
                any_cleaned = true;
            }
        }
    }
    return any_cleaned;
}

bool PortZombieDetector::is_port_free(std::uint16_t port) {
    auto all = scan_owned_ports();
    for (const auto& p : all) {
        if (p.port == port && p.in_use) {
            return false;
        }
    }
    return true;
}

std::string PortZombieDetector::generate_report() {
    std::ostringstream out;
    out << "=== Port Zombie Report ===\n";
    out << "Timestamp: " << std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() << "ms\n\n";

    auto owned = scan_owned_ports();
    auto zombies = detect_zombies();

    out << "Owned Ports:\n";
    for (const auto& p : owned) {
        out << "  Port " << p.port << ": " << (p.in_use ? "IN_USE" : "FREE")
            << " PID=" << p.pid << " (" << p.process_name << ")"
            << " State=" << p.state;
        if (p.is_zombie) out << " [ZOMBIE]";
        out << "\n";
    }

    out << "\nZombies Detected: " << zombies.size() << "\n";
    for (const auto& z : zombies) {
        out << "  Port " << z.port << " PID=" << z.pid << " State=" << z.state << "\n";
    }

    out << "\nAll Clear: " << (zombies.empty() ? "YES" : "NO") << "\n";
    return out.str();
}

} // namespace nlp3::platform