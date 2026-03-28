#include "ui/UiPrimitives.h"

namespace ui::primitives {
void DrawFocusRing(const Rectangle& bounds, bool isFocused) {
    DrawFocusRing(bounds, isFocused, 3.0F, 3.0F);
}

void DrawFocusRing(const Rectangle& bounds, bool isFocused, float expandPx, float lineThickness) {
    if (!isFocused) {
        return;
    }
    DrawRectangleLinesEx(
        Rectangle{
            .x = bounds.x - expandPx,
            .y = bounds.y - expandPx,
            .width = bounds.width + 2.0F * expandPx,
            .height = bounds.height + 2.0F * expandPx,
        },
        lineThickness,
        Color{162, 137, 5, 255});  // #A28905 selection frame
}

void DrawModalBackdrop(int width, int height) {
    DrawRectangle(0, 0, width, height, Fade(BLACK, 0.6F));
}
}  // namespace ui::primitives
