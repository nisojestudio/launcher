#include <cassert>
#include <cstdint>
#include <string>

#include "events/host_event.hpp"
#include "tts/mock_tts_backend.hpp"
#include "tts/tts_service.hpp"

namespace {

nlp3::events::HostEvent make_chat_event(
    std::int64_t timestamp_ms,
    std::string actor_id,
    std::string display_name,
    std::string message) {
    nlp3::events::HostEvent event{};
    event.kind = nlp3::events::HostEventKind::chat_message;
    event.actor.id = std::move(actor_id);
    event.actor.display_name = std::move(display_name);
    event.metadata.source = "test";
    event.metadata.source_timestamp_ms = timestamp_ms;
    event.message = std::move(message);
    return event;
}

} // namespace

int main() {
    using namespace nlp3::tts;

    MockTtsBackend backend{};
    TtsConfig config{};
    config.enabled = true;
    config.max_queue_size = 0;
    config.max_dispatch_per_tick = 8;

    TtsPolicy policy{};
    policy.allow_chat_messages = true;
    policy.include_actor_name_for_chat = true;
    policy.min_text_length = 1;
    policy.chat_cooldown_ms = 0;
    policy.chat_message_template = "{user}: {message}";

    HostTtsService service{config, policy, backend};

    assert(service.enqueue_chat_read(make_chat_event(1000, "ana", "Ana🙂", "si 😀")));
    assert(service.enqueue_chat_read(make_chat_event(1001, "luis", "Luis🔥", "ok 👍")));
    assert(service.enqueue_chat_read(make_chat_event(1002, "mia", "Mia", "vamos ñandú 🚀")));

    assert(service.queued_message_count() == 3);
    assert(service.dispatch_pending(3) == 3);
    assert(service.queued_message_count() == 0);

    const auto& spoken = backend.spoken_messages();
    assert(spoken.size() == 3);
    assert(spoken[0].actor_name == "Ana");
    assert(spoken[0].content_text == "si");
    assert(spoken[0].text == "si");
    assert(spoken[1].actor_name == "Luis");
    assert(spoken[1].content_text == "ok");
    assert(spoken[1].text == "ok");
    assert(spoken[2].actor_name == "Mia");
    assert(spoken[2].content_text == "vamos ñandú");
    assert(spoken[2].text == "vamos ñandú");

    assert(service.enqueue_chat_read(make_chat_event(1003, "teo", "Teo", "uno")));
    assert(service.enqueue_chat_read(make_chat_event(1004, "eva", "Eva", "dos")));
    assert(service.queued_message_count() == 2);
    service.clear_pending();
    assert(service.queued_message_count() == 0);
    assert(service.dispatch_pending(4) == 0);
    assert(backend.spoken_messages().size() == 3);

    return 0;
}
