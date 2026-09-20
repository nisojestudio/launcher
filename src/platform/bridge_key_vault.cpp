#include "platform/bridge_key_vault.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincrypt.h>
#pragma comment(lib, "crypt32.lib")
#endif

namespace nlp3::platform {

namespace {

constexpr const char* kCacheFileHeader = "NLP3KEYVAULT1:";

std::string base64_encode(const std::vector<unsigned char>& data) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve(((data.size() + 2) / 3) * 4);
    std::size_t index = 0;
    while (index + 2 < data.size()) {
        const auto triple = (static_cast<std::uint32_t>(data[index]) << 16)
            | (static_cast<std::uint32_t>(data[index + 1]) << 8)
            | static_cast<std::uint32_t>(data[index + 2]);
        output.push_back(kAlphabet[(triple >> 18) & 0x3F]);
        output.push_back(kAlphabet[(triple >> 12) & 0x3F]);
        output.push_back(kAlphabet[(triple >> 6) & 0x3F]);
        output.push_back(kAlphabet[triple & 0x3F]);
        index += 3;
    }
    const auto remaining = data.size() - index;
    if (remaining == 1) {
        const auto value = static_cast<std::uint32_t>(data[index]) << 16;
        output.push_back(kAlphabet[(value >> 18) & 0x3F]);
        output.push_back(kAlphabet[(value >> 12) & 0x3F]);
        output.push_back('=');
        output.push_back('=');
    } else if (remaining == 2) {
        const auto value = (static_cast<std::uint32_t>(data[index]) << 16)
            | (static_cast<std::uint32_t>(data[index + 1]) << 8);
        output.push_back(kAlphabet[(value >> 18) & 0x3F]);
        output.push_back(kAlphabet[(value >> 12) & 0x3F]);
        output.push_back(kAlphabet[(value >> 6) & 0x3F]);
        output.push_back('=');
    }
    return output;
}

std::vector<unsigned char> base64_decode(std::string_view text) {
    auto decode_char = [](char value) -> int {
        if (value >= 'A' && value <= 'Z') return value - 'A';
        if (value >= 'a' && value <= 'z') return value - 'a' + 26;
        if (value >= '0' && value <= '9') return value - '0' + 52;
        if (value == '+') return 62;
        if (value == '/') return 63;
        return -1;
    };

    std::vector<unsigned char> output;
    int buffer = 0;
    int bits = 0;
    for (const char raw : text) {
        if (raw == '=') {
            break;
        }
        const auto value = decode_char(raw);
        if (value < 0) {
            continue;
        }
        buffer = (buffer << 6) | value;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            output.push_back(static_cast<unsigned char>((buffer >> bits) & 0xFF));
        }
    }
    return output;
}

#ifdef _WIN32
std::vector<unsigned char> protect_bytes(const std::string& plain) {
    DATA_BLOB input{};
    input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()));
    input.cbData = static_cast<DWORD>(plain.size());
    DATA_BLOB output{};
    if (CryptProtectData(
            &input,
            L"Nisoje Studio bridge credentials",
            nullptr,
            nullptr,
            nullptr,
            0,
            &output) == FALSE) {
        return {};
    }
    std::vector<unsigned char> result(output.pbData, output.pbData + output.cbData);
    LocalFree(output.pbData);
    return result;
}

std::string unprotect_bytes(const std::vector<unsigned char>& blob) {
    if (blob.empty()) {
        return {};
    }
    DATA_BLOB input{};
    input.pbData = const_cast<BYTE*>(blob.data());
    input.cbData = static_cast<DWORD>(blob.size());
    DATA_BLOB output{};
    if (CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, 0, &output) == FALSE) {
        return {};
    }
    std::string result(reinterpret_cast<char*>(output.pbData), output.cbData);
    LocalFree(output.pbData);
    return result;
}
#endif

std::int64_t now_wall_clock_ms() {
    return static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

} // namespace

std::filesystem::path BridgeKeyVault::default_path() {
    const char* local_app_data = std::getenv("LOCALAPPDATA");
    if (local_app_data == nullptr || *local_app_data == '\0') {
        return {};
    }
    return std::filesystem::path(local_app_data) / "NisojeStudio" / "credentials.dat";
}

std::filesystem::path BridgeKeyVault::transient_pool_path() {
    const char* local_app_data = std::getenv("LOCALAPPDATA");
    if (local_app_data == nullptr || *local_app_data == '\0') {
        return {};
    }
    return std::filesystem::path(local_app_data) / "NisojeStudio" / "run" / "bridge-api-keys.json";
}

