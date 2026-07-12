#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nlp3::platform {

struct PortStatus {
    std::uint16_t port = 0;
    bool in_use = false;
    std::uint32_t pid = 0;
    std::string process_name;
    std::string state;
    bool is_zombie = false;
};

class PortZombieDetector {
public:
    // Puertos que el panel "posee"
    static constexpr std::uint16_t BRIDGE_WS_PORT = 8765;
    static constexpr std::uint16_t BRIDGE_CONTROL_PORT = 8770;
    static constexpr std::uint16_t OVERLAY_HTTP_PORT = 18913;
    static constexpr std::uint16_t PANEL_HTTP_PORT = 8080;

    static std::vector<PortStatus> scan_owned_ports();
    static std::vector<PortStatus> detect_zombies();
    static bool cleanup_zombies(const std::vector<PortStatus>& zombies);
    static bool force_cleanup_owned_ports();
    static bool is_port_free(std::uint16_t port);

    // Para diagnóstico
    static std::string generate_report();
};

} // namespace nlp3::platform