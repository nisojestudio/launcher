#include "tts/real_tts_backend.hpp"

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <sapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cctype>
#include <string_view>
#include <utility>

namespace nlp3::tts {

namespace {

using Microsoft::WRL::ComPtr;

std::string narrow_from_wide(std::wstring_view value) {
    if (value.empty()) {
        return {};
    }

    const auto required = WideCharToMultiByte(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (required <= 0) {
        return {};
    }

    std::string output(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        output.data(),
        required,
        nullptr,
        nullptr);
    return output;
}

std::wstring widen_from_utf8(std::string_view value) {
    if (value.empty()) {
        return {};
    }

    const auto required = MultiByteToWideChar(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0);
    if (required <= 0) {
        return {};
    }

    std::wstring output(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        output.data(),
        required);
    return output;
}

class ScopedCoInit {
public:
    ScopedCoInit() noexcept
        : hr_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {
    }

    ~ScopedCoInit() {
        if (hr_ == S_OK || hr_ == S_FALSE) {
            CoUninitialize();
        }
    }

    bool ok() const noexcept {
        return hr_ == S_OK || hr_ == S_FALSE;
    }

private:
    HRESULT hr_ = E_FAIL;
};

std::string lowercase_copy(std::string_view value) {
    std::string lowered(value);
    std::transform(
        lowered.begin(),
        lowered.end(),
        lowered.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return lowered;
}

std::string normalize_language_from_attr(std::string_view value, std::string_view fallback_description) {
    if (!value.empty()) {
        try {
            std::size_t consumed = 0;
            const auto lcid = std::stoul(std::string(value), &consumed, 16);
            if (consumed > 0) {
                wchar_t locale_name[LOCALE_NAME_MAX_LENGTH]{};
                if (LCIDToLocaleName(static_cast<LCID>(lcid), locale_name, LOCALE_NAME_MAX_LENGTH, 0) > 0) {
                    const auto narrowed = lowercase_copy(narrow_from_wide(locale_name));
                    if (narrowed.rfind("es", 0) == 0) {
                        return "es";
                    }
                    if (narrowed.rfind("en", 0) == 0) {
                        return "en";
                    }
                }
            }
        } catch (...) {
        }
    }

    const auto lowered = lowercase_copy(fallback_description);
    if (lowered.find("spanish") != std::string::npos || lowered.find("es-") != std::string::npos) {
        return "es";
    }
    return "en";
}

std::string normalize_gender(std::string_view value, std::string_view fallback_description) {
    auto lowered = lowercase_copy(value);
    if (lowered.find("female") != std::string::npos) {
        return "female";
    }
    if (lowered.find("male") != std::string::npos) {
        return "male";
    }

    lowered = lowercase_copy(fallback_description);
    if (lowered.find("zira") != std::string::npos
        || lowered.find("hazel") != std::string::npos
        || lowered.find("helena") != std::string::npos
        || lowered.find("sabina") != std::string::npos
        || lowered.find("maya") != std::string::npos
        || lowered.find("serena") != std::string::npos
        || lowered.find("sarah") != std::string::npos) {
        return "female";
    }
    if (lowered.find("david") != std::string::npos
        || lowered.find("mark") != std::string::npos
        || lowered.find("george") != std::string::npos
        || lowered.find("pablo") != std::string::npos
        || lowered.find("alejandro") != std::string::npos
        || lowered.find("jorge") != std::string::npos
        || lowered.find("alex") != std::string::npos
        || lowered.find("silas") != std::string::npos) {
        return "male";
    }
    if (lowered.find("female") != std::string::npos) {
        return "female";
    }
    if (lowered.find("male") != std::string::npos) {
        return "male";
    }
    return "neutral";
}

std::string get_token_string(ISpObjectToken* token, LPCWSTR key_name) {
    if (token == nullptr) {
        return {};
    }

    LPWSTR raw_value = nullptr;
    if (FAILED(token->GetStringValue(key_name, &raw_value)) || raw_value == nullptr) {
        return {};
    }

    const std::wstring wide_value(raw_value);
    ::CoTaskMemFree(raw_value);
    return narrow_from_wide(wide_value);
}

ComPtr<ISpObjectTokenCategory> create_voice_category() {
    ComPtr<ISpObjectTokenCategory> category{};
    if (FAILED(CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&category)))
        || category == nullptr
        || FAILED(category->SetId(SPCAT_VOICES, FALSE))) {
        return {};
    }
    return category;
}

std::vector<TtsVoiceDescriptor> enumerate_installed_voices() {
    ScopedCoInit init{};
    if (!init.ok()) {
        return {};
    }

    const auto category = create_voice_category();
    if (category == nullptr) {
        return {};
    }

    ComPtr<IEnumSpObjectTokens> token_enum{};
    ULONG count = 0;
    if (FAILED(category->EnumTokens(nullptr, nullptr, &token_enum))
        || token_enum == nullptr
        || FAILED(token_enum->GetCount(&count))) {
        return {};
    }

    std::vector<TtsVoiceDescriptor> voices{};
    voices.reserve(count);

    for (ULONG index = 0; index < count; ++index) {
        ComPtr<ISpObjectToken> token{};
        if (FAILED(token_enum->Next(1, token.GetAddressOf(), nullptr)) || token == nullptr) {
            continue;
        }

        LPWSTR token_id = nullptr;
        const auto id_hr = token->GetId(&token_id);
        const auto token_id_text =
            (SUCCEEDED(id_hr) && token_id != nullptr) ? narrow_from_wide(token_id) : std::string{};
        const auto description_text = [&]() {
            auto value = get_token_string(token.Get(), L"Name");
            if (!value.empty()) {
                return value;
            }
            value = get_token_string(token.Get(), nullptr);
            return value;
        }();
        const auto language = normalize_language_from_attr(
            get_token_string(token.Get(), L"Language"),
            description_text);
        const auto gender = normalize_gender(
            get_token_string(token.Get(), L"Gender"),
            description_text);

        if (!token_id_text.empty()) {
            voices.push_back(TtsVoiceDescriptor{
                token_id_text,
                !description_text.empty() ? description_text : token_id_text,
                language,
                gender,
                true,
                token_id_text,
            });
        }

        if (token_id != nullptr) {
            ::CoTaskMemFree(token_id);
        }
    }

    return voices;
}

std::vector<TtsVoiceDescriptor> build_runtime_voice_catalog() {
    auto catalog = build_curated_voice_catalog();
    const auto installed = enumerate_installed_voices();

    auto assign_profile = [&](std::string_view profile_id, std::string_view language, std::string_view gender) {
        auto it = std::find_if(
            installed.begin(),
            installed.end(),
            [&](const TtsVoiceDescriptor& voice) {
                return voice.language == language
                    && (gender == "neutral" || voice.gender == gender);
            });
        if (it == installed.end() && gender == "neutral") {
            it = std::find_if(
                installed.begin(),
                installed.end(),
                [&](const TtsVoiceDescriptor& voice) {
                    return voice.language == language;
                });
        }
        if (it == installed.end()) {
            return;
        }

        const auto profile_it = std::find_if(
            catalog.begin(),
            catalog.end(),
            [&](const TtsVoiceDescriptor& voice) { return voice.id == profile_id; });
        if (profile_it == catalog.end()) {
            return;
        }

        profile_it->available = true;
        profile_it->backend_voice_id = it->backend_voice_id;
        if (!it->display_name.empty()) {
            profile_it->display_name = profile_it->display_name + " - " + it->display_name;
        }
    };

    assign_profile("spanish-female", "es", "female");
    assign_profile("spanish-male", "es", "male");
    assign_profile("spanish-neutral", "es", "neutral");
    assign_profile("english-female", "en", "female");
    assign_profile("english-male", "en", "male");

    return catalog;
}

std::string resolve_backend_voice_id(
    const TtsConfig& config,
    const std::vector<TtsVoiceDescriptor>& catalog) {
    const auto selected_it = std::find_if(
        catalog.begin(),
        catalog.end(),
        [&](const TtsVoiceDescriptor& voice) {
            return voice.id == config.selected_voice_id
                && voice.available
                && !voice.backend_voice_id.empty();
        });
    if (selected_it != catalog.end()) {
        return selected_it->backend_voice_id;
    }

    const auto language_it = std::find_if(
        catalog.begin(),
        catalog.end(),
        [&](const TtsVoiceDescriptor& voice) {
            return voice.language == config.selected_language
                && voice.available
                && !voice.backend_voice_id.empty();
        });
    if (language_it != catalog.end()) {
        return language_it->backend_voice_id;
    }

    const auto any_it = std::find_if(
        catalog.begin(),
        catalog.end(),
        [](const TtsVoiceDescriptor& voice) {
            return voice.available && !voice.backend_voice_id.empty();
        });
    return any_it != catalog.end() ? any_it->backend_voice_id : std::string{};
}

int sapi_rate_for_frequency(std::string_view frequency) {
    if (frequency == "low") {
        return -2;
    }
    if (frequency == "high") {
        return 2;
    }
    return 0;
}

ComPtr<ISpObjectToken> create_token_from_id(std::string_view token_id) {
    const auto token_id_wide = widen_from_utf8(token_id);
    if (token_id_wide.empty()) {
        return {};
    }

    ComPtr<ISpObjectToken> token{};
    if (FAILED(CoCreateInstance(CLSID_SpObjectToken, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&token)))
        || token == nullptr
        || FAILED(token->SetId(SPCAT_VOICES, token_id_wide.c_str(), FALSE))) {
        return {};
    }

