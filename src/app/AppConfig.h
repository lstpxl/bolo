#pragma once

#include <string_view>

struct AppConfig {
    int screenWidth;
    int screenHeight;
    int hudWidth;
    int targetFps;
    float fixedDeltaSeconds;
    std::string_view windowTitle;
};

AppConfig MakeDefaultAppConfig();
