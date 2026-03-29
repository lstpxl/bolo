#include "ui/MenuScreen.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <string_view>
#include <string>
#include <unordered_map>
#include <vector>
#include "app/BuildInfo.h"
#include "core/Log.h"
#include "core/ResourceLocator.h"
#include "ui/BoltMenuSlider.h"
#include "ui/UiPrimitives.h"
#include "raygui.h"
#include "raylib.h"

namespace {
constexpr int kMinLevelNumber = 1;
constexpr int kMaxLevelNumber = 9;
constexpr int kMinMazeDensity = 1;
constexpr int kMaxMazeDensity = 5;
// Height of Start / Quit `GuiButton` rows and unified Debug / Start / Quit focus frame.
constexpr float kMenuQuitButtonHeight = 30.0F;
constexpr double kMenuRevealRowDurationSeconds = 1.0;

MenuScreen::FocusedControl NextFocusedControl(MenuScreen::FocusedControl current) {
    if (current == MenuScreen::FocusedControl::Quit) {
        return MenuScreen::FocusedControl::Start;
    }
    return static_cast<MenuScreen::FocusedControl>(static_cast<int>(current) + 1);
}

MenuScreen::FocusedControl PreviousFocusedControl(MenuScreen::FocusedControl current) {
    if (current == MenuScreen::FocusedControl::Start) {
        return MenuScreen::FocusedControl::Quit;
    }
    return static_cast<MenuScreen::FocusedControl>(static_cast<int>(current) - 1);
}

int RoundToNearestInt(float value) {
    return static_cast<int>(std::round(value));
}

std::string Trim(std::string_view value) {
    std::size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }
    std::size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return std::string(value.substr(start, end - start));
}

bool ParseIntegerAfterLabel(
    std::string_view text,
    std::string_view label,
    int& outValue) {
    const std::size_t labelPos = text.find(label);
    if (labelPos == std::string_view::npos) {
        return false;
    }
    std::size_t valueStart = labelPos + label.size();
    while (valueStart < text.size() && std::isspace(static_cast<unsigned char>(text[valueStart])) != 0) {
        ++valueStart;
    }
    std::size_t valueEnd = valueStart;
    while (valueEnd < text.size() && std::isdigit(static_cast<unsigned char>(text[valueEnd])) != 0) {
        ++valueEnd;
    }
    if (valueEnd == valueStart) {
        return false;
    }
    outValue = std::stoi(std::string(text.substr(valueStart, valueEnd - valueStart)));
    return true;
}

std::vector<int> Utf8ToCodepoints(const std::string& input) {
    std::vector<int> codepoints;
    codepoints.reserve(input.size());
    for (std::size_t i = 0; i < input.size();) {
        const unsigned char c0 = static_cast<unsigned char>(input[i]);
        if ((c0 & 0x80U) == 0) {
            codepoints.push_back(static_cast<int>(c0));
            ++i;
            continue;
        }
        if ((c0 & 0xE0U) == 0xC0U && i + 1 < input.size()) {
            const unsigned char c1 = static_cast<unsigned char>(input[i + 1]);
            const int cp = (static_cast<int>(c0 & 0x1FU) << 6) | static_cast<int>(c1 & 0x3FU);
            codepoints.push_back(cp);
            i += 2;
            continue;
        }
        if ((c0 & 0xF0U) == 0xE0U && i + 2 < input.size()) {
            const unsigned char c1 = static_cast<unsigned char>(input[i + 1]);
            const unsigned char c2 = static_cast<unsigned char>(input[i + 2]);
            const int cp =
                (static_cast<int>(c0 & 0x0FU) << 12) |
                (static_cast<int>(c1 & 0x3FU) << 6) |
                static_cast<int>(c2 & 0x3FU);
            codepoints.push_back(cp);
            i += 3;
            continue;
        }
        if ((c0 & 0xF8U) == 0xF0U && i + 3 < input.size()) {
            const unsigned char c1 = static_cast<unsigned char>(input[i + 1]);
            const unsigned char c2 = static_cast<unsigned char>(input[i + 2]);
            const unsigned char c3 = static_cast<unsigned char>(input[i + 3]);
            const int cp =
                (static_cast<int>(c0 & 0x07U) << 18) |
                (static_cast<int>(c1 & 0x3FU) << 12) |
                (static_cast<int>(c2 & 0x3FU) << 6) |
                static_cast<int>(c3 & 0x3FU);
            codepoints.push_back(cp);
            i += 4;
            continue;
        }
        ++i;
    }
    return codepoints;
}

std::string UnescapeQuotedString(const std::string& escaped) {
    std::string result;
    result.reserve(escaped.size());
    for (std::size_t i = 0; i < escaped.size(); ++i) {
        const char c = escaped[i];
        if (c == '\\' && i + 1 < escaped.size()) {
            const char n = escaped[i + 1];
            if (n == '\\' || n == '"') {
                result.push_back(n);
                ++i;
                continue;
            }
        }
        result.push_back(c);
    }
    return result;
}

