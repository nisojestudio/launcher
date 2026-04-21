#include "platform/panel_config_storage.hpp"

#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "tts/voice_catalog.hpp"

namespace nlp3::platform {

namespace {

using ordered_json = nlohmann::ordered_json;

std::string trim_copy(std::string_view value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) {
        return {};
    }

    const auto end = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(begin, end - begin + 1));
}

std::string normalize_bridge_mode(std::string_view bridge_mode) {
    return bridge_mode == "external" ? "external" : "stub";
}

void normalize_bridge_settings(PanelConfig& config) {
    config.bridge_mode = normalize_bridge_mode(config.bridge_mode);

    const bool external_mode = config.bridge_mode == "external";
    config.bridge.stub_mode = !external_mode;

    auto source_name = trim_copy(config.bridge.source_name);
    if (external_mode) {
        if (source_name.empty() || source_name == "tiktok" || source_name == "tiktok-stub") {
            source_name = "tiktok-external";
        }
    } else {
        if (source_name.empty() || source_name == "tiktok" || source_name == "tiktok-external") {
            source_name = "tiktok-stub";
        }
    }

    config.bridge.source_name = std::move(source_name);
}

bool try_get_object_field(
    const ordered_json& parent,
    std::string_view key,
    const ordered_json*& out_object) {
    const auto it = parent.find(std::string(key));
    if (it == parent.end() || it->is_null()) {
        out_object = nullptr;
        return true;
    }

    if (!it->is_object()) {
        return false;
    }

    out_object = &(*it);
    return true;
}

template <typename T>
bool try_read_unsigned(
    const ordered_json& parent,
    std::string_view key,
    T& out_value) {
    const auto it = parent.find(std::string(key));
    if (it == parent.end() || it->is_null()) {
        return true;
    }

    std::uint64_t parsed = 0;
    if (it->is_number_unsigned()) {
        parsed = it->get<std::uint64_t>();
    } else if (it->is_number_integer()) {
        const auto signed_value = it->get<std::int64_t>();
        if (signed_value < 0) {
            return false;
        }
        parsed = static_cast<std::uint64_t>(signed_value);
    } else {
        return false;
    }

    if (parsed > std::numeric_limits<T>::max()) {
        return false;
    }

    out_value = static_cast<T>(parsed);
    return true;
}

bool try_read_bool(
    const ordered_json& parent,
    std::string_view key,
    bool& out_value) {
    const auto it = parent.find(std::string(key));
    if (it == parent.end() || it->is_null()) {
        return true;
    }

    if (!it->is_boolean()) {
        return false;
    }

    out_value = it->get<bool>();
    return true;
}

bool try_read_string(
    const ordered_json& parent,
    std::string_view key,
    std::string& out_value) {
    const auto it = parent.find(std::string(key));
    if (it == parent.end() || it->is_null()) {
        return true;
    }

    if (!it->is_string()) {
        return false;
    }

    out_value = it->get<std::string>();
    return true;
}

bool try_read_string_array(
    const ordered_json& parent,
    std::string_view key,
    std::vector<std::string>& out_values) {
    const auto it = parent.find(std::string(key));
    if (it == parent.end() || it->is_null()) {
        return true;
    }

    if (!it->is_array()) {
        return false;
    }

    std::vector<std::string> parsed_values{};
    parsed_values.reserve(it->size());
    for (const auto& item : *it) {
        if (!item.is_string()) {
            return false;
        }
        parsed_values.push_back(item.get<std::string>());
    }

    out_values = std::move(parsed_values);
    return true;
}

