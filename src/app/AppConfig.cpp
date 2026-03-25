#include "app/AppConfig.h"

AppConfig MakeDefaultAppConfig() {
    return AppConfig{
        .screenWidth = 640,
        .screenHeight = 480,
        .targetFps = 60,
        .fixedDeltaSeconds = 1.0F / 60.0F,
        .windowTitle = "bolt",
    };
}

int ComputeHudWidth(const AppConfig& config) {
    return config.screenWidth / 4;
}
