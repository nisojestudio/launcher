#include "bridge/tiktok_external_event_codec.hpp"

#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace nlp3::bridge {

namespace {

enum class JsonValueKind {
    null_value,
    string_value,
    number_value,
    bool_value,
    object_value,
};

struct JsonValue {
    JsonValueKind kind = JsonValueKind::null_value;
    std::string string_value{};
    std::int64_t number_value = 0;
    bool bool_value = false;
    std::unordered_map<std::string, JsonValue> object_value{};
};

class JsonParser {
public:
    explicit JsonParser(std::string_view input) noexcept
        : input_(input) {
    }

    std::optional<JsonValue> parse() {
        skip_whitespace();
        auto value = parse_value();
        if (!value.has_value()) {
            return std::nullopt;
        }

        skip_whitespace();
        if (position_ != input_.size()) {
            return std::nullopt;
        }

        return value;
    }

private:
    void skip_whitespace() {
        while (position_ < input_.size()
            && std::isspace(static_cast<unsigned char>(input_[position_])) != 0) {
            ++position_;
        }
    }

    std::optional<JsonValue> parse_value() {
        skip_whitespace();
        if (position_ >= input_.size()) {
            return std::nullopt;
        }

        switch (input_[position_]) {
        case '{':
            return parse_object();
        case '"':
            return parse_string_value();
        case 'n':
            return parse_null();
        case 't':
        case 'f':
            return parse_bool_value();
        default:
            break;
        }

        if (input_[position_] == '-' || std::isdigit(static_cast<unsigned char>(input_[position_])) != 0) {
            return parse_number_value();
        }

        return std::nullopt;
    }

    std::optional<JsonValue> parse_object() {
        if (!consume('{')) {
            return std::nullopt;
        }

        JsonValue object{};
        object.kind = JsonValueKind::object_value;

        skip_whitespace();
        if (consume('}')) {
            return object;
        }

        while (true) {
            auto key = parse_string();
            if (!key.has_value()) {
                return std::nullopt;
            }

            skip_whitespace();
            if (!consume(':')) {
                return std::nullopt;
            }

            auto value = parse_value();
            if (!value.has_value()) {
                return std::nullopt;
            }

            object.object_value.emplace(std::move(*key), std::move(*value));

            skip_whitespace();
            if (consume('}')) {
                return object;
            }

            if (!consume(',')) {
                return std::nullopt;
            }
        }
    }

    std::optional<JsonValue> parse_string_value() {
        auto value = parse_string();
        if (!value.has_value()) {
            return std::nullopt;
        }

        JsonValue string_value{};
        string_value.kind = JsonValueKind::string_value;
        string_value.string_value = std::move(*value);
        return string_value;
    }

    std::optional<JsonValue> parse_number_value() {
        const auto start = position_;

        if (input_[position_] == '-') {
            ++position_;
        }

        const auto digits_start = position_;
        while (position_ < input_.size()
            && std::isdigit(static_cast<unsigned char>(input_[position_])) != 0) {
            ++position_;
        }

        if (digits_start == position_) {
            return std::nullopt;
        }

        try {
            JsonValue number_value{};
            number_value.kind = JsonValueKind::number_value;
            number_value.number_value = std::stoll(std::string(input_.substr(start, position_ - start)));
            return number_value;
        } catch (...) {
            return std::nullopt;
        }
    }

    std::optional<JsonValue> parse_null() {
        if (input_.substr(position_, 4) != "null") {
            return std::nullopt;
        }

        position_ += 4;
        return JsonValue{};
    }

    std::optional<JsonValue> parse_bool_value() {
        if (input_.substr(position_, 4) == "true") {
            position_ += 4;
            JsonValue bool_value{};
            bool_value.kind = JsonValueKind::bool_value;
            bool_value.bool_value = true;
            return bool_value;
        }

        if (input_.substr(position_, 5) == "false") {
            position_ += 5;
            JsonValue bool_value{};
            bool_value.kind = JsonValueKind::bool_value;
            bool_value.bool_value = false;
            return bool_value;
        }

        return std::nullopt;
    }

