#pragma once

#include <string>
#include <string_view>

namespace nlp3::tts {

struct TtsTemplateContext {
    std::string user{};
    std::string message{};
    std::string gift{};
    int count = 0;
    int viewers = 0;
};

std::string format_tts_template(
    std::string_view template_text,
    const TtsTemplateContext& context);

} // namespace nlp3::tts