bool ParseSpacingMap(
    const std::string& spacingData,
    std::unordered_map<int, int>& advanceByCodepoint) {
    std::size_t pos = 0;
    while (true) {
        const std::size_t open = spacingData.find('[', pos);
        if (open == std::string::npos) {
            break;
        }
        std::size_t numStart = open + 1;
        while (numStart < spacingData.size() && spacingData[numStart] == ' ') {
            ++numStart;
        }
        std::size_t numEnd = numStart;
        while (numEnd < spacingData.size() && std::isdigit(static_cast<unsigned char>(spacingData[numEnd])) != 0) {
            ++numEnd;
        }
        if (numEnd == numStart || numEnd >= spacingData.size() || spacingData[numEnd] != ',') {
            pos = open + 1;
            continue;
        }
        const int advance = std::stoi(spacingData.substr(numStart, numEnd - numStart));
        std::size_t quoteStart = spacingData.find('"', numEnd + 1);
        if (quoteStart == std::string::npos) {
            return false;
        }
        std::size_t quoteEnd = quoteStart + 1;
        bool escaped = false;
        for (; quoteEnd < spacingData.size(); ++quoteEnd) {
            const char ch = spacingData[quoteEnd];
            if (escaped) {
                escaped = false;
                continue;
            }
            if (ch == '\\') {
                escaped = true;
                continue;
            }
            if (ch == '"') {
                break;
            }
        }
        if (quoteEnd >= spacingData.size()) {
            return false;
        }
        const std::string escapedChars = spacingData.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
        const std::string chars = UnescapeQuotedString(escapedChars);
        for (int codepoint : Utf8ToCodepoints(chars)) {
            advanceByCodepoint[codepoint] = advance;
        }
        pos = quoteEnd + 1;
    }
    return !advanceByCodepoint.empty();
}

bool LoadAbsolute10BitmapFont(Font& outFont) {
    const std::string pngPath = core::resources::ResolveResourcePath("fonts", "absolute_10.png");
    const std::string txtPath = core::resources::ResolveResourcePath("fonts", "absolute_10.txt");
    if (pngPath.empty() || txtPath.empty()) {
        bolt::log::Warning("MENU: absolute_10 bitmap font files not found in resources/fonts");
        return false;
    }

    std::ifstream metadataFile(txtPath);
    if (!metadataFile.is_open()) {
        bolt::log::Warning("MENU: failed to open bitmap font metadata: %s", txtPath.c_str());
        return false;
    }
    const std::string metadata(
        (std::istreambuf_iterator<char>(metadataFile)),
        std::istreambuf_iterator<char>());
    metadataFile.close();

    int charWidth = 0;
    int charHeight = 0;
    if (!ParseIntegerAfterLabel(metadata, "Character width:", charWidth) ||
        !ParseIntegerAfterLabel(metadata, "Character height:", charHeight) ||
        charWidth <= 0 || charHeight <= 0) {
        bolt::log::Warning("MENU: invalid character size metadata in %s", txtPath.c_str());
        return false;
    }

    const std::size_t charsetLabelPos = metadata.find("Character set:");
    const std::size_t spacingLabelPos = metadata.find("Spacing data:");
    if (charsetLabelPos == std::string::npos || spacingLabelPos == std::string::npos ||
        spacingLabelPos <= charsetLabelPos) {
        bolt::log::Warning("MENU: malformed bitmap font metadata sections in %s", txtPath.c_str());
        return false;
    }

    const std::size_t charsetValueStart = charsetLabelPos + std::string_view("Character set:").size();
    const std::string charset = Trim(
        std::string_view(metadata).substr(charsetValueStart, spacingLabelPos - charsetValueStart));
    const std::vector<int> charsetCodepoints = Utf8ToCodepoints(charset);
    if (charsetCodepoints.empty()) {
        bolt::log::Warning("MENU: bitmap font character set is empty in %s", txtPath.c_str());
        return false;
    }

    std::unordered_map<int, int> advanceByCodepoint;
    const std::string spacingData = metadata.substr(spacingLabelPos);
    if (!ParseSpacingMap(spacingData, advanceByCodepoint)) {
        bolt::log::Warning("MENU: failed to parse spacing data in %s", txtPath.c_str());
        return false;
    }

    Texture2D texture = LoadTexture(pngPath.c_str());
    if (texture.id == 0) {
        bolt::log::Warning("MENU: failed to load bitmap font texture from %s", pngPath.c_str());
        return false;
    }
    SetTextureFilter(texture, TEXTURE_FILTER_POINT);

    const int columns = texture.width / charWidth;
    const int rows = texture.height / charHeight;
    const int maxGlyphs = columns * rows;
    const int glyphCount = static_cast<int>(charsetCodepoints.size());
    if (columns <= 0 || rows <= 0 || glyphCount > maxGlyphs) {
        bolt::log::Warning(
            "MENU: bitmap atlas capacity mismatch (%d glyphs, capacity %d) from %s",
            glyphCount,
            maxGlyphs,
            pngPath.c_str());
        UnloadTexture(texture);
        return false;
    }

    Rectangle* recs = static_cast<Rectangle*>(MemAlloc(static_cast<unsigned int>(sizeof(Rectangle) * glyphCount)));
    GlyphInfo* glyphs = static_cast<GlyphInfo*>(MemAlloc(static_cast<unsigned int>(sizeof(GlyphInfo) * glyphCount)));
    if (recs == nullptr || glyphs == nullptr) {
        if (recs != nullptr) {
            MemFree(recs);
        }
        if (glyphs != nullptr) {
            MemFree(glyphs);
        }
        UnloadTexture(texture);
        bolt::log::Warning("MENU: failed to allocate bitmap font glyph data");
        return false;
    }

    for (int i = 0; i < glyphCount; ++i) {
        const int codepoint = charsetCodepoints[static_cast<std::size_t>(i)];
        const int atlasX = (i % columns) * charWidth;
        const int atlasY = (i / columns) * charHeight;
        recs[i] = Rectangle{
            .x = static_cast<float>(atlasX),
            .y = static_cast<float>(atlasY),
            .width = static_cast<float>(charWidth),
            .height = static_cast<float>(charHeight),
        };
        glyphs[i] = GlyphInfo{
            .value = codepoint,
            .offsetX = 0,
            .offsetY = 0,
            .advanceX = charWidth,
            .image = Image{},
        };
        const auto it = advanceByCodepoint.find(codepoint);
        if (it != advanceByCodepoint.end()) {
            glyphs[i].advanceX = it->second;
        }
    }

    outFont = Font{
        .baseSize = charHeight,
        .glyphCount = glyphCount,
        .glyphPadding = 0,
        .texture = texture,
        .recs = recs,
        .glyphs = glyphs,
    };
    return true;
}

