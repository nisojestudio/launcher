#include "platform/win_http_client.hpp"

#include <fstream>
#include <string>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

namespace {

#ifdef _WIN32

struct ParsedUrl {
    std::wstring host{};
    std::wstring path_and_query{};
    INTERNET_PORT port = 0;
    bool secure = false;
    bool valid = false;
};

std::wstring utf8_to_wide(std::string_view value) {
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

    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        wide.data(),
        required);
    return wide;
}

ParsedUrl parse_url(std::string_view raw_url) {
    ParsedUrl parsed{};
    auto wide = utf8_to_wide(raw_url);
    if (wide.empty()) {
        return parsed;
    }

    URL_COMPONENTS components{};
    std::wstring host(256, L'\0');
    std::wstring path(2048, L'\0');
    std::wstring extra(2048, L'\0');
    components.dwStructSize = sizeof(components);
    components.lpszHostName = host.data();
    components.dwHostNameLength = static_cast<DWORD>(host.size());
    components.lpszUrlPath = path.data();
    components.dwUrlPathLength = static_cast<DWORD>(path.size());
    components.lpszExtraInfo = extra.data();
    components.dwExtraInfoLength = static_cast<DWORD>(extra.size());

    if (WinHttpCrackUrl(wide.c_str(), static_cast<DWORD>(wide.size()), 0, &components) == FALSE) {
        return parsed;
    }

    host.resize(components.dwHostNameLength);
    path.resize(components.dwUrlPathLength);
    extra.resize(components.dwExtraInfoLength);

    parsed.host = std::move(host);
    parsed.path_and_query = path + extra;
    if (parsed.path_and_query.empty()) {
        parsed.path_and_query = L"/";
    }
    parsed.port = components.nPort;
    parsed.secure = components.nScheme == INTERNET_SCHEME_HTTPS;
    parsed.valid = !parsed.host.empty();
    return parsed;
}

std::wstring build_header_block(
    std::string_view content_type,
    const std::vector<nlp3::platform::HttpHeader>& headers) {
    std::wstring block{};
    if (!content_type.empty()) {
        block += L"Content-Type: " + utf8_to_wide(content_type) + L"\r\n";
    }

    for (const auto& header : headers) {
        if (header.name.empty()) {
            continue;
        }
        block += utf8_to_wide(header.name);
        block += L": ";
        block += utf8_to_wide(header.value);
        block += L"\r\n";
    }
    return block;
}

bool query_content_length(HINTERNET request, std::uint64_t& value) {
    value = 0;

    wchar_t buffer[64] = {};
    DWORD buffer_size = sizeof(buffer);
    if (WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_CONTENT_LENGTH,
            WINHTTP_HEADER_NAME_BY_INDEX,
            buffer,
            &buffer_size,
            WINHTTP_NO_HEADER_INDEX) == FALSE) {
        return false;
    }

    try {
        value = static_cast<std::uint64_t>(std::stoull(std::wstring(buffer)));
        return true;
    } catch (...) {
        return false;
    }
}

nlp3::platform::HttpResponse open_request(
    std::string_view method,
    std::string_view url,
    std::string_view body,
    std::string_view content_type,
    const std::vector<nlp3::platform::HttpHeader>& headers,
    std::function<bool(HINTERNET request, nlp3::platform::HttpResponse& response)> consume) {
    nlp3::platform::HttpResponse response{};
    const auto parsed_url = parse_url(url);
    if (!parsed_url.valid) {
        response.error = "invalid_url";
        return response;
    }

    const auto session = WinHttpOpen(
        L"NisojeStudio/1.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (session == nullptr) {
        response.error = "winhttp_open_failed";
        return response;
    }

    WinHttpSetTimeouts(session, 10000, 10000, 30000, 30000);

    const auto connection = WinHttpConnect(session, parsed_url.host.c_str(), parsed_url.port, 0);
    if (connection == nullptr) {
        response.error = "winhttp_connect_failed";
        WinHttpCloseHandle(session);
        return response;
    }

    const auto wide_method = utf8_to_wide(method);
    const auto request = WinHttpOpenRequest(
        connection,
        wide_method.c_str(),
        parsed_url.path_and_query.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        parsed_url.secure ? WINHTTP_FLAG_SECURE : 0);
    if (request == nullptr) {
        response.error = "winhttp_open_request_failed";
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return response;
    }

    if (parsed_url.secure) {
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
        WinHttpSetOption(request, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

        DWORD security_flags = SECURITY_FLAG_IGNORE_REVOCATION;
        WinHttpSetOption(request, WINHTTP_OPTION_SECURITY_FLAGS, &security_flags, sizeof(security_flags));
    }

    const auto header_block = build_header_block(content_type, headers);
    const auto send_ok = WinHttpSendRequest(
        request,
        header_block.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : header_block.c_str(),
        header_block.empty() ? 0 : static_cast<DWORD>(header_block.size()),
        body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()),
        static_cast<DWORD>(body.size()),
        static_cast<DWORD>(body.size()),
        0);
    if (send_ok == FALSE || WinHttpReceiveResponse(request, nullptr) == FALSE) {
        response.error = "winhttp_send_failed";
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return response;
    }

    DWORD status_code = 0;
    DWORD status_size = sizeof(status_code);
    if (WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status_code,
            &status_size,
            WINHTTP_NO_HEADER_INDEX) == TRUE) {
        response.status_code = static_cast<int>(status_code);
    }

    query_content_length(request, response.content_length);
    if (!consume(request, response) && response.error.empty()) {
        response.error = "winhttp_consume_failed";
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return response;
}