ordered_json bridge_to_json(const bridge::TikTokBridgeConfig& config) {
    ordered_json bridge = ordered_json::object();
    bridge["enabled"] = config.enabled;
    bridge["stub_mode"] = config.stub_mode;
    bridge["emit_chat_events"] = config.emit_chat_events;
    bridge["emit_like_events"] = config.emit_like_events;
    bridge["emit_gift_events"] = config.emit_gift_events;
    bridge["emit_follow_events"] = config.emit_follow_events;
    bridge["emit_share_events"] = config.emit_share_events;
    bridge["emit_viewer_join_events"] = config.emit_viewer_join_events;
    bridge["emit_viewer_count_events"] = config.emit_viewer_count_events;
    bridge["emit_live_start_events"] = config.emit_live_start_events;
    bridge["emit_live_end_events"] = config.emit_live_end_events;
    bridge["emit_moderation_events"] = config.emit_moderation_events;
    bridge["emit_custom_raw_events"] = config.emit_custom_raw_events;
    bridge["passthrough_avatar_url"] = config.passthrough_avatar_url;
    bridge["source_name"] = config.source_name;
    return bridge;
}

ordered_json tts_runtime_to_json(const tts::TtsConfig& config) {
    ordered_json runtime = ordered_json::object();
    runtime["enabled"] = config.enabled;
    runtime["max_queue_size"] = config.max_queue_size;
    runtime["backend_queue_size"] = config.backend_queue_size;
    runtime["max_dispatch_per_tick"] = config.max_dispatch_per_tick;
    runtime["max_text_length"] = config.max_text_length;
    runtime["drop_oldest_on_overflow"] = config.drop_oldest_on_overflow;
    runtime["selected_voice_id"] = config.selected_voice_id;
    runtime["selected_language"] = config.selected_language;
    runtime["frequency"] = config.frequency;
    return runtime;
}

ordered_json tts_policy_to_json(const tts::TtsPolicy& config) {
    ordered_json policy = ordered_json::object();
    policy["allow_chat_messages"] = config.allow_chat_messages;
    policy["allow_scheduled_messages"] = config.allow_scheduled_messages;
    policy["allow_manual_messages"] = config.allow_manual_messages;
    policy["include_actor_name_for_chat"] = config.include_actor_name_for_chat;
    policy["min_text_length"] = config.min_text_length;
    policy["chat_filter_mode"] = std::string(nlp3::tts::to_string(config.chat_filter_mode));
    policy["chat_cooldown_ms"] = config.chat_cooldown_ms;
    policy["chat_message_template"] = config.chat_message_template;
    return policy;
}

ordered_json automation_to_json(const host::HostAutomationConfig& config) {
    ordered_json automation = ordered_json::object();
    automation["enable_gift_thanks_tts"] = config.enable_gift_thanks_tts;
    automation["enable_follow_thanks_tts"] = config.enable_follow_thanks_tts;
    automation["enable_like_thanks_tts"] = config.enable_like_thanks_tts;
    automation["enable_subscriber_thanks_tts"] = config.enable_subscriber_thanks_tts;
    automation["enable_share_thanks_tts"] = config.enable_share_thanks_tts;
    automation["gift_thanks_cooldown_ms"] = config.gift_thanks_cooldown_ms;
    automation["follow_thanks_cooldown_ms"] = config.follow_thanks_cooldown_ms;
    automation["like_thanks_cooldown_ms"] = config.like_thanks_cooldown_ms;
    automation["subscriber_thanks_cooldown_ms"] = config.subscriber_thanks_cooldown_ms;
    automation["share_thanks_cooldown_ms"] = config.share_thanks_cooldown_ms;
    automation["gift_thanks_template"] = config.gift_thanks_template;
    automation["follow_thanks_template"] = config.follow_thanks_template;
    automation["like_thanks_template"] = config.like_thanks_template;
    automation["subscriber_thanks_template"] = config.subscriber_thanks_template;
    automation["share_thanks_template"] = config.share_thanks_template;
    return automation;
}

ordered_json periodic_tts_to_json(const host::HostPeriodicTtsConfig& config) {
    ordered_json periodic = ordered_json::object();
    periodic["enabled"] = config.enabled;
    periodic["interval_ms"] = config.interval_ms;
    periodic["messages"] = config.messages;
    return periodic;
}

