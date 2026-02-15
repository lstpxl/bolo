#pragma once

#include <string_view>

struct AppConfig {
    int screenWidth;
    int screenHeight;
    int targetFps;
    float fixedDeltaSeconds;
    std::string_view windowTitle;
};

AppConfig MakeDefaultAppConfig();
int ComputeHudWidth(const AppConfig& config);