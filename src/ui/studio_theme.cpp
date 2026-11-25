#define IMGUI_DEFINE_MATH_OPERATORS
#include "ui/studio_theme.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace lps {

namespace {

inline ImVec2 glyphPt(const ImVec2& minP, const ImVec2& size, float u, float v) {
    return ImVec2(minP.x + u * size.x, minP.y + v * size.y);
}

inline void addArrowHead(ImDrawList* dl, ImVec2 tip, ImVec2 dir, float len, float halfW, ImU32 col) {
    float l = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    if (l < 1e-5f) return;
    ImVec2 d(dir.x / l, dir.y / l);
    ImVec2 n(-d.y, d.x);
    ImVec2 base(tip.x - d.x * len, tip.y - d.y * len);
    dl->AddTriangleFilled(tip, ImVec2(base.x + n.x * halfW, base.y + n.y * halfW),
                          ImVec2(base.x - n.x * halfW, base.y - n.y * halfW), col);
}

} // namespace

void loadStudioFonts(ImGuiIO& io, float fontSizePx) {
    const char* fontCandidates[] = {
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "C:\\Windows\\Fonts\\tahoma.ttf",
        "C:\\Windows\\Fonts\\arial.ttf"
    };
    for (const char* path : fontCandidates) {
        if (std::filesystem::exists(path)) {
            ImFont* f = io.Fonts->AddFontFromFileTTF(path, fontSizePx);
            if (f) {
                io.FontDefault = f;
                return;
            }
        }
    }
    io.Fonts->AddFontDefault();
}

void applyStudioTheme(float dpiScale) {
    ImGuiStyle& s = ImGui::GetStyle();

    // Clean Blender 4.5.9 + Adobe Illustrator CC geometry (soft flat pills, zero noisy boxed grids)
    s.WindowPadding        = ImVec2(6.0f, 5.0f);
    s.FramePadding         = ImVec2(6.0f, 3.0f);
    s.CellPadding          = ImVec2(4.0f, 2.0f);
    s.ItemSpacing          = ImVec2(5.0f, 4.0f);
    s.ItemInnerSpacing     = ImVec2(4.0f, 3.0f);
    s.IndentSpacing        = 12.0f;
    s.ScrollbarSize        = 9.0f;
    s.GrabMinSize          = 8.0f;

    // Crisp 1px #4E4E4E hairline Illustrator button/input borders + panel dividers
    s.WindowBorderSize     = 1.0f;
    s.ChildBorderSize      = 0.0f;
    s.PopupBorderSize      = 1.0f;
    s.FrameBorderSize      = 1.0f;
    s.TabBorderSize        = 0.0f;
    s.TabBarBorderSize     = 1.0f;
    s.TabBarOverlineSize   = 2.0f;
    s.DockingSeparatorSize = 2.0f;

    s.WindowRounding       = 2.0f;
    s.ChildRounding        = 2.0f;
    s.FrameRounding        = 3.0f;
    s.PopupRounding        = 3.0f;
    s.ScrollbarRounding    = 3.0f;
    s.GrabRounding         = 2.0f;
    s.TabRounding          = 3.0f;
    s.MenuItemRounding     = 2.0f;

    s.WindowTitleAlign     = ImVec2(0.0f, 0.5f);
    s.ButtonTextAlign      = ImVec2(0.5f, 0.5f);
    s.ColorButtonPosition  = ImGuiDir_Right;
    s.ColorMarkerSize      = 0.0f;
    s.DockingNodeHasCloseButton = false;

    if (dpiScale > 0.5f && std::fabs(dpiScale - 1.0f) > 0.01f) {
        s.ScaleAllSizes(dpiScale);
    }

    ImVec4* c = s.Colors;
    const ImVec4 trough   = StudioColors::ToVec4(StudioColors::CanvasTrough);
    const ImVec4 panel    = StudioColors::ToVec4(StudioColors::PanelBody);
    const ImVec4 header   = StudioColors::ToVec4(StudioColors::PanelHeader);
    const ImVec4 subHdr   = StudioColors::ToVec4(StudioColors::SubPanelHeader);
    const ImVec4 ctrl     = StudioColors::ToVec4(StudioColors::ControlBg);
    const ImVec4 hover    = StudioColors::ToVec4(StudioColors::ControlHover);
    const ImVec4 active   = StudioColors::ToVec4(StudioColors::ControlActive);
    const ImVec4 border   = StudioColors::ToVec4(StudioColors::BorderLight);
    const ImVec4 blue     = StudioColors::ToVec4(StudioColors::StudioBlue);
    const ImVec4 blueBg   = StudioColors::ToVec4(StudioColors::StudioBlueBg);
    const ImVec4 textPri  = StudioColors::ToVec4(StudioColors::TextPrimary);
    const ImVec4 textMuted= StudioColors::ToVec4(StudioColors::TextMuted);

    c[ImGuiCol_Text]                    = textPri;
    c[ImGuiCol_TextDisabled]            = textMuted;
    c[ImGuiCol_WindowBg]                = panel;
    c[ImGuiCol_ChildBg]                 = panel;
    c[ImGuiCol_PopupBg]                 = StudioColors::ToVec4(IM_COL32(0x2B, 0x2B, 0x2B, 0xFF));
    c[ImGuiCol_Border]                  = border;
    c[ImGuiCol_BorderShadow]            = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_FrameBg]                 = StudioColors::ToVec4(StudioColors::InputFieldBg);
    c[ImGuiCol_FrameBgHovered]          = StudioColors::ToVec4(StudioColors::ControlBg);
    c[ImGuiCol_FrameBgActive]           = active;
    c[ImGuiCol_TitleBg]                 = header;
    c[ImGuiCol_TitleBgActive]           = header;
    c[ImGuiCol_TitleBgCollapsed]        = trough;
    c[ImGuiCol_MenuBarBg]               = StudioColors::ToVec4(StudioColors::MenuBarBg);
    c[ImGuiCol_ScrollbarBg]             = StudioColors::ToVec4(IM_COL32(0x1E, 0x1E, 0x1E, 0xFF));
    c[ImGuiCol_ScrollbarGrab]           = ImVec4(0.28f, 0.28f, 0.28f, 1.0f);
    c[ImGuiCol_ScrollbarGrabHovered]    = ImVec4(0.38f, 0.38f, 0.38f, 1.0f);
    c[ImGuiCol_ScrollbarGrabActive]     = blue;
    c[ImGuiCol_CheckMark]               = textPri;
    c[ImGuiCol_CheckboxSelectedBg]      = blue;
    c[ImGuiCol_SliderGrab]              = ImVec4(0.45f, 0.45f, 0.45f, 1.0f);
    c[ImGuiCol_SliderGrabActive]        = blue;
    c[ImGuiCol_Button]                  = ctrl;
    c[ImGuiCol_ButtonHovered]           = hover;
    c[ImGuiCol_ButtonActive]            = active;
    c[ImGuiCol_Header]                  = subHdr;
    c[ImGuiCol_HeaderHovered]           = hover;
    c[ImGuiCol_HeaderActive]            = blueBg;
    c[ImGuiCol_Separator]               = border;
    c[ImGuiCol_SeparatorHovered]        = StudioColors::ToVec4(StudioColors::BorderLight);
    c[ImGuiCol_SeparatorActive]         = blue;
    c[ImGuiCol_ResizeGrip]              = ImVec4(0.26f, 0.26f, 0.26f, 0.5f);
    c[ImGuiCol_ResizeGripHovered]       = blue;
    c[ImGuiCol_ResizeGripActive]        = blue;
    c[ImGuiCol_InputTextCursor]         = textPri;
    c[ImGuiCol_Tab]                     = header;
    c[ImGuiCol_TabHovered]              = hover;
    c[ImGuiCol_TabSelected]             = panel;
    c[ImGuiCol_TabSelectedOverline]     = blue;
    c[ImGuiCol_TabDimmed]               = header;
    c[ImGuiCol_TabDimmedSelected]       = panel;
    c[ImGuiCol_TabDimmedSelectedOverline]= ImVec4(0.32f, 0.32f, 0.32f, 1.0f);
    c[ImGuiCol_DockingPreview]          = ImVec4(blue.x, blue.y, blue.z, 0.32f);
    c[ImGuiCol_DockingEmptyBg]          = trough;
    c[ImGuiCol_PlotLines]               = blue;
    c[ImGuiCol_PlotLinesHovered]        = StudioColors::ToVec4(StudioColors::AmberAccent);
    c[ImGuiCol_PlotHistogram]           = blue;
    c[ImGuiCol_PlotHistogramHovered]    = StudioColors::ToVec4(StudioColors::AmberAccent);
    c[ImGuiCol_TableHeaderBg]           = header;
    c[ImGuiCol_TableBorderStrong]       = border;
    c[ImGuiCol_TableBorderLight]        = ImVec4(0.20f, 0.20f, 0.20f, 1.0f);
    c[ImGuiCol_TableRowBg]              = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_TableRowBgAlt]           = ImVec4(1.0f, 1.0f, 1.0f, 0.02f);
    c[ImGuiCol_TextLink]                = blue;
    c[ImGuiCol_TextSelectedBg]          = ImVec4(blue.x, blue.y, blue.z, 0.35f);
    c[ImGuiCol_DragDropTarget]          = StudioColors::ToVec4(StudioColors::AmberAccent);
    c[ImGuiCol_NavCursor]               = blue;
    c[ImGuiCol_ModalWindowDimBg]        = ImVec4(0.05f, 0.05f, 0.05f, 0.62f);
}

