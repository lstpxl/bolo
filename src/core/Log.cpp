#include "core/Log.h"

#include <array>
#include <cstdio>
#include <memory>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>
#include "raylib.h"

namespace bolt::log {
namespace {

constexpr std::size_t kLogBufferSize = 1024;

std::shared_ptr<spdlog::logger> gMainLogger;
std::shared_ptr<spdlog::logger> gProfileLogger;

std::string LogBasePath() {
    const char* appDir = GetApplicationDirectory();
    if (appDir != nullptr && appDir[0] != '\0') {
        return std::string(appDir);
    }
    return ".";
}

void InitMainLogger(const std::string& basePath) {
    const std::string path = basePath + "/bolt.log";
    gMainLogger = spdlog::basic_logger_mt("bolt", path, /*truncate=*/true);
    gMainLogger->set_level(spdlog::level::debug);
    gMainLogger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
}

void InitProfileLogger(const std::string& basePath) {
    const std::string path = basePath + "/profile.log";
    gProfileLogger = spdlog::basic_logger_mt("profile", path, /*truncate=*/true);
    gProfileLogger->set_level(spdlog::level::info);
    gProfileLogger->set_pattern("%v");
}

}  // namespace

void Init() {
    if (gMainLogger != nullptr) {
        return;
    }
    const std::string basePath = LogBasePath();
    InitMainLogger(basePath);
    InitProfileLogger(basePath);
}

void Debug(const char* fmt, ...) {
    if (gMainLogger == nullptr) {
        return;
    }
    std::array<char, kLogBufferSize> buf{};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf.data(), buf.size(), fmt, args);
    va_end(args);
    gMainLogger->debug("{}", buf.data());
}

void Info(const char* fmt, ...) {
    if (gMainLogger == nullptr) {
        return;
    }
    std::array<char, kLogBufferSize> buf{};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf.data(), buf.size(), fmt, args);
    va_end(args);
    gMainLogger->info("{}", buf.data());
}

void Warning(const char* fmt, ...) {
    if (gMainLogger == nullptr) {
        return;
    }
    std::array<char, kLogBufferSize> buf{};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf.data(), buf.size(), fmt, args);
    va_end(args);
    gMainLogger->warn("{}", buf.data());
}

void Error(const char* fmt, ...) {
    if (gMainLogger == nullptr) {
        return;
    }
    std::array<char, kLogBufferSize> buf{};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf.data(), buf.size(), fmt, args);
    va_end(args);
    gMainLogger->error("{}", buf.data());
}

void Profile(const char* fmt, ...) {
    if (gProfileLogger == nullptr) {
        return;
    }
    std::array<char, kLogBufferSize> buf{};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf.data(), buf.size(), fmt, args);
    va_end(args);
    std::string line(buf.data());
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.pop_back();
    }
    gProfileLogger->info("{}", line);
}

}  // namespace bolt::log
