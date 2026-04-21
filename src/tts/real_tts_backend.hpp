#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

#include "tts/tts_backend.hpp"

namespace nlp3::tts {

class RealTtsBackend final : public ITtsBackend {
public:
    RealTtsBackend();
    ~RealTtsBackend() override;

    std::string_view backend_name() const noexcept override;
    bool available() const noexcept override;
    void apply_config(const TtsConfig& config) override;
    std::vector<TtsVoiceDescriptor> voice_catalog() const override;
    bool speak(const TtsMessage& message) override;
    std::size_t queued_message_count() const noexcept override;
    void clear_pending() noexcept override;

private:
    void worker_main();

    mutable std::mutex mutex_{};
    std::condition_variable cv_{};
    std::deque<TtsMessage> queue_{};
    std::thread worker_{};
    TtsConfig config_{};
    std::vector<TtsVoiceDescriptor> voice_catalog_{};
    std::atomic<bool> running_{true};
    std::atomic<bool> available_{false};
};

} // namespace nlp3::tts