ordered_json auth_to_json(const PanelAuthConfig& config) {
    ordered_json auth = ordered_json::object();
    auth["required"] = config.required;
    auth["firebase_api_key"] = config.firebase_api_key;
    auth["firebase_project_id"] = config.firebase_project_id;
    auth["firebase_auth_domain"] = config.firebase_auth_domain;
    auth["nisoje_api_base"] = config.nisoje_api_base;
    auth["me_licenses_path"] = config.me_licenses_path;
    auth["me_games_catalog_path"] = config.me_games_catalog_path;
    return auth;
}

bool load_bridge_config_object(
    const ordered_json& object,
    bridge::TikTokBridgeConfig& config) {
    return try_read_bool(object, "enabled", config.enabled)
        && try_read_bool(object, "stub_mode", config.stub_mode)
        && try_read_bool(object, "emit_chat_events", config.emit_chat_events)
        && try_read_bool(object, "emit_like_events", config.emit_like_events)
        && try_read_bool(object, "emit_gift_events", config.emit_gift_events)
        && try_read_bool(object, "emit_follow_events", config.emit_follow_events)
        && try_read_bool(object, "emit_share_events", config.emit_share_events)
        && try_read_bool(object, "emit_viewer_join_events", config.emit_viewer_join_events)
        && try_read_bool(object, "emit_viewer_count_events", config.emit_viewer_count_events)
        && try_read_bool(object, "emit_live_start_events", config.emit_live_start_events)
        && try_read_bool(object, "emit_live_end_events", config.emit_live_end_events)
        && try_read_bool(object, "emit_moderation_events", config.emit_moderation_events)
        && try_read_bool(object, "emit_custom_raw_events", config.emit_custom_raw_events)
        && try_read_bool(object, "passthrough_avatar_url", config.passthrough_avatar_url)
        && try_read_string(object, "source_name", config.source_name);
}

bool load_tts_runtime_object(
    const ordered_json& object,
    tts::TtsConfig& config) {
    return try_read_bool(object, "enabled", config.enabled)
        && try_read_unsigned(object, "max_queue_size", config.max_queue_size)
        && try_read_unsigned(object, "backend_queue_size", config.backend_queue_size)
        && try_read_unsigned(object, "max_dispatch_per_tick", config.max_dispatch_per_tick)
        && try_read_unsigned(object, "max_text_length", config.max_text_length)
        && try_read_bool(object, "drop_oldest_on_overflow", config.drop_oldest_on_overflow)
        && try_read_string(object, "selected_voice_id", config.selected_voice_id)
        && try_read_string(object, "selected_language", config.selected_language)
        && try_read_string(object, "frequency", config.frequency);
}

bool load_tts_policy_object(
    const ordered_json& object,
    tts::TtsPolicy& config) {
    std::string chat_filter_mode{};
    const bool parsed = try_read_bool(object, "allow_chat_messages", config.allow_chat_messages)
        && try_read_bool(object, "allow_scheduled_messages", config.allow_scheduled_messages)
        && try_read_bool(object, "allow_manual_messages", config.allow_manual_messages)
        && try_read_bool(object, "include_actor_name_for_chat", config.include_actor_name_for_chat)
        && try_read_unsigned(object, "min_text_length", config.min_text_length)
        && try_read_string(object, "chat_filter_mode", chat_filter_mode)
        && try_read_unsigned(object, "chat_cooldown_ms", config.chat_cooldown_ms)
        && try_read_string(object, "chat_message_template", config.chat_message_template);
    if (!parsed) {
        return false;
    }

    if (!chat_filter_mode.empty()) {
        config.chat_filter_mode = nlp3::tts::parse_tts_chat_filter_mode(chat_filter_mode);
    }
    return true;
}

