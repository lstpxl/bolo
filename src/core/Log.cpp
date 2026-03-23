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

bool IsDebugTelemetryLine(const std::string& line) {
    return line.find("_DEBUG") != std::string::npos ||
        line.find("_DIAG") != std::string::npos ||
        line.find("_DROP") != std::string::npos ||
        line.find("_EVENT") != std::string::npos;
}

/// Periodic enemy summaries used by scripts/compare-handheld-profiles.py and coarse perf reads.
/// Any other `[ENEMY_*]` line is detailed telemetry -> bolt.log only.
bool IsAllowedEnemyProfilePrefix(const std::string& line) {
    static constexpr const char* kAllowed[] = {
        "[ENEMY_STATS]",
        "[ENEMY_WINDOW]",
        "[ENEMY_SEGMENTS]",
        "[ENEMY_GRID]",
        "[ENEMY_COLLISION_WINDOW]",
        "[ENEMY_NAV_CACHE]",
        "[ENEMY_SAP]",
        "[ENEMY_PAIR_TYPES]",
    };
    for (const char* prefix : kAllowed) {
        if (line.starts_with(prefix)) {
            return true;
        }
    }
    return false;
}

bool ShouldRouteProfileLineToBoltLogOnly(const std::string& line) {
    if (IsDebugTelemetryLine(line)) {
        return true;
    }
    if (!line.starts_with("[ENEMY_")) {
        return false;
    }
    return !IsAllowedEnemyProfilePrefix(line);
}

void RaylibTraceToBoltLog(int logLevel, const char* text, va_list args) {
    if (gMainLogger == nullptr || text == nullptr) {
        return;
    }
    std::array<char, kLogBufferSize> buf{};
    std::vsnprintf(buf.data(), buf.size(), text, args);
    switch (logLevel) {
    case LOG_TRACE:
    case LOG_DEBUG:
        gMainLogger->debug("RAYLIB: {}", buf.data());
        break;
    case LOG_INFO:
        gMainLogger->info("RAYLIB: {}", buf.data());
        break;
    case LOG_WARNING:
        gMainLogger->warn("RAYLIB: {}", buf.data());
        break;
    case LOG_ERROR:
    case LOG_FATAL:
        gMainLogger->error("RAYLIB: {}", buf.data());
        break;
    default:
        gMainLogger->info("RAYLIB: {}", buf.data());
        break;
    }
}

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
    RedirectRaylibTraceLogsToBoltLog();
    SetTraceLogLevel(LOG_INFO);
}

void PrepareRaylibShutdown() {
    if (gMainLogger != nullptr) {
        gMainLogger->flush();
    }
    if (gProfileLogger != nullptr) {
        gProfileLogger->flush();
    }
    SetTraceLogLevel(LOG_NONE);
}

void Shutdown() {
    if (gMainLogger != nullptr) {
        gMainLogger->flush();
    }
    if (gProfileLogger != nullptr) {
        gProfileLogger->flush();
    }
    gMainLogger.reset();
    gProfileLogger.reset();
    spdlog::shutdown();
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
    if (gProfileLogger == nullptr && gMainLogger == nullptr) {
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
    if (ShouldRouteProfileLineToBoltLogOnly(line)) {
        if (gMainLogger != nullptr) {
            gMainLogger->debug("{}", line);
        }
        return;
    }
    if (gProfileLogger != nullptr) {
        gProfileLogger->info("{}", line);
    }
}

void RedirectRaylibTraceLogsToBoltLog() {
    if (gMainLogger == nullptr) {
        return;
    }
    SetTraceLogCallback(RaylibTraceToBoltLog);
}

}  // namespace bolt::log
