#pragma once

#include "raylib.h"

namespace ui::primitives {
void DrawFocusRing(const Rectangle& bounds, bool isFocused);
void DrawModalBackdrop(int width, int height);
}  // namespace ui::primitives
