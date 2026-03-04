#include "ui/UiPrimitives.h"

namespace ui::primitives {
void DrawFocusRing(const Rectangle& bounds, bool isFocused) {
    if (!isFocused) {
        return;
    }
    DrawRectangleLinesEx(
        Rectangle{
            .x = bounds.x - 3.0F,
            .y = bounds.y - 3.0F,
            .width = bounds.width + 6.0F,
            .height = bounds.height + 6.0F,
        },
        3.0F,
        Color{255, 209, 102, 255});
}

void DrawModalBackdrop(int width, int height) {
    DrawRectangle(0, 0, width, height, Fade(BLACK, 0.6F));
}
}  // namespace ui::primitives
