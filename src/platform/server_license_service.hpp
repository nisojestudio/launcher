#pragma once

#include <cstdint>
#include <string>

#include "platform/license_service.hpp"
#include "platform/panel_config.hpp"
#include "platform/panel_snapshot.hpp"

namespace nlp3::platform {

struct PanelAuthLoginRequest {
    std::string email{};
    std::string password{};
    std::string license_key{};
    std::string device_name{};
    std::string device_id{};
};

struct PanelAuthLoginResult {
    bool ok = false;
    std::string message{};
    std::string error_code{};
    LicenseSnapshot license{};
    PanelAuthStatus auth{};
};

class ServerLicenseService final : public ILicenseService {
public:
    explicit ServerLicenseService(PanelAuthConfig config = {}) noexcept;

    LicenseSnapshot snapshot() const override;
    PanelAuthStatus auth_snapshot() const;
    bool access_granted() const noexcept;
    bool access_required() const noexcept;

    PanelAuthLoginResult authenticate(const PanelAuthLoginRequest& request);
    void logout() noexcept;
    void update_config(PanelAuthConfig config) noexcept;
    std::string id_token() const;

private:
    void reset_for_current_mode() noexcept;

    PanelAuthConfig config_{};
    LicenseSnapshot license_snapshot_{};
    PanelAuthStatus auth_snapshot_{};
    std::string id_token_{};
};

} // namespace nlp3::platform