void drawAppMonogramLogo(ImDrawList* dl, const ImVec2& minP, const ImVec2& maxP) {
    // Coatroom v1.2 Mathematical Adobe Creative Cloud Squircle (|2x-1|^4.4 + |2y-1|^4.4 <= 1.0)
    // Midnight emerald-teal #052228 -> #041B20 with crisp bold #2FE6CE "Cr" (no outer glow, no brackets)
    ImVec2 sz = maxP - minP;
    ImVec2 ctr((minP.x + maxP.x) * 0.5f, (minP.y + maxP.y) * 0.5f);
    ImVec2 half(sz.x * 0.5f, sz.y * 0.5f);
    auto P = [&](float u, float v) { return glyphPt(minP, sz, u, v); };

    const float invExp = 2.0f / 4.4f;
    dl->PathClear();
    constexpr int N = 64;
    for (int i = 0; i < N; ++i) {
        float ang = (static_cast<float>(i) / N) * 6.2831853f;
        float ca = std::cos(ang), sa = std::sin(ang);
        float px = ctr.x + half.x * (ca >= 0.0f ? 1.0f : -1.0f) * std::pow(std::fabs(ca), invExp);
        float py = ctr.y + half.y * (sa >= 0.0f ? 1.0f : -1.0f) * std::pow(std::fabs(sa), invExp);
        dl->PathLineTo(ImVec2(px, py));
    }
    dl->PathFillConvex(IM_COL32(0x05, 0x22, 0x28, 0xFF));

    // Bottom subtle vertical shade inside squircle toward #041B20
    dl->PathClear();
    for (int i = 0; i <= N / 2; ++i) {
        float ang = (static_cast<float>(i) / (N / 2)) * 3.1415926f;
        float ca = std::cos(ang), sa = std::sin(ang);
        float px = ctr.x + half.x * 0.96f * (ca >= 0.0f ? 1.0f : -1.0f) * std::pow(std::fabs(ca), invExp);
        float py = ctr.y + half.y * 0.96f * std::pow(std::fabs(sa), invExp);
        dl->PathLineTo(ImVec2(px, py));
    }
    dl->PathFillConvex(IM_COL32(0x04, 0x1B, 0x20, 0xFF));

    const ImU32 tealGlyph = IM_COL32(0x2F, 0xE6, 0xCE, 0xFF);
    float sw = std::max(2.0f, sz.x * 0.115f);
    // Bold geometric 'C'
    dl->AddBezierCubic(P(0.51f, 0.30f), P(0.16f, 0.22f), P(0.16f, 0.78f), P(0.51f, 0.70f), tealGlyph, sw, 18);
    // Crisp bold lowercase 'r'
    dl->AddLine(P(0.61f, 0.41f), P(0.61f, 0.72f), tealGlyph, sw);
    dl->AddBezierCubic(P(0.61f, 0.49f), P(0.66f, 0.39f), P(0.76f, 0.39f), P(0.83f, 0.45f), tealGlyph, sw * 0.94f, 10);
}