    std::optional<std::string> parse_string() {
        if (!consume('"')) {
            return std::nullopt;
        }

        std::string value{};
        while (position_ < input_.size()) {
            const auto current = input_[position_++];
            if (current == '"') {
                return value;
            }

            if (current != '\\') {
                value.push_back(current);
                continue;
            }

            if (position_ >= input_.size()) {
                return std::nullopt;
            }

            const auto escaped = input_[position_++];
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
                return std::nullopt;
            }
        }

        return std::nullopt;
    }

    bool consume(char expected) {
        skip_whitespace();
        if (position_ >= input_.size() || input_[position_] != expected) {
            return false;
        }

        ++position_;
        return true;
    }

    std::string_view input_{};
    std::size_t position_ = 0;
};

std::string escape_json_string(std::string_view value) {
    std::string escaped{};
    escaped.reserve(value.size());

    for (const auto ch : value) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }

    return escaped;
}

const JsonValue* find_field(
    const std::unordered_map<std::string, JsonValue>& object,
    std::string_view key) {
    const auto iterator = object.find(std::string(key));
    return iterator != object.end() ? &iterator->second : nullptr;
}

std::optional<std::string> as_string(const JsonValue* value) {
    if (value == nullptr || value->kind != JsonValueKind::string_value) {
        return std::nullopt;
    }

    return value->string_value;
}

std::optional<std::int64_t> as_number(const JsonValue* value) {
    if (value == nullptr || value->kind != JsonValueKind::number_value) {
        return std::nullopt;
    }

    return value->number_value;
}

std::optional<bool> as_bool(const JsonValue* value) {
    if (value == nullptr) {
        return std::nullopt;
    }
    if (value->kind == JsonValueKind::bool_value) {
        return value->bool_value;
    }
    if (value->kind == JsonValueKind::number_value) {
        return value->number_value != 0;
    }
    return std::nullopt;
}

const std::unordered_map<std::string, JsonValue>* as_object(const JsonValue* value) {
    if (value == nullptr || value->kind != JsonValueKind::object_value) {
        return nullptr;
    }

    return &value->object_value;
}

std::optional<TikTokRawEventKind> parse_kind(std::string_view kind) {
    if (kind == "chat") {
        return TikTokRawEventKind::chat;
    }
    if (kind == "like") {
        return TikTokRawEventKind::like;
    }
    if (kind == "gift") {
        return TikTokRawEventKind::gift;
    }
    if (kind == "follow") {
        return TikTokRawEventKind::follow;
    }
    if (kind == "share") {
        return TikTokRawEventKind::share;
    }
    if (kind == "viewer_join") {
        return TikTokRawEventKind::viewer_join;
    }
    if (kind == "viewer_count") {
        return TikTokRawEventKind::viewer_count;
    }
    if (kind == "live_start") {
        return TikTokRawEventKind::live_start;
    }
    if (kind == "live_end") {
        return TikTokRawEventKind::live_end;
    }
    if (kind == "moderation") {
        return TikTokRawEventKind::moderation;
    }
    if (kind == "custom_raw") {
        return TikTokRawEventKind::custom_raw;
    }

    return std::nullopt;
}

std::string encode_actor_json(const TikTokRawActor& actor) {
    return "{\"id\":\"" + escape_json_string(actor.user_id)
        + "\",\"username\":\"" + escape_json_string(actor.username)
        + "\",\"display_name\":\"" + escape_json_string(actor.display_name)
        + "\",\"avatar_url\":\"" + escape_json_string(actor.avatar_url)
        + "\",\"is_follower\":" + std::string(actor.is_follower ? "true" : "false")
        + ",\"is_subscriber\":" + std::string(actor.is_subscriber ? "true" : "false")
        + ",\"is_moderator\":" + std::string(actor.is_moderator ? "true" : "false")
        + "}";
}

