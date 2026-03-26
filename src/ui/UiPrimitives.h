#pragma once

#include "raylib.h"

namespace ui::primitives {
void DrawFocusRing(const Rectangle& bounds, bool isFocused);
/// `expandPx` insets the stroke rectangle outward from `bounds` on each side before drawing.
void DrawFocusRing(const Rectangle& bounds, bool isFocused, float expandPx, float lineThickness);
void DrawModalBackdrop(int width, int height);
}  // namespace ui::primitives