void drawToolIcon(ImDrawList* dl, StudioTool tool, const ImVec2& minP, const ImVec2& maxP, ImU32 color, bool active) {
    ImVec2 fullSz = maxP - minP;
    float pad = std::round(std::min(fullSz.x, fullSz.y) * 0.20f);
    ImVec2 gMin(std::round(minP.x + pad), std::round(minP.y + pad));
    ImVec2 gMax(std::round(maxP.x - pad), std::round(maxP.y - pad));
    ImVec2 gSz = gMax - gMin;
    auto P = [&](float u, float v) { return glyphPt(gMin, gSz, u, v); };
    float t = 1.4f;

    switch (tool) {
    case StudioTool::SelectMove: {
        // Solid filled selection arrow (Illustrator Selection Tool V)
        ImVec2 pts[] = {
            P(0.24f, 0.12f), P(0.24f, 0.80f), P(0.42f, 0.62f),
            P(0.58f, 0.88f), P(0.68f, 0.82f), P(0.52f, 0.56f), P(0.76f, 0.54f)
        };
        dl->AddConvexPolyFilled(pts, 4, color);
        dl->AddLine(P(0.46f, 0.58f), P(0.62f, 0.84f), color, 2.2f);
        dl->AddPolyline(pts, 7, color, 1.2f, ImDrawFlags_Closed);
        break;
    }
    case StudioTool::NodeEdit: {
        // Hollow outlined direct selection arrow (Illustrator Direct Selection A)
        ImVec2 pts[] = {
            P(0.24f, 0.12f), P(0.24f, 0.80f), P(0.42f, 0.62f),
            P(0.58f, 0.88f), P(0.68f, 0.82f), P(0.52f, 0.56f), P(0.76f, 0.54f)
        };
        dl->AddPolyline(pts, 7, color, 1.4f, ImDrawFlags_Closed);
        dl->AddRectFilled(P(0.72f, 0.16f), P(0.86f, 0.30f), color);
        break;
    }
    case StudioTool::Orbit: {
        ImVec2 c = P(0.50f, 0.50f);
        float r = gSz.x * 0.35f;
        dl->AddCircle(c, r, color, 20, 1.3f);
        dl->AddEllipse(c, ImVec2(r, r * 0.36f), color, 0.0f, 20, 1.2f);
        dl->AddEllipse(c, ImVec2(r * 0.38f, r), color, 0.0f, 20, 1.0f);
        addArrowHead(dl, P(0.92f, 0.50f), ImVec2(0.4f, 1.0f), 4.0f, 2.4f, color);
        break;
    }
    case StudioTool::Brush: {
        dl->AddLine(P(0.84f, 0.14f), P(0.44f, 0.54f), color, 2.1f);
        dl->AddLine(P(0.39f, 0.49f), P(0.49f, 0.59f), color, 2.2f);
        ImVec2 tip[] = { P(0.44f, 0.54f), P(0.33f, 0.61f), P(0.26f, 0.75f), P(0.14f, 0.86f), P(0.32f, 0.81f), P(0.41f, 0.68f) };
        dl->AddConvexPolyFilled(tip, 6, color);
        break;
    }
    case StudioTool::StraightRuler: {
        ImVec2 poly[] = { P(0.10f, 0.68f), P(0.68f, 0.10f), P(0.90f, 0.32f), P(0.32f, 0.90f) };
        dl->AddPolyline(poly, 4, color, t, ImDrawFlags_Closed);
        for (int i = 1; i <= 4; ++i) {
            float f = i * 0.2f;
            ImVec2 a = P(0.10f + 0.58f * f, 0.68f - 0.58f * f);
            ImVec2 b = ImVec2(a.x + gSz.x * 0.10f, a.y + gSz.y * 0.10f);
            dl->AddLine(a, b, color, 1.2f);
        }
        break;
    }
    case StudioTool::CurveBlade: {
        // Illustrator Pen / Bezier Curve tool (P)
        ImVec2 p0 = P(0.16f, 0.80f), p1 = P(0.16f, 0.22f), p2 = P(0.84f, 0.78f), p3 = P(0.84f, 0.20f);
        dl->AddBezierCubic(p0, p1, p2, p3, color, 1.6f, 18);
        ImU32 faint = (color & 0x00FFFFFF) | 0x80000000;
        dl->AddLine(p0, P(0.16f, 0.38f), faint, 1.0f);
        dl->AddLine(p3, P(0.84f, 0.62f), faint, 1.0f);
        dl->AddRectFilled(p0 - ImVec2(2.0f, 2.0f), p0 + ImVec2(2.0f, 2.0f), color);
        dl->AddRectFilled(p3 - ImVec2(2.0f, 2.0f), p3 + ImVec2(2.0f, 2.0f), color);
        dl->AddCircle(P(0.16f, 0.38f), 1.8f, color, 8, 1.1f);
        break;
    }
    case StudioTool::RectBox: {
        // Rectangle / Racing Plate tool (U)
        dl->AddRect(P(0.16f, 0.24f), P(0.84f, 0.76f), color, 0.0f, 1.5f);
        float hs = 2.0f;
        for (ImVec2 c : {P(0.16f, 0.24f), P(0.84f, 0.24f), P(0.84f, 0.76f), P(0.16f, 0.76f)}) {
            dl->AddRectFilled(c - ImVec2(hs, hs), c + ImVec2(hs, hs), color);
        }
        break;
    }
    case StudioTool::Eraser: {
        ImVec2 top[] = { P(0.46f, 0.18f), P(0.84f, 0.44f), P(0.56f, 0.72f), P(0.18f, 0.46f) };
        ImVec2 bot[] = { P(0.18f, 0.46f), P(0.56f, 0.72f), P(0.44f, 0.84f), P(0.14f, 0.64f) };
        dl->AddPolyline(top, 4, color, t, ImDrawFlags_Closed);
        dl->AddConvexPolyFilled(bot, 4, color);
        dl->AddLine(P(0.12f, 0.86f), P(0.88f, 0.86f), color, 1.4f);
        break;
    }
    case StudioTool::FloodFill: {
        ImVec2 bucket[] = { P(0.42f, 0.22f), P(0.70f, 0.50f), P(0.42f, 0.78f), P(0.14f, 0.50f) };
        dl->AddPolyline(bucket, 4, color, t, ImDrawFlags_Closed);
        dl->AddLine(P(0.28f, 0.36f), P(0.48f, 0.16f), color, 1.3f);
        ImVec2 drop[] = { P(0.76f, 0.54f), P(0.84f, 0.74f), P(0.76f, 0.86f), P(0.68f, 0.74f) };
        dl->AddConvexPolyFilled(drop, 4, color);
        break;
    }
    case StudioTool::Eyedropper: {
        dl->AddLine(P(0.16f, 0.84f), P(0.58f, 0.42f), color, 1.7f);
        dl->AddLine(P(0.48f, 0.32f), P(0.68f, 0.52f), color, 1.9f);
        dl->AddLine(P(0.58f, 0.42f), P(0.78f, 0.22f), color, 3.4f);
        dl->AddCircleFilled(P(0.78f, 0.22f), 2.3f, color, 10);
        break;
    }
    case StudioTool::Text: {
        dl->AddLine(P(0.18f, 0.20f), P(0.82f, 0.20f), color, 1.9f);
        dl->AddLine(P(0.18f, 0.20f), P(0.18f, 0.31f), color, 1.5f);
        dl->AddLine(P(0.82f, 0.20f), P(0.82f, 0.31f), color, 1.5f);
        dl->AddLine(P(0.50f, 0.20f), P(0.50f, 0.82f), color, 1.9f);
        dl->AddLine(P(0.36f, 0.82f), P(0.64f, 0.82f), color, 1.6f);
        break;
    }
    case StudioTool::SponsorDecal: {
        dl->AddRect(P(0.12f, 0.20f), P(0.88f, 0.80f), color, 2.0f, 1.3f);
        ImVec2 c = P(0.50f, 0.51f);
        float rOut = gSz.x * 0.23f;
        float rIn  = gSz.x * 0.10f;
        ImVec2 star[10];
        for (int i = 0; i < 10; ++i) {
            float ang = -1.5707963f + i * 0.6283185f;
            float rad = (i % 2 == 0) ? rOut : rIn;
            star[i] = ImVec2(c.x + std::cos(ang) * rad, c.y + std::sin(ang) * rad);
        }
        dl->AddPolyline(star, 10, color, 1.2f, ImDrawFlags_Closed);
        break;
    }
    case StudioTool::VertexEdit: {
        ImVec2 a = P(0.50f, 0.18f), b = P(0.18f, 0.78f), c = P(0.82f, 0.78f);
        dl->AddTriangle(a, b, c, color, 1.2f);
        ImU32 nodeCol = active ? StudioColors::AmberAccent : color;
        float hs = 2.4f;
        dl->AddRectFilled(a - ImVec2(hs, hs), a + ImVec2(hs, hs), nodeCol);
        dl->AddRectFilled(b - ImVec2(hs, hs), b + ImVec2(hs, hs), nodeCol);
        dl->AddRectFilled(c - ImVec2(hs, hs), c + ImVec2(hs, hs), nodeCol);
        break;
    }
    case StudioTool::FacePaintMask: {
        ImVec2 q[] = { P(0.18f, 0.38f), P(0.56f, 0.16f), P(0.84f, 0.50f), P(0.46f, 0.82f) };
        ImU32 fillCol = active ? ((StudioColors::AmberAccent & 0x00FFFFFF) | 0x45000000) : ((color & 0x00FFFFFF) | 0x25000000);
        dl->AddConvexPolyFilled(q, 4, fillCol);
        dl->AddPolyline(q, 4, active ? StudioColors::AmberAccent : color, t, ImDrawFlags_Closed);
        dl->AddLine(P(0.32f, 0.48f), P(0.60f, 0.30f), color, 1.0f);
        dl->AddLine(P(0.40f, 0.64f), P(0.70f, 0.44f), color, 1.0f);
        break;
    }
    case StudioTool::HandPan: {
        // Clean geometric Hand / Pan icon (H)
        dl->AddRect(P(0.28f, 0.44f), P(0.72f, 0.84f), color, 3.0f, 1.4f);
        dl->AddLine(P(0.34f, 0.44f), P(0.34f, 0.22f), color, 1.5f);
        dl->AddLine(P(0.46f, 0.44f), P(0.46f, 0.14f), color, 1.5f);
        dl->AddLine(P(0.58f, 0.44f), P(0.58f, 0.18f), color, 1.5f);
        dl->AddLine(P(0.70f, 0.48f), P(0.70f, 0.26f), color, 1.5f);
        dl->AddLine(P(0.28f, 0.64f), P(0.16f, 0.48f), color, 1.5f);
        break;
    }
    case StudioTool::Zoom: {
        // Magnifying glass Zoom icon (Z)
        ImVec2 c = P(0.42f, 0.42f);
        float r = gSz.x * 0.26f;
        dl->AddCircle(c, r, color, 18, 1.5f);
        dl->AddLine(P(0.61f, 0.61f), P(0.86f, 0.86f), color, 2.2f);
        dl->AddLine(ImVec2(c.x - r * 0.45f, c.y), ImVec2(c.x + r * 0.45f, c.y), color, 1.2f);
        dl->AddLine(ImVec2(c.x, c.y - r * 0.45f), ImVec2(c.x, c.y + r * 0.45f), color, 1.2f);
        break;
    }
    case StudioTool::BeautyRender: {
        dl->AddRect(P(0.14f, 0.28f), P(0.86f, 0.80f), color, 2.0f, 1.4f);
        ImVec2 top[] = { P(0.34f, 0.28f), P(0.40f, 0.16f), P(0.60f, 0.16f), P(0.66f, 0.28f) };
        dl->AddPolyline(top, 4, color, 1.3f);
        ImVec2 c = P(0.50f, 0.54f);
        dl->AddCircle(c, gSz.x * 0.17f, color, 16, 1.4f);
        dl->AddCircleFilled(c, gSz.x * 0.06f, color, 10);
        break;
    }
    case StudioTool::TouchType: {
        ImVec2 bMin = P(0.16f, 0.18f), bMax = P(0.84f, 0.82f);
        dl->AddRect(bMin, bMax, color, 0.0f, 1.1f);
        float hs = 1.8f;
        dl->AddRectFilled(bMin - ImVec2(hs, hs), bMin + ImVec2(hs, hs), color);
        dl->AddRectFilled(ImVec2(bMax.x - hs, bMin.y - hs), ImVec2(bMax.x + hs, bMin.y + hs), color);
        dl->AddRectFilled(bMax - ImVec2(hs, hs), bMax + ImVec2(hs, hs), color);
        dl->AddRectFilled(ImVec2(bMin.x - hs, bMax.y - hs), ImVec2(bMin.x + hs, bMax.y + hs), color);
        dl->AddLine(P(0.32f, 0.34f), P(0.68f, 0.34f), color, 1.8f);
        dl->AddLine(P(0.50f, 0.34f), P(0.50f, 0.70f), color, 1.8f);
        dl->AddLine(P(0.41f, 0.70f), P(0.59f, 0.70f), color, 1.4f);
        break;
    }
    default:
        break;
    }
}

