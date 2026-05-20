#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "platform/panel_app.hpp"
#include "platform/panel_config.hpp"
#include "test_support.hpp"

namespace {

#ifdef _WIN32

bool ensure_http_test_winsock_initialized() {
    static const bool initialized = []() {
        WSADATA wsa_data{};
        return WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0;
    }();

    return initialized;
}

class HttpTestClient {
public:
    ~HttpTestClient() {
        close();
    }

    bool connect_tcp(std::uint16_t port) {
        if (!ensure_http_test_winsock_initialized()) {
            return false;
        }

        close();
        socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket_ == INVALID_SOCKET) {
            return false;
        }

        const DWORD timeout_ms = 500;
        setsockopt(
            socket_,
            SOL_SOCKET,
            SO_RCVTIMEO,
            reinterpret_cast<const char*>(&timeout_ms),
            sizeof(timeout_ms));
        setsockopt(
            socket_,
            SOL_SOCKET,
            SO_SNDTIMEO,
            reinterpret_cast<const char*>(&timeout_ms),
            sizeof(timeout_ms));

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        if (connect(
                socket_,
                reinterpret_cast<const sockaddr*>(&address),
                sizeof(address)) == SOCKET_ERROR) {
            close();
            return false;
        }

        return true;
    }

    bool send_request(const std::string& request) const {
        if (socket_ == INVALID_SOCKET) {
            return false;
        }

        return send(socket_, request.data(), static_cast<int>(request.size()), 0)
            == static_cast<int>(request.size());
    }

    std::string receive_response(nlp3::platform::PanelApp& panel_app) const {
        std::string response;
        std::array<char, 4096> buffer{};

        for (int attempt = 0; attempt < 100; ++attempt) {
            panel_app.tick(static_cast<std::uint64_t>(attempt * 10));
            const auto received = recv(socket_, buffer.data(), static_cast<int>(buffer.size()), 0);
            if (received > 0) {
                response.append(buffer.data(), static_cast<std::size_t>(received));
                continue;
            }

            if (received == 0) {
                break;
            }

            const auto error_code = WSAGetLastError();
            if (error_code != WSAEWOULDBLOCK && error_code != WSAETIMEDOUT) {
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        return response;
    }

    void close() {
        if (socket_ != INVALID_SOCKET) {
            closesocket(socket_);
            socket_ = INVALID_SOCKET;
        }
    }

private:
    SOCKET socket_ = INVALID_SOCKET;
};

bool require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "require failed: " << message << '\n';
        return false;
    }
    return true;
}

std::string make_get_request(const std::string& path) {
    return "GET " + path + " HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Connection: close\r\n\r\n";
}

std::string make_post_request(
    const std::string& path,
    const std::string& body,
    const std::string& content_type = "application/json; charset=utf-8",
    const std::string& extra_headers = "") {
    return "POST " + path + " HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: " + content_type + "\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        + extra_headers
        + "Connection: close\r\n\r\n"
        + body;
}

std::string issue_request(
    nlp3::platform::PanelApp& app,
    std::uint16_t port,
    const std::string& request) {
    HttpTestClient client;
    if (!client.connect_tcp(port)) {
        return {};
    }
    if (!client.send_request(request)) {
        return {};
    }
    return client.receive_response(app);
}

std::string extract_json_string_field(const std::string& text, const std::string& key) {
    const auto needle = "\"" + key + "\":\"";
    const auto start = text.find(needle);
    if (start == std::string::npos) {
        return {};
    }

    std::string value;
    for (std::size_t index = start + needle.size(); index < text.size(); ++index) {
        const auto ch = text[index];
        if (ch == '\\') {
            if (index + 1 >= text.size()) {
                return {};
            }

            const auto escaped = text[++index];
            switch (escaped) {
            case '\\':
                value.push_back('\\');
                break;
            case '"':
                value.push_back('"');
                break;
            case 'n':
                value.push_back('\n');
                break;
            case 'r':
                value.push_back('\r');
                break;
            case 't':
                value.push_back('\t');
                break;
            default:
                value.push_back(escaped);
                break;
            }
            continue;
        }

        if (ch == '"') {
            return value;
        }
        value.push_back(ch);
    }

    return {};
}

