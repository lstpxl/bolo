#pragma once

#include <cstdarg>

namespace bolt::log {

/// Initialize log files. Call once at startup before InitWindow so raylib TraceLog is routed
/// to bolt.log from the first frame (GetApplicationDirectory may fall back to "." until window exists).
/// Logs go to {appDir}/bolt.log and {appDir}/profile.log.
void Init();

/// Flush logs and silence raylib unload/teardown TraceLog before CloseWindow(). Avoids stderr (or
/// mis-merged captures) splicing unload INFO lines into profile.log mid-line.
void PrepareRaylibShutdown();

/// Flush and tear down loggers. Safe to call if Init was never called or already shut down.
void Shutdown();

/// Main logger: debug, info, warning, error. Writes to bolt.log.
void Debug(const char* fmt, ...);
void Info(const char* fmt, ...);
void Warning(const char* fmt, ...);
void Error(const char* fmt, ...);

/// Profile logger: writes raw lines to profile.log (no timestamp/level).
/// [PROFILE] scope rows and allowlisted periodic [ENEMY_*] aggregates; other enemy debug lines go to
/// bolt.log only (see Log.cpp).
void Profile(const char* fmt, ...);

/// Redirect raylib TraceLog output into bolt.log.
/// This keeps profile.log focused on profiling telemetry only.
void RedirectRaylibTraceLogsToBoltLog();

}  // namespace bolt::log
