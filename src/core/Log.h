#pragma once

#include <cstdarg>

namespace bolt::log {

/// Initialize log files. Call once at startup after InitWindow (needs GetApplicationDirectory).
/// Logs go to {appDir}/bolt.log and {appDir}/profile.log.
void Init();

/// Flush and tear down loggers. Safe to call if Init was never called or already shut down.
void Shutdown();

/// Main logger: debug, info, warning, error. Writes to bolt.log.
void Debug(const char* fmt, ...);
void Info(const char* fmt, ...);
void Warning(const char* fmt, ...);
void Error(const char* fmt, ...);

/// Profile logger: writes raw lines to profile.log (no timestamp/level).
/// Preserves [PROFILE], [ENEMY_*] etc. format for compare-handheld-profiles.py.
void Profile(const char* fmt, ...);

}  // namespace bolt::log
