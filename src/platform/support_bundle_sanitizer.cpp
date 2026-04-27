#include "platform/support_bundle_sanitizer.hpp"

#include <algorithm>
#include <array>
#include <cctype>

#include "nlohmann/json.hpp"

namespace {

std::string canonicalize_key(std::string_view key) {
    std::string normalized{};
    normalized.reserve(key.size());
    for (const auto ch : key) {
        const auto lowered = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if ((lowered >= 'a' && lowered <= 'z') || (lowered >= '0' && lowered <= '9')) {
            normalized.push_back(lowered);
        }
    }
    return normalized;
}

bool is_sensitive_key(std::string_view key) {
    static constexpr std::array<std::string_view, 11> kSensitiveKeys{
        "email",
        "firebaseuid",
        "licensekey",
        "password",
        "idtoken",
        "refreshtoken",
        "accesstoken",
        "sessiontoken",
        "authtoken",
        "authorization",
        "apikey",
    };

    const auto normalized = canonicalize_key(key);
    return std::find(kSensitiveKeys.begin(), kSensitiveKeys.end(), normalized) != kSensitiveKeys.end();
}

bool is_empty_string(const nlohmann::json& value) {
    return value.is_string() && value.get_ref<const std::string&>().empty();
}

void redact_sensitive_fields(nlohmann::json& value) {
    if (value.is_object()) {
        for (auto it = value.begin(); it != value.end(); ++it) {
            if (is_sensitive_key(it.key())) {
                if (!it->is_null() && !is_empty_string(*it)) {
                    *it = "[redacted]";
                }
                continue;
            }
            redact_sensitive_fields(*it);
        }
        return;
    }

    if (value.is_array()) {
        for (auto& item : value) {
            redact_sensitive_fields(item);
        }
    }
}

} // namespace

namespace nlp3::platform {

nlohmann::json sanitize_support_bundle_json(nlohmann::json value) {
    redact_sensitive_fields(value);
    return value;
}

std::string sanitize_support_bundle_log_line(std::string_view line) {
    auto parsed = nlohmann::json::parse(line, nullptr, false);
    if (parsed.is_discarded()) {
        return std::string(line);
    }

    redact_sensitive_fields(parsed);
    return parsed.dump();
}

} // namespace nlp3::platform