bool BridgeKeyVault::load(const std::filesystem::path& path) {
    entries_.clear();
    if (path.empty()) {
        return false;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    const auto raw = buffer.str();
    if (raw.empty()) {
        return false;
    }

    const std::string header{kCacheFileHeader};
    if (raw.rfind(header, 0) != 0) {
        return false;
    }

    nlohmann::json payload = nlohmann::json::parse(raw.substr(header.size()), nullptr, false);
    if (payload.is_discarded() || !payload.is_object()) {
        return false;
    }

    const auto cipher_text = payload.value("payload", std::string{});
    if (cipher_text.empty()) {
        return false;
    }
#ifndef _WIN32
    (void)cipher_text;
    return false;
#else
    const auto plain = unprotect_bytes(base64_decode(cipher_text));
    if (plain.empty()) {
        return false;
    }

    nlohmann::json parsed = nlohmann::json::parse(plain, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        return false;
    }

    const auto keys = parsed.find("keys");
    if (keys == parsed.end() || !keys->is_array()) {
        return false;
    }

    for (const auto& item : *keys) {
        if (!item.is_object()) {
            continue;
        }
        BridgeKeyEntry entry{};
        entry.label = item.value("label", std::string{});
        entry.secret = item.value("secret", std::string{});
        entry.cooldown_until_ms = item.value("cooldown_until_ms", static_cast<std::int64_t>(0));
        entry.last_used_ms = item.value("last_used_ms", static_cast<std::int64_t>(0));
        entry.failure_count = static_cast<std::size_t>(item.value("failure_count", 0));
        if (!entry.secret.empty()) {
            entries_.push_back(std::move(entry));
        }
    }
    return true;
#endif
}

bool BridgeKeyVault::save(const std::filesystem::path& path) const {
    if (path.empty()) {
        return false;
    }

    nlohmann::json keys = nlohmann::json::array();
    for (const auto& entry : entries_) {
        keys.push_back({
            {"label", entry.label},
            {"secret", entry.secret},
            {"cooldown_until_ms", entry.cooldown_until_ms},
            {"last_used_ms", entry.last_used_ms},
            {"failure_count", entry.failure_count},
        });
    }

    const auto plain = nlohmann::json{{"keys", keys}}.dump();
#ifndef _WIN32
    (void)plain;
    return false;
#else
    const auto encrypted = protect_bytes(plain);
    if (encrypted.empty()) {
        return false;
    }

    std::error_code error;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            return false;
        }
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output << kCacheFileHeader << nlohmann::json{{"payload", base64_encode(encrypted)}}.dump();
    return output.good();
#endif
}

bool BridgeKeyVault::add(std::string label, std::string secret) {
    if (secret.empty()) {
        return false;
    }
    BridgeKeyEntry entry{};
    entry.label = label.empty() ? ("cuenta " + std::to_string(entries_.size() + 1)) : std::move(label);
    entry.secret = std::move(secret);
    entries_.push_back(std::move(entry));
    return true;
}

bool BridgeKeyVault::replace(std::size_t index, std::string label, std::string secret) {
    if (index >= entries_.size() || secret.empty()) {
        return false;
    }
    entries_[index].label = label.empty() ? entries_[index].label : std::move(label);
    entries_[index].secret = std::move(secret);
    entries_[index].cooldown_until_ms = 0;
    entries_[index].failure_count = 0;
    return true;
}

bool BridgeKeyVault::remove_at(std::size_t index) {
    if (index >= entries_.size()) {
        return false;
    }
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

bool BridgeKeyVault::mark_cooldown(std::size_t index, std::int64_t until_ms) {
    if (index >= entries_.size()) {
        return false;
    }
    entries_[index].cooldown_until_ms = until_ms;
    return true;
}

int BridgeKeyVault::first_available(std::int64_t now_ms) const {
    for (std::size_t index = 0; index < entries_.size(); ++index) {
        if (entries_[index].cooldown_until_ms <= now_ms) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

std::string BridgeKeyVault::fingerprint(const std::string& secret) {
    if (secret.empty()) {
        return {};
    }
    const auto prefix_length = std::min<std::size_t>(6, secret.size());
    std::uint64_t hash = 1469598103934665603ULL;
    for (const char value : secret) {
        hash ^= static_cast<unsigned char>(value);
        hash *= 1099511628211ULL;
    }
    std::ostringstream output;
    output << secret.substr(0, prefix_length) << "..." << std::hex << ((hash >> 24) & 0xFFFF);
    return output.str();
}

std::string BridgeKeyVault::to_pool_json(std::int64_t now_ms) const {
    nlohmann::json keys = nlohmann::json::array();
    for (auto entry : entries_) {
        if (entry.cooldown_until_ms > now_ms) {
            continue;
        }
        keys.push_back({
            {"label", entry.label},
            {"value", entry.secret},
        });
    }
    return nlohmann::json{{"keys", keys}}.dump();
}

} // namespace nlp3::platform
