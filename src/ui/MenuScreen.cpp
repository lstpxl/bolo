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
#include "ui/UiPrimitives.h"
#include "raygui.h"
#include "raylib.h"

namespace {
constexpr int kMinLevelNumber = 1;
constexpr int kMaxLevelNumber = 9;
constexpr int kMinMazeDensity = 1;
constexpr int kMaxMazeDensity = 5;

MenuScreen::FocusedControl NextFocusedControl(MenuScreen::FocusedControl current) {
    if (current == MenuScreen::FocusedControl::Quit) {
        return MenuScreen::FocusedControl::Level;
    }
    return static_cast<MenuScreen::FocusedControl>(static_cast<int>(current) + 1);
}

MenuScreen::FocusedControl PreviousFocusedControl(MenuScreen::FocusedControl current) {
    if (current == MenuScreen::FocusedControl::Level) {
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
constexpr int kMenuTitleX = 10;
constexpr int kMenuTitleY = 10;
constexpr int kBetaLabelFontBaseSize = 16;
constexpr float kBetaLabelRenderSize = 16.0F;
constexpr float kBetaLabelYOffset = 10.0F;
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
    return titleFontLoaded_ || betaFontLoaded_;
}

void MenuScreen::UnloadResources() {
    if (!titleFontLoaded_) {
    } else {
        UnloadFont(titleFont_);
        titleFont_ = Font{};
        titleFontLoaded_ = false;
    }
    if (!betaFontLoaded_) {
        return;
    }
    UnloadFont(betaFont_);
    betaFont_ = Font{};
    betaFontLoaded_ = false;
}

MenuScreenResult MenuScreen::Render(
    const MenuSettings& currentSettings,
    const AppConfig& config,
    const FrameInput& input) {
    levelNumber_ = std::clamp(currentSettings.levelNumber, kMinLevelNumber, kMaxLevelNumber);
    mazeDensity_ = std::clamp(currentSettings.mazeDensity, kMinMazeDensity, kMaxMazeDensity);
    debugInfo_ = currentSettings.debugInfo;
    bool interactionOccurred = false;

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

    const float panelWidth = std::min(440.0F, static_cast<float>(config.screenWidth) - 24.0F);
    const float panelHeight = static_cast<float>(config.screenHeight) - 24.0F;
    const Rectangle panel = {
        .x = (static_cast<float>(config.screenWidth) - panelWidth) * 0.5F,
        .y = (static_cast<float>(config.screenHeight) - panelHeight) * 0.5F,
        .width = panelWidth,
        .height = panelHeight,
    };
    const float panelCenterX = panel.x + panel.width * 0.5F;
    const float panelInnerPaddingX = 24.0F;
    const float controlsPaddingX = panelInnerPaddingX;
    const float controlsWidth = panel.width - controlsPaddingX * 2.0F;
    const float gaugeWidth = std::min(320.0F, controlsWidth);
    const float gaugeX = panelCenterX - gaugeWidth * 0.5F;
    const float buttonsX = panelCenterX - controlsWidth * 0.5F;

    const float titleY = panel.y + 20.0F;
    const float subtitleY = titleY + 46.0F;
    const float levelLabelY = subtitleY + 44.0F;
    const float levelGaugeY = levelLabelY + 28.0F;
    const float densityLabelY = levelGaugeY + 32.0F;
    const float densityGaugeY = densityLabelY + 28.0F;
    const float debugInfoY = densityGaugeY + 38.0F;
    const float buildTextY = panel.y + panel.height - 20.0F;
    const float quitButtonY = buildTextY - 38.0F - 60.0F;
    const float startButtonY = quitButtonY - 60.0F;

    DrawRectangleRounded(panel, 0.05F, 8, Color{38, 45, 58, 240});
    if (titleFontLoaded_) {
        DrawTextEx(
            titleFont_,
            "Bolt",
            Vector2{static_cast<float>(kMenuTitleX), static_cast<float>(kMenuTitleY)},
            kMenuTitleRenderSize,
            0.0F,
            YELLOW);
    } else {
        DrawText("Bolt", kMenuTitleX, kMenuTitleY, 20, YELLOW);
    }
    const float betaLabelY = static_cast<float>(kMenuTitleY) + kMenuTitleRenderSize + kBetaLabelYOffset;
    if (betaFontLoaded_) {
        DrawTextEx(
            betaFont_,
            "Beta Version",
            Vector2{static_cast<float>(kMenuTitleX), betaLabelY},
            kBetaLabelRenderSize,
            0.0F,
            GRAY);
    } else {
        DrawText("Beta Version", kMenuTitleX, static_cast<int>(betaLabelY), 20, GRAY);
    }
    const int titleFontSize = 40;
    DrawText(
        "BOLT",
        static_cast<int>(panelCenterX) - MeasureText("BOLT", titleFontSize) / 2,
        static_cast<int>(titleY),
        titleFontSize,
        RAYWHITE);
    DrawText(
        TextFormat("Build #%d", CurrentBuildNumber()),
        static_cast<int>(panel.x + controlsPaddingX),
        static_cast<int>(buildTextY),
        10,
        GRAY);

    const int previousTextSize = GuiGetStyle(DEFAULT, TEXT_SIZE);
    GuiSetStyle(DEFAULT, TEXT_SIZE, 20);

    const char* subtitle = "Select level (1-9) and density (1-5)";
    DrawText(
        subtitle,
        static_cast<int>(panelCenterX) - MeasureText(subtitle, 20) / 2,
        static_cast<int>(subtitleY),
        20,
        LIGHTGRAY);

    const Rectangle levelGauge = Rectangle{gaugeX, levelGaugeY, gaugeWidth, 28.0F};
    const Rectangle densityGauge = Rectangle{gaugeX, densityGaugeY, gaugeWidth, 28.0F};
    const Rectangle debugInfoControl = Rectangle{gaugeX + 8.0F, debugInfoY, 28.0F, 28.0F};
    const Rectangle startButton = Rectangle{buttonsX, startButtonY, controlsWidth, 30.0F};
    const Rectangle quitButton = Rectangle{buttonsX, quitButtonY, controlsWidth, 30.0F};

    float levelValue = static_cast<float>(levelNumber_);
    DrawText(
        "Level",
        static_cast<int>(panelCenterX) - MeasureText("Level", 20) / 2,
        static_cast<int>(levelLabelY),
        20,
        LIGHTGRAY);
    if (!quitConfirmationOpen_ && focusedControl_ == FocusedControl::Level) {
        if (input.menuNavigateLeftPressed) {
            levelValue = std::max(static_cast<float>(kMinLevelNumber), levelValue - 1.0F);
            interactionOccurred = true;
        }
        if (input.menuNavigateRightPressed) {
            levelValue = std::min(static_cast<float>(kMaxLevelNumber), levelValue + 1.0F);
            interactionOccurred = true;
        }
    }
    const int previousLevelNumber = levelNumber_;
    GuiSliderBar(
        levelGauge,
        "",
        TextFormat("%d", levelNumber_),
        &levelValue,
        static_cast<float>(kMinLevelNumber),
        static_cast<float>(kMaxLevelNumber));
    levelNumber_ = RoundToNearestInt(levelValue);
    if (levelNumber_ != previousLevelNumber) {
        interactionOccurred = true;
    }

    float densityValue = static_cast<float>(mazeDensity_);
    DrawText(
        "Density",
        static_cast<int>(panelCenterX) - MeasureText("Density", 20) / 2,
        static_cast<int>(densityLabelY),
        20,
        LIGHTGRAY);
    if (!quitConfirmationOpen_ && focusedControl_ == FocusedControl::Density) {
        if (input.menuNavigateLeftPressed) {
            densityValue = std::max(static_cast<float>(kMinMazeDensity), densityValue - 1.0F);
            interactionOccurred = true;
        }
        if (input.menuNavigateRightPressed) {
            densityValue = std::min(static_cast<float>(kMaxMazeDensity), densityValue + 1.0F);
            interactionOccurred = true;
        }
    }
    const int previousDensity = mazeDensity_;
    GuiSliderBar(
        densityGauge,
        "",
        TextFormat("%d", mazeDensity_),
        &densityValue,
        static_cast<float>(kMinMazeDensity),
        static_cast<float>(kMaxMazeDensity));
    mazeDensity_ = RoundToNearestInt(densityValue);
    if (mazeDensity_ != previousDensity) {
        interactionOccurred = true;
    }

    bool debugInfoValue = debugInfo_;
    if (!quitConfirmationOpen_ && focusedControl_ == FocusedControl::DebugInfo) {
        if (input.menuNavigateLeftPressed || input.menuNavigateRightPressed || input.menuSelectPressed) {
            debugInfoValue = !debugInfoValue;
            interactionOccurred = true;
        }
    }
    GuiCheckBox(debugInfoControl, "Debug info", &debugInfoValue);
    if (debugInfoValue != debugInfo_) {
        interactionOccurred = true;
    }
    debugInfo_ = debugInfoValue;

    bool startPressed = GuiButton(startButton, "Start");
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
        } else if (focusedControl_ == FocusedControl::Quit) {
            quitPressed = true;
        }
    }