constexpr float kMenuTitleRenderSize = 128.0F;
constexpr int kMenuTitleTopMarginPx = 4;
constexpr Color kBoltWordmarkColor = Color{224, 206, 4, 255};
constexpr int kBetaLabelFontBaseSize = 16;
constexpr float kBetaLabelRenderSize = 16.0F;
constexpr float kBetaLabelYOffset = -2.0F;
// Inset build # text left from Bolt’s right edge (both Pixuf and default-font paths).
constexpr float kBuildLabelInsetFromBoltRightPx = 6.0F;

constexpr int kDensityHatchCellPx = 16;
constexpr int kDensityHatchSpriteCount = 5;
constexpr int kDensityHatchGapPx = 16;
constexpr int kDensityHatchRenderScale = 2;
constexpr int kDensityHatchDrawPx = kDensityHatchCellPx * kDensityHatchRenderScale;

// Main menu focusable row text, raygui packed colors, and density-hatch sprite tint (#0D8152).
constexpr Color kMenuFocusableItemColor = Color{13, 129, 82, 255};
const int kMenuFocusableTextPacked = static_cast<int>(ColorToInt(kMenuFocusableItemColor));

void ReplaceOpaquePixelsRgb(Image& image, Color rgb) {
    if (image.data == nullptr) {
        return;
    }
    ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    auto* pixels = static_cast<Color*>(image.data);
    const int count = image.width * image.height;
    for (int i = 0; i < count; ++i) {
        if (pixels[i].a == 0) {
            continue;
        }
        pixels[i].r = rgb.r;
        pixels[i].g = rgb.g;
        pixels[i].b = rgb.b;
    }
}

bool TryLoadDensityHatchSheet(Texture2D& outTexture) {
    const std::string path = core::resources::ResolveResourcePath("textures", "density-hatch.png");
    if (path.empty() || !FileExists(path.c_str())) {
        bolt::log::Warning("MENU: density-hatch.png not found under resources/textures");
        return false;
    }
    Image image = LoadImage(path.c_str());
    if (image.data == nullptr) {
        bolt::log::Warning("MENU: failed to decode density-hatch.png");
        return false;
    }

    const bool horizontalStrip =
        image.width == kDensityHatchCellPx * kDensityHatchSpriteCount && image.height == kDensityHatchCellPx;
    const bool verticalStrip =
        image.width == kDensityHatchCellPx && image.height == kDensityHatchCellPx * kDensityHatchSpriteCount;
    if (!horizontalStrip && !verticalStrip) {
        bolt::log::Warning(
            "MENU: density-hatch.png expected strip %dx%d or %dx%d, got %dx%d",
            kDensityHatchCellPx * kDensityHatchSpriteCount,
            kDensityHatchCellPx,
            kDensityHatchCellPx,
            kDensityHatchCellPx * kDensityHatchSpriteCount,
            image.width,
            image.height);
        UnloadImage(image);
        return false;
    }

    ReplaceOpaquePixelsRgb(image, kMenuFocusableItemColor);
    outTexture = LoadTextureFromImage(image);
    UnloadImage(image);
    if (outTexture.id == 0) {
        bolt::log::Warning("MENU: failed to upload density-hatch texture");
        return false;
    }
    SetTextureFilter(outTexture, TEXTURE_FILTER_POINT);
    return true;
}
}  // namespace