std::string encode_metadata_json(const TikTokRawMetadata& metadata) {
    return "{\"event_id\":\"" + escape_json_string(metadata.event_id)
        + "\",\"room_id\":\"" + escape_json_string(metadata.room_id)
        + "\",\"source_event_type\":\"" + escape_json_string(metadata.raw_event_type)
        + "\",\"timestamp_ms\":" + std::to_string(metadata.timestamp_ms)
        + "}";
}

std::string encode_gift_json(const std::optional<TikTokRawGiftData>& gift) {
    if (!gift.has_value()) {
        return "null";
    }

    return "{\"gift_id\":\"" + escape_json_string(gift->gift_id)
        + "\",\"gift_name\":\"" + escape_json_string(gift->gift_name)
        + "\",\"quantity\":" + std::to_string(gift->repeat_count)
        + ",\"diamond_count\":" + std::to_string(gift->diamond_value)
        + "}";
}

} // namespace

std::string TikTokExternalEventCodec::encode_json(const TikTokRawEvent& event) const {
    return "{"
        "\"kind\":\"" + std::string(to_string(event.kind)) + "\","
        "\"actor\":" + encode_actor_json(event.actor) + ","
        "\"metadata\":" + encode_metadata_json(event.metadata) + ","
        "\"text\":\"" + escape_json_string(event.message) + "\","
        "\"gift\":" + encode_gift_json(event.gift) + ","
        "\"viewer_count\":" + std::to_string(event.viewer_count) + ","
        "\"like_count\":" + std::to_string(event.like_count) + ","
        "\"moderation_action\":\"" + escape_json_string(event.moderation_action) + "\","
        "\"raw_payload\":\"" + escape_json_string(event.raw_payload) + "\""
        + "}";
}

