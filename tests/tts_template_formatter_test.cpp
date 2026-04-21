#include <cassert>
#include <string>

#include "tts/tts_template_formatter.hpp"

int main() {
    nlp3::tts::TtsTemplateContext context{};
    context.user = "Alice";
    context.message = "Hola chat";
    context.gift = "Rose";
    context.count = 3;
    context.viewers = 42;

    const auto formatted = nlp3::tts::format_tts_template(
        "Gracias {user} por {gift} x{count}. {message}. Viewers {viewers}",
        context);
    assert(formatted == "Gracias Alice por Rose x3. Hola chat. Viewers 42");

    const auto compacted = nlp3::tts::format_tts_template(
        "  {user}    dijo   {message}  ",
        context);
    assert(compacted == "Alice dijo Hola chat");

    const auto unknown_placeholder = nlp3::tts::format_tts_template(
        "Hola {unknown} {user}",
        context);
    assert(unknown_placeholder == "Hola Alice");

    const auto empty_template = nlp3::tts::format_tts_template("", context);
    assert(empty_template.empty());

    return 0;
}
