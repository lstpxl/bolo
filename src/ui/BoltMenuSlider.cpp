#include "ui/BoltMenuSlider.h"

#include "raygui.h"

namespace ui::bolt_menu_slider {
namespace {

constexpr Color kMenuSliderTrackBg = Color{40, 46, 58, 180};
constexpr Color kMenuFocusableItemColor = Color{13, 129, 82, 255};

}  // namespace

void ApplyBoltMenuSliderRayGuiStyle(Color normalStateFillColor, int borderWidthPx, int paddingPx) {
    GuiLoadStyleDefault();

    const int transparent = static_cast<int>(ColorToInt(BLANK));
    const int trackBg = static_cast<int>(ColorToInt(kMenuSliderTrackBg));
    const int focusPacked = static_cast<int>(ColorToInt(kMenuFocusableItemColor));
    const int fillPacked = static_cast<int>(ColorToInt(normalStateFillColor));

    GuiSetStyle(BUTTON, BORDER_WIDTH, 0);
    GuiSetStyle(BUTTON, BORDER_COLOR_NORMAL, transparent);
    GuiSetStyle(BUTTON, BASE_COLOR_NORMAL, transparent);
    GuiSetStyle(BUTTON, TEXT_COLOR_NORMAL, focusPacked);
    GuiSetStyle(BUTTON, BORDER_COLOR_FOCUSED, transparent);
    GuiSetStyle(BUTTON, BASE_COLOR_FOCUSED, transparent);
    GuiSetStyle(BUTTON, TEXT_COLOR_FOCUSED, focusPacked);
    GuiSetStyle(BUTTON, BORDER_COLOR_PRESSED, transparent);
    GuiSetStyle(BUTTON, BASE_COLOR_PRESSED, transparent);
    GuiSetStyle(BUTTON, TEXT_COLOR_PRESSED, focusPacked);

    GuiSetStyle(SLIDER, BORDER_WIDTH, borderWidthPx);
    GuiSetStyle(SLIDER, SLIDER_PADDING, paddingPx);
    GuiSetStyle(SLIDER, BORDER_COLOR_NORMAL, focusPacked);
    GuiSetStyle(SLIDER, BORDER_COLOR_FOCUSED, focusPacked);
    GuiSetStyle(SLIDER, BORDER_COLOR_PRESSED, focusPacked);
    GuiSetStyle(SLIDER, BASE_COLOR_NORMAL, trackBg);
    GuiSetStyle(SLIDER, BASE_COLOR_PRESSED, fillPacked);
    GuiSetStyle(SLIDER, TEXT_COLOR_FOCUSED, focusPacked);
    GuiSetStyle(SLIDER, TEXT_COLOR_PRESSED, focusPacked);

    GuiSetStyle(LABEL, TEXT_COLOR_NORMAL, focusPacked);
    GuiSetStyle(LABEL, TEXT_COLOR_FOCUSED, focusPacked);
    GuiSetStyle(LABEL, TEXT_COLOR_PRESSED, focusPacked);
}

void DrawInnerSideGapMasks(
    const Rectangle& barBounds,
    int borderWidthPx,
    int paddingPx,
    float innerSideGapPx) {
    const float fillY = barBounds.y + static_cast<float>(borderWidthPx + paddingPx);
    const float fillH = barBounds.height - 2.0F * static_cast<float>(borderWidthPx + paddingPx);
    if (fillH <= 0.0F || innerSideGapPx <= 0.0F) {
        return;
    }
    const float leftGapX = barBounds.x + static_cast<float>(borderWidthPx);
    const float rightGapX =
        barBounds.x + barBounds.width - static_cast<float>(borderWidthPx) - innerSideGapPx;
    DrawRectangleRec(
        Rectangle{
            leftGapX,
            fillY,
            innerSideGapPx,
            fillH,
        },
        kMenuSliderTrackBg);
    DrawRectangleRec(
        Rectangle{
            rightGapX,
            fillY,
            innerSideGapPx,
            fillH,
        },
        kMenuSliderTrackBg);
}

void DrawSliderBarReadOnly(const Rectangle& barBounds, float value, float minValue, float maxValue) {
    float v = value;
    GuiLock();
    GuiSliderBar(barBounds, "", "", &v, minValue, maxValue);
    GuiUnlock();
}

}  // namespace ui::bolt_menu_slider
