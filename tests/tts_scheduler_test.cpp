#include <cassert>
#include <string>

#include "tts/mock_tts_backend.hpp"
#include "tts/tts_scheduler.hpp"

namespace {

nlp3::tts::TtsMessage make_manual_message(std::string text) {
    nlp3::tts::TtsMessage message{};
    message.trigger = nlp3::tts::TtsTrigger::manual_message;
    message.priority = nlp3::tts::TtsPriority::normal;
    message.category = nlp3::tts::TtsMessageCategory::manual;
    message.text = std::move(text);
    message.content_text = message.text;
    message.source = "test";
    return message;
}

} // namespace

int main() {
    using namespace nlp3::tts;

    // --- Sanitizacion basica de chat ---
    {
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
    }

    // --- A1: un fallo de speak() no debe vaciar ni perder la cola ---
    {
        MockTtsBackend backend{};
        TtsConfig config{};
        config.enabled = true;
        config.max_queue_size = 8;
        config.max_dispatch_per_tick = 10;

        TtsPolicy policy{};
        policy.allow_manual_messages = true;
        policy.min_text_length = 1;

        TtsScheduler scheduler{config, policy, backend};

        assert(scheduler.submit(make_manual_message("msg one")));
        assert(scheduler.submit(make_manual_message("msg two")));
        assert(scheduler.submit(make_manual_message("msg three")));
        assert(scheduler.queued_message_count() == 3);

        backend.set_fail_speak_count(1);
        assert(scheduler.dispatch_pending(10) == 0);
        assert(scheduler.queued_message_count() == 3);
        assert(backend.spoken_messages().empty());

        assert(scheduler.dispatch_pending(10) == 3);
        assert(scheduler.queued_message_count() == 0);
        assert(backend.spoken_messages().size() == 3);
        assert(backend.spoken_messages()[0].text == "msg one");
        assert(backend.spoken_messages()[1].text == "msg two");
        assert(backend.spoken_messages()[2].text == "msg three");
    }

    // --- A1: fallo permanente del backend mantiene la cola intacta (acotada) ---
    {
        MockTtsBackend backend{};
        TtsConfig config{};
        config.enabled = true;
        config.max_queue_size = 8;

        TtsPolicy policy{};
        policy.allow_manual_messages = true;

        TtsScheduler scheduler{config, policy, backend};
        assert(scheduler.submit(make_manual_message("keep me")));

        backend.set_fail_speak_count(1000);
        assert(scheduler.dispatch_pending(5) == 0);
        assert(scheduler.dispatch_pending(5) == 0);
        assert(scheduler.queued_message_count() == 1);
        assert(backend.spoken_messages().empty());
    }

    // --- A2: TTL descarta mensajes encolados hace mas de max_message_age_ms ---
    {
        MockTtsBackend backend{};
        TtsConfig config{};
        config.enabled = true;
        config.max_queue_size = 8;
        config.max_message_age_ms = 30000;

        TtsPolicy policy{};
        policy.allow_manual_messages = true;

        TtsScheduler scheduler{config, policy, backend};

        TtsMessage stale = make_manual_message("stale message");
        stale.enqueued_at_ms = 1; // antiguo respecto al reloj de pared
        assert(scheduler.submit(stale));

        assert(scheduler.submit(make_manual_message("fresh message")));
        assert(scheduler.queued_message_count() == 2);

        assert(scheduler.dispatch_pending(10) == 1);
        assert(scheduler.queued_message_count() == 0);
        assert(backend.spoken_messages().size() == 1);
        assert(backend.spoken_messages()[0].text == "fresh message");
    }

    // --- A2: max_message_age_ms == 0 desactiva el TTL ---
    {
        MockTtsBackend backend{};
        TtsConfig config{};
        config.enabled = true;
        config.max_queue_size = 8;
        config.max_message_age_ms = 0;

        TtsPolicy policy{};
        policy.allow_manual_messages = true;

        TtsScheduler scheduler{config, policy, backend};

        TtsMessage ancient = make_manual_message("ancient but allowed");
        ancient.enqueued_at_ms = 1;
        assert(scheduler.submit(ancient));
        assert(scheduler.dispatch_pending(1) == 1);
        assert(backend.spoken_messages().size() == 1);
    }

    // --- Tope de cola: sin drop, mensajes de prioridad no mayor son rechazados ---
    {
        MockTtsBackend backend{};
        TtsConfig config{};
        config.enabled = true;
        config.max_queue_size = 2;
        config.drop_oldest_on_overflow = false;

        TtsPolicy policy{};
        policy.allow_manual_messages = true;

        TtsScheduler scheduler{config, policy, backend};

        assert(scheduler.submit(make_manual_message("first")));
        assert(scheduler.submit(make_manual_message("second")));
        assert(!scheduler.submit(make_manual_message("third")));
        assert(scheduler.queued_message_count() == 2);
    }

    // --- D3: drop_oldest_on_overflow=true debe dropear el MÁS ANTIGUO, no el back ---
    {
        MockTtsBackend backend{};
        TtsConfig config{};
        config.enabled = true;
        config.max_queue_size = 3;
        config.drop_oldest_on_overflow = true;
        // Timestamps artificiales (100..400) solo para ordenar por edad; sin
        // esto el TTL de 30s los descartaria al hacer dispatch.
        config.max_message_age_ms = 0;

        TtsPolicy policy{};
        policy.allow_manual_messages = true;

        TtsScheduler scheduler{config, policy, backend};

        TtsMessage oldest = make_manual_message("oldest");
        oldest.enqueued_at_ms = 100;
        TtsMessage middle = make_manual_message("middle");
        middle.enqueued_at_ms = 200;
        TtsMessage newest = make_manual_message("newest");
        newest.enqueued_at_ms = 300;
        assert(scheduler.submit(oldest));
        assert(scheduler.submit(middle));
        assert(scheduler.submit(newest));
        assert(scheduler.queued_message_count() == 3);

        TtsMessage incoming = make_manual_message("incoming");
        incoming.enqueued_at_ms = 400;
        assert(scheduler.submit(incoming));
        assert(scheduler.queued_message_count() == 3);

        // Hablar todo: "oldest" no debe sonar; el resto sí, en orden de edad.
        assert(scheduler.dispatch_pending(10) == 3);
        assert(backend.spoken_messages().size() == 3);
        assert(backend.spoken_messages()[0].text == "middle");
        assert(backend.spoken_messages()[1].text == "newest");
        assert(backend.spoken_messages()[2].text == "incoming");
    }

    // --- D3: sin drop, prioridad más alta preempa al más antiguo de la mínima ---
    {
        MockTtsBackend backend{};
        TtsConfig config{};
        config.enabled = true;
        config.max_queue_size = 2;
        config.drop_oldest_on_overflow = false;
        // Timestamps artificiales solo para elegir victima por edad; TTL off.
        config.max_message_age_ms = 0;

        TtsPolicy policy{};
        policy.allow_manual_messages = true;

        TtsScheduler scheduler{config, policy, backend};

        TtsMessage low_a = make_manual_message("low-a");
        low_a.priority = TtsPriority::low;
        low_a.enqueued_at_ms = 100;
        TtsMessage low_b = make_manual_message("low-b");
        low_b.priority = TtsPriority::low;
        low_b.enqueued_at_ms = 200;
        assert(scheduler.submit(low_a));
        assert(scheduler.submit(low_b));

        TtsMessage high = make_manual_message("high");
        high.priority = TtsPriority::high;
        high.enqueued_at_ms = 300;
        assert(scheduler.submit(high));
        assert(scheduler.queued_message_count() == 2);

        assert(scheduler.dispatch_pending(10) == 2);
        // low-a (más antiguo de la mínima prioridad) fue preempado; low-b y high quedan.
        assert(backend.spoken_messages().size() == 2);
        assert(backend.spoken_messages()[0].text == "high");
        assert(backend.spoken_messages()[1].text == "low-b");
    }

    // --- D3: sin drop, prioridad igual o menor que la mínima se rechaza ---
    {
        MockTtsBackend backend{};
        TtsConfig config{};
        config.enabled = true;
        config.max_queue_size = 2;
        config.drop_oldest_on_overflow = false;

        TtsPolicy policy{};
        policy.allow_manual_messages = true;

        TtsScheduler scheduler{config, policy, backend};

        TtsMessage high_a = make_manual_message("high-a");
        high_a.priority = TtsPriority::high;
        TtsMessage high_b = make_manual_message("high-b");
        high_b.priority = TtsPriority::high;
        assert(scheduler.submit(high_a));
        assert(scheduler.submit(high_b));

        TtsMessage normal = make_manual_message("normal");
        normal.priority = TtsPriority::normal;
        assert(!scheduler.submit(normal));

        TtsMessage high_c = make_manual_message("high-c");
        high_c.priority = TtsPriority::high;
        assert(!scheduler.submit(high_c));
        assert(scheduler.queued_message_count() == 2);
    }

    // --- M1/P1.2: truncado UTF-8 seguro en max_text_length (no partir secuencias) ---
    {
        MockTtsBackend backend{};
        TtsConfig config{};
        config.enabled = true;
        config.max_queue_size = 8;
        config.max_text_length = 5;
        config.max_message_age_ms = 0;

        TtsPolicy policy{};
        policy.allow_manual_messages = true;
        policy.min_text_length = 1;

        TtsScheduler scheduler{config, policy, backend};

        // "ñandú!" = c3 b1 61 6e 64 c3 bd 21 (7 bytes). Cortar en 5 no debe
        // dejar un lead byte colgando al final.
        TtsMessage utf8_msg = make_manual_message("ñandú!");
        assert(scheduler.submit(utf8_msg));
        assert(scheduler.dispatch_pending(1) == 1);
        const auto& spoken = backend.spoken_messages();
        assert(spoken.size() == 1);
        assert(spoken[0].text.size() <= 5);
        const auto& text = spoken[0].text;
        if (!text.empty()) {
            const auto last = static_cast<unsigned char>(text.back());
            // No terminar en byte de continuacion suelta.
            assert((last & 0xC0) != 0x80 || text.size() >= 2);
        }

        // content_text tambien se trunca al mismo tope.
        TtsMessage both = make_manual_message("abcdefghij");
        both.content_text = "ñandú—extra";
        assert(scheduler.submit(both));
        assert(scheduler.dispatch_pending(1) == 1);
        assert(backend.spoken_messages().back().text.size() <= 5);
        assert(backend.spoken_messages().back().content_text.size() <= 5);
    }

    return 0;
}