#endif

} // namespace

int main() {
#ifndef _WIN32
    return 0;
#else
    nlp3::platform::PanelConfig config{};
    config.bridge_mode = "external";
    config.default_game_id = "event-counter";
    config.external_ws_port = 8765;

    const auto config_path = nlp3::testsupport::write_temp_panel_config(
        "nlp3_panel_http_ui_test_config.json",
        config);

    nlp3::platform::PanelApp panel_app;
    if (!require(panel_app.initialize(config_path.string()), "panel initialize")) {
        return 1;
    }

    constexpr std::uint16_t kPort = 18881;
    if (!require(panel_app.start_http_ui(kPort), "http ui start")) {
        return 1;
    }

    const auto html = issue_request(panel_app, kPort, make_get_request("/"));
    if (!require(html.find("HTTP/1.1 200 OK") != std::string::npos, "html 200")) return 1;
    if (!require(html.find("Nisoje Studio") != std::string::npos, "html titlebar brand")) return 1;
    if (!require(html.find("Conexi") != std::string::npos, "html connection section")) return 1;
    if (!require(html.find("Asistente de voz") != std::string::npos, "html voice section")) return 1;
    if (!require(html.find("Asistente de inicio") == std::string::npos, "html startup assistant removed")) return 1;
    if (!require(html.find("Actividad del live") != std::string::npos, "html activity monitor")) return 1;
    if (!require(html.find("M&eacute;tricas") != std::string::npos, "html metrics panel")) return 1;
    if (!require(html.find("Reiniciar conteo") != std::string::npos, "html metrics reset action")) return 1;
    if (!require(html.find("Juego interactivo") != std::string::npos, "html game section")) return 1;
    if (!require(html.find("Mensajes autom") != std::string::npos, "html voice notices title")) return 1;
    if (!require(html.find("Leer mensajes del chat") != std::string::npos, "html voice chat toggle")) return 1;
    if (!require(html.find("window-minimize-button") != std::string::npos, "html minimize button")) return 1;
    if (!require(html.find("titlebar-clock-text") != std::string::npos, "html titlebar clock")) return 1;
    if (!require(html.find("connection-status-meta") != std::string::npos, "html titlebar connection meta")) return 1;
    if (!require(html.find("activity-gift-menu") != std::string::npos, "html gift picker")) return 1;
    if (!require(html.find("voice-notices-list") != std::string::npos, "html voice notices list")) return 1;
    if (!require(html.find("voice-save-button") != std::string::npos, "html voice save button")) return 1;
    if (!require(html.find("voice-read-likes") != std::string::npos, "html voice likes bridge")) return 1;
    if (!require(html.find("auth-form-body") != std::string::npos, "html auth form body")) return 1;
    if (!require(html.find("auth-field-wide") != std::string::npos, "html auth wide field")) return 1;
    if (!require(html.find("auth-meta-grid") != std::string::npos, "html auth meta grid")) return 1;
    if (!require(html.find("auth-brand-copy-group") != std::string::npos, "html auth brand copy group")) return 1;
    if (!require(html.find("/game-previews.js") != std::string::npos, "html game previews asset")) return 1;

    const auto css = issue_request(panel_app, kPort, make_get_request("/app.css"));
    if (!require(css.find("HTTP/1.1 200 OK") != std::string::npos, "css 200")) return 1;
    if (!require(css.find("--bg: #09111d;") != std::string::npos, "css dark theme")) return 1;
    if (!require(css.find(".hero-card") != std::string::npos, "css hero card")) return 1;
    if (!require(css.find(".game-showcase-card") != std::string::npos, "css game card")) return 1;
    if (!require(css.find(".app-titlebar") != std::string::npos, "css custom titlebar")) return 1;
    if (!require(css.find(".titlebar-metric") != std::string::npos, "css compact titlebar metric")) return 1;
    if (!require(css.find(".activity-monitor-log") != std::string::npos, "css activity monitor log")) return 1;
    if (!require(css.find(".gift-picker") != std::string::npos, "css gift picker")) return 1;
    if (!require(css.find(".notice-accordion") != std::string::npos, "css collapsible notices")) return 1;
    if (!require(css.find(".auth-form-body") != std::string::npos, "css auth form body")) return 1;
    if (!require(css.find("--app-viewport-height") != std::string::npos, "css auth viewport variable")) return 1;
    if (!require(css.find("html.auth-locked") != std::string::npos, "css html auth lock")) return 1;
    if (!require(css.find("position: sticky;") != std::string::npos, "css sticky action areas")) return 1;
    if (!require(css.find("body::-webkit-scrollbar") != std::string::npos, "css body scrollbar")) return 1;
    if (!require(css.find(".metric-danger") != std::string::npos, "css latency tone")) return 1;

    const auto js = issue_request(panel_app, kPort, make_get_request("/app.js"));
    if (!require(js.find("HTTP/1.1 200 OK") != std::string::npos, "js 200")) return 1;
    if (!require(js.find("POLL_REALTIME_MS = 250") != std::string::npos, "js realtime poll")) return 1;
    if (!require(js.find("/api/system/reconnect") != std::string::npos, "js reconnect endpoint")) return 1;
    if (!require(js.find("/api/support/export") != std::string::npos, "js support export endpoint")) return 1;
    if (!require(js.find("sendWindowAction(\"minimize\")") != std::string::npos, "js window minimize bridge")) return 1;
    if (!require(js.find("sendWindowAction(\"toggle-maximize\")") != std::string::npos, "js window maximize bridge")) return 1;
    if (!require(js.find("notice-accordion") != std::string::npos, "js collapsible notices")) return 1;
    if (!require(js.find("activity-gift-menu") != std::string::npos, "js activity gift menu")) return 1;
    if (!require(js.find("[\"gift\", \"share\", \"follow\", \"like\", \"timer\"]") != std::string::npos, "js default notice tabs")) return 1;
    if (!require(js.find("updateTitlebarClock") != std::string::npos, "js titlebar clock")) return 1;
    if (!require(js.find("likeThanksEnabled") != std::string::npos, "js like voice support")) return 1;
    if (!require(js.find("AUTH_FORM_STORAGE_KEY") != std::string::npos, "js auth draft storage key")) return 1;
    if (!require(js.find("AUTH_FORM_LEGACY_STORAGE_KEYS") != std::string::npos, "js auth draft legacy storage")) return 1;
    if (!require(js.find("syncAuthDraft") != std::string::npos, "js auth draft sync")) return 1;
    if (!require(js.find("syncViewportMetrics") != std::string::npos, "js auth viewport sync")) return 1;
    if (!require(js.find("authMetaGrid") != std::string::npos, "js auth meta grid visibility")) return 1;
    if (!require(js.find("document.documentElement.classList.toggle(\"auth-locked\", locked)") != std::string::npos, "js html auth lock")) return 1;
    if (!require(js.find("Conecta tu cuenta para empezar el live.") != std::string::npos, "js simple ui logs")) return 1;
    if (!require(js.find("/api/realtime") != std::string::npos, "js realtime combined endpoint")) return 1;
    if (!require(js.find("POLL_REALTIME_HIDDEN_MS = 1500") != std::string::npos, "js hidden realtime poll")) return 1;
    if (!require(js.find("document.addEventListener(\"visibilitychange\"") != std::string::npos, "js visibility polling hook")) return 1;
    if (!require(js.find("restartPollingLoops(false)") != std::string::npos, "js adaptive polling start")) return 1;
    if (!require(js.find("function setHtml") != std::string::npos, "js html render cache helper")) return 1;
    if (!require(js.find("element.innerHTML === html") != std::string::npos, "js html cache checks dom")) return 1;
    if (!require(js.find("recentActivityMarkup") != std::string::npos, "js activity render cache")) return 1;
    if (!require(js.find("gamesMarkup") != std::string::npos, "js games render cache")) return 1;
    if (!require(js.find("renderVoiceSaveState") != std::string::npos, "js voice save state")) return 1;
    if (!require(js.find("Escuchando") != std::string::npos, "js latency listening state")) return 1;

    const auto game_previews = issue_request(panel_app, kPort, make_get_request("/game-previews.js"));
    if (!require(game_previews.find("HTTP/1.1 200 OK") != std::string::npos, "game previews 200")) return 1;
    if (!require(game_previews.find("window.__GAME_PREVIEW_IMAGES__") != std::string::npos, "game previews object")) return 1;
    if (!require(game_previews.find("\"arena_live\"") != std::string::npos, "game previews arena image")) return 1;
    if (!require(game_previews.find("\"conquista\"") != std::string::npos, "game previews conquista image")) return 1;
    if (!require(game_previews.find("\"super_chat\"") != std::string::npos, "game previews super chat image")) return 1;
    if (!require(game_previews.find("\"push_esferas\"") != std::string::npos, "game previews push esferas image")) return 1;

    const auto raw_chat = nlp3::testsupport::make_chat_event(
        "user-1",
        "alice",
        "Alice",
        "evt-chat-1",
        "room-local",
        "hello panel",
        1000);
    const auto raw_gift = nlp3::testsupport::make_gift_event(
        "user-2",
        "bob",
        "Bob",
        "evt-gift-1",
        "room-local",
        "gift-rose",
        "Rose",
        3,
        150,
        1010);
    if (!require(panel_app.submit_external_bridge_event(raw_chat), "submit raw chat")) return 1;
    if (!require(panel_app.submit_external_bridge_event(raw_gift), "submit raw gift")) return 1;
    panel_app.tick(1500);

    const auto state_json = issue_request(panel_app, kPort, make_get_request("/api/state"));
    if (!require(state_json.find("\"panelName\":\"Nisoje Studio\"") != std::string::npos, "state panel name")) return 1;
    if (!require(state_json.find("\"catalog\"") != std::string::npos, "state catalog")) return 1;
    if (!require(state_json.find("\"gameDetail\"") != std::string::npos, "state game detail")) return 1;
    if (!require(state_json.find("\"host\"") != std::string::npos, "state host")) return 1;
    if (!require(state_json.find("\"externalBridge\"") != std::string::npos, "state external bridge")) return 1;
    if (!require(state_json.find("\"runtimeChecked\":") != std::string::npos, "state runtime checked")) return 1;
    if (!require(state_json.find("\"runtimeSummary\":") != std::string::npos, "state runtime summary")) return 1;

    const auto events_json = issue_request(panel_app, kPort, make_get_request("/api/events"));
    if (!require(events_json.find("\"items\"") != std::string::npos, "events items")) return 1;
    if (!require(events_json.find("chat_message") != std::string::npos, "events chat item")) return 1;

    const auto metrics_json = issue_request(panel_app, kPort, make_get_request("/api/metrics"));
    if (!require(metrics_json.find("\"eventsPerMinute\"") != std::string::npos, "metrics epm")) return 1;
    if (!require(metrics_json.find("\"hostSession\"") != std::string::npos, "metrics host session")) return 1;
    if (!require(metrics_json.find("\"chatMessages\":1") != std::string::npos, "metrics chat count before reset")) return 1;
    if (!require(metrics_json.find("\"gifts\":1") != std::string::npos, "metrics gift count before reset")) return 1;

    const auto realtime_json = issue_request(panel_app, kPort, make_get_request("/api/realtime"));
    if (!require(realtime_json.find("\"metrics\":{") != std::string::npos, "realtime metrics section")) return 1;
    if (!require(realtime_json.find("\"events\":{") != std::string::npos, "realtime events section")) return 1;
    if (!require(realtime_json.find("\"chatMessages\":1") != std::string::npos, "realtime chat count")) return 1;
    if (!require(realtime_json.find("chat_message") != std::string::npos, "realtime chat item")) return 1;

    const auto metrics_reset = issue_request(
        panel_app,
        kPort,
        make_post_request("/api/metrics/reset", "{}"));
    if (!require(metrics_reset.find("metrics_reset") != std::string::npos, "metrics reset ok")) return 1;

    const auto metrics_after_reset = issue_request(panel_app, kPort, make_get_request("/api/metrics"));
    if (!require(metrics_after_reset.find("\"chatMessages\":0") != std::string::npos, "metrics chat count reset")) return 1;
    if (!require(metrics_after_reset.find("\"gifts\":0") != std::string::npos, "metrics gift count reset")) return 1;
    if (!require(panel_app.snapshot().total_events == 0, "metrics reset cleared host totals")) return 1;

    const auto cross_origin_metrics_reset = issue_request(
        panel_app,
        kPort,
        make_post_request(
            "/api/metrics/reset",
            "{}",
            "application/json; charset=utf-8",
            "Origin: https://example.invalid\r\n"));
    if (!require(
            cross_origin_metrics_reset.find("HTTP/1.1 403 Forbidden") != std::string::npos,
            "cross-origin metrics reset blocked")) {
        return 1;
    }
    if (!require(
            cross_origin_metrics_reset.find("\"errorCode\":\"origin_not_allowed\"") != std::string::npos,
            "cross-origin metrics reset origin error")) {
        return 1;
    }

    const auto same_origin_metrics_reset = issue_request(
        panel_app,
        kPort,
        make_post_request(
            "/api/metrics/reset",
            "{}",
            "application/json; charset=utf-8",
            "Origin: http://127.0.0.1:" + std::to_string(kPort) + "\r\n"));
    if (!require(same_origin_metrics_reset.find("metrics_reset") != std::string::npos, "same-origin metrics reset allowed")) return 1;

    const auto tts_before = issue_request(panel_app, kPort, make_get_request("/api/tts/config"));
    if (!require(tts_before.find("\"ttsBackendName\"") != std::string::npos, "tts config backend name")) return 1;
    if (!require(tts_before.find("\"voiceCatalog\"") != std::string::npos, "tts config voice catalog")) return 1;
    if (!require(tts_before.find("\"chatFilterMode\"") != std::string::npos, "tts config chat filter")) return 1;

    const auto game_start = issue_request(
        panel_app,
        kPort,
        make_post_request("/api/game/start", "{\"gameId\":\"event-counter\"}"));
    if (!require(game_start.find("\"ok\":true") != std::string::npos, "game start ok")) return 1;

    const auto game_pause = issue_request(
        panel_app,
        kPort,
        make_post_request("/api/game/pause", "{}"));
    if (!require(game_pause.find("game_paused") != std::string::npos, "game pause ok")) return 1;

    const auto game_resume = issue_request(
        panel_app,
        kPort,
        make_post_request("/api/game/start", "{\"gameId\":\"event-counter\"}"));
    if (!require(game_resume.find("game_resumed") != std::string::npos, "game resume ok")) return 1;

    const auto state_before_trigger = panel_app.snapshot().total_events;
    const auto game_trigger = issue_request(
        panel_app,
        kPort,
        make_post_request(
            "/api/game/trigger",
            "{\"kind\":\"chat\",\"actorName\":\"ui-tester\",\"message\":\"triggered from http\",\"magnitude\":2}"));
    if (!require(game_trigger.find("event_injected") != std::string::npos, "game trigger ok")) return 1;
    panel_app.tick(1600);
    if (!require(panel_app.snapshot().total_events > state_before_trigger, "trigger changed total events")) return 1;

    const auto host_tts = issue_request(
        panel_app,
        kPort,
        make_post_request(
            "/api/host/tts",
            "{\"energyLevel\":\"hype\",\"toneStyle\":\"electric\",\"giftThanksEnabled\":true,"
            "\"followThanksEnabled\":true,\"periodicEnabled\":true,\"periodicIntervalMs\":1500,"
            "\"message\":\"Hola desde la UI\"}"));
    if (!require(host_tts.find("\"ok\":true") != std::string::npos, "host tts ok")) return 1;
    if (!require(host_tts.find("\"spoke\":true") != std::string::npos, "host tts spoke")) return 1;

    const auto tts_config_update = issue_request(
        panel_app,
        kPort,
        make_post_request(
            "/api/tts/config",
            "{\"ttsEnabled\":true,"
            "\"voiceId\":\"english-female\","
            "\"voiceLanguage\":\"en\","
            "\"voiceFrequency\":\"high\","
            "\"allowChatMessages\":true,"
            "\"chatFilterMode\":\"subscribers_only\","
            "\"chatMessageTemplate\":\"Hello {user}: {message}\","
            "\"giftThanksEnabled\":true,"
            "\"followThanksEnabled\":true,"
            "\"likeThanksEnabled\":true,"
            "\"subscriberThanksEnabled\":true,"
            "\"shareThanksEnabled\":true,"
            "\"likeThanksTemplate\":\"Thanks {user} for {count} likes\","
            "\"subscriberThanksTemplate\":\"Thanks {user} for subscribing\","
            "\"periodicEnabled\":true,"
            "\"periodicIntervalMs\":30000,"
            "\"periodicMessages\":[\"Remember to follow\"]}"));
    if (!require(tts_config_update.find("\"ok\":true") != std::string::npos, "tts config update ok")) return 1;

    const auto tts_after = issue_request(panel_app, kPort, make_get_request("/api/tts/config"));
    if (!require(tts_after.find("\"voiceId\":\"english-female\"") != std::string::npos, "tts voice updated")) return 1;
    if (!require(tts_after.find("\"voiceLanguage\":\"en\"") != std::string::npos, "tts language updated")) return 1;
    if (!require(tts_after.find("\"voiceFrequency\":\"high\"") != std::string::npos, "tts frequency updated")) return 1;
    if (!require(tts_after.find("\"chatFilterMode\":\"subscribers_only\"") != std::string::npos, "tts chat filter updated")) return 1;
    if (!require(tts_after.find("\"chatMessageTemplate\":\"Hello {user}: {message}\"") != std::string::npos, "tts chat template updated")) return 1;
    if (!require(tts_after.find("\"likeThanksEnabled\":true") != std::string::npos, "tts like enabled updated")) return 1;
    if (!require(tts_after.find("\"likeThanksTemplate\":\"Thanks {user} for {count} likes\"") != std::string::npos, "tts like template updated")) return 1;
    if (!require(tts_after.find("\"subscriberThanksEnabled\":true") != std::string::npos, "tts subscriber enabled updated")) return 1;
    if (!require(tts_after.find("\"subscriberThanksTemplate\":\"Thanks {user} for subscribing\"") != std::string::npos, "tts subscriber template updated")) return 1;

    const auto tts_test = issue_request(
        panel_app,
        kPort,
        make_post_request("/api/tts/test", "{\"message\":\"Voice endpoint check\"}"));
    if (!require(tts_test.find("\"ok\":true") != std::string::npos, "tts test ok")) return 1;
    if (!require(tts_test.find("tts_announcement_enqueued") != std::string::npos, "tts test enqueued")) return 1;

    const auto state_after_host = issue_request(panel_app, kPort, make_get_request("/api/state"));
    if (!require(state_after_host.find("\"energyLevel\":\"hype\"") != std::string::npos, "host energy updated")) return 1;
    if (!require(state_after_host.find("\"toneStyle\":\"electric\"") != std::string::npos, "host tone updated")) return 1;
    if (!require(state_after_host.find("\"queuedMessages\":") != std::string::npos, "tts queue visible")) return 1;

    const auto reconnect = issue_request(
        panel_app,
        kPort,
        make_post_request("/api/system/reconnect", "{}"));
    if (!require(reconnect.find("\"message\":") != std::string::npos, "system reconnect response")) return 1;

    for (int index = 0; index < 48; ++index) {
        const auto event = nlp3::testsupport::make_chat_event(
            "burst-user",
            "burst",
            "Burst",
            "evt-burst-" + std::to_string(index),
            "room-local",
            "burst message",
            1700 + index);
        if (!require(panel_app.submit_external_bridge_event(event), "burst submit")) return 1;
    }
    panel_app.tick(2200);

    const auto burst_events = issue_request(panel_app, kPort, make_get_request("/api/events"));
    if (!require(burst_events.find("\"total\":") != std::string::npos, "burst events total")) return 1;
    if (!require(burst_events.find("burst message") != std::string::npos, "burst event text")) return 1;

    panel_app.stop_http_ui();
    if (!require(!panel_app.http_ui_status().running, "http ui stopped")) return 1;

    nlp3::platform::PanelConfig locked_config{};
    locked_config.bridge_mode = "external";
    locked_config.default_game_id = "event-counter";
    locked_config.external_ws_port = 8766;
    locked_config.auth.required = true;

    const auto locked_config_path = nlp3::testsupport::write_temp_panel_config(
        "nlp3_panel_http_ui_test_locked_config.json",
        locked_config);

    nlp3::platform::PanelApp locked_panel_app;
    if (!require(locked_panel_app.initialize(locked_config_path.string()), "locked panel initialize")) {
        return 1;
    }

    constexpr std::uint16_t kLockedPort = 18882;
    if (!require(locked_panel_app.start_http_ui(kLockedPort), "locked http ui start")) {
        return 1;
    }

    const auto locked_state = issue_request(locked_panel_app, kLockedPort, make_get_request("/api/state"));
    if (!require(locked_state.find("\"auth\":{") != std::string::npos, "locked state auth section")) return 1;
    if (!require(locked_state.find("\"required\":true") != std::string::npos, "locked state auth required")) return 1;
    if (!require(locked_state.find("\"authenticated\":false") != std::string::npos, "locked state auth locked")) return 1;

    const auto blocked_reconnect = issue_request(
        locked_panel_app,
        kLockedPort,
        make_post_request("/api/system/reconnect", "{}"));
    if (!require(blocked_reconnect.find("HTTP/1.1 403 Forbidden") != std::string::npos, "blocked reconnect status")) {
        return 1;
    }
    if (!require(blocked_reconnect.find("\"errorCode\":\"auth_required\"") != std::string::npos, "blocked reconnect auth required")) {
        return 1;
    }

    const auto missing_login = issue_request(
        locked_panel_app,
        kLockedPort,
        make_post_request("/api/auth/login", "{}"));
    if (!require(missing_login.find("HTTP/1.1 200 OK") != std::string::npos, "missing login status")) return 1;
    if (!require(missing_login.find("\"ok\":false") != std::string::npos, "missing login failed")) return 1;
    if (!require(missing_login.find("\"errorCode\":\"missing_fields\"") != std::string::npos, "missing login code")) {
        return 1;
    }

    const auto locked_chat = nlp3::testsupport::make_chat_event(
        "locked-user",
        "locked",
        "Locked",
        "evt-locked-chat-1",
        "room-locked",
        "logout should clear me",
        3000);
    if (!require(locked_panel_app.submit_external_bridge_event(locked_chat), "locked submit raw chat")) return 1;
    locked_panel_app.tick(3200);

    const auto locked_events_before_logout = issue_request(locked_panel_app, kLockedPort, make_get_request("/api/events"));
    if (!require(
            locked_events_before_logout.find("logout should clear me") != std::string::npos,
            "locked events before logout")) {
        return 1;
    }

    const auto logged_out = issue_request(
        locked_panel_app,
        kLockedPort,
        make_post_request("/api/auth/logout", "{}"));
    if (!require(logged_out.find("\"message\":\"auth_logged_out\"") != std::string::npos, "logout endpoint ok")) {
        return 1;
    }

    const auto locked_events_after_logout = issue_request(locked_panel_app, kLockedPort, make_get_request("/api/events"));
    if (!require(locked_events_after_logout.find("\"total\":0") != std::string::npos, "logout cleared recent events")) return 1;

    const auto locked_metrics_after_logout = issue_request(locked_panel_app, kLockedPort, make_get_request("/api/metrics"));
    if (!require(locked_metrics_after_logout.find("\"chatMessages\":0") != std::string::npos, "logout cleared chat metrics")) return 1;

    const auto support_export = issue_request(
        locked_panel_app,
        kLockedPort,
        make_post_request("/api/support/export", "{\"reason\":\"locked_auth_test\"}"));
    if (!require(support_export.find("HTTP/1.1 200 OK") != std::string::npos, "support export status")) return 1;
    if (!require(support_export.find("\"message\":\"support_bundle_exported\"") != std::string::npos, "support export message")) {
        return 1;
    }
    const auto support_path = extract_json_string_field(support_export, "path");
    if (!require(!support_path.empty(), "support export path present")) return 1;
    if (!require(std::filesystem::exists(std::filesystem::path(support_path)), "support export file exists")) return 1;

    locked_panel_app.stop_http_ui();
    if (!require(!locked_panel_app.http_ui_status().running, "locked http ui stopped")) return 1;

    std::filesystem::remove(config_path);
    std::filesystem::remove(locked_config_path);
    return 0;
#endif
}