void drawSmallIcon(ImDrawList* dl, SmallIconKind kind, const ImVec2& minP, const ImVec2& maxP, ImU32 color) {
    ImVec2 sz = maxP - minP;
    auto P = [&](float u, float v) { return glyphPt(minP, sz, u, v); };
    switch (kind) {
    case SmallIconKind::EyeOpen: {
        ImVec2 c = P(0.50f, 0.50f);
        dl->AddBezierCubic(P(0.12f, 0.50f), P(0.32f, 0.20f), P(0.68f, 0.20f), P(0.88f, 0.50f), color, 1.3f, 10);
        dl->AddBezierCubic(P(0.88f, 0.50f), P(0.68f, 0.80f), P(0.32f, 0.80f), P(0.12f, 0.50f), color, 1.3f, 10);
        dl->AddCircleFilled(c, sz.x * 0.13f, color, 10);
        break;
    }
    case SmallIconKind::EyeClosed: {
        dl->AddBezierCubic(P(0.14f, 0.50f), P(0.34f, 0.72f), P(0.66f, 0.72f), P(0.86f, 0.50f), color, 1.2f, 10);
        dl->AddLine(P(0.18f, 0.22f), P(0.82f, 0.78f), color, 1.3f);
        break;
    }
    case SmallIconKind::Locked: {
        dl->AddRectFilled(P(0.24f, 0.46f), P(0.76f, 0.84f), color, 1.5f);
        dl->AddBezierCubic(P(0.32f, 0.46f), P(0.32f, 0.16f), P(0.68f, 0.16f), P(0.68f, 0.46f), color, 1.4f, 10);
        break;
    }
    case SmallIconKind::Unlocked: {
        dl->AddRect(P(0.24f, 0.48f), P(0.76f, 0.84f), color, 1.5f, 1.2f);
        dl->AddBezierCubic(P(0.32f, 0.48f), P(0.32f, 0.16f), P(0.68f, 0.16f), P(0.68f, 0.34f), color, 1.2f, 8);
        break;
    }
    case SmallIconKind::FitView: {
        dl->AddLine(P(0.15f, 0.35f), P(0.15f, 0.15f), color, 1.4f);
        dl->AddLine(P(0.15f, 0.15f), P(0.35f, 0.15f), color, 1.4f);
        dl->AddLine(P(0.65f, 0.15f), P(0.85f, 0.15f), color, 1.4f);
        dl->AddLine(P(0.85f, 0.15f), P(0.85f, 0.35f), color, 1.4f);
        dl->AddLine(P(0.85f, 0.65f), P(0.85f, 0.85f), color, 1.4f);
        dl->AddLine(P(0.85f, 0.85f), P(0.65f, 0.85f), color, 1.4f);
        dl->AddLine(P(0.35f, 0.85f), P(0.15f, 0.85f), color, 1.4f);
        dl->AddLine(P(0.15f, 0.85f), P(0.15f, 0.65f), color, 1.4f);
        break;
    }
    case SmallIconKind::Grid: {
        for (float f : {0.25f, 0.50f, 0.75f}) {
            dl->AddLine(P(f, 0.15f), P(f, 0.85f), color, 1.1f);
            dl->AddLine(P(0.15f, f), P(0.85f, f), color, 1.1f);
        }
        break;
    }
    case SmallIconKind::Wireframe: {
        dl->AddTriangle(P(0.50f, 0.16f), P(0.16f, 0.82f), P(0.84f, 0.82f), color, 1.2f);
        dl->AddLine(P(0.50f, 0.16f), P(0.50f, 0.82f), color, 1.0f);
        break;
    }
    case SmallIconKind::Symmetry: {
        dl->AddLine(P(0.50f, 0.12f), P(0.50f, 0.88f), color, 1.3f);
        dl->AddTriangleFilled(P(0.16f, 0.50f), P(0.40f, 0.28f), P(0.40f, 0.72f), color);
        dl->AddTriangle(P(0.84f, 0.50f), P(0.60f, 0.28f), P(0.60f, 0.72f), color, 1.2f);
        break;
    }
    case SmallIconKind::ColorWheel: {
        ImVec2 c = P(0.50f, 0.50f);
        float r = sz.x * 0.34f;
        dl->AddCircle(c, r, color, 16, 1.4f);
        dl->AddCircleFilled(P(0.38f, 0.42f), sz.x * 0.08f, IM_COL32(220, 80, 80, 220), 8);
        dl->AddCircleFilled(P(0.62f, 0.42f), sz.x * 0.08f, IM_COL32(90, 200, 110, 220), 8);
        dl->AddCircleFilled(P(0.50f, 0.64f), sz.x * 0.08f, IM_COL32(85, 145, 235, 220), 8);
        break;
    }
    case SmallIconKind::MaterialSphere: {
        ImVec2 c = P(0.50f, 0.50f);
        float r = sz.x * 0.34f;
        dl->AddCircle(c, r, color, 18, 1.4f);
        dl->AddCircleFilled(ImVec2(c.x - r * 0.25f, c.y - r * 0.25f), r * 0.28f, color, 10);
        break;
    }
    case SmallIconKind::Lights: {
        ImVec2 c = P(0.50f, 0.48f);
        dl->AddCircle(c, sz.x * 0.18f, color, 12, 1.3f);
        for (int k = 0; k < 8; ++k) {
            float a = k * (kTwoPi / 8.0f);
            dl->AddLine(ImVec2(c.x + std::cos(a) * sz.x * 0.26f, c.y + std::sin(a) * sz.y * 0.26f),
                        ImVec2(c.x + std::cos(a) * sz.x * 0.39f, c.y + std::sin(a) * sz.y * 0.39f), color, 1.2f);
        }
        break;
    }
    case SmallIconKind::Outliner: {
        dl->AddRectFilled(P(0.18f, 0.20f), P(0.42f, 0.34f), color);
        dl->AddLine(P(0.30f, 0.34f), P(0.30f, 0.78f), color, 1.2f);
        dl->AddLine(P(0.30f, 0.52f), P(0.82f, 0.52f), color, 1.3f);
        dl->AddLine(P(0.30f, 0.76f), P(0.82f, 0.76f), color, 1.3f);
        break;
    }
    case SmallIconKind::SwapArrows: {
        dl->AddLine(P(0.18f, 0.28f), P(0.76f, 0.28f), color, 1.2f);
        dl->AddLine(P(0.76f, 0.28f), P(0.76f, 0.80f), color, 1.2f);
        addArrowHead(dl, P(0.14f, 0.28f), ImVec2(-1, 0), 3.5f, 2.2f, color);
        addArrowHead(dl, P(0.76f, 0.84f), ImVec2(0, 1), 3.5f, 2.2f, color);
        break;
    }
    case SmallIconKind::Camera: {
        dl->AddRect(P(0.16f, 0.28f), P(0.84f, 0.76f), color, 1.5f, 1.3f);
        dl->AddCircle(P(0.50f, 0.52f), sz.x * 0.14f, color, 12, 1.2f);
        break;
    }
    case SmallIconKind::OrthoGrid: {
        dl->AddRect(P(0.18f, 0.18f), P(0.82f, 0.82f), color, 0.0f, 1.2f);
        dl->AddLine(P(0.18f, 0.50f), P(0.82f, 0.50f), color, 1.0f);
        dl->AddLine(P(0.50f, 0.18f), P(0.50f, 0.82f), color, 1.0f);
        break;
    }
    case SmallIconKind::WindowMin:
        dl->AddLine(P(0.22f, 0.55f), P(0.78f, 0.55f), color, 1.2f);
        break;
    case SmallIconKind::WindowMax:
        dl->AddRect(P(0.22f, 0.22f), P(0.78f, 0.78f), color, 0.0f, 1.2f);
        break;
    case SmallIconKind::WindowRestore:
        dl->AddRect(P(0.32f, 0.18f), P(0.82f, 0.68f), color, 0.0f, 1.1f);
        dl->AddRectFilled(P(0.18f, 0.32f), P(0.68f, 0.82f), StudioColors::HeaderBg);
        dl->AddRect(P(0.18f, 0.32f), P(0.68f, 0.82f), color, 0.0f, 1.1f);
        break;
    case SmallIconKind::WindowClose:
        dl->AddLine(P(0.24f, 0.24f), P(0.76f, 0.76f), color, 1.3f);
        dl->AddLine(P(0.76f, 0.24f), P(0.24f, 0.76f), color, 1.3f);
        break;
    case SmallIconKind::AlignLeft:
        dl->AddLine(P(0.18f, 0.14f), P(0.18f, 0.86f), color, 1.4f);
        dl->AddRectFilled(P(0.26f, 0.24f), P(0.78f, 0.44f), color);
        dl->AddRectFilled(P(0.26f, 0.56f), P(0.60f, 0.76f), color);
        break;
    case SmallIconKind::AlignCenterH:
        dl->AddLine(P(0.50f, 0.12f), P(0.50f, 0.88f), color, 1.3f);
        dl->AddRectFilled(P(0.20f, 0.24f), P(0.80f, 0.44f), color);
        dl->AddRectFilled(P(0.30f, 0.56f), P(0.70f, 0.76f), color);
        break;
    case SmallIconKind::AlignRight:
        dl->AddLine(P(0.82f, 0.14f), P(0.82f, 0.86f), color, 1.4f);
        dl->AddRectFilled(P(0.22f, 0.24f), P(0.74f, 0.44f), color);
        dl->AddRectFilled(P(0.40f, 0.56f), P(0.74f, 0.76f), color);
        break;
    case SmallIconKind::Home: {
        ImVec2 roof[3] = {P(0.50f, 0.16f), P(0.86f, 0.48f), P(0.14f, 0.48f)};
        dl->AddTriangleFilled(roof[0], roof[1], roof[2], color);
        dl->AddRectFilled(P(0.23f, 0.46f), P(0.77f, 0.84f), color);
        dl->AddRectFilled(P(0.42f, 0.58f), P(0.58f, 0.84f), IM_COL32(0x22, 0x22, 0x22, 230));
        break;
    }
    case SmallIconKind::Templates: {
        dl->AddRect(P(0.24f, 0.28f), P(0.76f, 0.82f), color, 1.0f, 1.2f);
        const ImVec2 tp[3] = {P(0.32f, 0.20f), P(0.82f, 0.20f), P(0.82f, 0.72f)};
        dl->AddPolyline(tp, 3, color, 1.2f, ImDrawFlags_None);
        dl->AddLine(P(0.35f, 0.46f), P(0.65f, 0.46f), color, 1.1f);
        dl->AddLine(P(0.35f, 0.62f), P(0.58f, 0.62f), color, 1.1f);
        break;
    }
    case SmallIconKind::ViewGrid:
        dl->AddRectFilled(P(0.18f, 0.18f), P(0.44f, 0.44f), color, 1.0f);
        dl->AddRectFilled(P(0.56f, 0.18f), P(0.82f, 0.44f), color, 1.0f);
        dl->AddRectFilled(P(0.18f, 0.56f), P(0.44f, 0.82f), color, 1.0f);
        dl->AddRectFilled(P(0.56f, 0.56f), P(0.82f, 0.82f), color, 1.0f);
        break;
    case SmallIconKind::ViewList:
        dl->AddRectFilled(P(0.16f, 0.22f), P(0.84f, 0.34f), color, 1.0f);
        dl->AddRectFilled(P(0.16f, 0.44f), P(0.84f, 0.56f), color, 1.0f);
        dl->AddRectFilled(P(0.16f, 0.66f), P(0.84f, 0.78f), color, 1.0f);
        break;
    }
}

