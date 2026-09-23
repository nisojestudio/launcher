#include <cassert>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

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

    assert(service.enqueue_chat_read(make_chat_event(1000, "ana", "Ana", "si")));
    assert(service.enqueue_chat_read(make_chat_event(1001, "luis", "Luis", "ok")));
    assert(service.enqueue_chat_read(make_chat_event(1002, "mia", "Mia", "vamos nandu")));

    assert(service.queued_message_count() == 3);
    assert(service.dispatch_pending(3) == 3);
    assert(service.queued_message_count() == 0);

    const auto& spoken = backend.spoken_messages();
    assert(spoken.size() == 3);
    assert(spoken[0].actor_name == "Ana");
    assert(spoken[0].content_text == "si");
    assert(spoken[0].text == "Ana: si");
    assert(spoken[1].actor_name == "Luis");
    assert(spoken[1].content_text == "ok");
    assert(spoken[1].text == "Luis: ok");
    assert(spoken[2].actor_name == "Mia");
    assert(spoken[2].content_text == "vamos nandu");
    assert(spoken[2].text == "Mia: vamos nandu");

    TtsPolicy no_actor_policy = policy;
    no_actor_policy.include_actor_name_for_chat = false;
    HostTtsService no_actor_service{config, no_actor_policy, backend};
    assert(no_actor_service.enqueue_chat_read(make_chat_event(1005, "leo", "Leo", "solo mensaje")));
    assert(no_actor_service.dispatch_pending(1) == 1);
    assert(backend.spoken_messages().size() == 4);
    // P2.2/M5: plantilla SIEMPRE; {user} vacio cuando el flag esta off.
    // sanitize colapsa "Dice : hola" -> "Dice: hola".
    assert(backend.spoken_messages().back().text == ": solo mensaje");

    // P2.2/M5: plantilla estilo "Dice {user}: {message}" con flag off -> "Dice: hola"
    TtsPolicy dice_policy = policy;
    dice_policy.include_actor_name_for_chat = false;
    dice_policy.chat_message_template = "Dice {user}: {message}";
    HostTtsService dice_service{config, dice_policy, backend};
    assert(dice_service.enqueue_chat_read(make_chat_event(1010, "zoe", "Zoe", "hola")));
    assert(dice_service.dispatch_pending(1) == 1);
    assert(backend.spoken_messages().back().text == "Dice: hola");

    assert(service.enqueue_chat_read(make_chat_event(1006, "teo", "Teo", "uno")));
    assert(service.enqueue_chat_read(make_chat_event(1007, "eva", "Eva", "dos")));
    assert(service.queued_message_count() == 2);
    service.clear_pending();
    assert(service.queued_message_count() == 0);
    assert(service.dispatch_pending(4) == 0);
    // 3 (primer lote) + 1 (no_actor) + 1 (dice) = 5; clear no suma.
    assert(backend.spoken_messages().size() == 5);

    // --- M2/P1.3: cooldown con reloj de RECEPCION, no source_timestamp_ms ---
    {
        MockTtsBackend cd_backend{};
        TtsConfig cd_config{};
        cd_config.enabled = true;
        cd_config.max_queue_size = 8;
        cd_config.max_dispatch_per_tick = 8;

        TtsPolicy cd_policy{};
        cd_policy.allow_chat_messages = true;
        cd_policy.include_actor_name_for_chat = true;
        cd_policy.min_text_length = 1;
        cd_policy.chat_cooldown_ms = 80;
        cd_policy.chat_message_template = "{user}: {message}";

        HostTtsService cd_service{cd_config, cd_policy, cd_backend};

        // Evento con timestamp=0: el cooldown NO debe saltarse (antes se
        // ignoraba porque event_time==0).
        assert(cd_service.enqueue_chat_read(make_chat_event(0, "a1", "A1", "uno")));
        // Segundo inmediato: dentro de la ventana de 80ms -> rechazado,
        // aunque su source_timestamp_ms sea 0 o muy antiguo.
        assert(!cd_service.enqueue_chat_read(make_chat_event(0, "a2", "A2", "dos")));
        assert(!cd_service.enqueue_chat_read(make_chat_event(999999, "a3", "A3", "tres")));
        assert(cd_service.queued_message_count() == 1);

        // Fuera de la ventana (drenaje honesto con sleep corto).
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        assert(cd_service.enqueue_chat_read(make_chat_event(0, "a4", "A4", "cuatro")));
        assert(cd_service.queued_message_count() == 2);
        assert(cd_service.dispatch_pending(4) == 2);
        assert(cd_backend.spoken_messages().size() == 2);
    }

    // --- P1.5: enqueue_announcement vacio / solo emoji se rechaza ---
    {
        MockTtsBackend ann_backend{};
        TtsConfig ann_config{};
        ann_config.enabled = true;
        ann_config.max_queue_size = 8;
        ann_config.max_message_age_ms = 0;

        TtsPolicy ann_policy{};
        ann_policy.allow_manual_messages = true;
        ann_policy.min_text_length = 1;

        HostTtsService ann_service{ann_config, ann_policy, ann_backend};
        assert(!ann_service.enqueue_announcement(""));
        assert(!ann_service.enqueue_announcement("   "));
        assert(!ann_service.enqueue_announcement("🔥🚀🙂"));
        assert(ann_service.queued_message_count() == 0);
        assert(ann_service.enqueue_announcement("Hola panel"));
        assert(ann_service.queued_message_count() == 1);
        assert(ann_service.dispatch_pending(1) == 1);
        assert(ann_backend.spoken_messages().size() == 1);
        assert(ann_backend.spoken_messages()[0].text == "Hola panel");
    }

    return 0;
}
