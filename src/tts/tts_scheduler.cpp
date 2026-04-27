#include "tts/tts_scheduler.hpp"

#include <chrono>
#include <cstdint>
#include <cctype>
#include <string>
#include <utility>

namespace nlp3::tts {

namespace {

std::string trim_copy(const std::string& input) {
    auto begin = input.begin();
    auto end = input.end();

    while (begin != end && std::isspace(static_cast<unsigned char>(*begin)) != 0) {
        ++begin;
    }

    while (begin != end) {
        const auto last = end - 1;
        if (std::isspace(static_cast<unsigned char>(*last)) == 0) {
            break;
        }

        end = last;
    }

    return std::string(begin, end);
}

bool suppresses_leading_space(std::uint32_t codepoint) {
    switch (codepoint) {
    case '.':
    case ',':
    case ';':
    case ':':
    case '!':
    case '?':
    case ')':
    case '/':
    case '%':
    case 0x2019:
    case 0x201D:
        return true;
    default:
        return false;
    }
}

bool decode_utf8_codepoint(const std::string& input, std::size_t& index, std::uint32_t& codepoint) {
    if (index >= input.size()) {
        return false;
    }

    const auto byte0 = static_cast<unsigned char>(input[index]);
    if (byte0 < 0x80) {
        codepoint = byte0;
        ++index;
        return true;
    }

    const auto remaining = input.size() - index;
    auto read_continuation = [&](std::size_t offset, std::uint32_t& value) -> bool {
        if (offset >= remaining) {
            return false;
        }
        const auto byte = static_cast<unsigned char>(input[index + offset]);
        if ((byte & 0xC0) != 0x80) {
            return false;
        }
        value = static_cast<std::uint32_t>(byte & 0x3F);
        return true;
    };

    if ((byte0 & 0xE0) == 0xC0) {
        std::uint32_t b1 = 0;
        if (!read_continuation(1, b1)) {
            ++index;
            return false;
        }
        codepoint = ((byte0 & 0x1F) << 6) | b1;
        if (codepoint < 0x80) {
            ++index;
            return false;
        }
        index += 2;
        return true;
    }

    if ((byte0 & 0xF0) == 0xE0) {
        std::uint32_t b1 = 0;
        std::uint32_t b2 = 0;
        if (!read_continuation(1, b1) || !read_continuation(2, b2)) {
            ++index;
            return false;
        }
        codepoint = ((byte0 & 0x0F) << 12) | (b1 << 6) | b2;
        if (codepoint < 0x800 || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
            ++index;
            return false;
        }
        index += 3;
        return true;
    }

    if ((byte0 & 0xF8) == 0xF0) {
        std::uint32_t b1 = 0;
        std::uint32_t b2 = 0;
        std::uint32_t b3 = 0;
        if (!read_continuation(1, b1) || !read_continuation(2, b2) || !read_continuation(3, b3)) {
            ++index;
            return false;
        }
        codepoint = ((byte0 & 0x07) << 18) | (b1 << 12) | (b2 << 6) | b3;
        if (codepoint < 0x10000 || codepoint > 0x10FFFF) {
            ++index;
            return false;
        }
        index += 4;
        return true;
    }

    ++index;
    return false;
}

void append_utf8_codepoint(std::string& output, std::uint32_t codepoint) {
    if (codepoint <= 0x7F) {
        output.push_back(static_cast<char>(codepoint));
        return;
    }

    if (codepoint <= 0x7FF) {
        output.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        return;
    }

    if (codepoint <= 0xFFFF) {
        output.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        return;
    }

    output.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
}

bool is_unicode_whitespace(std::uint32_t codepoint) {
    if (codepoint <= 0x7F) {
        return std::isspace(static_cast<unsigned char>(codepoint)) != 0;
    }

    switch (codepoint) {
    case 0x00A0:
    case 0x1680:
    case 0x2000:
    case 0x2001:
    case 0x2002:
    case 0x2003:
    case 0x2004:
    case 0x2005:
    case 0x2006:
    case 0x2007:
    case 0x2008:
    case 0x2009:
    case 0x200A:
    case 0x2028:
    case 0x2029:
    case 0x202F:
    case 0x205F:
    case 0x3000:
        return true;
    default:
        return false;
    }
}

bool is_allowed_tts_text_codepoint(std::uint32_t codepoint) {
    if ((codepoint >= '0' && codepoint <= '9')
        || (codepoint >= 'A' && codepoint <= 'Z')
        || (codepoint >= 'a' && codepoint <= 'z')) {
        return true;
    }

    switch (codepoint) {
    case '.':
    case ',':
    case ';':
    case ':':
    case '!':
    case '?':
    case '\'':
    case '"':
    case '-':
    case '(':
    case ')':
    case '/':
    case '@':
    case '#':
    case '&':
    case '+':
    case '%':
    case 0x00A1:
    case 0x00BF:
    case 0x2018:
    case 0x2019:
    case 0x201C:
    case 0x201D:
    case 0x2026:
        return true;
    default:
        break;
    }

    if ((codepoint >= 0x00C0 && codepoint <= 0x00D6)
        || (codepoint >= 0x00D8 && codepoint <= 0x00F6)
        || (codepoint >= 0x00F8 && codepoint <= 0x00FF)
        || (codepoint >= 0x0100 && codepoint <= 0x024F)
        || (codepoint >= 0x1E00 && codepoint <= 0x1EFF)
        || (codepoint >= 0x0300 && codepoint <= 0x036F)) {
        return true;
    }

    return false;
}

std::string sanitize_tts_text(const std::string& input) {
    std::string output{};
    output.reserve(input.size());
    bool pending_space = false;

    for (std::size_t index = 0; index < input.size();) {
        std::uint32_t codepoint = 0;
        if (!decode_utf8_codepoint(input, index, codepoint)) {
            pending_space = !output.empty();
            continue;
        }

        if (is_unicode_whitespace(codepoint)) {
            pending_space = !output.empty();
            continue;
        }

        if (is_allowed_tts_text_codepoint(codepoint)) {
            if (pending_space && !suppresses_leading_space(codepoint)) {
                output.push_back(' ');
            }
            append_utf8_codepoint(output, codepoint);
            pending_space = false;
            continue;
        }

        pending_space = !output.empty();
    }

    return trim_copy(output);
}

} // namespace

TtsScheduler::TtsScheduler(TtsConfig config, TtsPolicy policy, ITtsBackend& backend) noexcept
    : config_(config),
      policy_(policy),
      backend_(&backend) {
    backend_->apply_config(config_);
}

bool TtsScheduler::submit(TtsMessage message) {
    if (!config_.enabled || backend_ == nullptr) {
        return false;
    }

    if (!allows_message(policy_, message)) {
        return false;
    }

    if (!sanitize_message(message)) {
        return false;
    }

    return queue_.push(std::move(message), config_.max_queue_size, config_.drop_oldest_on_overflow);
}

std::size_t TtsScheduler::dispatch_pending(std::size_t max_messages) {
    if (backend_ == nullptr) {
        return 0;
    }

    const auto limit = max_messages == 0 ? config_.max_dispatch_per_tick : max_messages;
    std::size_t dispatched = 0;

    while (dispatched < limit) {
        auto next = queue_.pop();
        if (!next.has_value()) {
            break;
        }

        if (backend_->speak(*next)) {
            ++dispatched;
        }
    }

    return dispatched;
}

bool TtsScheduler::available() const noexcept {
    return backend_ != nullptr && backend_->available();
}

std::size_t TtsScheduler::queued_message_count() const noexcept {
    return queue_.size() + (backend_ != nullptr ? backend_->queued_message_count() : 0);
}

void TtsScheduler::clear_pending() noexcept {
    queue_.clear();
    if (backend_ != nullptr) {
        backend_->clear_pending();
    }
}

void TtsScheduler::set_config(TtsConfig config) {
    config_ = std::move(config);
    if (backend_ != nullptr) {
        backend_->apply_config(config_);
    }
}

void TtsScheduler::set_policy(TtsPolicy policy) noexcept {
    policy_ = std::move(policy);
}

bool TtsScheduler::sanitize_message(TtsMessage& message) const {
    message.actor_name = sanitize_tts_text(trim_copy(message.actor_name));
    message.text = sanitize_tts_text(trim_copy(message.text));
    message.content_text = sanitize_tts_text(trim_copy(message.content_text));

    if (message.content_text.empty()) {
        message.content_text = message.text;
    }

    if (message.text.empty()) {
        return false;
    }

    if (config_.max_text_length > 0 && message.text.size() > config_.max_text_length) {
        message.text.resize(config_.max_text_length);
    }

    if (message.created_at_ms == 0) {
        using namespace std::chrono;
        message.created_at_ms = static_cast<std::int64_t>(
            duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
    }

    return message.content_text.size() >= policy_.min_text_length;
}

} // namespace nlp3::tts
