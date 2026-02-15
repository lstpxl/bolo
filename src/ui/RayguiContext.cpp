#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

void ConfigureRayguiDefaultStyle() {
    GuiSetStyle(DEFAULT, TEXT_SIZE, 18);
}
