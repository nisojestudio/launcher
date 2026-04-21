#include "bridge/tiktok_external_inbox_adapter.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "bridge/tiktok_external_event_codec.hpp"
#include "platform/panel_app.hpp"

namespace {

std::filesystem::path make_bucket_target(
    const std::filesystem::path& bucket_dir,
    const std::filesystem::path& source_path) {
    auto target_path = bucket_dir / source_path.filename();
    if (!std::filesystem::exists(target_path)) {
        return target_path;
    }

    const auto stem = source_path.stem().string();
    const auto extension = source_path.extension().string();
    for (std::size_t suffix = 1; ; ++suffix) {
        target_path = bucket_dir / (stem + "-" + std::to_string(suffix) + extension);
        if (!std::filesystem::exists(target_path)) {
            return target_path;
        }
    }
}

void move_to_bucket(
    const std::filesystem::path& source_path,
    const std::filesystem::path& bucket_dir) {
    std::error_code error;
    std::filesystem::create_directories(bucket_dir, error);
    error.clear();

    const auto target_path = make_bucket_target(bucket_dir, source_path);
    std::filesystem::rename(source_path, target_path, error);
}

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }

    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>{},
    };
}

} // namespace

namespace nlp3::bridge {

TikTokExternalInboxAdapter::TikTokExternalInboxAdapter(platform::PanelApp* app) noexcept
    : app_(app) {
}

TikTokExternalInboxResult TikTokExternalInboxAdapter::process_inbox(const std::string& inbox_dir) {
    TikTokExternalInboxResult result{};
    if (app_ == nullptr || inbox_dir.empty()) {
        return result;
    }

    namespace fs = std::filesystem;

    const fs::path inbox_path{inbox_dir};
    std::error_code error;
    if (!fs::exists(inbox_path, error) || !fs::is_directory(inbox_path, error)) {
        return result;
    }

    const fs::path processed_dir = inbox_path / "processed";
    const fs::path failed_dir = inbox_path / "failed";
    fs::create_directories(processed_dir, error);
    error.clear();
    fs::create_directories(failed_dir, error);
    error.clear();

    const TikTokExternalEventCodec codec{};
    for (const auto& entry : fs::directory_iterator(inbox_path, error)) {
        if (error) {
            break;
        }

        if (!entry.is_regular_file(error)) {
            error.clear();
            continue;
        }

        const auto path = entry.path();
        if (path.extension() != ".json") {
            continue;
        }

        ++result.files_seen;

        const auto payload = read_text_file(path);
        const auto decoded_event = codec.decode_json(payload);
        if (!decoded_event.has_value()) {
            ++result.files_failed;
            move_to_bucket(path, failed_dir);
            continue;
        }

        if (!app_->submit_external_bridge_event(*decoded_event)) {
            ++result.files_failed;
            move_to_bucket(path, failed_dir);
            continue;
        }

        ++result.files_processed;
        ++result.events_accepted;
        app_->tick(0);
        move_to_bucket(path, processed_dir);
    }

    return result;
}

} // namespace nlp3::bridge
