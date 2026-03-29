#pragma once

#include "raylib.h"

namespace ui::bolt_menu_slider {

/// Outer band height used for level-row layout and focus ring geometry in the main menu.
inline constexpr float kFocusBandHeightPx = 32.0F;
/// Vertical inset from the focus band to `GuiSliderBar` bounds (matches menu level slider).
inline constexpr float kBarVertInsetPx = 2.0F;
/// Post-draw horizontal mask width so border-to-fill gap matches vertical padding (raygui quirk).
inline constexpr float kInnerSideGapPx = 2.0F;
inline constexpr int kBorderPx = 2;
inline constexpr int kPaddingPx = 2;

inline constexpr float kBarBoundsHeightPx = kFocusBandHeightPx - 2.0F * kBarVertInsetPx;

/// Full main-menu raygui setup for Start/Quit/Level slider: buttons, labels, and slider track/fill.
/// `normalStateFillColor` is `SLIDER` `BASE_COLOR_PRESSED` (fill when the control is in normal state).
void ApplyBoltMenuSliderRayGuiStyle(Color normalStateFillColor, int borderWidthPx, int paddingPx);

void DrawInnerSideGapMasks(
    const Rectangle& barBounds,
    int borderWidthPx,
    int paddingPx,
    float innerSideGapPx);

/// Non-interactive `GuiSliderBar` (input ignored via `GuiLock`).
void DrawSliderBarReadOnly(const Rectangle& barBounds, float value, float minValue, float maxValue);

}  // namespace ui::bolt_menu_slider
