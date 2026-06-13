#include "platform/device_identifier.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <winreg.h>
#endif

namespace nlp3::platform {

namespace {

/// Returns %LOCALAPPDATA%\NisojeStudio as the data directory.
std::string get_panel_data_directory_impl() {
#ifdef _WIN32
    // Read LOCALAPPDATA environment variable.
    std::wstring local_app_data;
    {
        const auto length = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
        if (length > 0) {
            local_app_data.resize(static_cast<std::size_t>(length) - 1);
            GetEnvironmentVariableW(
                L"LOCALAPPDATA",
                local_app_data.data(),
                static_cast<DWORD>(local_app_data.size() + 1));
        }
    }

    if (!local_app_data.empty()) {
        auto path = std::filesystem::path(local_app_data) / L"NisojeStudio";
        return path.string();
    }
#endif
    // Fallback: current directory.
    std::error_code ec;
    auto cwd = std::filesystem::current_path(ec);
    if (!ec) {
        return (cwd / "NisojeStudio").string();
    }
    return "NisojeStudio";
}

/// Generates a UUID v4 string.
std::string generate_uuid_v4() {
    // Use a random device to seed a 64-bit Mersenne Twister.
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<std::uint64_t> dist;

    // Generate 128 bits.
    auto hi = dist(gen);
    auto lo = dist(gen);

    // Version 4: set bits 12-15 of the time_hi_and_version field to 0100 (version 4).
    hi &= 0xFFFFFFFFFFFF0FFFULL; // Clear version bits.
    hi |= 0x0000000000004000ULL; // Set version to 4.

    // Variant 1: set bits 6-7 of clock_seq_hi_and_reserved to 10.
    lo &= 0x3FFFFFFFFFFFFFFFULL; // Clear variant bits.
    lo |= 0x8000000000000000ULL; // Set variant to 1.

    // Format as 8-4-4-4-12 hex string.
    std::ostringstream oss;
    oss << std::hex << std::nouppercase << std::setfill('0');
    oss << std::setw(8) << ((hi >> 32) & 0xFFFFFFFF);
    oss << '-';
    oss << std::setw(4) << ((hi >> 16) & 0xFFFF);
    oss << '-';
    oss << std::setw(4) << (hi & 0xFFFF);
    oss << '-';
    oss << std::setw(4) << ((lo >> 48) & 0xFFFF);
    oss << '-';
    oss << std::setw(12) << (lo & 0xFFFFFFFFFFFFULL);
    return oss.str();
}

/// Reads the device-id from the .device-id file, or generates+stores a new one.
std::string get_or_create_device_uuid() {
    const auto dir = get_panel_data_directory_impl();
    const auto file_path = std::filesystem::path(dir) / ".device-id";

    // Ensure the directory exists.
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(dir), ec);

    // Try to read existing UUID.
    {
        std::ifstream infile(file_path);
        if (infile.is_open()) {
            std::string uuid;
            std::getline(infile, uuid);
            // Trim whitespace.
            uuid.erase(0, uuid.find_first_not_of(" \t\r\n"));
            uuid.erase(uuid.find_last_not_of(" \t\r\n") + 1);
            if (!uuid.empty()) {
                return uuid;
            }
        }
    }

    // Generate new UUID.
    const auto uuid = generate_uuid_v4();

    // Store it.
    {
        std::ofstream outfile(file_path);
        if (outfile.is_open()) {
            outfile << uuid << "\n";
        }
    }

    return uuid;
}

/// Reads MachineGUID from the Windows registry.
/// Returns empty string on failure or non-Windows.
std::string read_machine_guid() {
#ifdef _WIN32
    HKEY hKey = nullptr;
    const auto result = RegOpenKeyExW(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Cryptography",
        0,
        KEY_READ | KEY_WOW64_64KEY,
        &hKey);

    if (result != ERROR_SUCCESS) {
        return {};
    }

    wchar_t buffer[256] = {};
    DWORD buffer_size = sizeof(buffer);
    DWORD type = 0;
    const auto query_result = RegQueryValueExW(
        hKey,
        L"MachineGuid",
        nullptr,
        &type,
        reinterpret_cast<LPBYTE>(buffer),
        &buffer_size);

    RegCloseKey(hKey);

    if (query_result != ERROR_SUCCESS || type != REG_SZ) {
        return {};
    }

    // Convert wide string to narrow.
    const auto len = WideCharToMultiByte(
        CP_UTF8, 0, buffer, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        return {};
    }
    std::string guid(static_cast<std::size_t>(len) - 1, '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, buffer, -1, guid.data(), len, nullptr, nullptr);
    return guid;
#else
    return {};
#endif
}

/// Returns a human-readable platform name.
std::string get_platform_name() {
#ifdef _WIN32
    std::wstring buf(256, L'\0');
    auto len = GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SISO639LANGNAME, buf.data(), static_cast<int>(buf.size()));
    std::string lang;
    if (len > 0) {
        buf.resize(static_cast<std::size_t>(len) - 1);
        lang.resize(static_cast<std::size_t>(len) - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, buf.data(), -1, lang.data(), static_cast<int>(lang.size() + 1), nullptr, nullptr);
    } else {
        lang = "unknown";
    }
    return "Windows · " + lang;
#else
    return "Unknown";
#endif
}

} // anonymous namespace

std::string get_panel_data_directory() {
    return get_panel_data_directory_impl();
}

std::string get_composite_device_id() {
    const auto uuid = get_or_create_device_uuid();
    const auto machine_guid = read_machine_guid();

    if (machine_guid.empty()) {
        // No MachineGUID available (non-Windows or permission issue).
        return uuid;
    }

    // Composite: "uuid | machine-guid"
    return uuid + " | " + machine_guid;
}

std::string get_device_name() {
    return get_platform_name();
}

} // namespace nlp3::platform
