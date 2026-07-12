#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace nlp3 {
namespace platform {
class PanelApp;
}
} // namespace nlp3

namespace nlp3::bridge {

struct TikTokExternalWsStatus {
    bool running = false;
    std::uint16_t port = 0;
    std::size_t accepted_messages = 0;
    std::size_t rejected_messages = 0;
};

class TikTokExternalWsServer {
public:
    // Test-only: allow SO_REUSEADDR for test isolation (port reuse after crashes)
    // Production code should NOT use this - bridge MUST use SO_EXCLUSIVEADDRUSE
    static void set_test_mode(bool enabled) noexcept {
        test_mode_ = enabled;
    }
    static bool test_mode() noexcept {
        return test_mode_;
    }

    explicit TikTokExternalWsServer(platform::PanelApp* app) noexcept;
    ~TikTokExternalWsServer();

    bool start(std::uint16_t port = 8765);
    void stop();
    std::size_t poll();

    bool handle_text_message(const std::string& payload);

    bool running() const noexcept;
    TikTokExternalWsStatus status() const noexcept;

private:
    bool process_text_message(const std::string& payload, bool tick_after_accept);

    struct Impl;

    platform::PanelApp* app_ = nullptr;
    std::unique_ptr<Impl> impl_{};
    bool running_ = false;
    std::uint16_t port_ = 0;
    std::size_t accepted_messages_ = 0;
    std::size_t rejected_messages_ = 0;

    static bool test_mode_;
};

} // namespace nlp3::bridge