bool load_automation_object(
    const ordered_json& object,
    host::HostAutomationConfig& config) {
    return try_read_bool(object, "enable_gift_thanks_tts", config.enable_gift_thanks_tts)
        && try_read_bool(object, "enable_follow_thanks_tts", config.enable_follow_thanks_tts)
        && try_read_bool(object, "enable_like_thanks_tts", config.enable_like_thanks_tts)
        && try_read_bool(object, "enable_subscriber_thanks_tts", config.enable_subscriber_thanks_tts)
        && try_read_bool(object, "enable_share_thanks_tts", config.enable_share_thanks_tts)
        && try_read_unsigned(object, "gift_thanks_cooldown_ms", config.gift_thanks_cooldown_ms)
        && try_read_unsigned(object, "follow_thanks_cooldown_ms", config.follow_thanks_cooldown_ms)
        && try_read_unsigned(object, "like_thanks_cooldown_ms", config.like_thanks_cooldown_ms)
        && try_read_unsigned(object, "subscriber_thanks_cooldown_ms", config.subscriber_thanks_cooldown_ms)
        && try_read_unsigned(object, "share_thanks_cooldown_ms", config.share_thanks_cooldown_ms)
        && try_read_string(object, "gift_thanks_template", config.gift_thanks_template)
        && try_read_string(object, "follow_thanks_template", config.follow_thanks_template)
        && try_read_string(object, "like_thanks_template", config.like_thanks_template)
        && try_read_string(object, "subscriber_thanks_template", config.subscriber_thanks_template)
        && try_read_string(object, "share_thanks_template", config.share_thanks_template);
}

bool load_periodic_tts_object(
    const ordered_json& object,
    host::HostPeriodicTtsConfig& config) {
    return try_read_bool(object, "enabled", config.enabled)
        && try_read_unsigned(object, "interval_ms", config.interval_ms)
        && try_read_string_array(object, "messages", config.messages);
}

bool load_auth_object(
    const ordered_json& object,
    PanelAuthConfig& config) {
    return try_read_bool(object, "required", config.required)
        && try_read_string(object, "firebase_api_key", config.firebase_api_key)
        && try_read_string(object, "firebase_project_id", config.firebase_project_id)
        && try_read_string(object, "firebase_auth_domain", config.firebase_auth_domain)
        && try_read_string(object, "nisoje_api_base", config.nisoje_api_base)
        && try_read_string(object, "me_licenses_path", config.me_licenses_path)
        && try_read_string(object, "me_games_catalog_path", config.me_games_catalog_path);
}

} // namespace

bool PanelConfigStorage::save_to_file(const PanelConfig& input_config, const std::string& path) const {
    try {
        const auto normalized_bridge_mode_value = normalize_bridge_mode(input_config.bridge_mode);
        auto normalized_bridge = input_config.bridge;
        normalized_bridge.stub_mode = normalized_bridge_mode_value != "external";

        auto source_name = trim_copy(normalized_bridge.source_name);
        if (normalized_bridge_mode_value == "external") {
            if (source_name.empty() || source_name == "tiktok" || source_name == "tiktok-stub") {
                source_name = "tiktok-external";
            }
        } else {
            if (source_name.empty() || source_name == "tiktok" || source_name == "tiktok-external") {
                source_name = "tiktok-stub";
            }
        }
        normalized_bridge.source_name = std::move(source_name);

        const auto file_path = std::filesystem::path(path);
        if (file_path.has_parent_path()) {
            std::error_code error_code;
            std::filesystem::create_directories(file_path.parent_path(), error_code);
            if (error_code) {
                return false;
            }
        }

        ordered_json root = ordered_json::object();
        root["panel_name"] = input_config.panel_name;
        root["default_game_id"] = input_config.default_game_id;
        root["bridge_mode"] = normalized_bridge_mode_value;
        root["external_target_user"] = input_config.external_target_user;
        root["external_ws_port"] = input_config.external_ws_port;
        root["embedded_ui_enabled"] = input_config.embedded_ui_enabled;
        root["embedded_ui_fallback_to_browser"] = input_config.embedded_ui_fallback_to_browser;
        root["embedded_ui_devtools"] = input_config.embedded_ui_devtools;
        root["embedded_ui_url"] = input_config.embedded_ui_url;
        root["embedded_ui_startup_timeout_ms"] = input_config.embedded_ui_startup_timeout_ms;
        root["host_energy_level"] = input_config.host_energy_level;
        root["host_tone_style"] = input_config.host_tone_style;
        root["auth"] = auth_to_json(input_config.auth);
        root["bridge"] = bridge_to_json(normalized_bridge);
        root["tts_runtime"] = tts_runtime_to_json(input_config.tts_runtime);
        root["tts"] = tts_policy_to_json(input_config.tts);
        root["automation"] = automation_to_json(input_config.automation);
        root["periodic_tts"] = periodic_tts_to_json(input_config.periodic_tts);

        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) {
            return false;
        }

        output << root.dump(2) << "\n";
        return output.good();
    } catch (const std::exception&) {
        return false;
    } catch (...) {
        return false;
    }
}