    return token;
}

} // namespace

RealTtsBackend::RealTtsBackend() {
    voice_catalog_ = build_runtime_voice_catalog();
    available_.store(std::any_of(
        voice_catalog_.begin(),
        voice_catalog_.end(),
        [](const TtsVoiceDescriptor& voice) { return voice.available; }));
    worker_ = std::thread([this]() { worker_main(); });
}

RealTtsBackend::~RealTtsBackend() {
    running_.store(false);
    cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

std::string_view RealTtsBackend::backend_name() const noexcept {
    return "windows-sapi-tts-backend";
}

bool RealTtsBackend::available() const noexcept {
    return available_.load();
}

void RealTtsBackend::apply_config(const TtsConfig& config) {
    std::scoped_lock lock{mutex_};
    config_ = config;
}

std::vector<TtsVoiceDescriptor> RealTtsBackend::voice_catalog() const {
    std::scoped_lock lock{mutex_};
    return voice_catalog_;
}

bool RealTtsBackend::speak(const TtsMessage& message) {
    if (message.text.empty()) {
        return false;
    }

    std::scoped_lock lock{mutex_};
    if (!config_.enabled || !available_.load()) {
        return false;
    }

    if (config_.backend_queue_size > 0 && queue_.size() >= config_.backend_queue_size) {
        if (!config_.drop_oldest_on_overflow
            && static_cast<int>(message.priority) <= static_cast<int>(queue_.back().priority)) {
            return false;
        }
        queue_.pop_back();
    }

    const auto insert_at = std::find_if(
        queue_.begin(),
        queue_.end(),
        [&message](const TtsMessage& existing) {
            return static_cast<int>(message.priority) > static_cast<int>(existing.priority);
        });
    queue_.insert(insert_at, message);
    cv_.notify_one();
    return true;
}

std::size_t RealTtsBackend::queued_message_count() const noexcept {
    std::scoped_lock lock{mutex_};
    return queue_.size();
}

void RealTtsBackend::clear_pending() noexcept {
    std::scoped_lock lock{mutex_};
    queue_.clear();
    cv_.notify_all();
}

void RealTtsBackend::worker_main() {
    ScopedCoInit init{};
    if (!init.ok()) {
        available_.store(false);
        return;
    }

    ComPtr<ISpVoice> voice{};
    if (FAILED(::CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&voice))) || voice == nullptr) {
        available_.store(false);
        return;
    }

    while (running_.load()) {
        TtsMessage next{};
        TtsConfig local_config{};
        std::string backend_voice_id{};

        {
            std::unique_lock lock{mutex_};
            cv_.wait(lock, [this]() { return !running_.load() || !queue_.empty(); });
            if (!running_.load() && queue_.empty()) {
                break;
            }
            next = std::move(queue_.front());
            queue_.pop_front();
            local_config = config_;
            backend_voice_id = resolve_backend_voice_id(local_config, voice_catalog_);
        }

        voice->SetRate(sapi_rate_for_frequency(local_config.frequency));

        if (!backend_voice_id.empty()) {
            const auto token = create_token_from_id(backend_voice_id);
            if (token != nullptr) {
                voice->SetVoice(token.Get());
            }
        }

        const auto text = widen_from_utf8(next.text);
        if (!text.empty()) {
            voice->Speak(text.c_str(), SPF_IS_NOT_XML, nullptr);
        }
    }
}

} // namespace nlp3::tts

#else

namespace nlp3::tts {

RealTtsBackend::RealTtsBackend() = default;
RealTtsBackend::~RealTtsBackend() = default;
std::string_view RealTtsBackend::backend_name() const noexcept { return "real-tts-unavailable"; }
bool RealTtsBackend::available() const noexcept { return false; }
void RealTtsBackend::apply_config(const TtsConfig&) {}
std::vector<TtsVoiceDescriptor> RealTtsBackend::voice_catalog() const { return build_curated_voice_catalog(); }
bool RealTtsBackend::speak(const TtsMessage&) { return false; }
std::size_t RealTtsBackend::queued_message_count() const noexcept { return 0; }
void RealTtsBackend::clear_pending() noexcept {}
void RealTtsBackend::worker_main() {}

} // namespace nlp3::tts

#endif