bool MenuScreen::LoadResources() {
    UnloadResources();
    titleFontLoaded_ = LoadAbsolute10BitmapFont(titleFont_);
    if (!titleFontLoaded_) {
        titleFont_ = Font{};
    }
    const std::string betaFontPath = core::resources::ResolveResourcePath("fonts", "pixuf.ttf");
    if (betaFontPath.empty()) {
        bolt::log::Warning("MENU: pixuf.ttf not found in resources/fonts");
        betaFont_ = Font{};
        betaFontLoaded_ = false;
    } else {
        betaFont_ = LoadFontEx(betaFontPath.c_str(), kBetaLabelFontBaseSize, nullptr, 0);
        betaFontLoaded_ = betaFont_.texture.id != 0;
        if (!betaFontLoaded_) {
            bolt::log::Warning("MENU: failed to load beta font from %s", betaFontPath.c_str());
            betaFont_ = Font{};
        } else {
            SetTextureFilter(betaFont_.texture, TEXTURE_FILTER_POINT);
        }
    }
    densityHatchLoaded_ = TryLoadDensityHatchSheet(densityHatchTexture_);
    return titleFontLoaded_ || betaFontLoaded_;
}

void MenuScreen::UnloadResources() {
    if (densityHatchLoaded_) {
        UnloadTexture(densityHatchTexture_);
        densityHatchTexture_ = Texture2D{};
        densityHatchLoaded_ = false;
    }
    if (titleFontLoaded_) {
        UnloadFont(titleFont_);
        titleFont_ = Font{};
        titleFontLoaded_ = false;
    }
    if (betaFontLoaded_) {
        UnloadFont(betaFont_);
        betaFont_ = Font{};
        betaFontLoaded_ = false;
    }
}