std::optional<TikTokRawEvent> TikTokExternalEventCodec::decode_json(const std::string& payload) const {
    JsonParser parser{payload};
    auto root = parser.parse();
    if (!root.has_value() || root->kind != JsonValueKind::object_value) {
        return std::nullopt;
    }

    const auto kind_string = as_string(find_field(root->object_value, "kind"));
    if (!kind_string.has_value()) {
        return std::nullopt;
    }

    const auto parsed_kind = parse_kind(*kind_string);
    if (!parsed_kind.has_value()) {
        return std::nullopt;
    }

    const auto* actor_object = as_object(find_field(root->object_value, "actor"));
    const auto* metadata_object = as_object(find_field(root->object_value, "metadata"));
    if (actor_object == nullptr || metadata_object == nullptr) {
        return std::nullopt;
    }

    TikTokRawEvent event{
        *parsed_kind,
        {},
        {},
        "",
        std::nullopt,
        0,
    };

    if (const auto actor_id = as_string(find_field(*actor_object, "id")); actor_id.has_value()) {
        event.actor.user_id = std::move(*actor_id);
    } else if (const auto actor_user_id = as_string(find_field(*actor_object, "user_id"));
        actor_user_id.has_value()) {
        event.actor.user_id = std::move(*actor_user_id);
    }

    if (const auto username = as_string(find_field(*actor_object, "username")); username.has_value()) {
        event.actor.username = std::move(*username);
    }

    if (const auto display_name = as_string(find_field(*actor_object, "display_name"));
        display_name.has_value()) {
        event.actor.display_name = std::move(*display_name);
    }

    if (const auto avatar_url = as_string(find_field(*actor_object, "avatar_url")); avatar_url.has_value()) {
        event.actor.avatar_url = std::move(*avatar_url);
    }
    if (const auto is_follower = as_bool(find_field(*actor_object, "is_follower")); is_follower.has_value()) {
        event.actor.is_follower = *is_follower;
    } else if (const auto legacy_follower = as_bool(find_field(*actor_object, "isFollower"));
        legacy_follower.has_value()) {
        event.actor.is_follower = *legacy_follower;
    }
    if (const auto is_subscriber = as_bool(find_field(*actor_object, "is_subscriber")); is_subscriber.has_value()) {
        event.actor.is_subscriber = *is_subscriber;
    } else if (const auto legacy_subscriber = as_bool(find_field(*actor_object, "isSubscriber"));
        legacy_subscriber.has_value()) {
        event.actor.is_subscriber = *legacy_subscriber;
    }
    if (const auto is_moderator = as_bool(find_field(*actor_object, "is_moderator")); is_moderator.has_value()) {
        event.actor.is_moderator = *is_moderator;
    } else if (const auto legacy_moderator = as_bool(find_field(*actor_object, "isModerator"));
        legacy_moderator.has_value()) {
        event.actor.is_moderator = *legacy_moderator;
    }

    if (const auto event_id = as_string(find_field(*metadata_object, "event_id")); event_id.has_value()) {
        event.metadata.event_id = std::move(*event_id);
    }

    if (const auto room_id = as_string(find_field(*metadata_object, "room_id")); room_id.has_value()) {
        event.metadata.room_id = std::move(*room_id);
    }

    if (const auto source_event_type = as_string(find_field(*metadata_object, "source_event_type"));
        source_event_type.has_value()) {
        event.metadata.raw_event_type = std::move(*source_event_type);
    } else if (const auto raw_event_type = as_string(find_field(*metadata_object, "raw_event_type"));
        raw_event_type.has_value()) {
        event.metadata.raw_event_type = std::move(*raw_event_type);
    } else if (const auto event_type = as_string(find_field(root->object_value, "event_type"));
        event_type.has_value()) {
        event.metadata.raw_event_type = std::move(*event_type);
    }

    if (const auto timestamp_ms = as_number(find_field(*metadata_object, "timestamp_ms")); timestamp_ms.has_value()) {
        event.metadata.timestamp_ms = *timestamp_ms;
    }

    if (const auto moderation_action = as_string(find_field(*metadata_object, "moderation_action"));
        moderation_action.has_value()) {
        event.moderation_action = std::move(*moderation_action);
    } else if (const auto moderation_action_root = as_string(find_field(root->object_value, "moderation_action"));
        moderation_action_root.has_value()) {
        event.moderation_action = std::move(*moderation_action_root);
    }

    if (const auto text = as_string(find_field(root->object_value, "text")); text.has_value()) {
        event.message = std::move(*text);
    }

    if (const auto viewer_count = as_number(find_field(root->object_value, "viewer_count")); viewer_count.has_value()) {
        event.viewer_count = static_cast<int>(*viewer_count);
    }

    if (const auto like_count = as_number(find_field(root->object_value, "like_count")); like_count.has_value()) {
        event.like_count = static_cast<int>(*like_count);
    } else if (event.kind == TikTokRawEventKind::like && event.viewer_count > 0) {
        event.like_count = event.viewer_count;
    }

    if (const auto* gift_object = as_object(find_field(root->object_value, "gift")); gift_object != nullptr) {
        TikTokRawGiftData gift{};

        if (const auto gift_id = as_string(find_field(*gift_object, "gift_id")); gift_id.has_value()) {
            gift.gift_id = std::move(*gift_id);
        }
        if (const auto gift_name = as_string(find_field(*gift_object, "gift_name")); gift_name.has_value()) {
            gift.gift_name = std::move(*gift_name);
        }
        if (const auto quantity = as_number(find_field(*gift_object, "quantity")); quantity.has_value()) {
            gift.repeat_count = static_cast<int>(*quantity);
        }
        if (const auto diamond_count = as_number(find_field(*gift_object, "diamond_count"));
            diamond_count.has_value()) {
            gift.diamond_value = static_cast<int>(*diamond_count);
        }

        event.gift = std::move(gift);
    }

    if (const auto raw_payload = as_string(find_field(root->object_value, "raw_payload")); raw_payload.has_value()) {
        event.raw_payload = std::move(*raw_payload);
    } else if (find_field(root->object_value, "raw_payload") != nullptr) {
        // Preserve passthrough data even when the nested payload is an object we do not parse field-by-field.
        event.raw_payload = payload;
    }

    return event;
}

} // namespace nlp3::bridge
