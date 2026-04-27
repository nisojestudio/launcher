#include <cassert>
#include <string>

#include "nlohmann/json.hpp"

#include "platform/support_bundle_sanitizer.hpp"

int main() {
    auto payload = nlohmann::json{
        {"state",
         {
             {"auth",
              {
                  {"email", "user@example.com"},
                  {"firebaseUid", "firebase-uid-123"},
                  {"licenseKey", "LIC-123"},
                  {"message", "ok"},
              }},
         }},
        {"metrics",
         {
             {"nested",
              {
                  {"apiKey", "api-key-123"},
                  {"sessionToken", "token-123"},
                  {"status", "ready"},
              }},
         }},
        {"events",
         nlohmann::json::array({
             {
                 {"payload",
                  {
                      {"email", "another@example.com"},
                      {"tier", "pro"},
                  }},
             },
         })},
    };

    payload = nlp3::platform::sanitize_support_bundle_json(std::move(payload));

    assert(payload["state"]["auth"]["email"] == "[redacted]");
    assert(payload["state"]["auth"]["firebaseUid"] == "[redacted]");
    assert(payload["state"]["auth"]["licenseKey"] == "[redacted]");
    assert(payload["state"]["auth"]["message"] == "ok");
    assert(payload["metrics"]["nested"]["apiKey"] == "[redacted]");
    assert(payload["metrics"]["nested"]["sessionToken"] == "[redacted]");
    assert(payload["metrics"]["nested"]["status"] == "ready");
    assert(payload["events"][0]["payload"]["email"] == "[redacted]");
    assert(payload["events"][0]["payload"]["tier"] == "pro");

    const auto redacted_log = nlp3::platform::sanitize_support_bundle_log_line(
        R"({"email":"log@example.com","authorization":"Bearer token-123","message":"bridge ok"})");
    const auto parsed_log = nlohmann::json::parse(redacted_log);
    assert(parsed_log["email"] == "[redacted]");
    assert(parsed_log["authorization"] == "[redacted]");
    assert(parsed_log["message"] == "bridge ok");

    const auto plain_line = std::string("plain text line without JSON");
    assert(nlp3::platform::sanitize_support_bundle_log_line(plain_line) == plain_line);
    return 0;
}
