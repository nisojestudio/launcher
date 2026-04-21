#pragma once

#include <cstddef>
#include <string>

namespace nlp3::tts {

struct TtsConfig {
    bool enabled = true;
    std::size_t max_queue_size = 0;
    std::size_t max_dispatch_per_tick = 1;
    std::size_t max_text_length = 160;
    bool drop_oldest_on_overflow = false;
    std::size_t backend_queue_size = 0;
    std::string selected_voice_id = "spanish-neutral";
    std::string selected_language = "es";
    std::string frequency = "normal";
};

} // namespace nlp3::tts