MenuScreenResult MenuScreen::Render(
    const MenuSettings& currentSettings,
    const AppConfig& config,
    const FrameInput& input) {
    levelNumber_ = std::clamp(currentSettings.levelNumber, kMinLevelNumber, kMaxLevelNumber);
    mazeDensity_ = std::clamp(currentSettings.mazeDensity, kMinMazeDensity, kMaxMazeDensity);
    debugInfo_ = currentSettings.debugInfo;
    bool interactionOccurred = false;
    bool menuButtonActivatedViaMenuSelect = false;

    if (!quitConfirmationOpen_) {
        if (input.menuNavigateDownPressed) {
            focusedControl_ = NextFocusedControl(focusedControl_);
            interactionOccurred = true;
        }
        if (input.menuNavigateUpPressed) {
            focusedControl_ = PreviousFocusedControl(focusedControl_);
            interactionOccurred = true;
        }
    }

    if (quitConfirmationOpen_) {
        densitySpritesRevealUntilTime_ = 0.0;
        levelSliderRevealUntilTime_ = 0.0;
    }

    const float panelCenterX = static_cast<float>(config.screenWidth) * 0.5F;
    const float controlsWidth = std::min(440.0F, static_cast<float>(config.screenWidth) - 24.0F);
    const float gaugeWidth = std::min(320.0F, controlsWidth);
    const float buttonsX = panelCenterX - controlsWidth * 0.5F;

    constexpr int kDebugInfoMenuFontPx = 20;
    constexpr int kUnifiedMenuFocusExtraWidthPx = 40;
    const float unifiedMenuFocusW = static_cast<float>(
        MeasureText("Debug info: Off", kDebugInfoMenuFontPx) + kUnifiedMenuFocusExtraWidthPx);
    const float unifiedMenuFocusX = panelCenterX - unifiedMenuFocusW * 0.5F;

    constexpr const char* kBoltWordmarkText = "Bolt";
    constexpr const char* kBetaVersionText = "Beta Version";
    Vector2 boltSize{};
    if (titleFontLoaded_) {
        boltSize = MeasureTextEx(titleFont_, kBoltWordmarkText, kMenuTitleRenderSize, 0.0F);
    } else {
        boltSize = Vector2{
            static_cast<float>(MeasureText(kBoltWordmarkText, 20)),
            20.0F,
        };
    }
    const float boltX = panelCenterX - boltSize.x * 0.5F;
    const float boltY = static_cast<float>(kMenuTitleTopMarginPx);
    if (titleFontLoaded_) {
        DrawTextEx(
            titleFont_,
            kBoltWordmarkText,
            Vector2{boltX, boltY},
            kMenuTitleRenderSize,
            0.0F,
            kBoltWordmarkColor);
    } else {
        DrawText(kBoltWordmarkText, static_cast<int>(boltX), static_cast<int>(boltY), 20, kBoltWordmarkColor);
    }
    const float subtitleRowY = boltY + boltSize.y + kBetaLabelYOffset;
    const char* const buildLabel = TextFormat("Build #%d", CurrentBuildNumber());
    float betaLabelY = subtitleRowY;
    if (betaFontLoaded_) {
        DrawTextEx(
            betaFont_,
            kBetaVersionText,
            Vector2{boltX, subtitleRowY},
            kBetaLabelRenderSize,
            0.0F,
            GRAY);
        const Vector2 betaSize = MeasureTextEx(betaFont_, kBetaVersionText, kBetaLabelRenderSize, 0.0F);
        const Vector2 buildSize = MeasureTextEx(betaFont_, buildLabel, kBetaLabelRenderSize, 0.0F);
        const float buildX =
            boltX + boltSize.x - buildSize.x - kBuildLabelInsetFromBoltRightPx;
        DrawTextEx(
            betaFont_,
            buildLabel,
            Vector2{buildX, subtitleRowY},
            kBetaLabelRenderSize,
            0.0F,
            GRAY);
        betaLabelY = subtitleRowY + std::max(betaSize.y, buildSize.y);
    } else {
        DrawText(kBetaVersionText, static_cast<int>(boltX), static_cast<int>(subtitleRowY), 20, GRAY);
        const int buildW = MeasureText(buildLabel, 20);
        DrawText(
            buildLabel,
            static_cast<int>(
                boltX + boltSize.x - static_cast<float>(buildW) - kBuildLabelInsetFromBoltRightPx),
            static_cast<int>(subtitleRowY),
            20,
            GRAY);
        betaLabelY = subtitleRowY + 20.0F;
    }

    constexpr float kLevelBlockOffsetFromBeta = 20.0F;
    // Single gap between Level / Density / Debug rows; double gap after Start and before Quit.
    constexpr float kMenuItemSingleGapPx = 28.0F;
    constexpr float kMenuItemDoubleGapPx = 56.0F;
    // Top of Start through top of Quit (exclusive of Quit row): 4 rows + 2 double gaps + 2 single gaps.
    constexpr float kMenuStackStartToQuitTopPx =
        4.0F * kMenuQuitButtonHeight + 2.0F * kMenuItemDoubleGapPx + 2.0F * kMenuItemSingleGapPx;

    const float quitButtonY = static_cast<float>(config.screenHeight) - 38.0F - 20.0F;
    const float menuSectionAnchorY = betaLabelY + kLevelBlockOffsetFromBeta;
    // Start sits at least 30 px below the old Level anchor; if that would crowd Quit, pin from Quit upward.
    const float minStartButtonY = menuSectionAnchorY + 30.0F;
    const float startButtonY =
        std::max(minStartButtonY, quitButtonY - kMenuStackStartToQuitTopPx);

    constexpr float kLevelRowUpPx = 20.0F;
    constexpr float kDensityRowUpPx = 40.0F;
    constexpr float kDebugRowUpPx = 60.0F;

    const float levelLabelY = startButtonY + kMenuQuitButtonHeight + kMenuItemDoubleGapPx - kLevelRowUpPx;
    const float levelGaugeY =
        levelLabelY + (kMenuQuitButtonHeight - ui::bolt_menu_slider::kFocusBandHeightPx) * 0.5F;
    const float densityLabelY =
        levelLabelY + kMenuQuitButtonHeight + kMenuItemSingleGapPx - (kDensityRowUpPx - kLevelRowUpPx);
    constexpr int kDensityLabelFontPx = 20;
    const float densitySpritesY =
        densityLabelY +
        (kMenuQuitButtonHeight - static_cast<float>(kDensityHatchDrawPx)) * 0.5F;
    const float debugInfoY =
        densityLabelY + kMenuQuitButtonHeight + kMenuItemSingleGapPx - (kDebugRowUpPx - kDensityRowUpPx);

    ui::bolt_menu_slider::ApplyBoltMenuSliderRayGuiStyle(
        kMenuFocusableItemColor, ui::bolt_menu_slider::kBorderPx, ui::bolt_menu_slider::kPaddingPx);
    GuiSetStyle(DEFAULT, TEXT_SIZE, 20);

    constexpr float kLevelSliderWidthFactor = 0.75F;
    const float levelSliderWidth = gaugeWidth * kLevelSliderWidthFactor;
    const float levelSliderX = panelCenterX - levelSliderWidth * 0.5F;
    const Rectangle levelGauge =
        Rectangle{levelSliderX, levelGaugeY, levelSliderWidth, ui::bolt_menu_slider::kFocusBandHeightPx};
    const Rectangle levelSliderBarBounds = Rectangle{
        levelGauge.x,
        levelGauge.y + ui::bolt_menu_slider::kBarVertInsetPx,
        levelGauge.width,
        ui::bolt_menu_slider::kBarBoundsHeightPx,
    };

    const float densityBlockWidth =
        static_cast<float>(
            kDensityHatchSpriteCount * kDensityHatchDrawPx +
            (kDensityHatchSpriteCount - 1) * kDensityHatchGapPx);
    const float densityRowX = panelCenterX - densityBlockWidth * 0.5F;
    Rectangle densitySpriteDests[kDensityHatchSpriteCount]{};
    for (int i = 0; i < kDensityHatchSpriteCount; ++i) {
        densitySpriteDests[i] = Rectangle{
            densityRowX + static_cast<float>(i * (kDensityHatchDrawPx + kDensityHatchGapPx)),
            densitySpritesY,
            static_cast<float>(kDensityHatchDrawPx),
            static_cast<float>(kDensityHatchDrawPx),
        };
    }

    if (!quitConfirmationOpen_ && focusedControl_ == FocusedControl::Density) {
        if (input.menuNavigateLeftPressed || input.menuNavigateRightPressed) {
            if (input.menuNavigateLeftPressed) {
                mazeDensity_ = std::max(kMinMazeDensity, mazeDensity_ - 1);
            }
            if (input.menuNavigateRightPressed) {
                mazeDensity_ = std::min(kMaxMazeDensity, mazeDensity_ + 1);
            }
            densitySpritesRevealUntilTime_ = GetTime() + kMenuRevealRowDurationSeconds;
            interactionOccurred = true;
        }
    }

    if (!quitConfirmationOpen_ && focusedControl_ == FocusedControl::Level) {
        if (input.menuNavigateLeftPressed || input.menuNavigateRightPressed) {
            if (input.menuNavigateLeftPressed) {
                levelNumber_ = std::max(kMinLevelNumber, levelNumber_ - 1);
            }
            if (input.menuNavigateRightPressed) {
                levelNumber_ = std::min(kMaxLevelNumber, levelNumber_ + 1);
            }
            levelSliderRevealUntilTime_ = GetTime() + kMenuRevealRowDurationSeconds;
            interactionOccurred = true;
        }
    }

    int hoveredDensitySprite = -1;
    if (!quitConfirmationOpen_ && GetTime() < densitySpritesRevealUntilTime_) {
        const Vector2 mouse = GetMousePosition();
        for (int i = 0; i < kDensityHatchSpriteCount; ++i) {
            if (CheckCollisionPointRec(mouse, densitySpriteDests[i]) != 0) {
                hoveredDensitySprite = i;
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) != 0) {
                    const int picked = i + 1;
                    if (mazeDensity_ != picked) {
                        mazeDensity_ = picked;
                        interactionOccurred = true;
                    }
                    focusedControl_ = FocusedControl::Density;
                    densitySpritesRevealUntilTime_ = GetTime() + kMenuRevealRowDurationSeconds;
                }
                break;
            }
        }
    }

    const Rectangle startButton = Rectangle{buttonsX, startButtonY, controlsWidth, kMenuQuitButtonHeight};
    const Rectangle quitButton = Rectangle{buttonsX, quitButtonY, controlsWidth, kMenuQuitButtonHeight};

    const Rectangle debugInfoControl = {
        unifiedMenuFocusX,
        debugInfoY,
        unifiedMenuFocusW,
        kMenuQuitButtonHeight,
    };

    bool debugInfoValue = debugInfo_;
    bool debugInfoHovered = false;
    if (!quitConfirmationOpen_) {
        const Vector2 mouseDbg = GetMousePosition();
        debugInfoHovered = CheckCollisionPointRec(mouseDbg, debugInfoControl) != 0;
        if (debugInfoHovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) != 0) {
            debugInfoValue = !debugInfoValue;
            interactionOccurred = true;
            focusedControl_ = FocusedControl::DebugInfo;
        }
    }
    if (!quitConfirmationOpen_ && focusedControl_ == FocusedControl::DebugInfo) {
        if (input.menuNavigateLeftPressed || input.menuNavigateRightPressed || input.menuSelectPressed) {
            debugInfoValue = !debugInfoValue;
            interactionOccurred = true;
        }
    }

    bool startPressed = GuiButton(startButton, "Start");

    const bool densityShowSprites =
        !quitConfirmationOpen_ && GetTime() < densitySpritesRevealUntilTime_;
    const bool levelShowSlider =
        !quitConfirmationOpen_ && GetTime() < levelSliderRevealUntilTime_;

    constexpr int kLevelLabelFontPx = 20;
    constexpr const char* kLevelUiLabel = "Level";
    constexpr int kLevelLabelNumberGapPx = 10;
    const int levelLabelW = MeasureText(kLevelUiLabel, kLevelLabelFontPx);
    const int levelNumberSlotW = MeasureText("8", kLevelLabelFontPx);
    const int levelHeaderBlockW = levelLabelW + kLevelLabelNumberGapPx + levelNumberSlotW;
    const int levelHeaderBlockLeftX = static_cast<int>(panelCenterX) - levelHeaderBlockW / 2;
    const float levelTextDrawY =
        levelLabelY + (kMenuQuitButtonHeight - static_cast<float>(kLevelLabelFontPx)) * 0.5F;
    const Rectangle levelFocusFrame = {
        unifiedMenuFocusX,
        levelLabelY,
        unifiedMenuFocusW,
        kMenuQuitButtonHeight,
    };

    if (!levelShowSlider) {
        DrawText(
            kLevelUiLabel,
            levelHeaderBlockLeftX,
            static_cast<int>(levelTextDrawY),
            kLevelLabelFontPx,
            kMenuFocusableItemColor);
        const char* levelNumberText = TextFormat("%d", levelNumber_);
        const int levelNumberDrawW = MeasureText(levelNumberText, kLevelLabelFontPx);
        const int levelNumberDrawX =
            levelHeaderBlockLeftX + levelLabelW + kLevelLabelNumberGapPx +
            (levelNumberSlotW - levelNumberDrawW) / 2;
        DrawText(
            levelNumberText,
            levelNumberDrawX,
            static_cast<int>(levelTextDrawY),
            kLevelLabelFontPx,
            kMenuFocusableItemColor);
    }

    constexpr const char* kDensityUiLabel = "Density";
    const int densityLabelW = MeasureText(kDensityUiLabel, kDensityLabelFontPx);
    const int densityNumberSlotW = MeasureText("8", kDensityLabelFontPx);
    const int densityHeaderBlockW = densityLabelW + kLevelLabelNumberGapPx + densityNumberSlotW;
    const int densityHeaderBlockLeftX = static_cast<int>(panelCenterX) - densityHeaderBlockW / 2;
    const float densityTextDrawY =
        densityLabelY + (kMenuQuitButtonHeight - static_cast<float>(kDensityLabelFontPx)) * 0.5F;
    const Rectangle densityFocusFrame = {
        unifiedMenuFocusX,
        densityLabelY,
        unifiedMenuFocusW,
        kMenuQuitButtonHeight,
    };

    if (!densityShowSprites) {
        DrawText(
            kDensityUiLabel,
            densityHeaderBlockLeftX,
            static_cast<int>(densityTextDrawY),
            kDensityLabelFontPx,
            kMenuFocusableItemColor);
        const char* densityNumberText = TextFormat("%d", mazeDensity_);
        const int densityNumberDrawW = MeasureText(densityNumberText, kDensityLabelFontPx);
        const int densityNumberDrawX =
            densityHeaderBlockLeftX + densityLabelW + kLevelLabelNumberGapPx +
            (densityNumberSlotW - densityNumberDrawW) / 2;
        DrawText(
            densityNumberText,
            densityNumberDrawX,
            static_cast<int>(densityTextDrawY),
            kDensityLabelFontPx,
            kMenuFocusableItemColor);
    }

    if (densityShowSprites) {
        if (densityHatchLoaded_) {
            const bool horizontalStrip = densityHatchTexture_.width > densityHatchTexture_.height;
            for (int i = 0; i < kDensityHatchSpriteCount; ++i) {
                const Rectangle source =
                    horizontalStrip
                        ? Rectangle{
                              .x = static_cast<float>(i * kDensityHatchCellPx),
                              .y = 0.0F,
                              .width = static_cast<float>(kDensityHatchCellPx),
                              .height = static_cast<float>(kDensityHatchCellPx),
                          }
                        : Rectangle{
                              .x = 0.0F,
                              .y = static_cast<float>(i * kDensityHatchCellPx),
                              .width = static_cast<float>(kDensityHatchCellPx),
                              .height = static_cast<float>(kDensityHatchCellPx),
                          };
                DrawTexturePro(
                    densityHatchTexture_,
                    source,
                    densitySpriteDests[i],
                    Vector2{0.0F, 0.0F},
                    0.0F,
                    WHITE);
            }
        } else {
            static constexpr const char* kFallbackDigits[kDensityHatchSpriteCount] = {"1", "2", "3", "4", "5"};
            for (int i = 0; i < kDensityHatchSpriteCount; ++i) {
                DrawRectangleRec(densitySpriteDests[i], Color{60, 65, 78, 200});
                const char* digit = kFallbackDigits[i];
                const int dw = MeasureText(digit, 20);
                DrawText(
                    digit,
                    static_cast<int>(densitySpriteDests[i].x + densitySpriteDests[i].width * 0.5F - dw * 0.5F),
                    static_cast<int>(densitySpriteDests[i].y + densitySpriteDests[i].height * 0.5F - 10),
                    20,
                    LIGHTGRAY);
            }
        }
    }

    if (levelShowSlider) {
        float levelValue = static_cast<float>(levelNumber_);
        const float levelValueBefore = levelValue;
        const int sliderResult = GuiSliderBar(
            levelSliderBarBounds,
            "",
            "",
            &levelValue,
            static_cast<float>(kMinLevelNumber),
            static_cast<float>(kMaxLevelNumber));
        const int previousLevelNumber = levelNumber_;
        levelNumber_ = RoundToNearestInt(levelValue);
        if (levelValueBefore != levelValue || sliderResult != 0) {
            levelSliderRevealUntilTime_ = GetTime() + kMenuRevealRowDurationSeconds;
            interactionOccurred = true;
        }
        if (levelNumber_ != previousLevelNumber) {
            interactionOccurred = true;
        }

        // raygui's SliderBar applies SLIDER_PADDING vertically but not horizontally.
        ui::bolt_menu_slider::DrawInnerSideGapMasks(
            levelSliderBarBounds,
            ui::bolt_menu_slider::kBorderPx,
            ui::bolt_menu_slider::kPaddingPx,
            ui::bolt_menu_slider::kInnerSideGapPx);
    }

    const char* const debugMenuLine = TextFormat("Debug info: %s", debugInfoValue ? "On" : "Off");
    const int debugDrawW = MeasureText(debugMenuLine, kDebugInfoMenuFontPx);
    const float debugTextY =
        debugInfoY + (kMenuQuitButtonHeight - static_cast<float>(kDebugInfoMenuFontPx)) * 0.5F;
    DrawText(
        debugMenuLine,
        static_cast<int>(panelCenterX) - debugDrawW / 2,
        static_cast<int>(debugTextY),
        kDebugInfoMenuFontPx,
        kMenuFocusableItemColor);
    if (debugInfoValue != debugInfo_) {
        interactionOccurred = true;
    }
    debugInfo_ = debugInfoValue;

    bool quitPressed = GuiButton(quitButton, "Quit");
    if (quitConfirmationOpen_) {
        startPressed = false;
        quitPressed = false;
    } else if (startPressed || quitPressed) {
        interactionOccurred = true;
    }

    if (!quitConfirmationOpen_ && input.menuSelectPressed) {
        interactionOccurred = true;
        if (focusedControl_ == FocusedControl::Start) {
            startPressed = true;
            menuButtonActivatedViaMenuSelect = true;
        } else if (focusedControl_ == FocusedControl::Quit) {
            quitPressed = true;
            menuButtonActivatedViaMenuSelect = true;
        }
    }

    ui::primitives::DrawFocusRing(
        levelFocusFrame,
        !quitConfirmationOpen_ && !levelShowSlider && focusedControl_ == FocusedControl::Level);
    // Raylib draws rectangle lines *inward* from the expanded rect; gap from slider edge to stroke
    // is expandPx - lineThickness (see `DrawRectangleLinesEx` in raylib).
    constexpr float kLevelSliderFocusClearGapPx = 2.0F;
    constexpr float kLevelSliderFocusLinePx = 3.0F;
    constexpr float kLevelSliderFocusExpandPx = kLevelSliderFocusClearGapPx + kLevelSliderFocusLinePx;
    ui::primitives::DrawFocusRing(
        levelGauge,
        !quitConfirmationOpen_ && levelShowSlider && focusedControl_ == FocusedControl::Level,
        kLevelSliderFocusExpandPx,
        kLevelSliderFocusLinePx);
    ui::primitives::DrawFocusRing(
        densityFocusFrame,
        !quitConfirmationOpen_ && !densityShowSprites && focusedControl_ == FocusedControl::Density);
    for (int i = 0; i < kDensityHatchSpriteCount; ++i) {
        const bool densityRing = !quitConfirmationOpen_ && densityShowSprites &&
            (hoveredDensitySprite == i ||
             (hoveredDensitySprite < 0 && focusedControl_ == FocusedControl::Density && mazeDensity_ == i + 1));
        const Rectangle densityFocusBounds = {
            densitySpriteDests[i].x - 2.0F,
            densitySpriteDests[i].y - 2.0F,
            densitySpriteDests[i].width + 2.0F,
            densitySpriteDests[i].height + 2.0F,
        };
        ui::primitives::DrawFocusRing(densityFocusBounds, densityRing);
    }
    const Rectangle startFocusFrame = {
        unifiedMenuFocusX,
        startButtonY,
        unifiedMenuFocusW,
        kMenuQuitButtonHeight,
    };
    const Rectangle quitFocusFrame = {
        unifiedMenuFocusX,
        quitButtonY,
        unifiedMenuFocusW,
        kMenuQuitButtonHeight,
    };
    ui::primitives::DrawFocusRing(
        debugInfoControl,
        !quitConfirmationOpen_ &&
            (focusedControl_ == FocusedControl::DebugInfo || debugInfoHovered));
    ui::primitives::DrawFocusRing(
        startFocusFrame, !quitConfirmationOpen_ && focusedControl_ == FocusedControl::Start);
    ui::primitives::DrawFocusRing(
        quitFocusFrame, !quitConfirmationOpen_ && focusedControl_ == FocusedControl::Quit);

    if (quitPressed) {
        quitConfirmationOpen_ = true;
        quitConfirmationDialog_.Open(ConfirmationDialog::Focus::Cancel);
        interactionOccurred = true;
    }

    bool confirmQuitPressed = false;
    if (quitConfirmationOpen_) {
        const float dialogWidth = std::min(400.0F, static_cast<float>(config.screenWidth) - 48.0F);
        const float dialogHeight = 150.0F;
        const Rectangle dialog = {
            .x = (static_cast<float>(config.screenWidth) - dialogWidth) * 0.5F,
            .y = (static_cast<float>(config.screenHeight) - dialogHeight) * 0.5F,
            .width = dialogWidth,
            .height = dialogHeight,
        };
        const ConfirmationDialogResult modalResult = quitConfirmationDialog_.Render(
            ConfirmationDialog::Spec{
                .bounds = dialog,
                .message = "Are you sure want to quit",
                .confirmButtonLabel = "Quit",
                .cancelButtonLabel = "Cancel",
            },
            input);
        if (modalResult.interactionOccurred) {
            interactionOccurred = true;
        }
        if (modalResult.buttonActivatedViaMenuSelect) {
            menuButtonActivatedViaMenuSelect = true;
        }

        if (modalResult.confirmPressed) {
            confirmQuitPressed = true;
            quitConfirmationOpen_ = false;
        } else if (modalResult.cancelPressed) {
            quitConfirmationOpen_ = false;
        }
    }

    GuiLoadStyleDefault();

    return MenuScreenResult{
        .startGameRequested = startPressed,
        .quitRequested = confirmQuitPressed,
        .interactionOccurred = interactionOccurred,
        .menuButtonActivatedViaMenuSelect = menuButtonActivatedViaMenuSelect,
        .menuSettings =
            MenuSettings{
                .levelNumber = levelNumber_,
                .mazeDensity = mazeDensity_,
                .invisibility = currentSettings.invisibility,
                .debugInfo = debugInfo_,
            },
    };
}
