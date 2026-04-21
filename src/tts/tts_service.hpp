#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "events/host_event.hpp"
#include "tts/tts_backend.hpp"
#include "tts/tts_config.hpp"
#include "tts/tts_message.hpp"
#include "tts/tts_policy.hpp"
#include "tts/tts_scheduler.hpp"

namespace nlp3::tts {

class ITtsService {
public:
    virtual ~ITtsService() = default;

    virtual std::string_view service_name() const noexcept = 0;
    virtual bool available() const noexcept = 0;
    virtual bool submit(TtsMessage message) = 0;
    virtual bool enqueue_chat_read(const events::HostEvent& event) = 0;
    virtual bool enqueue_announcement(std::string_view message) = 0;
    virtual std::size_t dispatch_pending(std::size_t max_messages = 0) = 0;
    virtual std::size_t queued_message_count() const noexcept = 0;
    virtual void clear_pending() noexcept = 0;
    virtual void set_config(TtsConfig config) = 0;
    virtual void set_policy(TtsPolicy policy) = 0;
};

class HostTtsService final : public ITtsService {
public:
    HostTtsService(TtsConfig config, TtsPolicy policy, ITtsBackend& backend) noexcept;

    std::string_view service_name() const noexcept override;
    bool available() const noexcept override;
    bool submit(TtsMessage message) override;
    bool enqueue_chat_read(const events::HostEvent& event) override;
    bool enqueue_announcement(std::string_view message) override;
    std::size_t dispatch_pending(std::size_t max_messages = 0) override;
    std::size_t queued_message_count() const noexcept override;
    void clear_pending() noexcept override;
    void set_config(TtsConfig config) override;
    void set_policy(TtsPolicy policy) override;

private:
    bool allow_chat_event(const events::HostEvent& event) const;
    TtsMessage build_chat_message(const events::HostEvent& event) const;

    TtsConfig config_;
    TtsPolicy policy_;
    TtsScheduler scheduler_;
    std::int64_t last_chat_enqueued_at_ms_ = 0;
};

} // namespace nlp3::tts
