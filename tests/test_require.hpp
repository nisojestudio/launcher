#pragma once

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace nlp3::testsupport {

[[noreturn]] inline void fail_check(int line, const char* expression) {
    std::array<char, 512> buffer{};
    const auto written = std::snprintf(
        buffer.data(),
        buffer.size(),
        "test failure at line %d: %s\n",
        line,
        expression != nullptr ? expression : "<null>");
    const auto length = written > 0 ? static_cast<std::size_t>(written) : std::strlen(buffer.data());
#ifdef _WIN32
    const auto handle = GetStdHandle(STD_ERROR_HANDLE);
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
        DWORD ignored = 0;
        WriteFile(handle, buffer.data(), static_cast<DWORD>(length), &ignored, nullptr);
    }
#endif
    std::fwrite(buffer.data(), 1, length, stderr);
    std::fflush(stderr);
    std::abort();
}

} // namespace nlp3::testsupport

#define NLP3_TEST_REQUIRE(EXPR) \
    do { \
        if (!(EXPR)) { \
            ::nlp3::testsupport::fail_check(__LINE__, #EXPR); \
        } \
    } while (false)