#endif

} // namespace

namespace nlp3::platform {

HttpResponse http_request(
    std::string_view method,
    std::string_view url,
    std::string_view body,
    std::string_view content_type,
    const std::vector<HttpHeader>& headers) {
#ifdef _WIN32
    return open_request(
        method,
        url,
        body,
        content_type,
        headers,
        [](HINTERNET request, HttpResponse& response) {
            std::string response_body;
            while (true) {
                DWORD available = 0;
                if (WinHttpQueryDataAvailable(request, &available) == FALSE) {
                    response.error = "winhttp_query_data_failed";
                    return false;
                }
                if (available == 0) {
                    break;
                }

                std::string chunk(static_cast<std::size_t>(available), '\0');
                DWORD read = 0;
                if (WinHttpReadData(request, chunk.data(), available, &read) == FALSE) {
                    response.error = "winhttp_read_failed";
                    return false;
                }
                chunk.resize(static_cast<std::size_t>(read));
                response_body += chunk;
            }

            response.body = std::move(response_body);
            return true;
        });
#else
    (void)method;
    (void)url;
    (void)body;
    (void)content_type;
    (void)headers;
    HttpResponse response{};
    response.error = "windows_only_http_runtime";
    return response;
#endif
}

HttpResponse download_to_file(
    std::string_view url,
    const std::filesystem::path& destination_path,
    const std::vector<HttpHeader>& headers,
    HttpDownloadProgressCallback progress_callback) {
#ifdef _WIN32
    return open_request(
        "GET",
        url,
        {},
        {},
        headers,
        [&](HINTERNET request, HttpResponse& response) {
            std::error_code error;
            std::filesystem::create_directories(destination_path.parent_path(), error);
            if (error) {
                response.error = "download_directory_create_failed";
                return false;
            }

            std::ofstream output(destination_path, std::ios::binary | std::ios::trunc);
            if (!output.good()) {
                response.error = "download_file_open_failed";
                return false;
            }

            std::uint64_t written = 0;
            while (true) {
                DWORD available = 0;
                if (WinHttpQueryDataAvailable(request, &available) == FALSE) {
                    response.error = "winhttp_query_data_failed";
                    return false;
                }
                if (available == 0) {
                    break;
                }

                std::string chunk(static_cast<std::size_t>(available), '\0');
                DWORD read = 0;
                if (WinHttpReadData(request, chunk.data(), available, &read) == FALSE) {
                    response.error = "winhttp_read_failed";
                    return false;
                }

                output.write(chunk.data(), static_cast<std::streamsize>(read));
                if (!output.good()) {
                    response.error = "download_file_write_failed";
                    return false;
                }

                written += static_cast<std::uint64_t>(read);
                if (progress_callback) {
                    progress_callback(written, response.content_length);
                }
            }

            output.close();
            return true;
        });
#else
    (void)url;
    (void)destination_path;
    (void)headers;
    (void)progress_callback;
    HttpResponse response{};
    response.error = "windows_only_http_runtime";
    return response;
#endif
}

} // namespace nlp3::platform