bool PanelConfigStorage::load_from_file(const std::string& path, PanelConfig& out_config) const {
    try {
        if (!std::filesystem::exists(path)) {
            return false;
        }

        std::ifstream input(path, std::ios::binary);
        if (!input) {
            return false;
        }

        std::ostringstream buffer;
        buffer << input.rdbuf();
        if (!input.good() && !input.eof()) {
            return false;
        }

        const auto parsed = ordered_json::parse(buffer.str(), nullptr, false);
        if (parsed.is_discarded() || !parsed.is_object()) {
            return false;
        }

        PanelConfig config{};
        if (!try_read_string(parsed, "panel_name", config.panel_name)
            || !try_read_string(parsed, "default_game_id", config.default_game_id)
            || !try_read_string(parsed, "bridge_mode", config.bridge_mode)
            || !try_read_string(parsed, "external_target_user", config.external_target_user)
            || !try_read_unsigned(parsed, "external_ws_port", config.external_ws_port)
            || !try_read_bool(parsed, "embedded_ui_enabled", config.embedded_ui_enabled)
            || !try_read_bool(parsed, "embedded_ui_fallback_to_browser", config.embedded_ui_fallback_to_browser)
            || !try_read_bool(parsed, "embedded_ui_devtools", config.embedded_ui_devtools)
            || !try_read_string(parsed, "embedded_ui_url", config.embedded_ui_url)
            || !try_read_unsigned(parsed, "embedded_ui_startup_timeout_ms", config.embedded_ui_startup_timeout_ms)
            || !try_read_string(parsed, "host_energy_level", config.host_energy_level)
            || !try_read_string(parsed, "host_tone_style", config.host_tone_style)) {
            return false;
        }

        const ordered_json* bridge = nullptr;
        const ordered_json* tts_runtime = nullptr;
        const ordered_json* tts_policy = nullptr;
        const ordered_json* automation = nullptr;
        const ordered_json* periodic_tts = nullptr;
        const ordered_json* auth = nullptr;
        if (!try_get_object_field(parsed, "bridge", bridge)
            || !try_get_object_field(parsed, "auth", auth)
            || !try_get_object_field(parsed, "tts_runtime", tts_runtime)
            || !try_get_object_field(parsed, "tts", tts_policy)
            || !try_get_object_field(parsed, "automation", automation)
            || !try_get_object_field(parsed, "periodic_tts", periodic_tts)) {
            return false;
        }

        if ((auth != nullptr && !load_auth_object(*auth, config.auth))
            || (bridge != nullptr && !load_bridge_config_object(*bridge, config.bridge))
            || (tts_runtime != nullptr && !load_tts_runtime_object(*tts_runtime, config.tts_runtime))
            || (tts_policy != nullptr && !load_tts_policy_object(*tts_policy, config.tts))
            || (automation != nullptr && !load_automation_object(*automation, config.automation))
            || (periodic_tts != nullptr && !load_periodic_tts_object(*periodic_tts, config.periodic_tts))) {
            return false;
        }

        normalize_bridge_settings(config);
        out_config = std::move(config);
        return true;
    } catch (const std::exception&) {
        return false;
    } catch (...) {
        return false;
    }
}

} // namespace nlp3::platform
