#pragma once

#include <string>

namespace nlp3::platform {

enum class LicenseStatus {
    unknown,
    inactive,
    active,
};

struct LicenseSnapshot {
    LicenseStatus status = LicenseStatus::unknown;
    std::string message{};
    std::string tier{};
};

class ILicenseService {
public:
    virtual ~ILicenseService() = default;

    virtual LicenseSnapshot snapshot() const = 0;
};

} // namespace nlp3::platform