    ui::primitives::DrawFocusRing(levelGauge, !quitConfirmationOpen_ && focusedControl_ == FocusedControl::Level);
    ui::primitives::DrawFocusRing(densityGauge, !quitConfirmationOpen_ && focusedControl_ == FocusedControl::Density);
    ui::primitives::DrawFocusRing(debugInfoControl, !quitConfirmationOpen_ && focusedControl_ == FocusedControl::DebugInfo);
    ui::primitives::DrawFocusRing(startButton, !quitConfirmationOpen_ && focusedControl_ == FocusedControl::Start);
    ui::primitives::DrawFocusRing(quitButton, !quitConfirmationOpen_ && focusedControl_ == FocusedControl::Quit);

    if (quitPressed) {
        quitConfirmationOpen_ = true;
        quitConfirmationDialog_.Open(ConfirmationDialog::Focus::Cancel);
        interactionOccurred = true;
    }

    bool confirmQuitPressed = false;
    if (quitConfirmationOpen_) {
        ui::primitives::DrawModalBackdrop(config.screenWidth, config.screenHeight);

        const Rectangle dialog = {
            .x = panel.x + 40.0F,
            .y = panel.y + 116.0F,
            .width = panel.width - 80.0F,
            .height = 150.0F,
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

        if (modalResult.confirmPressed) {
            confirmQuitPressed = true;
            quitConfirmationOpen_ = false;
        } else if (modalResult.cancelPressed) {
            quitConfirmationOpen_ = false;
        }
    }

    GuiSetStyle(DEFAULT, TEXT_SIZE, previousTextSize);

    return MenuScreenResult{
        .startGameRequested = startPressed,
        .quitRequested = confirmQuitPressed,
        .interactionOccurred = interactionOccurred,
        .menuSettings =
            MenuSettings{
                .levelNumber = levelNumber_,
                .mazeDensity = mazeDensity_,
                .invisibility = currentSettings.invisibility,
                .debugInfo = debugInfo_,
            },
    };
}
