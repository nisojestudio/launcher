#include "platform/overlay_assets.hpp"

#include <sstream>

namespace {

static constexpr const char* kLiveTimerOverlayHtml =
#include "overlay_live_timer_html.inc"
;

} // namespace

namespace nlp3::platform {

std::string_view panel_overlay_live_timer_html() noexcept {
    return kLiveTimerOverlayHtml;
}

std::string build_live_timer_state_json(const games::LiveTimerGame* game) {
    if (game == nullptr) {
        return "{"
            "\"remainingSeconds\":0,"
            "\"format\":\"00:00:00\","
            "\"running\":false,"
            "\"paused\":false,"
            "\"enabled\":true,"
            "\"completed\":false,"
            "\"title\":\"\","
            "\"subtitle\":\"\","
            "\"titleStyle\":{},"
            "\"counterStyle\":{},"
            "\"subtitleStyle\":{},"
            "\"bgColor\":\"#000000\","
            "\"recentEvents\":[]"
            "}";
    }

    const auto& s = game->state();
    auto rem = game->remaining_seconds();

    auto json_quote = [](std::string_view v) -> std::string {
        std::string r;
        r.reserve(v.size() + 8);
        for (auto c : v) {
            if (c == '\\') r += "\\\\";
            else if (c == '"') r += "\\\"";
            else if (c == '\n') r += "\\n";
            else if (c == '\r') r += "\\r";
            else if (c == '\t') r += "\\t";
            else r += c;
        }
        return "\"" + r + "\"";
    };

    auto style_json = [&](const games::LiveTimerVisualStyle& st) -> std::string {
        std::ostringstream oss;
        oss << "{"
            << "\"font_size_px\":" << st.font_size_px << ","
            << "\"font_color\":" << json_quote(st.font_color) << ","
            << "\"font_family\":" << json_quote(st.font_family) << ","
            << "\"bold\":" << (st.bold ? "true" : "false")
            << "}";
        return oss.str();
    };

    auto substitute_placeholders = [&](std::string_view tmpl) -> std::string {
        std::string r(tmpl);
        auto rep_num = [&](std::string_view ph, double val) {
            std::string vs = std::to_string(val);
            auto p = r.find(ph);
            while (p != std::string::npos) {
                r.replace(p, ph.size(), vs);
                p = r.find(ph, p + vs.size());
            }
        };
        auto rep_str = [&](std::string_view ph, const std::string& val) {
            auto p = r.find(ph);
            while (p != std::string::npos) {
                r.replace(p, ph.size(), val);
                p = r.find(ph, p + val.size());
            }
        };
        rep_num("{time_per_like}", s.time_per_like);
        rep_num("{time_per_share}", s.time_per_share);
        rep_num("{time_per_follow}", s.time_per_follow);
        rep_num("{time_per_gift_coin}", s.time_per_gift_coin);
        rep_num("{time_per_chat}", s.time_per_chat);
        rep_num("{initial_time}", s.initial_seconds);
        rep_str("{title}", s.title_text);
        return r;
    };

    std::ostringstream events_json;
    events_json << "[";
    bool first = true;
    for (const auto& ev : s.recent_events) {
        if (!first) events_json << ",";
        first = false;
        events_json << "{"
            << "\"icon\":" << json_quote(ev.icon) << ","
            << "\"label\":" << json_quote(ev.label) << ","
            << "\"delta\":" << ev.delta_seconds << ","
            << "\"isAddition\":" << (ev.is_addition ? "true" : "false")
            << "}";
    }
    events_json << "]";

    std::ostringstream oss;
    oss << "{"
        << "\"remainingSeconds\":" << rem << ","
        << "\"format\":" << json_quote(game->format_time()) << ","
        << "\"running\":" << (s.running ? "true" : "false") << ","
        << "\"paused\":" << (s.paused ? "true" : "false") << ","
        << "\"enabled\":" << (game->is_enabled() ? "true" : "false") << ","
        << "\"completed\":" << (s.completed ? "true" : "false") << ","
        << "\"title\":" << json_quote(s.title_text) << ","
        << "\"subtitle\":" << json_quote(substitute_placeholders(s.subtitle_text)) << ","
        << "\"titleStyle\":" << style_json(s.title_style) << ","
        << "\"counterStyle\":" << style_json(s.counter_style) << ","
        << "\"subtitleStyle\":" << style_json(s.subtitle_style) << ","
        << "\"bgColor\":" << json_quote(s.background_color) << ","
        << "\"recentEvents\":" << events_json.str()
        << "}";
    return oss.str();
}

} // namespace nlp3::platform
