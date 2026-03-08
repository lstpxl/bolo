#include "app/AppConfig.h"

AppConfig MakeDefaultAppConfig() {
    return AppConfig{
#if defined(__APPLE__) && !defined(NDEBUG)
        .screenWidth = 1920,
        .screenHeight = 1440,
#else
        .screenWidth = 640,
        .screenHeight = 480,
#endif
        .targetFps = 60,
        .fixedDeltaSeconds = 1.0F / 60.0F,
        .windowTitle = "bolt",
    };
}

int ComputeHudWidth(const AppConfig& config) {
    return config.screenWidth / 4;
}
