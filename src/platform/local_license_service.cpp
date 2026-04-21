#include "platform/local_license_service.hpp"

#include <utility>

namespace nlp3::platform {

LocalLicenseService::LocalLicenseService(LicenseSnapshot snapshot) noexcept
    : snapshot_(std::move(snapshot)) {
    if (snapshot_.status == LicenseStatus::unknown
        && snapshot_.message.empty()
        && snapshot_.tier.empty()) {
        snapshot_ = LicenseSnapshot{
            LicenseStatus::active,
            "local development mode",
            "local-dev",
        };
    }
}

void LocalLicenseService::set_snapshot(LicenseSnapshot snapshot) noexcept {
    snapshot_ = std::move(snapshot);
}

LicenseSnapshot LocalLicenseService::snapshot() const {
    return snapshot_;
}

} // namespace nlp3::platform
