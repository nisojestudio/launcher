#include "tts/tts_template_formatter.hpp"

#include <cctype>

namespace nlp3::tts {

namespace {

std::string trim_copy(std::string_view value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) {
        return {};
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(begin, end - begin + 1));
}

void append_placeholder_value(
    std::string& output,
    std::string_view placeholder,
    const TtsTemplateContext& context) {
    if (placeholder == "user") {
        output += context.user;
    } else if (placeholder == "message") {
        output += context.message;
    } else if (placeholder == "gift") {
        output += context.gift;
    } else if (placeholder == "count") {
        output += std::to_string(context.count);
    } else if (placeholder == "viewers") {
        output += std::to_string(context.viewers);
    }
}

} // namespace

std::string format_tts_template(
    std::string_view template_text,
    const TtsTemplateContext& context) {
    std::string output{};
    output.reserve(template_text.size() + 16);

    for (std::size_t index = 0; index < template_text.size(); ++index) {
        if (template_text[index] != '{') {
            output.push_back(template_text[index]);
            continue;
        }

        const auto end = template_text.find('}', index + 1);
        if (end == std::string_view::npos) {
            output.push_back(template_text[index]);
            continue;
        }

        append_placeholder_value(output, template_text.substr(index + 1, end - index - 1), context);
        index = end;
    }

    std::string compact{};
    compact.reserve(output.size());
    bool previous_space = false;
    for (const auto ch : output) {
        const auto is_space = std::isspace(static_cast<unsigned char>(ch)) != 0;
        if (is_space) {
            if (!previous_space) {
                compact.push_back(' ');
            }
        } else {
            compact.push_back(ch);
        }
        previous_space = is_space;
    }

    return trim_copy(compact);
}

} // namespace nlp3::tts
