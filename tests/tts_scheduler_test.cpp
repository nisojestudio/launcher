#include <cassert>

#include "tts/mock_tts_backend.hpp"
#include "tts/tts_scheduler.hpp"

int main() {
    using namespace nlp3::tts;

    MockTtsBackend backend{};
    TtsConfig config{};
    config.enabled = true;
    config.max_queue_size = 8;

    TtsPolicy policy{};
    policy.allow_chat_messages = true;
    policy.min_text_length = 3;

    TtsScheduler scheduler{config, policy, backend};

    TtsMessage readable{};
    readable.trigger = TtsTrigger::chat_event;
    readable.priority = TtsPriority::low;
    readable.category = TtsMessageCategory::chat;
    readable.actor_name = "Pepe🙂";
    readable.content_text = "Hola 😊 mundo ñandú 🚀";
    readable.text = "Pepe🙂: Hola 😊 mundo ñandú 🚀";
    readable.source = "test";
    readable.created_at_ms = 123;

    assert(scheduler.submit(readable));
    assert(scheduler.dispatch_pending(1) == 1);
    assert(backend.spoken_messages().size() == 1);
    assert(backend.spoken_messages()[0].actor_name == "Pepe");
    assert(backend.spoken_messages()[0].content_text == "Hola mundo ñandú");
    assert(backend.spoken_messages()[0].text == "Pepe: Hola mundo ñandú");

    TtsMessage only_emoji{};
    only_emoji.trigger = TtsTrigger::chat_event;
    only_emoji.priority = TtsPriority::low;
    only_emoji.category = TtsMessageCategory::chat;
    only_emoji.actor_name = "🙂";
    only_emoji.content_text = "🔥🚀🙂";
    only_emoji.text = "🔥🚀🙂";
    only_emoji.source = "test";
    only_emoji.created_at_ms = 456;

    assert(!scheduler.submit(only_emoji));
    assert(backend.spoken_messages().size() == 1);

    return 0;
}
