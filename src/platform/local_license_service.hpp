#pragma once

#include "platform/license_service.hpp"

namespace nlp3::platform {

class LocalLicenseService final : public ILicenseService {
public:
    explicit LocalLicenseService(LicenseSnapshot snapshot = {}) noexcept;

    void set_snapshot(LicenseSnapshot snapshot) noexcept;
    LicenseSnapshot snapshot() const override;

private:
    LicenseSnapshot snapshot_{};
};

} // namespace nlp3::platform
