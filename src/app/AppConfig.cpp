#include "app/AppConfig.h"

AppConfig MakeDefaultAppConfig() {
    return AppConfig{
        .screenWidth = 960,
        .screenHeight = 540,
        .hudWidth = 260,
        .targetFps = 60,
        .fixedDeltaSeconds = 1.0F / 60.0F,
        .windowTitle = "bolo",
    };
}