const char* toolDisplayName(StudioTool tool) {
    switch (tool) {
    case StudioTool::SelectMove:    return "Selection Tool";
    case StudioTool::NodeEdit:      return "Direct Selection Tool";
    case StudioTool::Orbit:         return "Orbit View";
    case StudioTool::Brush:         return "Brush Tool";
    case StudioTool::StraightRuler: return "Line Tool";
    case StudioTool::CurveBlade:    return "Pen Tool";
    case StudioTool::RectBox:       return "Rectangle Tool";
    case StudioTool::Eraser:        return "Eraser Tool";
    case StudioTool::FloodFill:     return "Paint Bucket Tool";
    case StudioTool::Eyedropper:    return "Eyedropper Tool";
    case StudioTool::Text:          return "Type Tool";
    case StudioTool::SponsorDecal:  return "Import Image";
    case StudioTool::VertexEdit:    return "Vertex Edit";
    case StudioTool::FacePaintMask: return "Face Mask";
    case StudioTool::HandPan:       return "Hand Tool";
    case StudioTool::Zoom:          return "Zoom Tool";
    case StudioTool::BeautyRender:  return "Render Photo";
    case StudioTool::TouchType:     return "Touch Type Tool";
    default:                        return "Tool";
    }
}

const char* toolShortcutHint(StudioTool tool) {
    switch (tool) {
    case StudioTool::SelectMove:    return "V";
    case StudioTool::NodeEdit:      return "A";
    case StudioTool::Orbit:         return "O";
    case StudioTool::Brush:         return "B";
    case StudioTool::StraightRuler: return "\\";
    case StudioTool::CurveBlade:    return "P";
    case StudioTool::RectBox:       return "M";
    case StudioTool::Eraser:        return "E";
    case StudioTool::FloodFill:     return "G";
    case StudioTool::Eyedropper:    return "I";
    case StudioTool::Text:          return "T";
    case StudioTool::SponsorDecal:  return "D";
    case StudioTool::VertexEdit:    return "N";
    case StudioTool::FacePaintMask: return "F";
    case StudioTool::HandPan:       return "H";
    case StudioTool::Zoom:          return "Z";
    case StudioTool::BeautyRender:  return "F12";
    case StudioTool::TouchType:     return "Shift+T";
    default:                        return "";
    }
}

