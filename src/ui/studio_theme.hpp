#pragma once

#include "ui/ui_types.hpp"
#include "imgui.h"

namespace lps {

namespace StudioColors {
    // Authentic Adobe Illustrator CC + Blender 4.5.9 LTS Neutral Charcoal Palette
    inline constexpr ImU32 CanvasTrough     = IM_COL32(0x18, 0x18, 0x18, 0xFF); // #181818
    inline constexpr ImU32 UvDesk           = IM_COL32(0x2C, 0x2C, 0x2C, 0xFF); // #2C2C2C Illustrator artboard surround
    inline constexpr ImU32 HeaderBg         = IM_COL32(0x2E, 0x2E, 0x2E, 0xFF); // #2E2E2E Unified top header
    inline constexpr ImU32 MenuBarBg        = IM_COL32(0x2E, 0x2E, 0x2E, 0xFF); // #2E2E2E
    inline constexpr ImU32 PanelHeader      = IM_COL32(0x2B, 0x2B, 0x2B, 0xFF); // #2B2B2B
    inline constexpr ImU32 PanelBody        = IM_COL32(0x32, 0x32, 0x32, 0xFF); // #323232 Soft Illustrator charcoal dock
    inline constexpr ImU32 SubPanelHeader   = IM_COL32(0x37, 0x37, 0x37, 0xFF); // #373737
    inline constexpr ImU32 ControlBg        = IM_COL32(0x3A, 0x3A, 0x3A, 0xFF); // #3A3A3A Flat Illustrator button bg
    inline constexpr ImU32 InputFieldBg     = IM_COL32(0x26, 0x26, 0x26, 0xFF); // #262626 Recessed numeric/text input well
    inline constexpr ImU32 ControlHover     = IM_COL32(0x46, 0x46, 0x46, 0xFF); // #464646
    inline constexpr ImU32 ControlActive    = IM_COL32(0x24, 0x24, 0x24, 0xFF); // #242424 Inset active tool well
    inline constexpr ImU32 ToolActiveBg     = ControlActive;
    inline constexpr ImU32 Border           = IM_COL32(0x22, 0x22, 0x22, 0xFF); // #222222 Structural divider
    inline constexpr ImU32 BorderLight      = IM_COL32(0x4E, 0x4E, 0x4E, 0xFF); // #4E4E4E 1px hairline button border

    // Creative Coatroom Brand Accent (racing vermilion - distinct from Adobe orange) + Blender accents
    inline constexpr ImU32 BrandAccent      = IM_COL32(0xD9, 0x42, 0x34, 0xFF); // #D94234
    inline constexpr ImU32 BrandAccentHover = IM_COL32(0xE6, 0x53, 0x44, 0xFF); // #E65344
    inline constexpr ImU32 StudioBlue       = IM_COL32(0x47, 0x72, 0xB3, 0xFF); // #4772B3
    inline constexpr ImU32 StudioBlueBg     = IM_COL32(0x38, 0x55, 0x82, 0xFF); // #385582
    inline constexpr ImU32 AmberAccent      = IM_COL32(0xD9, 0x82, 0x2B, 0xFF); // #D9822B
    inline constexpr ImU32 AmberAccentBg    = IM_COL32(0x4D, 0x35, 0x1B, 0xFF);
    inline constexpr ImU32 GuideCyan        = IM_COL32(0x00, 0xB8, 0xFF, 0xFF); // #00B8FF Bright cyan ruler guide

    inline constexpr ImU32 TextPrimary    = IM_COL32(0xE0, 0xE0, 0xE0, 0xFF); // #E0E0E0
    inline constexpr ImU32 TextSecondary  = IM_COL32(0xA8, 0xA8, 0xA8, 0xFF); // #A8A8A8
    inline constexpr ImU32 TextMuted      = IM_COL32(0x78, 0x78, 0x78, 0xFF); // #787878

    inline constexpr ImVec4 ToVec4(ImU32 c) {
        return ImVec4(
            ((c >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f,
            ((c >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f,
            ((c >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f,
            ((c >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f
        );
    }
} // namespace StudioColors

enum class SmallIconKind {
    EyeOpen = 0,
    EyeClosed,
    Locked,
    Unlocked,
    FitView,
    Grid,
    Wireframe,
    Symmetry,
    ColorWheel,
    MaterialSphere,
    Lights,
    Outliner,
    SwapArrows,
    Camera,
    OrthoGrid,
    WindowMin,
    WindowMax,
    WindowRestore,
    WindowClose,
    AlignLeft,
    AlignCenterH,
    AlignRight,
    Home,
    Templates,
    ViewGrid,
    ViewList
};

void applyStudioTheme(float dpiScale = 1.0f);
void loadStudioFonts(ImGuiIO& io, float fontSizePx = 14.0f);

inline constexpr int kRichTooltipMirrorIdx = static_cast<int>(StudioTool::COUNT);

void drawAppMonogramLogo(ImDrawList* dl, const ImVec2& minP, const ImVec2& maxP);
void drawToolIcon(ImDrawList* dl, StudioTool tool, const ImVec2& minP, const ImVec2& maxP, ImU32 color, bool active);
void drawSmallIcon(ImDrawList* dl, SmallIconKind kind, const ImVec2& minP, const ImVec2& maxP, ImU32 color);
void drawRichTooltipCard(ImDrawList* dl, const ImVec2& pos, int toolOrMirrorIdx, double animTimeSec);

const char* toolDisplayName(StudioTool tool);
const char* toolShortcutHint(StudioTool tool);
const char* toolRichDescription(int toolOrMirrorIdx);

} // namespace lps
