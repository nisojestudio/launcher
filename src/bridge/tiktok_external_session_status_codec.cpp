#include "bridge/tiktok_external_session_status_codec.hpp"

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
    object_value,
};

struct JsonValue {
    JsonValueKind kind = JsonValueKind::null_value;
    std::string string_value{};
    std::int64_t number_value = 0;
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

} // namespace

std::string TikTokExternalSessionStatusCodec::encode_json(const TikTokExternalSessionStatus& status) const {
    return "{"
        "\"message_type\":\"session_status\","
        "\"target_user\":\"" + escape_json_string(status.target_user) + "\","
        "\"room_id\":\"" + escape_json_string(status.room_id) + "\","
        "\"connection_state\":\"" + std::string(to_string(status.connection_state)) + "\","
        "\"message\":\"" + escape_json_string(status.message) + "\","
        "\"timestamp_ms\":" + std::to_string(status.timestamp_ms)
        + "}";
}

std::optional<TikTokExternalSessionStatus> TikTokExternalSessionStatusCodec::decode_json(
    const std::string& payload) const {
    JsonParser parser{payload};
    auto root = parser.parse();
    if (!root.has_value() || root->kind != JsonValueKind::object_value) {
        return std::nullopt;
    }

    const auto message_type = as_string(find_field(root->object_value, "message_type"));
    if (!message_type.has_value() || *message_type != "session_status") {
        return std::nullopt;
    }

    TikTokExternalSessionStatus status{};
    if (const auto target_user = as_string(find_field(root->object_value, "target_user")); target_user.has_value()) {
        status.target_user = std::move(*target_user);
    }
    if (const auto room_id = as_string(find_field(root->object_value, "room_id")); room_id.has_value()) {
        status.room_id = std::move(*room_id);
    }
    const auto connection_state = as_string(find_field(root->object_value, "connection_state"));
    if (!connection_state.has_value()) {
        return std::nullopt;
    }
    const auto parsed_state = parse_external_session_connection_state(*connection_state);
    if (!parsed_state.has_value()) {
        return std::nullopt;
    }
    status.connection_state = *parsed_state;
    if (const auto message = as_string(find_field(root->object_value, "message")); message.has_value()) {
        status.message = std::move(*message);
    }
    if (const auto timestamp_ms = as_number(find_field(root->object_value, "timestamp_ms")); timestamp_ms.has_value()) {
        status.timestamp_ms = *timestamp_ms;
    }

    return status;
}

} // namespace nlp3::bridge