const char* toolRichDescription(int toolOrMirrorIdx) {
    if (toolOrMirrorIdx == kRichTooltipMirrorIdx) {
        return "Mirror paint strokes, Pen curves, shapes, Type, and Images across the vehicle centerline (X=0).";
    }
    switch (static_cast<StudioTool>(toolOrMirrorIdx)) {
    case StudioTool::SelectMove:    return "Select, move, scale, and rotate vectors, rectangles, Type, or imported Images on 3D and 2D UV.";
    case StudioTool::NodeEdit:      return "Adjust individual anchor points, bend Bezier segments, and drag smooth tangent handles.";
    case StudioTool::Orbit:         return "Rotate the 3D showroom camera smoothly around the active vehicle model.";
    case StudioTool::Brush:         return "Paint smooth anti-aliased strokes across 3D seams and on the 2D UV canvas.";
    case StudioTool::StraightRuler: return "Draw precision straight stripes and pin-stripes; hold Shift to snap to 15-degree angles.";
    case StudioTool::CurveBlade:    return "Create straight lines and smooth curved paths using anchor points and Bezier handles.";
    case StudioTool::RectBox:       return "Draw clean filled or stroked racing plates and sponsor blocks. Right-drag handles to resize.";
    case StudioTool::Eraser:        return "Erase paint non-destructively from the active paint buffer on 3D surface or 2D UV.";
    case StudioTool::FloodFill:     return "Fill contiguous areas with the current Fill color matching within the tolerance threshold.";
    case StudioTool::Eyedropper:    return "Sample any color under the cursor directly from the composite livery canvas.";
    case StudioTool::Text:          return "Click for Point Type or drag an Area Type box; resize handles reflow wrapped text.";
    case StudioTool::TouchType:     return "Adjust individual characters' scale, position, rotation, and color directly.";
    case StudioTool::SponsorDecal:  return "Place or paste crisp RGBA PNG/JPG decals and sponsor crests aligned to the car surface.";
    case StudioTool::VertexEdit:    return "Select and sculpt welded 3D vertices with optional bilateral X=0 symmetry.";
    case StudioTool::FacePaintMask: return "Paint-select connected body panels to isolate Paint, Fill, or Eraser strokes.";
    case StudioTool::HandPan:       return "Pan across the 2D UV artboard or drag inside the 3D showroom viewport.";
    case StudioTool::Zoom:          return "Click to zoom in or hold Shift+Click to zoom out smoothly around the cursor.";
    case StudioTool::BeautyRender:  return "Capture a high-resolution studio showroom photo of your painted livery (F12).";
    default:                        return "";
    }
}

