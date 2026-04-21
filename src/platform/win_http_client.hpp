#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace nlp3::platform {

struct HttpHeader {
    std::string name{};
    std::string value{};
};

struct HttpResponse {
    int status_code = 0;
    std::string body{};
    std::string error{};
    std::uint64_t content_length = 0;
};

using HttpDownloadProgressCallback = std::function<void(std::uint64_t bytes_downloaded, std::uint64_t total_bytes)>;

HttpResponse http_request(
    std::string_view method,
    std::string_view url,
    std::string_view body = {},
    std::string_view content_type = {},
    const std::vector<HttpHeader>& headers = {});

HttpResponse download_to_file(
    std::string_view url,
    const std::filesystem::path& destination_path,
    const std::vector<HttpHeader>& headers = {},
    HttpDownloadProgressCallback progress_callback = {});

} // namespace nlp3::platform