void drawRichTooltipCard(ImDrawList* dl, const ImVec2& pos, int toolOrMirrorIdx, double animTimeSec) {
    constexpr float W = 262.0f;
    constexpr float stageH = 106.0f;
    constexpr float bodyH = 102.0f;
    constexpr float H = stageH + bodyH;

    ImVec2 cMin(std::round(pos.x), std::round(pos.y));
    ImVec2 cMax(cMin.x + W, cMin.y + H);
    ImVec2 stMin = cMin;
    ImVec2 stMax(cMax.x, cMin.y + stageH);

    // Subtle soft shadow matching Illustrator Rich Tooltips
    dl->AddRectFilled(cMin + ImVec2(3.0f, 5.0f), cMax + ImVec2(4.0f, 7.0f), IM_COL32(0, 0, 0, 120), 5.0f);
    dl->AddRectFilled(cMin, cMax, IM_COL32(0x22, 0x22, 0x22, 0xFF), 4.0f);

    // Top animated illustration stage (#787878)
    dl->AddRectFilled(stMin, stMax, IM_COL32(0x78, 0x78, 0x78, 0xFF), 4.0f, ImDrawFlags_RoundCornersTop);
    dl->PushClipRect(stMin + ImVec2(1.0f, 1.0f), stMax - ImVec2(1.0f, 1.0f), true);

    float loopT = static_cast<float>(std::fmod(animTimeSec, 3.0) / 3.0);
    auto SP = [&](float u, float v) { return ImVec2(stMin.x + u * W, stMin.y + v * stageH); };
    const ImU32 darkInk  = IM_COL32(0x1E, 0x1E, 0x1E, 0xFF);
    const ImU32 cyanAcc  = IM_COL32(0x00, 0xB8, 0xFF, 0xFF);
    const ImU32 tealFill = IM_COL32(0x2F, 0xE6, 0xCE, 0xD8);
    const ImU32 limeAcc  = IM_COL32(0xCD, 0xE8, 0x20, 0xFF);
    const ImU32 wht      = IM_COL32(0xFA, 0xFA, 0xFA, 0xFF);

    auto drawCursorArrow = [&](ImVec2 tip) {
        ImVec2 pts[5] = {
            tip, tip + ImVec2(0.0f, 15.0f), tip + ImVec2(4.0f, 11.5f),
            tip + ImVec2(8.5f, 11.5f), tip
        };
        dl->AddConvexPolyFilled(pts, 4, wht);
        dl->AddPolyline(pts, 4, darkInk, 1.2f, ImDrawFlags_Closed);
    };

    if (toolOrMirrorIdx == kRichTooltipMirrorIdx) {
        // Vertical symmetry axis + mirrored curving stroke on L & R
        dl->AddLine(SP(0.50f, 0.10f), SP(0.50f, 0.90f), cyanAcc, 1.5f);
        float prog = std::min(1.0f, loopT * 1.25f);
        ImVec2 curR{};
        for (int k = 0; k < 16; ++k) {
            float t0 = (k / 16.0f) * prog;
            float t1 = ((k + 1) / 16.0f) * prog;
            auto rx = [](float t) { return 0.56f + 0.24f * std::sin(t * 2.4f); };
            auto ry = [](float t) { return 0.24f + 0.54f * t; };
            ImVec2 aR = SP(rx(t0), ry(t0)), bR = SP(rx(t1), ry(t1));
            ImVec2 aL = SP(1.0f - rx(t0), ry(t0)), bL = SP(1.0f - rx(t1), ry(t1));
            dl->AddLine(aR, bR, tealFill, 4.0f);
            dl->AddLine(aL, bL, IM_COL32(0x2F, 0xE6, 0xCE, 0x99), 4.0f);
            curR = bR;
        }
        drawCursorArrow(curR);
    } else {
        switch (static_cast<StudioTool>(toolOrMirrorIdx)) {
        case StudioTool::CurveBlade: {
            ImVec2 a0 = SP(0.22f, 0.74f), a1 = SP(0.52f, 0.24f), a2 = SP(0.80f, 0.72f);
            float ph = std::clamp(loopT * 1.4f, 0.0f, 1.0f);
            ImVec2 hOut = SP(0.52f + 0.14f * std::sin(ph * 3.14159f), 0.24f);
            ImVec2 hIn  = SP(0.52f - 0.14f * std::sin(ph * 3.14159f), 0.24f);
            dl->AddBezierCubic(a0, a0, hIn, a1, darkInk, 2.2f, 20);
            if (ph > 0.35f) dl->AddBezierCubic(a1, hOut, a2, a2, darkInk, 2.2f, 20);
            dl->AddLine(hIn, hOut, cyanAcc, 1.3f);
            dl->AddCircleFilled(hIn, 3.2f, cyanAcc, 12);
            dl->AddCircleFilled(hOut, 3.2f, cyanAcc, 12);
            for (ImVec2 anc : {a0, a1, a2}) {
                dl->AddRectFilled(anc - ImVec2(3.5f, 3.5f), anc + ImVec2(3.5f, 3.5f), wht);
                dl->AddRect(anc - ImVec2(3.5f, 3.5f), anc + ImVec2(3.5f, 3.5f), cyanAcc, 0.0f, 1.3f);
            }
            drawCursorArrow(hOut);
            break;
        }
        case StudioTool::StraightRuler: {
            float prog = std::min(1.0f, loopT * 1.3f);
            ImVec2 p0 = SP(0.24f, 0.74f);
            ImVec2 p1 = SP(0.24f + 0.52f * prog, 0.74f - 0.50f * prog);
            dl->AddLine(p0, p1, limeAcc, 3.0f);
            dl->AddRectFilled(p0 - ImVec2(3.5f, 3.5f), p0 + ImVec2(3.5f, 3.5f), wht);
            dl->AddRectFilled(p1 - ImVec2(3.5f, 3.5f), p1 + ImVec2(3.5f, 3.5f), cyanAcc);
            drawCursorArrow(p1);
            break;
        }
        case StudioTool::NodeEdit: {
            float bend = std::sin(loopT * 6.28318f) * 0.16f;
            ImVec2 a0 = SP(0.20f, 0.72f), a1 = SP(0.80f, 0.72f);
            ImVec2 c0 = SP(0.35f, 0.30f + bend), c1 = SP(0.65f, 0.30f - bend);
            dl->AddBezierCubic(a0, c0, c1, a1, darkInk, 2.4f, 22);
            dl->AddLine(a0, c0, cyanAcc, 1.2f);
            dl->AddLine(a1, c1, cyanAcc, 1.2f);
            dl->AddCircleFilled(c0, 3.5f, cyanAcc, 12);
            dl->AddCircleFilled(c1, 3.5f, wht, 12);
            drawCursorArrow(c0);
            break;
        }
        case StudioTool::Brush: {
            float prog = std::min(1.0f, loopT * 1.2f);
            ImVec2 tip = SP(0.22f, 0.62f);
            for (int k = 0; k < 18; ++k) {
                float t0 = (k / 18.0f) * prog, t1 = ((k + 1) / 18.0f) * prog;
                ImVec2 a = SP(0.22f + 0.56f * t0, 0.55f - 0.22f * std::sin(t0 * 5.0f));
                ImVec2 b = SP(0.22f + 0.56f * t1, 0.55f - 0.22f * std::sin(t1 * 5.0f));
                dl->AddLine(a, b, limeAcc, 7.5f);
                tip = b;
            }
            dl->AddCircle(tip, 7.5f, wht, 20, 1.4f);
            drawCursorArrow(tip);
            break;
        }
        case StudioTool::Eraser: {
            dl->AddRectFilled(SP(0.24f, 0.28f), SP(0.76f, 0.74f), tealFill, 5.0f);
            float ePos = std::clamp(loopT * 1.2f, 0.0f, 1.0f);
            ImVec2 erMin = SP(0.22f + 0.44f * ePos, 0.38f);
            dl->AddLine(SP(0.24f, 0.46f), erMin + ImVec2(8.0f, 8.0f), IM_COL32(0x78, 0x78, 0x78, 0xFF), 13.0f);
            dl->AddRectFilled(erMin, erMin + ImVec2(14.0f, 14.0f), wht, 2.0f);
            dl->AddRect(erMin, erMin + ImVec2(14.0f, 14.0f), darkInk, 2.0f, 1.2f);
            break;
        }
        case StudioTool::RectBox: {
            float s = std::clamp(loopT * 1.3f, 0.1f, 1.0f);
            ImVec2 rMin = SP(0.24f, 0.24f);
            ImVec2 rMax = SP(0.24f + 0.52f * s, 0.24f + 0.54f * s);
            dl->AddRectFilled(rMin, rMax, tealFill, 2.0f);
            dl->AddRect(rMin, rMax, cyanAcc, 2.0f, 1.5f);
            dl->AddRectFilled(rMax - ImVec2(3.5f, 3.5f), rMax + ImVec2(3.5f, 3.5f), wht);
            drawCursorArrow(rMax);
            break;
        }
        case StudioTool::FloodFill: {
            bool filledNow = (loopT > 0.40f);
            if (filledNow) dl->AddCircleFilled(SP(0.50f, 0.56f), 28.0f, limeAcc, 24);
            dl->AddCircle(SP(0.50f, 0.56f), 28.0f, darkInk, 24, 2.0f);
            drawToolIcon(dl, StudioTool::FloodFill, SP(0.52f, 0.16f), SP(0.68f, 0.48f), wht, true);
            break;
        }
        case StudioTool::Eyedropper: {
            ImU32 swatches[3] = {IM_COL32(0x2F, 0xE6, 0xCE, 0xFF), limeAcc, IM_COL32(0xEB, 0x6C, 0x18, 0xFF)};
            int activeSw = static_cast<int>(loopT * 3.0f) % 3;
            for (int k = 0; k < 3; ++k) {
                ImVec2 s0 = SP(0.24f + k * 0.18f, 0.48f);
                dl->AddRectFilled(s0, s0 + ImVec2(36.0f, 28.0f), swatches[k], 2.0f);
            }
            ImVec2 cur = SP(0.31f + activeSw * 0.18f, 0.58f);
            dl->AddCircle(cur, 11.0f, swatches[activeSw], 20, 3.0f);
            drawToolIcon(dl, StudioTool::Eyedropper, cur + ImVec2(0.0f, -24.0f), cur + ImVec2(24.0f, 0.0f), wht, true);
            break;
        }
        case StudioTool::Text: {
            float sc = 0.85f + 0.18f * std::sin(loopT * 6.28318f);
            ImVec2 bMin = SP(0.24f, 0.30f), bMax = SP(0.24f + 0.52f * sc, 0.72f);
            dl->AddRect(bMin, bMax, cyanAcc, 0.0f, 1.3f);
            dl->AddText(SP(0.33f, 0.44f), wht, "Lorem ipsum");
            drawCursorArrow(bMax);
            break;
        }
        case StudioTool::TouchType: {
            float lift = 0.12f * (0.5f - 0.5f * std::cos(loopT * 6.28318f));
            dl->AddText(SP(0.26f, 0.48f), IM_COL32(210, 214, 220, 200), "T");
            dl->AddText(SP(0.41f, 0.48f), IM_COL32(210, 214, 220, 200), "y");
            dl->AddText(SP(0.68f, 0.48f), IM_COL32(210, 214, 220, 200), "e");
            ImVec2 chMin = SP(0.51f, 0.34f - lift);
            ImVec2 chMax = SP(0.64f, 0.68f - lift);
            dl->AddRect(chMin, chMax, cyanAcc, 0.0f, 1.3f);
            dl->AddRectFilled(chMin - ImVec2(2.5f, 2.5f), chMin + ImVec2(2.5f, 2.5f), wht);
            dl->AddRectFilled(chMax - ImVec2(2.5f, 2.5f), chMax + ImVec2(2.5f, 2.5f), wht);
            dl->AddText(SP(0.54f, 0.43f - lift), IM_COL32(0x2F, 0xE6, 0xCE, 0xFF), "p");
            drawCursorArrow(chMax);
            break;
        }
        case StudioTool::SponsorDecal: {
            float lift = 0.16f * (1.0f - std::min(1.0f, loopT * 1.4f));
            ImVec2 pMin = SP(0.30f, 0.24f - lift), pMax = SP(0.70f, 0.76f - lift);
            dl->AddRectFilled(pMin, pMax, IM_COL32(0x22, 0x26, 0x2E, 0xFF), 4.0f);
            dl->AddRect(pMin, pMax, cyanAcc, 4.0f, 1.5f);
            dl->AddTriangleFilled(SP(0.35f, 0.68f - lift), SP(0.50f, 0.44f - lift), SP(0.65f, 0.68f - lift), tealFill);
            drawCursorArrow(pMax);
            break;
        }
        case StudioTool::HandPan: {
            float dx = 0.14f * std::sin(loopT * 6.28318f);
            dl->AddRect(SP(0.26f + dx, 0.24f), SP(0.74f + dx, 0.76f), wht, 2.0f, 1.6f);
            dl->AddCircleFilled(SP(0.50f + dx, 0.50f), 14.0f, tealFill, 20);
            drawToolIcon(dl, StudioTool::HandPan, SP(0.46f + dx, 0.44f), SP(0.60f + dx, 0.74f), wht, true);
            break;
        }
        case StudioTool::Zoom: {
            float zR = 14.0f + 10.0f * (0.5f - 0.5f * std::cos(loopT * 6.28318f));
            dl->AddRectFilled(SP(0.34f, 0.34f), SP(0.66f, 0.68f), tealFill, 3.0f);
            dl->AddCircle(SP(0.50f, 0.48f), zR, wht, 24, 2.0f);
            dl->AddLine(SP(0.55f, 0.60f), SP(0.66f, 0.80f), wht, 2.8f);
            break;
        }
        default: {
            float shiftX = 0.16f * std::sin(loopT * 6.28318f);
            ImVec2 rMin = SP(0.28f + shiftX, 0.30f), rMax = SP(0.62f + shiftX, 0.70f);
            dl->AddRectFilled(rMin, rMax, tealFill, 3.0f);
            dl->AddRect(rMin, rMax, cyanAcc, 3.0f, 1.4f);
            drawCursorArrow(rMin + ImVec2(14.0f, 12.0f));
            break;
        }
        }
    }
    dl->PopClipRect();

    // Bottom dark info box (#222222)
    dl->AddLine(ImVec2(cMin.x, stMax.y), ImVec2(cMax.x, stMax.y), IM_COL32(0x3A, 0x3A, 0x3A, 0xFF), 1.0f);
    const char* title = (toolOrMirrorIdx == kRichTooltipMirrorIdx)
                        ? "Mirror Symmetry"
                        : toolDisplayName(static_cast<StudioTool>(toolOrMirrorIdx));
    const char* sc = (toolOrMirrorIdx == kRichTooltipMirrorIdx)
                     ? "Shift+M"
                     : toolShortcutHint(static_cast<StudioTool>(toolOrMirrorIdx));
    dl->AddText(ImVec2(cMin.x + 14.0f, stMax.y + 12.0f), IM_COL32(0xF2, 0xF2, 0xF2, 0xFF), title);

    if (sc && sc[0]) {
        ImVec2 scSz = ImGui::CalcTextSize(sc);
        ImVec2 pMax(cMax.x - 14.0f, stMax.y + 12.0f + scSz.y + 4.0f);
        ImVec2 pMin(pMax.x - (scSz.x + 12.0f), stMax.y + 10.0f);
        dl->AddRectFilled(pMin, pMax, IM_COL32(0x3A, 0x3A, 0x3A, 0xFF), 2.0f);
        dl->AddText(ImVec2(pMin.x + 6.0f, pMin.y + 2.0f), IM_COL32(0xDC, 0xDC, 0xDC, 0xFF), sc);
    }

    const char* desc = toolRichDescription(toolOrMirrorIdx);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.92f,
                ImVec2(cMin.x + 14.0f, stMax.y + 38.0f),
                IM_COL32(0xB8, 0xB8, 0xB8, 0xFF), desc, nullptr, W - 28.0f);

    // Subtle 1px #3A3A3A outer card border
    dl->AddRect(cMin, cMax, IM_COL32(0x3A, 0x3A, 0x3A, 0xFF), 4.0f, 1.0f);
}

} // namespace lps
