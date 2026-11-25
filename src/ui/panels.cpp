#define IMGUI_DEFINE_MATH_OPERATORS
#include "ui/panels.hpp"
#include "ui/studio_theme.hpp"
#include "ui/uv_viewport.hpp"
#include "imgui_internal.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace lps {

namespace {

inline void color4ToFloat4(Color4u8 c, float out[4]) {
    out[0] = c.r / 255.0f;
    out[1] = c.g / 255.0f;
    out[2] = c.b / 255.0f;
    out[3] = c.a / 255.0f;
}

inline Color4u8 float4ToColor4(const float in[4]) {
    auto cl = [](float v) -> uint8_t {
        return static_cast<uint8_t>(std::clamp(static_cast<int>(v * 255.0f + 0.5f), 0, 255));
    };
    return Color4u8{cl(in[0]), cl(in[1]), cl(in[2]), cl(in[3])};
}

inline ImU32 toImU32(Color4u8 c) {
    return IM_COL32(c.r, c.g, c.b, c.a);
}

bool colorEdit4u8(const char* label, Color4u8& col, bool compactSwatch = false) {
    float f[4];
    color4ToFloat4(col, f);
    ImGuiColorEditFlags fl = ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_Uint8;
    if (compactSwatch) fl |= ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel;
    if (ImGui::ColorEdit4(label, f, fl)) {
        col = float4ToColor4(f);
        return true;
    }
    return false;
}

bool flatTogglePill(const char* label, bool selected, bool amber = false, const ImVec2& size = ImVec2(0, 0)) {
    // Authentic Adobe Illustrator CC flat button: #3A3A3A idle, #242424 inset active well with crisp outline
    ImVec4 bg = StudioColors::ToVec4(selected ? StudioColors::ToolActiveBg : StudioColors::ControlBg);
    ImVec4 hov = StudioColors::ToVec4(selected ? (amber ? StudioColors::AmberAccentBg : StudioColors::ToolActiveBg)
                                               : StudioColors::ControlHover);
    ImVec4 brd = StudioColors::ToVec4(selected ? (amber ? StudioColors::AmberAccent : StudioColors::StudioBlue)
                                               : StudioColors::BorderLight);
    ImGui::PushStyleColor(ImGuiCol_Button, bg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hov);
    ImGui::PushStyleColor(ImGuiCol_Border, brd);
    bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return clicked;
}

inline void itemTooltip(const char* tip) {
    if (tip && tip[0] && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("%s", tip);
    }
}

inline bool compactScrubFloat(const char* lbl, const char* id, float* v, float speed, float minV, float maxV,
                              const char* fmt, const char* tip = nullptr) {
    ImGui::TextColored(StudioColors::ToVec4(StudioColors::TextSecondary), "%s", lbl);
    ImGui::SameLine(46.0f);
    ImGui::SetNextItemWidth(-1.0f);
    bool ch = ImGui::DragFloat(id, v, speed, minV, maxV, fmt);
    itemTooltip(tip);
    return ch;
}

inline bool compactScrubPct(const char* lbl, const char* id, float* unitVal, const char* tip = nullptr) {
    ImGui::TextColored(StudioColors::ToVec4(StudioColors::TextSecondary), "%s", lbl);
    ImGui::SameLine(46.0f);
    ImGui::SetNextItemWidth(-1.0f);
    int pct = static_cast<int>(std::round(*unitVal * 100.0f));
    bool ch = ImGui::DragInt(id, &pct, 1.0f, 1, 100, "%d%%");
    if (ch) *unitVal = std::clamp(pct / 100.0f, 0.01f, 1.0f);
    itemTooltip(tip);
    return ch;
}

void triggerFillPlaceholderText(StudioUiState& state, CanvasEngine& canvas, const ObjMesh& mesh,
                                const StudioCallbacks& callbacks) {
    if (callbacks.onFillPlaceholderText) {
        callbacks.onFillPlaceholderText();
        return;
    }
    int idx = canvas.activeLayerIdx;
    VectorItem* actItem = canvas.activeVectorItem();
    if (actItem && actItem->kind == VectorItemKind::Text) {
        if (idx >= 0 && idx < static_cast<int>(canvas.layers.size()) && canvas.layers[idx].locked) return;
        canvas.pushUndoSnapshot();
        actItem->text.text = "Lorem ipsum";
        std::snprintf(state.textEditBuf, sizeof(state.textEditBuf), "%s", actItem->text.text.c_str());
        state.textEditSyncedLayerId = actItem->id;
        state.activeTool = StudioTool::Text;
        state.activePropsCat = PropertiesCategory::Text;
        canvas.rasterizeLayer(idx, &mesh);
        if (callbacks.onCanvasModified) callbacks.onCanvasModified();
        return;
    }
    if (idx >= 0 && idx < static_cast<int>(canvas.layers.size()) &&
        canvas.layers[idx].kind == LayerKind::Text) {
        Layer& l = canvas.layers[idx];
        if (l.locked) return;
        canvas.pushUndoSnapshot();
        l.text.text = "Lorem ipsum";
        std::snprintf(state.textEditBuf, sizeof(state.textEditBuf), "%s", l.text.text.c_str());
        state.textEditSyncedLayerId = l.id;
        state.activeTool = StudioTool::Text;
        state.activePropsCat = PropertiesCategory::Text;
        canvas.rasterizeLayer(idx, &mesh);
        if (callbacks.onCanvasModified) callbacks.onCanvasModified();
        return;
    }
    TextLayerProps tp;
    tp.text = "Lorem ipsum";
    tp.uvPos = Vec2{0.5f, 0.5f};
    tp.anchoredIn3D = false;
    tp.fontFamily = "Arial";
    tp.fontStyle = "Bold";
    tp.fontPath = canvas.findFontPath("", tp.fontFamily, tp.fontStyle);
    tp.fillColor = state.brush.color;
    tp.outlineColor = state.secondaryColor;
    tp.mirrorX = state.brush.mirrorX;
    tp.mirrorMode = state.brush.mirrorX ? MirrorTextMode::ReadableBothSides : MirrorTextMode::None;
    tp.syncCharStyles();
    int newItemIdx = canvas.addTextItem("Text Layer", tp, &mesh);
    canvas.selectedItemIdx = newItemIdx;
    if (VectorItem* created = canvas.activeVectorItem()) {
        std::snprintf(state.textEditBuf, sizeof(state.textEditBuf), "%s", created->text.text.c_str());
        state.textEditSyncedLayerId = created->id;
    }
    state.activeTool = StudioTool::Text;
    state.activeTypeToolVariant = 0;
    state.activePropsCat = PropertiesCategory::Text;
    if (callbacks.onCanvasModified) callbacks.onCanvasModified();
}

void syncActiveLayerColors(const StudioUiState& state, CanvasEngine& canvas,
                           const ObjMesh& mesh, const StudioCallbacks& callbacks,
                           bool strokeWasEdited) {
    int idx = canvas.activeLayerIdx;
    if (idx < 0 || idx >= static_cast<int>(canvas.layers.size())) return;
    Layer& l = canvas.layers[idx];
    if (l.locked) return;
    auto applyTextCol = [&](TextLayerProps& tx) {
        if (strokeWasEdited) {
            tx.outlineColor = state.secondaryColor;
        } else if (tx.selectedCharIdx >= 0 && tx.selectedCharIdx < static_cast<int>(tx.text.size())) {
            tx.syncCharStyles();
            tx.charStyles[tx.selectedCharIdx].hasColor = true;
            tx.charStyles[tx.selectedCharIdx].color = state.brush.color;
        } else {
            tx.fillColor = state.brush.color;
        }
    };
    if (VectorItem* it = canvas.activeVectorItem()) {
        if (it->locked) return;
        if (it->kind == VectorItemKind::Text) {
            applyTextCol(it->text);
            canvas.rasterizeLayer(idx, &mesh);
            if (callbacks.onCanvasModified) callbacks.onCanvasModified();
            return;
        }
        if (it->kind == VectorItemKind::PenPath) {
            if (strokeWasEdited) {
                it->pen.color = state.secondaryColor;
            } else {
                it->pen.fillColor = state.brush.color;
                if (!it->pen.filled) it->pen.color = state.brush.color;
            }
            canvas.rasterizeLayer(idx, &mesh);
            if (callbacks.onCanvasModified) callbacks.onCanvasModified();
            return;
        }
        if (it->kind == VectorItemKind::RectShape) {
            if (strokeWasEdited) it->rect.strokeColor = state.secondaryColor;
            else it->rect.fillColor = state.brush.color;
            canvas.rasterizeLayer(idx, &mesh);
            if (callbacks.onCanvasModified) callbacks.onCanvasModified();
            return;
        }
    }
    if (l.kind == LayerKind::Text) {
        applyTextCol(l.text);
        canvas.rasterizeLayer(idx, &mesh);
        if (callbacks.onCanvasModified) callbacks.onCanvasModified();
    } else if (l.kind == LayerKind::RulerStripe) {
        if (strokeWasEdited) {
            l.ruler.color = state.secondaryColor;
        } else {
            l.ruler.fillColor = state.brush.color;
            if (!l.ruler.filled) l.ruler.color = state.brush.color;
        }
        canvas.rasterizeLayer(idx, &mesh);
        if (callbacks.onCanvasModified) callbacks.onCanvasModified();
    }
}

void syncBothColorsToActiveLayer(const StudioUiState& state, CanvasEngine& canvas,
                                 const ObjMesh& mesh, const StudioCallbacks& callbacks) {
    int idx = canvas.activeLayerIdx;
    if (idx < 0 || idx >= static_cast<int>(canvas.layers.size())) return;
    Layer& l = canvas.layers[idx];
    if (l.locked) return;
    if (VectorItem* it = canvas.activeVectorItem()) {
        if (it->locked) return;
        if (it->kind == VectorItemKind::Text) {
            it->text.fillColor = state.brush.color;
            it->text.outlineColor = state.secondaryColor;
            canvas.rasterizeLayer(idx, &mesh);
            if (callbacks.onCanvasModified) callbacks.onCanvasModified();
            return;
        }
        if (it->kind == VectorItemKind::PenPath) {
            it->pen.fillColor = state.brush.color;
            it->pen.color = state.secondaryColor;
            canvas.rasterizeLayer(idx, &mesh);
            if (callbacks.onCanvasModified) callbacks.onCanvasModified();
            return;
        }
        if (it->kind == VectorItemKind::RectShape) {
            it->rect.fillColor = state.brush.color;
            it->rect.strokeColor = state.secondaryColor;
            canvas.rasterizeLayer(idx, &mesh);
            if (callbacks.onCanvasModified) callbacks.onCanvasModified();
            return;
        }
    }
    if (l.kind == LayerKind::Text) {
        l.text.fillColor = state.brush.color;
        l.text.outlineColor = state.secondaryColor;
        canvas.rasterizeLayer(idx, &mesh);
        if (callbacks.onCanvasModified) callbacks.onCanvasModified();
    } else if (l.kind == LayerKind::RulerStripe) {
        l.ruler.fillColor = state.brush.color;
        l.ruler.color = state.secondaryColor;
        canvas.rasterizeLayer(idx, &mesh);
        if (callbacks.onCanvasModified) callbacks.onCanvasModified();
    }
}

bool containsCaseInsensitive(const std::string& haystack, const char* needle) {
    if (!needle || needle[0] == '\0') return true;
    std::string nh(haystack.size(), '\0');
    for (size_t i = 0; i < haystack.size(); ++i) {
        nh[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(haystack[i])));
    }
    std::string nn;
    for (const char* p = needle; *p; ++p) {
        nn.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*p))));
    }
    return nh.find(nn) != std::string::npos;
}

const char* mirrorModeName(MirrorTextMode m) {
    switch (m) {
    case MirrorTextMode::None:              return "None";
    case MirrorTextMode::ReadableBothSides: return "Readable Both Sides";
    case MirrorTextMode::TrueFlip:          return "True Flip";
    default:                                return "Readable Both Sides";
    }
}

const char* blendModeName(BlendMode m) {
    switch (m) {
    case BlendMode::Normal:   return "Normal";
    case BlendMode::Multiply: return "Multiply";
    case BlendMode::Screen:   return "Screen";
    case BlendMode::Overlay:  return "Overlay";
    default:                  return "Normal";
    }
}

const char* modeDisplayName(StudioMode m) {
    switch (m) {
    case StudioMode::TexturePaint: return "Texture Paint";
    case StudioMode::Object:       return "Object Mode";
    case StudioMode::EditMesh:     return "Edit Mesh";
    default:                       return "Texture Paint";
    }
}

void drawShadingSphereIcon(ImDrawList* dl, ShadingMode mode, const ImVec2& center, float r, bool active) {
    if (active) {
        dl->AddCircleFilled(center, r + 3.5f, StudioColors::StudioBlueBg, 18);
    }
    switch (mode) {
    case ShadingMode::Wireframe: {
        dl->AddCircle(center, r, StudioColors::TextPrimary, 16, 1.2f);
        dl->AddEllipse(center, ImVec2(r, r * 0.4f), StudioColors::TextSecondary, 0.0f, 14, 1.0f);
        dl->AddEllipse(center, ImVec2(r * 0.42f, r), StudioColors::TextSecondary, 0.0f, 14, 1.0f);
        break;
    }
    case ShadingMode::Clay: {
        dl->AddCircleFilled(center, r, IM_COL32(165, 168, 173, 255), 18);
        dl->AddCircleFilled(ImVec2(center.x - r * 0.28f, center.y - r * 0.28f), r * 0.34f,
                            IM_COL32(220, 222, 226, 255), 12);
        break;
    }
    case ShadingMode::Material: {
        dl->AddCircleFilled(center, r, IM_COL32(82, 88, 98, 255), 18);
        dl->AddCircleFilled(ImVec2(center.x - r * 0.25f, center.y - r * 0.25f), r * 0.42f,
                            IM_COL32(200, 128, 110, 255), 12);
        dl->AddCircle(center, r, StudioColors::TextSecondary, 18, 1.0f);
        break;
    }
    case ShadingMode::Rendered: {
        dl->AddCircleFilled(center, r, IM_COL32(55, 58, 66, 255), 18);
        dl->AddCircleFilled(ImVec2(center.x - r * 0.30f, center.y - r * 0.30f), r * 0.36f,
                            IM_COL32(236, 238, 242, 255), 12);
        dl->AddCircle(center, r, IM_COL32(210, 214, 222, 255), 18, 1.1f);
        break;
    }
    }
}

void drawColorTabContent(StudioUiState& state, CanvasEngine& canvas, const ObjMesh& mesh,
                         const StudioCallbacks& callbacks) {
    Color4u8& targetCol = state.editingStrokeColor ? state.secondaryColor : state.brush.color;

    float f[4];
    color4ToFloat4(targetCol, f);

    // Compact Fill / Stroke target selector driven by state.editingStrokeColor
    float halfW = (ImGui::GetContentRegionAvail().x - 6.0f) * 0.5f;
    if (flatTogglePill("Fill Color", !state.editingStrokeColor, false, ImVec2(halfW, 20.0f))) {
        state.editingStrokeColor = false;
    }
    itemTooltip("Edit Primary Fill Color");
    ImGui::SameLine();
    if (flatTogglePill("Stroke", state.editingStrokeColor, false, ImVec2(halfW, 20.0f))) {
        state.editingStrokeColor = true;
    }
    itemTooltip("Edit Secondary / Outline Stroke Color");

    bool colorChanged = false;
    ImGui::Spacing();
    float availW = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(std::clamp(availW - 6.0f, 136.0f, 224.0f));
    if (ImGui::ColorPicker4("##IllusSVPicker", f,
                            ImGuiColorEditFlags_PickerHueBar |
                            ImGuiColorEditFlags_NoSidePreview |
                            ImGuiColorEditFlags_NoSmallPreview |
                            ImGuiColorEditFlags_NoInputs |
                            ImGuiColorEditFlags_NoLabel |
                            ImGuiColorEditFlags_NoAlpha)) {
        targetCol.r = static_cast<uint8_t>(std::clamp(static_cast<int>(f[0] * 255.0f + 0.5f), 0, 255));
        targetCol.g = static_cast<uint8_t>(std::clamp(static_cast<int>(f[1] * 255.0f + 0.5f), 0, 255));
        targetCol.b = static_cast<uint8_t>(std::clamp(static_cast<int>(f[2] * 255.0f + 0.5f), 0, 255));
        colorChanged = true;
    }

    char curHex[16];
    std::snprintf(curHex, sizeof(curHex), "%02x%02x%02x", targetCol.r, targetCol.g, targetCol.b);
    if (!ImGui::IsItemActive()) {
        std::snprintf(state.hexColorBuf, sizeof(state.hexColorBuf), "%s", curHex);
    }

    ImGui::SetNextItemWidth(68.0f);
    if (ImGui::InputText("#", state.hexColorBuf, sizeof(state.hexColorBuf),
                         ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
        unsigned int rv = 0, gv = 0, bv = 0;
        if (std::sscanf(state.hexColorBuf, "%02x%02x%02x", &rv, &gv, &bv) == 3) {
            targetCol.r = static_cast<uint8_t>(std::min(rv, 255u));
            targetCol.g = static_cast<uint8_t>(std::min(gv, 255u));
            targetCol.b = static_cast<uint8_t>(std::min(bv, 255u));
            colorChanged = true;
        }
    }
    itemTooltip("Hex Color Code");
    ImGui::SameLine();
    int rgb[3] = {targetCol.r, targetCol.g, targetCol.b};
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::DragInt3("##RGB", rgb, 1.0f, 0, 255, "%d")) {
        targetCol.r = static_cast<uint8_t>(std::clamp(rgb[0], 0, 255));
        targetCol.g = static_cast<uint8_t>(std::clamp(rgb[1], 0, 255));
        targetCol.b = static_cast<uint8_t>(std::clamp(rgb[2], 0, 255));
        colorChanged = true;
    }
    itemTooltip("Red / Green / Blue (0..255)");

    ImGui::Spacing();
    ImGui::TextColored(StudioColors::ToVec4(StudioColors::TextSecondary), "SWATCHES");
    struct Swatch { const char* name; Color4u8 col; };
    static constexpr Swatch kSwatches[] = {
        {"Carbon Black",  {22, 24, 29, 255}},
        {"Matte Slate",   {52, 56, 64, 255}},
        {"Racing White",  {244, 246, 249, 255}},
        {"Silver Metallic",{185, 190, 198, 255}},
        {"Acid Lime",     {205, 232, 32, 255}},
        {"Guards Red",    {214, 38, 38, 255}},
        {"Signal Orange", {235, 108, 24, 255}},
        {"Speed Yellow",  {242, 198, 34, 255}},
        {"Cobalt Blue",   {37, 99, 235, 255}},
        {"Gulf Cyan",     {96, 178, 214, 255}},
    };
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));
    float swW = std::max(18.0f, (ImGui::GetContentRegionAvail().x - 16.0f) / 5.0f);
    for (int i = 0; i < 10; ++i) {
        if (i > 0 && (i % 5) != 0) ImGui::SameLine();
        float sf[4];
        color4ToFloat4(kSwatches[i].col, sf);
        ImGui::PushID(i);
        if (ImGui::ColorButton(kSwatches[i].name, ImVec4(sf[0], sf[1], sf[2], sf[3]),
                               ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoDragDrop,
                               ImVec2(swW, 17.0f))) {
            targetCol = kSwatches[i].col;
            colorChanged = true;
        }
        itemTooltip(kSwatches[i].name);
        ImGui::PopID();
    }
    if (!state.customSwatches.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(StudioColors::ToVec4(StudioColors::TextMuted), "CUSTOM");
        for (int ci = 0; ci < static_cast<int>(state.customSwatches.size()); ++ci) {
            if (ci > 0 && (ci % 5) != 0) ImGui::SameLine();
            float sf[4];
            color4ToFloat4(state.customSwatches[ci], sf);
            ImGui::PushID(500 + ci);
            if (ImGui::ColorButton("##CustomSw", ImVec4(sf[0], sf[1], sf[2], sf[3]),
                                   ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoDragDrop,
                                   ImVec2(swW, 17.0f))) {
                targetCol = state.customSwatches[ci];
                colorChanged = true;
            }
            itemTooltip("Custom Swatch");
            ImGui::PopID();
        }
    }
    ImGui::PopStyleVar();

    // Right-click Color panel context menu
    if (ImGui::BeginPopupContextWindow("##ColorPanelCtx", ImGuiPopupFlags_MouseButtonRight)) {
        if (ImGui::MenuItem("Swap Fill & Stroke", "X")) {
            std::swap(state.brush.color, state.secondaryColor);
            syncBothColorsToActiveLayer(state, canvas, mesh, callbacks);
        }
        if (ImGui::MenuItem("Default Colors", "D")) {
            state.brush.color = {244, 246, 249, 255};
            state.secondaryColor = {22, 24, 28, 255};
            syncBothColorsToActiveLayer(state, canvas, mesh, callbacks);
        }
        if (ImGui::MenuItem("Add Current to Swatches")) {
            state.customSwatches.push_back(targetCol);
        }
        ImGui::EndPopup();
    }

    if (colorChanged) {
        syncActiveLayerColors(state, canvas, mesh, callbacks, state.editingStrokeColor);
    }
}

void drawBrushSubpanel(StudioUiState& state) {
    if (ImGui::BeginTable("##BrushGrid", 2, ImGuiTableFlags_SizingStretchSame)) {
        if (state.activeTool == StudioTool::FloodFill) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            compactScrubFloat("Tol", "##FillTol", &state.floodFillTolerance, 0.01f, 0.0f, 0.5f, "%.2f", "Color Match Tolerance");
            ImGui::TableSetColumnIndex(1);
            ImGui::Checkbox("Mirror", &state.brush.mirrorX);
        } else {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            compactScrubFloat("Size", "##BrRad", &state.brush.radiusPx, 1.0f, 1.0f, 256.0f, "%.0f px", "2D Brush Radius (px)");
            ImGui::TableSetColumnIndex(1);
            compactScrubFloat("3D r", "##Br3d", &state.brush.worldRadius, 0.004f, 0.005f, 0.80f, "%.3f m", "3D Projected Brush Radius (m)");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            compactScrubPct("Hard", "##BrHard", &state.brush.hardness, "Brush Edge Hardness");
            ImGui::TableSetColumnIndex(1);
            compactScrubPct("Opac", "##BrOp", &state.brush.opacity, "Brush Opacity");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            compactScrubFloat("Step", "##BrStep", &state.brush.spacing, 0.01f, 0.05f, 1.0f, "%.2f", "Dab Spacing");
            ImGui::TableSetColumnIndex(1);
            ImGui::Checkbox("Mirror", &state.brush.mirrorX);
            itemTooltip("Symmetry Across X=0 (Shift+M)");
        }
        ImGui::EndTable();
    }
    if (state.activeTool != StudioTool::FloodFill && state.activeTool != StudioTool::RectBox) {
        ImGui::Checkbox("3D Seam Proj", &state.brush.project3DBrush);
        itemTooltip("Project Cylindrical Dab Cleanly Across UV Seams");
        ImGui::SameLine();
        ImGui::Checkbox("Mask Sel", &state.brush.maskSelectedFacesOnly);
        itemTooltip("Restrict Paint to Selected 3D Faces");
    }
}

void drawRectSubpanel(StudioUiState& state, CanvasEngine& canvas, const ObjMesh& mesh,
                      const StudioCallbacks& callbacks) {
    int idx = canvas.activeLayerIdx;
    VectorItem* actItem = canvas.activeVectorItem();
    if (!actItem || actItem->kind != VectorItemKind::RectShape) {
        if (ImGui::BeginTable("##RectDefGrid", 2, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            compactScrubFloat("Edge", "##RectStrokeDef", &state.rectStrokeWidthPx, 0.5f, 0.0f, 32.0f, "%.0f px", "Stroke Width (0 = Solid Fill)");
            ImGui::TableSetColumnIndex(1);
            compactScrubPct("Opac", "##RectOpacDef", &state.brush.opacity, "Rectangle Opacity");
            ImGui::EndTable();
        }
        ImGui::Checkbox("Mirror", &state.brush.mirrorX);
        ImGui::TextColored(StudioColors::ToVec4(StudioColors::TextMuted),
                           "Drag on canvas or 3D model with Rectangle (M).");
        return;
    }

    RectShapeProps& rc = actItem->rect;
    bool changed = false;
    auto checkActivated = [&]() {
        if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();
    };

    float thirdW = (ImGui::GetContentRegionAvail().x - 10.0f) / 3.0f;
    if (flatTogglePill("Filled", rc.filled, false, ImVec2(thirdW, 0))) {
        canvas.pushUndoSnapshot();
        rc.filled = !rc.filled;
        changed = true;
    }
    ImGui::SameLine();
    if (flatTogglePill("Stroke", rc.hasStroke, false, ImVec2(thirdW, 0))) {
        canvas.pushUndoSnapshot();
        rc.hasStroke = !rc.hasStroke;
        changed = true;
    }
    ImGui::SameLine();
    if (flatTogglePill("Mirror", rc.mirrorX, false, ImVec2(thirdW, 0))) {
        canvas.setMirrorOnActiveItem(!rc.mirrorX, &mesh);
        state.brush.mirrorX = rc.mirrorX;
        if (callbacks.onCanvasModified) callbacks.onCanvasModified();
        return;
    }

    if (ImGui::BeginTable("##RectGrid", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        float wPct = rc.uvScale.x * 100.0f;
        if (compactScrubFloat("W", "##RcW", &wPct, 0.2f, 1.0f, 100.0f, "%.1f%%", "Rectangle Width")) {
            rc.uvScale.x = std::clamp(wPct * 0.01f, 0.01f, 1.0f);
            rc.anchoredIn3D = false;
            canvas.syncItem2DTo3D(*actItem, &mesh);
            changed = true;
        }
        checkActivated();
        ImGui::TableSetColumnIndex(1);
        float hPct = rc.uvScale.y * 100.0f;
        if (compactScrubFloat("H", "##RcH", &hPct, 0.2f, 1.0f, 100.0f, "%.1f%%", "Rectangle Height")) {
            rc.uvScale.y = std::clamp(hPct * 0.01f, 0.01f, 1.0f);
            rc.anchoredIn3D = false;
            canvas.syncItem2DTo3D(*actItem, &mesh);
            changed = true;
        }
        checkActivated();

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        changed |= compactScrubFloat("Corner", "##RcCor", &rc.cornerRadiusPx, 0.5f, 0.0f, 128.0f, "%.0f px", "Corner Radius");
        checkActivated();
        ImGui::TableSetColumnIndex(1);
        changed |= compactScrubFloat("Edge", "##RcStW", &rc.strokeWidthPx, 0.5f, 0.0f, 48.0f, "%.0f px", "Stroke Width");
        checkActivated();

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        changed |= compactScrubFloat("3D W", "##Rc3W", &rc.worldWidth, 0.01f, 0.02f, 2.50f, "%.2f m", "Projected 3D Width");
        checkActivated();
        ImGui::TableSetColumnIndex(1);
        changed |= compactScrubFloat("Rot", "##RcRot", &rc.rotationDeg, 1.0f, -180.0f, 180.0f, "%.0f deg", "Rotation Angle");
        checkActivated();
        ImGui::EndTable();
    }

    changed |= colorEdit4u8("##RcFill", rc.fillColor, true);
    checkActivated();
    itemTooltip("Fill Color");
    ImGui::SameLine();
    ImGui::TextColored(StudioColors::ToVec4(StudioColors::TextSecondary), "Fill");
    ImGui::SameLine(0.0f, 10.0f);
    changed |= colorEdit4u8("##RcStroke", rc.strokeColor, true);
    checkActivated();
    itemTooltip("Stroke Color");
    ImGui::SameLine();
    ImGui::TextColored(StudioColors::ToVec4(StudioColors::TextSecondary), "Stroke");
    ImGui::SameLine(0.0f, 10.0f);
    bool ancClick = ImGui::Checkbox("3D Anchor", &rc.anchoredIn3D);
    if (ancClick) {
        canvas.pushUndoSnapshot();
        changed = true;
    }

    if (changed) {
        canvas.rasterizeLayer(idx, &mesh);
        if (callbacks.onCanvasModified) callbacks.onCanvasModified();
    }
}

bool equalsCaseInsensitive(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

bool drawGroupedFontPickers(StudioUiState& state, CanvasEngine& canvas, TextLayerProps& tx,
                            float famWidth, float styleWidth, bool sameLineStyle, const char* idPrefix) {
    if (canvas.fontFamilies.empty()) canvas.scanSystemFonts();

    const FontFamilyGroup* curFam = nullptr;
    for (const FontFamilyGroup& fg : canvas.fontFamilies) {
        if (!tx.fontPath.empty()) {
            for (const FontFaceEntry& st : fg.styles) {
                if (st.filePath == tx.fontPath) {
                    curFam = &fg;
                    tx.fontFamily = fg.family;
                    tx.fontStyle = st.style;
                    break;
                }
            }
        }
        if (curFam) break;
    }
    if (!curFam) {
        for (const FontFamilyGroup& fg : canvas.fontFamilies) {
            if (equalsCaseInsensitive(fg.family, tx.fontFamily)) {
                curFam = &fg;
                break;
            }
        }
    }
    if (!curFam && !canvas.fontFamilies.empty()) curFam = &canvas.fontFamilies[0];

    std::string famLbl = curFam ? curFam->family : (tx.fontFamily.empty() ? "Arial" : tx.fontFamily);
    std::string styLbl = tx.fontStyle.empty() ? "Regular" : tx.fontStyle;
    bool changed = false;

    char famId[48], styId[48];
    std::snprintf(famId, sizeof(famId), "##%sFam", idPrefix);
    std::snprintf(styId, sizeof(styId), "##%sSty", idPrefix);

    ImGui::SetNextItemWidth(famWidth);
    if (ImGui::BeginCombo(famId, famLbl.c_str())) {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##FamFilter", "Filter font families...", state.fontSearchBuf,
                                 sizeof(state.fontSearchBuf));
        for (const FontFamilyGroup& fg : canvas.fontFamilies) {
            if (!containsCaseInsensitive(fg.family, state.fontSearchBuf)) continue;
            bool sel = (curFam == &fg);
            if (ImGui::Selectable(fg.family.c_str(), sel) && !fg.styles.empty()) {
                canvas.pushUndoSnapshot();
                const FontFaceEntry* chosen = nullptr;
                for (const FontFaceEntry& st : fg.styles) {
                    if (equalsCaseInsensitive(st.style, tx.fontStyle)) { chosen = &st; break; }
                }
                if (!chosen) {
                    for (const FontFaceEntry& st : fg.styles) {
                        if (equalsCaseInsensitive(st.style, "Regular")) { chosen = &st; break; }
                    }
                }
                if (!chosen) chosen = &fg.styles[0];
                tx.fontFamily = fg.family;
                tx.fontStyle = chosen->style;
                tx.fontPath = chosen->filePath;
                curFam = &fg;
                changed = true;
            }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    itemTooltip("Font Family (1)");

    if (sameLineStyle) ImGui::SameLine();
    ImGui::SetNextItemWidth(styleWidth);
    if (ImGui::BeginCombo(styId, styLbl.c_str())) {
        if (curFam) {
            for (const FontFaceEntry& st : curFam->styles) {
                bool sel = (tx.fontPath == st.filePath) || equalsCaseInsensitive(tx.fontStyle, st.style);
                if (ImGui::Selectable(st.style.c_str(), sel)) {
                    canvas.pushUndoSnapshot();
                    tx.fontFamily = curFam->family;
                    tx.fontStyle = st.style;
                    tx.fontPath = st.filePath;
                    changed = true;
                }
                if (sel) ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    itemTooltip("Font Weight / Style (2)");
    return changed;
}

void drawTextSubpanel(StudioUiState& state, CanvasEngine& canvas, const ObjMesh& mesh,
                      const StudioCallbacks& callbacks) {
    int idx = canvas.activeLayerIdx;
    VectorItem* actItem = canvas.activeVectorItem();
    TextLayerProps* pTx = nullptr;
    uint32_t uid = 0;
    if (actItem && actItem->kind == VectorItemKind::Text) {
        pTx = &actItem->text;
        uid = actItem->id;
    } else if (idx >= 0 && idx < static_cast<int>(canvas.layers.size()) &&
               canvas.layers[idx].kind == LayerKind::Text) {
        pTx = &canvas.layers[idx].text;
        uid = canvas.layers[idx].id;
    }
    if (!pTx) {
        ImGui::TextColored(StudioColors::ToVec4(StudioColors::TextMuted),
                           "Click for Point Type or drag an Area Box (T).");
        if (ImGui::Button("Create Lorem ipsum Text", ImVec2(-1.0f, 0.0f))) {
            triggerFillPlaceholderText(state, canvas, mesh, callbacks);
        }
        return;
    }

    TextLayerProps& tx = *pTx;
    tx.syncCharStyles();
    if (state.textEditSyncedLayerId != uid) {
        std::snprintf(state.textEditBuf, sizeof(state.textEditBuf), "%s", tx.text.c_str());
        state.textEditSyncedLayerId = uid;
    }

    bool changed = false;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 58.0f);
    if (ImGui::InputText("##TextBuf", state.textEditBuf, sizeof(state.textEditBuf))) {
        tx.text = state.textEditBuf;
        tx.syncCharStyles();
        changed = true;
    }
    if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();
    itemTooltip("Live Typography Content");
    ImGui::SameLine();
    if (ImGui::Button("Lorem", ImVec2(-1.0f, 0.0f))) {
        canvas.pushUndoSnapshot();
        tx.text = "Lorem ipsum";
        tx.syncCharStyles();
        std::snprintf(state.textEditBuf, sizeof(state.textEditBuf), "%s", tx.text.c_str());
        changed = true;
    }
    itemTooltip("Fill with Placeholder Text (Lorem ipsum)");

    // Adobe Character Panel (1) Family & (2) Style stacked cleanly
    changed |= drawGroupedFontPickers(state, canvas, tx, -1.0f, -1.0f, false, "Char");

    if (ImGui::BeginTable("##TextGrid", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        changed |= compactScrubFloat("Size", "##TxSz", &tx.fontSizePx, 1.0f, 8.0f, 512.0f, "%.0f px", "Font Height (px)");
        if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();
        ImGui::TableSetColumnIndex(1);
        changed |= compactScrubFloat("3D H", "##Tx3H", &tx.worldHeight, 0.005f, 0.03f, 1.50f, "%.2f m", "Projected 3D Height (m)");
        if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        changed |= compactScrubFloat("Track", "##TxTrk", &tx.tracking, 0.4f, -20.0f, 80.0f, "%.1f", "Tracking / Letter Spacing");
        if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();
        ImGui::TableSetColumnIndex(1);
        changed |= compactScrubFloat("Tilt", "##TxIt", &tx.italicShear, 0.01f, -0.50f, 0.50f, "%.2f", "Oblique / Italic Shear");
        if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        changed |= compactScrubFloat("V-Sc", "##TxVSc", &tx.vScalePct, 1.0f, 20.0f, 300.0f, "%.0f%%", "Vertical Scale (%)");
        if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();
        ImGui::TableSetColumnIndex(1);
        changed |= compactScrubFloat("H-Sc", "##TxHSc", &tx.hScalePct, 1.0f, 20.0f, 300.0f, "%.0f%%", "Horizontal Scale (%)");
        if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        changed |= compactScrubFloat("Base", "##TxBas", &tx.baselineShiftPx, 0.5f, -150.0f, 150.0f, "%.1f px", "Baseline Shift");
        if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();
        ImGui::TableSetColumnIndex(1);
        changed |= compactScrubFloat("ChRot", "##TxCRot", &tx.charRotateDeg, 1.0f, -180.0f, 180.0f, "%.0f deg", "Character Rotation");
        if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        changed |= compactScrubFloat("Out", "##TxOut", &tx.outlineWidth, 0.2f, 0.0f, 24.0f, "%.1f px", "Stroke Outline Width");
        if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();
        ImGui::TableSetColumnIndex(1);
        changed |= compactScrubFloat("Rot", "##TxRot", &tx.rotationDeg, 1.0f, -180.0f, 180.0f, "%.0f deg", "Block Rotation Angle");
        if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();
        ImGui::EndTable();
    }

    // Adobe Style Toggles Row: TT All Caps | Tt Small Caps | Strike | Area Box
    float togW = (ImGui::GetContentRegionAvail().x - 12.0f) / 4.0f;
    if (flatTogglePill("TT", tx.allCaps, false, ImVec2(togW, 0))) {
        canvas.pushUndoSnapshot();
        tx.allCaps = !tx.allCaps;
        if (tx.allCaps) tx.smallCaps = false;
        changed = true;
    }
    itemTooltip("All Caps");
    ImGui::SameLine(0.0f, 4.0f);
    if (flatTogglePill("Tt", tx.smallCaps, false, ImVec2(togW, 0))) {
        canvas.pushUndoSnapshot();
        tx.smallCaps = !tx.smallCaps;
        if (tx.smallCaps) tx.allCaps = false;
        changed = true;
    }
    itemTooltip("Small Caps");
    ImGui::SameLine(0.0f, 4.0f);
    if (flatTogglePill("Strike", tx.strikethrough, false, ImVec2(togW, 0))) {
        canvas.pushUndoSnapshot();
        tx.strikethrough = !tx.strikethrough;
        changed = true;
    }
    itemTooltip("Strikethrough");
    ImGui::SameLine(0.0f, 4.0f);
    if (flatTogglePill("Area Box", tx.isAreaBox, false, ImVec2(togW, 0))) {
        canvas.pushUndoSnapshot();
        tx.isAreaBox = !tx.isAreaBox;
        changed = true;
    }
    itemTooltip("Area Type Text Box (Reflow & Resize Bounds)");

    if (tx.isAreaBox) {
        if (ImGui::BeginTable("##AreaBoxGrid", 2, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            changed |= compactScrubFloat("Box W", "##TxBW", &tx.boxWidthPx, 2.0f, 40.0f, 4096.0f, "%.0f px", "Area Text Box Width");
            if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();
            ImGui::TableSetColumnIndex(1);
            changed |= compactScrubFloat("Box H", "##TxBH", &tx.boxHeightPx, 2.0f, 24.0f, 4096.0f, "%.0f px", "Area Text Box Height");
            if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();
            ImGui::EndTable();
        }
    }

    ImGui::Separator();
    ImGui::TextColored(StudioColors::ToVec4(StudioColors::TextSecondary), "Per-Letter Color & Style");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 106.0f);
    if (flatTogglePill("All", tx.selectedCharIdx < 0, false, ImVec2(32.0f, 18.0f))) {
        tx.selectedCharIdx = -1;
    }
    itemTooltip("Edit Whole String");
    ImGui::SameLine(0.0f, 4.0f);
    if (flatTogglePill("Touch", state.activeTool == StudioTool::TouchType, false, ImVec2(66.0f, 18.0f))) {
        state.activeTool = StudioTool::TouchType;
        state.activeTypeToolVariant = 1;
        state.activePropsCat = PropertiesCategory::Text;
    }
    itemTooltip("Touch Type Tool (Shift+T) - Transform Individual Letters on Canvas");

    // Interactive per-character tile strip
    float availStripW = ImGui::GetContentRegionAvail().x;
    float curX = 0.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 2.0f));
    for (int ci = 0; ci < static_cast<int>(tx.text.size()); ++ci) {
        if (ci > 0 && curX + 22.0f <= availStripW) {
            ImGui::SameLine();
        } else if (ci > 0) {
            curX = 0.0f;
        }
        char c = tx.text[ci];
        char lbl[8] = { (c == ' ' || c == '\t' || c == '\n') ? '_' : c, '#', '#', 0 };
        bool cSel = (tx.selectedCharIdx == ci);
        ImGui::PushID(1200 + ci);
        ImVec2 bp = ImGui::GetCursorScreenPos();
        ImVec4 bg = cSel ? StudioColors::ToVec4(StudioColors::ToolActiveBg) : StudioColors::ToVec4(StudioColors::ControlBg);
        ImVec4 brd = cSel ? StudioColors::ToVec4(StudioColors::AmberAccent) : StudioColors::ToVec4(StudioColors::BorderLight);
        ImGui::PushStyleColor(ImGuiCol_Button, bg);
        ImGui::PushStyleColor(ImGuiCol_Border, brd);
        if (ImGui::Button("##ch", ImVec2(20.0f, 22.0f))) {
            tx.selectedCharIdx = cSel ? -1 : ci;
        }
        ImGui::PopStyleColor(2);
        Color4u8 dispCol = (ci < static_cast<int>(tx.charStyles.size()) && tx.charStyles[ci].hasColor)
                           ? tx.charStyles[ci].color
                           : tx.fillColor;
        char glyphBuf[2] = { lbl[0], 0 };
        ImVec2 gSz = ImGui::CalcTextSize(glyphBuf);
        dl->AddText(ImVec2(bp.x + (20.0f - gSz.x) * 0.5f, bp.y + 1.0f), StudioColors::TextPrimary, glyphBuf);
        dl->AddRectFilled(ImVec2(bp.x + 2.0f, bp.y + 17.0f), ImVec2(bp.x + 18.0f, bp.y + 20.0f), toImU32(dispCol), 1.0f);
        ImGui::PopID();
        curX += 22.0f;
    }
    ImGui::PopStyleVar();

    if (tx.selectedCharIdx >= 0 && tx.selectedCharIdx < static_cast<int>(tx.charStyles.size())) {
        CharStyle& cs = tx.charStyles[tx.selectedCharIdx];
        ImGui::PushStyleColor(ImGuiCol_ChildBg, StudioColors::ToVec4(StudioColors::PanelHeader));
        if (ImGui::BeginChild("##SelCharBox", ImVec2(-1.0f, 78.0f), ImGuiChildFlags_Borders)) {
            char chDisp = tx.text[tx.selectedCharIdx];
            Color4u8 editCol = cs.hasColor ? cs.color : tx.fillColor;
            if (colorEdit4u8("##SelChCol", editCol, true)) {
                cs.hasColor = true;
                cs.color = editCol;
                changed = true;
            }
            if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();
            itemTooltip("Override Color for Selected Letter");
            ImGui::SameLine();
            ImGui::TextColored(StudioColors::ToVec4(StudioColors::AmberAccent),
                               "Letter #%d '%c'", tx.selectedCharIdx + 1,
                               (chDisp == ' ' ? '_' : chDisp));
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 46.0f);
            if (ImGui::Button("Reset", ImVec2(46.0f, 18.0f))) {
                canvas.pushUndoSnapshot();
                cs = CharStyle{};
                changed = true;
            }

            if (ImGui::BeginTable("##CharStyGrid", 2, ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                float scPct = cs.scale * 100.0f;
                if (compactScrubFloat("Scale", "##CsSc", &scPct, 1.0f, 20.0f, 300.0f, "%.0f%%", "Letter Scale")) {
                    cs.scale = std::clamp(scPct * 0.01f, 0.2f, 3.0f);
                    changed = true;
                }
                if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();
                ImGui::TableSetColumnIndex(1);
                changed |= compactScrubFloat("Rot", "##CsRot", &cs.rotDeg, 1.0f, -180.0f, 180.0f, "%.0f deg", "Letter Rotation");
                if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                changed |= compactScrubFloat("Base", "##CsBas", &cs.baselinePx, 0.5f, -150.0f, 150.0f, "%.1f px", "Letter Baseline Offset");
                if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();
                ImGui::TableSetColumnIndex(1);
                changed |= compactScrubFloat("X Off", "##CsHOff", &cs.hOffsetPx, 0.5f, -150.0f, 150.0f, "%.1f px", "Letter Horizontal Nudge");
                if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();
                ImGui::EndTable();
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    changed |= colorEdit4u8("##TxFillCol", tx.fillColor, true);
    if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();
    itemTooltip("Default Text Fill Color");
    ImGui::SameLine();
    ImGui::TextColored(StudioColors::ToVec4(StudioColors::TextSecondary), "Fill");
    ImGui::SameLine(0.0f, 10.0f);
    changed |= colorEdit4u8("##TxStrokeCol", tx.outlineColor, true);
    if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();
    itemTooltip("Text Outline Stroke Color");
    ImGui::SameLine();
    ImGui::TextColored(StudioColors::ToVec4(StudioColors::TextSecondary), "Edge");

    float alignX[3] = {0.20f, 0.50f, 0.80f};
    SmallIconKind alignIcons[3] = {SmallIconKind::AlignLeft, SmallIconKind::AlignCenterH, SmallIconKind::AlignRight};
    const char* alignTips[3] = {"Nudge Left / Align Left", "Center on Vehicle / UV Center", "Nudge Right / Align Right"};
    for (int ai = 0; ai < 3; ++ai) {
        ImGui::SameLine(ai == 0 ? ImGui::GetContentRegionAvail().x - 62.0f : 0.0f, 3.0f);
        ImGui::PushID(800 + ai);
        ImVec2 bp = ImGui::GetCursorScreenPos();
        if (ImGui::Button("##al", ImVec2(18.0f, 18.0f))) {
            canvas.pushUndoSnapshot();
            if (tx.anchoredIn3D && mesh.loaded) {
                if (ai == 1) {
                    Vec3 targetP{mesh.centerSymmetryX, tx.pos3D.y, tx.pos3D.z};
                    RayHit hc = mesh.projectPointToSurface(targetP, tx.nrm3D, 0.75f);
                    if (hc.hit) {
                        tx.pos3D = hc.pos;
                        tx.nrm3D = hc.normal;
                    } else {
                        tx.pos3D.x = mesh.centerSymmetryX;
                    }
                } else {
                    float dirSign = (ai == 0) ? -1.0f : 1.0f;
                    Vec3 T = tx.right3D - tx.nrm3D * dot(tx.right3D, tx.nrm3D);
                    if (lengthSq(T) < 1e-6f) T = cross(Vec3{0, 1, 0}, tx.nrm3D);
                    T = normalized(T);
                    Vec3 targetP = tx.pos3D + T * (dirSign * 0.08f);
                    RayHit hn = mesh.projectPointToSurface(targetP, tx.nrm3D, 0.45f);
                    if (hn.hit) {
                        tx.pos3D = hn.pos;
                        tx.nrm3D = hn.normal;
                    }
                }
            } else {
                tx.uvPos.x = alignX[ai];
                tx.anchoredIn3D = false;
                if (actItem && actItem->kind == VectorItemKind::Text) canvas.syncItem2DTo3D(*actItem, &mesh);
                else canvas.syncLayer2DTo3D(idx, &mesh);
            }
            changed = true;
        }
        itemTooltip(alignTips[ai]);
        drawSmallIcon(dl, alignIcons[ai], bp + ImVec2(2, 2), bp + ImVec2(16, 16), StudioColors::TextPrimary);
        ImGui::PopID();
    }

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.56f);
    if (ImGui::BeginCombo("##TxMirror", mirrorModeName(tx.mirrorMode))) {
        MirrorTextMode modes[] = {MirrorTextMode::ReadableBothSides, MirrorTextMode::TrueFlip, MirrorTextMode::None};
        for (MirrorTextMode m : modes) {
            bool sel = (tx.mirrorMode == m);
            if (ImGui::Selectable(mirrorModeName(m), sel)) {
                canvas.pushUndoSnapshot();
                tx.mirrorMode = m;
                tx.mirrorX = (m != MirrorTextMode::None);
                changed = true;
            }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    itemTooltip("L<->R Symmetry Text Orientation");
    ImGui::SameLine();
    if (ImGui::Checkbox("3D", &tx.anchoredIn3D)) {
        canvas.pushUndoSnapshot();
        changed = true;
    }
    itemTooltip("Lock Placement to 3D Car Surface");
    ImGui::SameLine();
    bool mirChk = tx.mirrorX;
    if (ImGui::Checkbox("Mirror", &mirChk)) {
        canvas.setMirrorOnActiveItem(mirChk, &mesh);
        state.brush.mirrorX = mirChk;
        if (callbacks.onCanvasModified) callbacks.onCanvasModified();
        return;
    }

    if (changed) {
        canvas.rasterizeLayer(idx, &mesh);
        if (callbacks.onCanvasModified) callbacks.onCanvasModified();
    }
}

void drawDecalSubpanel(StudioUiState& state, CanvasEngine& canvas, const ObjMesh& mesh,
                       const StudioCallbacks& callbacks) {
    int idx = canvas.activeLayerIdx;
    VectorItem* actItem = canvas.activeVectorItem();
    DecalLayerProps* pDc = nullptr;
    if (actItem && actItem->kind == VectorItemKind::Image) {
        pDc = &actItem->image;
    } else if (idx >= 0 && idx < static_cast<int>(canvas.layers.size()) &&
               canvas.layers[idx].kind == LayerKind::Decal) {
        pDc = &canvas.layers[idx].decal;
    }
    if (!pDc) {
        ImGui::TextColored(StudioColors::ToVec4(StudioColors::TextMuted),
                           "Use Image (D) tool or File > Import Image.");
        if (ImGui::Button("Import Image...", ImVec2(-1.0f, 0.0f))) {
            if (callbacks.onImportImage) callbacks.onImportImage();
            else if (callbacks.onImportDecal) callbacks.onImportDecal();
        }
        return;
    }

    DecalLayerProps& dc = *pDc;
    std::string fname = dc.sourcePath.empty()
                        ? "(Embedded Image)"
                        : std::filesystem::path(dc.sourcePath).filename().string();
    ImGui::TextColored(StudioColors::ToVec4(StudioColors::TextSecondary), "%s (%dx%d)",
                       fname.c_str(), dc.imgW, dc.imgH);
    if (ImGui::Button("Replace Image...", ImVec2(-1.0f, 0.0f))) {
        if (callbacks.onImportImage) callbacks.onImportImage();
        else if (callbacks.onImportDecal) callbacks.onImportDecal();
    }

    bool changed = false;
    if (ImGui::BeginTable("##DecalGrid", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        float wPct = dc.uvScale.x * 100.0f;
        if (compactScrubFloat("W", "##DcW", &wPct, 0.25f, 2.0f, 150.0f, "%.1f%%", "Image UV Width")) {
            float sx = std::clamp(wPct * 0.01f, 0.02f, 1.50f);
            dc.uvScale.x = sx;
            if (state.decalLockAspect && dc.imgW > 0 && dc.imgH > 0) {
                dc.uvScale.y = sx * (static_cast<float>(dc.imgH) / dc.imgW);
            }
            changed = true;
        }
        if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();
        ImGui::TableSetColumnIndex(1);
        float hPct = dc.uvScale.y * 100.0f;
        if (compactScrubFloat("H", "##DcH", &hPct, 0.25f, 2.0f, 150.0f, "%.1f%%", "Image UV Height")) {
            float sy = std::clamp(hPct * 0.01f, 0.02f, 1.50f);
            dc.uvScale.y = sy;
            if (state.decalLockAspect && dc.imgW > 0 && dc.imgH > 0) {
                dc.uvScale.x = sy * (static_cast<float>(dc.imgW) / dc.imgH);
            }
            changed = true;
        }
        if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        changed |= compactScrubFloat("3D W", "##Dc3W", &dc.worldWidth, 0.01f, 0.05f, 2.50f, "%.2f m", "Projected 3D Image Width");
        if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();
        ImGui::TableSetColumnIndex(1);
        changed |= compactScrubFloat("Rot", "##DcRot", &dc.rotationDeg, 1.0f, -180.0f, 180.0f, "%.0f deg", "Rotation Angle");
        if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();
        ImGui::EndTable();
    }

    changed |= colorEdit4u8("##DcTint", dc.tint, true);
    if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();
    itemTooltip("Image Multiplicative Tint");
    ImGui::SameLine();
    ImGui::TextColored(StudioColors::ToVec4(StudioColors::TextSecondary), "Tint");
    ImGui::SameLine(0.0f, 8.0f);
    ImGui::Checkbox("Lock", &state.decalLockAspect);
    ImGui::SameLine();
    if (ImGui::Checkbox("3D", &dc.anchoredIn3D)) {
        canvas.pushUndoSnapshot();
        changed = true;
    }
    itemTooltip("Lock Placement to 3D Car Surface");
    ImGui::SameLine();
    bool mirDc = dc.mirrorX;
    if (ImGui::Checkbox("Mirror", &mirDc)) {
        canvas.setMirrorOnActiveItem(mirDc, &mesh);
        state.brush.mirrorX = mirDc;
        if (callbacks.onCanvasModified) callbacks.onCanvasModified();
        return;
    }

    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##DcMir", mirrorModeName(dc.mirrorMode))) {
        MirrorTextMode modes[] = {MirrorTextMode::ReadableBothSides, MirrorTextMode::TrueFlip, MirrorTextMode::None};
        for (MirrorTextMode m : modes) {
            bool sel = (dc.mirrorMode == m);
            if (ImGui::Selectable(mirrorModeName(m), sel)) {
                canvas.pushUndoSnapshot();
                dc.mirrorMode = m;
                dc.mirrorX = (m != MirrorTextMode::None);
                changed = true;
            }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    if (changed) {
        canvas.rasterizeLayer(idx, &mesh);
        if (callbacks.onCanvasModified) callbacks.onCanvasModified();
    }
}

void drawRulerSubpanel(StudioUiState& state, CanvasEngine& canvas, const ObjMesh& mesh,
                       const StudioCallbacks& callbacks) {
    int idx = canvas.activeLayerIdx;
    VectorItem* actItem = canvas.activeVectorItem();
    RulerStripeProps* pRu = nullptr;
    if (actItem && actItem->kind == VectorItemKind::PenPath) {
        pRu = &actItem->pen;
    } else if (idx >= 0 && idx < static_cast<int>(canvas.layers.size()) &&
               canvas.layers[idx].kind == LayerKind::RulerStripe) {
        pRu = &canvas.layers[idx].ruler;
    }
    if (!pRu) {
        ImGui::TextColored(StudioColors::ToVec4(StudioColors::TextMuted),
                           "Click car or UV canvas with Pen (P) or Line (\\).");
        return;
    }

    RulerStripeProps& pen = *pRu;
    bool changed = false;

    float halfW = (ImGui::GetContentRegionAvail().x - 6.0f) * 0.5f;
    if (flatTogglePill("Line", !pen.smoothCurve, false, ImVec2(halfW, 0))) {
        canvas.pushUndoSnapshot();
        pen.smoothCurve = false;
        state.activeTool = StudioTool::Line;
        state.activePenToolVariant = 1;
        changed = true;
    }
    ImGui::SameLine();
    if (flatTogglePill("Bezier Pen", pen.smoothCurve, false, ImVec2(halfW, 0))) {
        canvas.pushUndoSnapshot();
        pen.smoothCurve = true;
        state.activeTool = StudioTool::Pen;
        state.activePenToolVariant = 0;
        changed = true;
    }

    float thirdTogW = (ImGui::GetContentRegionAvail().x - 10.0f) / 3.0f;
    if (flatTogglePill("Filled", pen.filled, false, ImVec2(thirdTogW, 0))) {
        canvas.pushUndoSnapshot();
        pen.filled = !pen.filled;
        if (pen.filled && pen.nodes.size() >= 3) pen.closed = true;
        changed = true;
    }
    itemTooltip("Fill Closed Shape with Fill Color");
    ImGui::SameLine();
    if (flatTogglePill("Closed", pen.closed, false, ImVec2(thirdTogW, 0))) {
        canvas.pushUndoSnapshot();
        pen.closed = !pen.closed;
        changed = true;
    }
    itemTooltip("Close Contour Between Last and First Anchor");
    ImGui::SameLine();
    if (flatTogglePill("Stroke", pen.hasStroke, false, ImVec2(thirdTogW, 0))) {
        canvas.pushUndoSnapshot();
        pen.hasStroke = !pen.hasStroke;
        changed = true;
    }
    itemTooltip("Outline Path Contour with Stroke Color");

    if (ImGui::BeginTable("##RulerGrid", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        changed |= compactScrubFloat("Width", "##RuPx", &pen.widthPx, 0.5f, 1.0f, 200.0f, "%.0f px", "2D Stroke Width");
        if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();
        ImGui::TableSetColumnIndex(1);
        changed |= compactScrubFloat("3D W", "##Ru3W", &pen.worldWidth, 0.002f, 0.003f, 0.50f, "%.3f m", "3D Stroke Width");
        if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        changed |= compactScrubFloat("Taper", "##RuTap", &pen.endTaper, 0.01f, 0.0f, 1.0f, "%.2f", "Endpoint Taper (Open Paths)");
        if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();
        ImGui::TableSetColumnIndex(1);
        changed |= compactScrubFloat("Out", "##RuOut", &pen.outlineWidth, 0.2f, 0.0f, 24.0f, "%.1f px", "Outer Halo Width");
        if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();
        ImGui::EndTable();
    }

    changed |= colorEdit4u8("##RuFill", pen.fillColor, true);
    if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();
    itemTooltip("Closed Path Fill Color");
    ImGui::SameLine();
    ImGui::TextColored(StudioColors::ToVec4(StudioColors::TextSecondary), "Fill");
    ImGui::SameLine(0.0f, 8.0f);
    changed |= colorEdit4u8("##RuStroke", pen.color, true);
    if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();
    itemTooltip("Path Stroke Color");
    ImGui::SameLine();
    ImGui::TextColored(StudioColors::ToVec4(StudioColors::TextSecondary), "Stroke");
    if (pen.outlineWidth > 0.05f) {
        ImGui::SameLine(0.0f, 8.0f);
        changed |= colorEdit4u8("##RuOutCol", pen.outlineColor, true);
        if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();
        itemTooltip("Outer Halo Color");
    }
    ImGui::Checkbox("3D Seam Proj", &pen.use3DSpace);
    if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();
    itemTooltip("Project Path Across UV Seams on 3D Surface");
    ImGui::SameLine();
    bool mirPen = pen.mirrorX;
    if (ImGui::Checkbox("Mirror", &mirPen)) {
        canvas.setMirrorOnActiveItem(mirPen, &mesh);
        state.brush.mirrorX = mirPen;
        if (callbacks.onCanvasModified) callbacks.onCanvasModified();
        return;
    }
    itemTooltip("Bilateral X=0 Mirror Symmetry");

    ImGui::TextColored(StudioColors::ToVec4(StudioColors::TextSecondary),
                       "Anchors: %zu (RMB Anchor = Delete)", pen.nodes.size());

    float thirdW = (ImGui::GetContentRegionAvail().x - 10.0f) / 3.0f;
    if (ImGui::Button("Undo Node", ImVec2(thirdW, 0)) && !pen.nodes.empty()) {
        int lastPt = static_cast<int>(pen.nodes.size()) - 1;
        if (actItem && callbacks.onRemovePenPoint) {
            callbacks.onRemovePenPoint(canvas.selectedItemIdx, lastPt);
            return;
        }
        canvas.pushUndoSnapshot();
        pen.nodes.pop_back();
        if (pen.nodes.size() < 3) pen.closed = false;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear", ImVec2(thirdW, 0)) && !pen.nodes.empty()) {
        canvas.pushUndoSnapshot();
        pen.nodes.clear();
        pen.closed = false;
        state.rulerFinishRequested = true;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Finish", ImVec2(thirdW, 0))) {
        state.rulerFinishRequested = true;
    }

    if (changed) {
        canvas.rasterizeLayer(idx, &mesh);
        if (callbacks.onCanvasModified) callbacks.onCanvasModified();
    }
}

void drawMeshEditSubpanel(StudioUiState& state, ObjMesh& mesh, const StudioCallbacks& callbacks) {
    int selVerts = 0;
    for (uint8_t s : mesh.vertexSelected) if (s) ++selVerts;
    int selFaces = 0;
    for (const Triangle& t : mesh.tris) if (t.selected) ++selFaces;

    if (ImGui::BeginTable("##MeshGrid", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        compactScrubFloat("Sel r", "##SelR", &state.vertexSelectRadius, 0.01f, 0.02f, 1.00f, "%.2f m", "Vertex Selection Radius");
        ImGui::TableSetColumnIndex(1);
        compactScrubFloat("Angle", "##MaskAng", &state.faceMaskAngleDeg, 1.0f, 5.0f, 90.0f, "%.0f deg", "Connected Face Angle Limit");
        ImGui::EndTable();
    }
    ImGui::Checkbox("Connected Angle Mask", &state.faceMaskConnectedAngle);

    float thirdW = (ImGui::GetContentRegionAvail().x - 10.0f) / 3.0f;
    if (flatTogglePill("Grab (G)", state.editSubTool == EditSubTool::Grab, true, ImVec2(thirdW, 0))) {
        state.editSubTool = EditSubTool::Grab;
        state.activeTool = StudioTool::VertexEdit;
        state.activeMode = StudioMode::EditMesh;
    }
    ImGui::SameLine();
    if (flatTogglePill("Rotate (R)", state.editSubTool == EditSubTool::Rotate, true, ImVec2(thirdW, 0))) {
        state.editSubTool = EditSubTool::Rotate;
        state.activeTool = StudioTool::VertexEdit;
        state.activeMode = StudioMode::EditMesh;
    }
    ImGui::SameLine();
    if (flatTogglePill("Scale (S)", state.editSubTool == EditSubTool::Scale, true, ImVec2(thirdW, 0))) {
        state.editSubTool = EditSubTool::Scale;
        state.activeTool = StudioTool::VertexEdit;
        state.activeMode = StudioMode::EditMesh;
    }

    if (ImGui::Button("Clear Sel", ImVec2(thirdW, 0)) && mesh.loaded) {
        mesh.clearSelection();
        state.meshGpuDirty = true;
        if (callbacks.onMeshModified) callbacks.onMeshModified();
    }
    ImGui::SameLine();
    if (ImGui::Button("Invert", ImVec2(thirdW, 0)) && mesh.loaded) {
        for (Triangle& t : mesh.tris) t.selected = !t.selected;
        mesh.syncRenderVertices();
        state.meshGpuDirty = true;
        if (callbacks.onMeshModified) callbacks.onMeshModified();
    }
    ImGui::SameLine();
    if (ImGui::Button("Smooth", ImVec2(thirdW, 0)) && mesh.loaded && selVerts > 0) {
        mesh.smoothSelectedVertices(state.smoothStrength, state.smoothIterations, state.brush.mirrorX);
        state.meshGpuDirty = true;
        if (callbacks.onMeshModified) callbacks.onMeshModified();
    }
    ImGui::TextColored(StudioColors::ToVec4(StudioColors::TextSecondary),
                       "Selected: %d verts | %d faces", selVerts, selFaces);
}

void drawMaterialSubpanel(StudioUiState& state) {
    if (ImGui::BeginTable("##MatGrid", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        compactScrubFloat("Metal", "##MatMet", &state.material.metallic, 0.01f, 0.0f, 1.0f, "%.2f", "Metallic Reflectance");
        ImGui::TableSetColumnIndex(1);
        compactScrubFloat("Rough", "##MatRgh", &state.material.roughness, 0.01f, 0.04f, 1.0f, "%.2f", "Surface Roughness");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        compactScrubFloat("Coat", "##MatCoat", &state.material.clearcoat, 0.01f, 0.0f, 1.0f, "%.2f", "Automotive Clearcoat");
        ImGui::TableSetColumnIndex(1);
        compactScrubFloat("Ct R", "##MatCtR", &state.material.clearcoatRoughness, 0.01f, 0.01f, 0.5f, "%.2f", "Clearcoat Roughness");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        compactScrubFloat("IBL", "##MatIBL", &state.material.envIntensity, 0.02f, 0.0f, 3.0f, "%.2f", "Studio Environment Reflection");
        ImGui::TableSetColumnIndex(1);
        ImGui::Checkbox("Floor Grid", &state.material.showFloorGrid);
        ImGui::EndTable();
    }
    ImGui::Checkbox("Symmetry Plane (X=0)", &state.material.showSymmetryPlane);
}

void drawLightsSubpanel(StudioUiState& state, const StudioCallbacks& callbacks) {
    for (int i = 0; i < 3; ++i) {
        StudioLightUi& lt = state.lights[i];
        ImGui::PushID(i);
        ImGui::Checkbox(lt.label.c_str(), &lt.enabled);
        if (lt.enabled) {
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 95.0f);
            float c[3] = {lt.color.x, lt.color.y, lt.color.z};
            if (ImGui::ColorEdit3("##LtCol", c, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
                lt.color = {c[0], c[1], c[2]};
            }
            itemTooltip("Light Color");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::DragFloat("##LtInt", &lt.intensity, 0.05f, 0.0f, 8.0f, "%.1f W");
            itemTooltip("Light Intensity");
        }
        ImGui::PopID();
    }
    ImGui::Spacing();
    if (flatTogglePill("Render Beauty Shot (F12)", true, true, ImVec2(-1.0f, 22.0f))) {
        if (callbacks.onRenderBeautyShot) callbacks.onRenderBeautyShot();
    }
    itemTooltip("Render Clean 3D Showroom Shot (F12)");
}

void drawOutlinerTabContent(StudioUiState& state, CanvasEngine& canvas, ObjMesh& mesh,
                            const StudioCallbacks& callbacks) {
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##OutlinerSearch", "Search Scene & Parts...", state.outlinerSearchBuf,
                             sizeof(state.outlinerSearchBuf));

    float halfW = (ImGui::GetContentRegionAvail().x - 6.0f) * 0.5f;
    if (ImGui::Button("Show All", ImVec2(halfW, 0))) {
        for (SubGroup& g : mesh.groups) g.visible = true;
        state.meshGpuDirty = true;
        if (callbacks.onMeshModified) callbacks.onMeshModified();
    }
    ImGui::SameLine();
    if (ImGui::Button("Hide All", ImVec2(halfW, 0))) {
        for (SubGroup& g : mesh.groups) g.visible = false;
        state.meshGpuDirty = true;
        if (callbacks.onMeshModified) callbacks.onMeshModified();
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, StudioColors::ToVec4(StudioColors::CanvasTrough));
    if (ImGui::BeginChild("##OutlinerTree", ImVec2(-1.0f, -1.0f), ImGuiChildFlags_None)) {
        if (ImGui::TreeNodeEx("Scene Collection", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextColored(StudioColors::ToVec4(StudioColors::TextSecondary), "  Camera (Persp/Ortho)");
            for (int li = 0; li < 3; ++li) {
                ImGui::PushID(1000 + li);
                ImGui::Checkbox("##lvis", &state.lights[li].enabled);
                ImGui::SameLine();
                ImGui::TextColored(StudioColors::ToVec4(StudioColors::AmberAccent), "%s", state.lights[li].label.c_str());
                ImGui::PopID();
            }

            std::string meshTitle = mesh.loaded
                                    ? std::filesystem::path(mesh.sourcePath).filename().string()
                                    : "Car Mesh (None)";
            if (ImGui::TreeNodeEx(meshTitle.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                float outlinerW = ImGui::GetContentRegionAvail().x;
                for (int i = 0; i < static_cast<int>(mesh.groups.size()); ++i) {
                    SubGroup& g = mesh.groups[i];
                    if (!containsCaseInsensitive(g.name, state.outlinerSearchBuf)) continue;
                    ImGui::PushID(i);
                    if (ImGui::Checkbox("##gvis", &g.visible)) {
                        state.meshGpuDirty = true;
                        if (callbacks.onMeshModified) callbacks.onMeshModified();
                    }
                    ImGui::SameLine();
                    ImGui::Selectable(g.name.c_str(), false, ImGuiSelectableFlags_None,
                                      ImVec2(std::max(60.0f, outlinerW - 76.0f), 0.0f));
                    if (ImGui::BeginPopupContextItem("##OutlinerPartCtx")) {
                        if (ImGui::MenuItem("Isolate Mesh Part")) {
                            for (int k = 0; k < static_cast<int>(mesh.groups.size()); ++k) {
                                mesh.groups[k].visible = (k == i);
                            }
                            state.meshGpuDirty = true;
                            if (callbacks.onMeshModified) callbacks.onMeshModified();
                        }
                        if (ImGui::MenuItem("Show All Parts")) {
                            for (SubGroup& sg : mesh.groups) sg.visible = true;
                            state.meshGpuDirty = true;
                            if (callbacks.onMeshModified) callbacks.onMeshModified();
                        }
                        if (ImGui::MenuItem("Create Layer for Part")) {
                            canvas.addRasterLayer(g.name, false, {0, 0, 0, 0});
                            if (callbacks.onCanvasModified) callbacks.onCanvasModified();
                        }
                        if (ImGui::MenuItem("Hide Part")) {
                            g.visible = false;
                            state.meshGpuDirty = true;
                            if (callbacks.onMeshModified) callbacks.onMeshModified();
                        }
                        ImGui::EndPopup();
                    }
                    char cntBuf[24];
                    std::snprintf(cntBuf, sizeof(cntBuf), "%d", g.triCount);
                    float cntW = ImGui::CalcTextSize(cntBuf).x;
                    float prevRightX = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;
                    if (prevRightX + cntW + 10.0f < outlinerW) {
                        ImGui::SameLine(outlinerW - cntW - 6.0f);
                    } else {
                        ImGui::SameLine();
                    }
                    ImGui::TextColored(StudioColors::ToVec4(StudioColors::TextMuted), "%s", cntBuf);
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
            ImGui::TreePop();
        }
        if (ImGui::BeginPopupContextWindow("##OutlinerTreeCtx", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::MenuItem("Show All Parts")) {
                for (SubGroup& sg : mesh.groups) sg.visible = true;
                state.meshGpuDirty = true;
                if (callbacks.onMeshModified) callbacks.onMeshModified();
            }
            ImGui::EndPopup();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void drawShortcutsHelpModal(StudioUiState& state, const StudioCallbacks& callbacks) {
    if (state.showShortcutsModal) {
        ImGui::OpenPopup("Keyboard Shortcuts##Modal");
        state.showShortcutsModal = false;
    }
    if (ImGui::BeginPopupModal("Keyboard Shortcuts##Modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        struct ScRow { const char* key; const char* desc; };
        static constexpr ScRow kRows[] = {
            {"Ctrl + R",        "Toggle Pixel Rulers & Guides"},
            {"Ctrl + Z / Y",    "Undo / Redo (200 steps)"},
            {"Ctrl + C / X / V","Copy / Cut / Paste Selection or Image"},
            {"Ctrl + N / O",    "New Project / Open OBJ Mesh"},
            {"Ctrl + S / L",    "Save / Open Project (.cr)"},
            {"Ctrl + Shift + E","Export Flattened PNG"},
            {"V / A",           "Selection Tool / Direct Selection"},
            {"B / E / G / I",   "Brush / Eraser / Bucket / Eyedropper"},
            {"P / \\ / M",      "Pen Tool / Line Tool / Rectangle Tool"},
            {"T / Shift+T / D", "Type / Touch Type Tool / Import Image"},
            {"H / Z / O",       "Hand Tool / Zoom Tool / Orbit 3D"},
            {"Shift + M",       "Toggle Mirror Symmetry"},
            {"1 / 3 / 7 / 5",   "Front / Side / Top / Persp-Ortho"},
            {"Shift (Drag)",    "Snap 45 deg / Constrain Square"},
            {"F12",             "Render Photo PNG"},
        };
        if (ImGui::BeginTable("##ScTbl", 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg)) {
            for (const ScRow& r : kRows) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(StudioColors::ToVec4(StudioColors::StudioBlue), "%-18s", r.key);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(r.desc);
            }
            ImGui::EndTable();
        }
        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(100.0f, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (state.requestOpenUpdatesModal) {
        ImGui::OpenPopup("Coatroom Updates##Modal");
        state.requestOpenUpdatesModal = false;
    }
    if (ImGui::BeginPopupModal("Coatroom Updates##Modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Coatroom v1.2.0 - Automotive Livery Studio");
        if (state.otaUpdateAvailable) {
            std::string availMsg = state.otaLatestVersion.empty()
                                   ? "New update available!"
                                   : ("New version available: Coatroom v" + state.otaLatestVersion);
            ImGui::TextColored(ImVec4(0.85f, 0.61f, 0.15f, 1.0f), "%s", availMsg.c_str());
        }
        ImGui::Separator();
        ImGui::TextWrapped("Checks signed GitHub Release manifests over HTTPS and verifies SHA-256 checksums via Windows BCrypt before launching updates.");
        ImGui::Spacing();
        ImGui::TextColored(StudioColors::ToVec4(StudioColors::TextSecondary), "Status: %s", state.statusText.c_str());
        ImGui::Spacing();
        if (ImGui::Button("Check Online Now", ImVec2(140.0f, 0))) {
            if (callbacks.onCheckUpdates) callbacks.onCheckUpdates();
        }
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(90.0f, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (state.requestOpenTermsModal) {
        ImGui::OpenPopup("Terms of Use##Modal");
        state.requestOpenTermsModal = false;
    }
    if (ImGui::BeginPopupModal("Terms of Use##Modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Coatroom Software License & Terms of Use");
        ImGui::Separator();
        ImGui::BeginChild("##TouScroll", ImVec2(440.0f, 210.0f), ImGuiChildFlags_Borders);
        ImGui::TextWrapped(
            "1. OWNERSHIP OF CONTENT: All liveries, textures, decals, and .cr project files created in Coatroom belong 100%% to you.\n\n"
            "2. PRIVACY & NETWORKING: Coatroom runs completely offline by default and includes no telemetry. Network access occurs solely when you explicitly click Check for Updates.\n\n"
            "3. WARRANTY DISCLAIMER: Provided \"AS IS\" without warranty of any kind.\n\n"
            "4. THIRD-PARTY LIBRARIES: Built with Dear ImGui (MIT), stb libraries (MIT/Public Domain), tinyobjloader (MIT), nlohmann/json (MIT), and Microsoft Direct3D 11.");
        ImGui::EndChild();
        ImGui::Spacing();
        if (callbacks.onOpenTerms && ImGui::Button("Open Full Document...", ImVec2(170.0f, 0))) {
            callbacks.onOpenTerms();
        }
        if (callbacks.onOpenTerms) ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(90.0f, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

const char* workspaceDisplayName(WorkspaceTab tab) {
    switch (tab) {
    case WorkspaceTab::Layout:       return "Essentials";
    case WorkspaceTab::UvEditing:    return "2D UV Artboard";
    case WorkspaceTab::TexturePaint: return "3D Paint";
    case WorkspaceTab::Shading:      return "Shading";
    case WorkspaceTab::Rendering:    return "Showroom";
    default:                         return "Essentials";
    }
}

void applyWorkspacePreset(StudioUiState& state, WorkspaceTab tab) {
    state.activeWorkspace = tab;
    state.dockLayoutResetRequested = true;
    switch (tab) {
    case WorkspaceTab::Layout:
        state.show3DOverlays = true;
        state.shadingMode = ShadingMode::Rendered;
        state.activeRightTab = RightPanelTab::Properties;
        break;
    case WorkspaceTab::UvEditing:
        state.show3DOverlays = true;
        state.uvShowWireframe = true;
        state.showRulers = true;
        state.uvShowGridRulers = true;
        state.activeRightTab = RightPanelTab::Properties;
        break;
    case WorkspaceTab::TexturePaint:
        state.show3DOverlays = true;
        state.activeMode = StudioMode::TexturePaint;
        if (state.activeTool == StudioTool::VertexEdit || state.activeTool == StudioTool::FacePaintMask) {
            state.activeTool = StudioTool::Brush;
        }
        state.shadingMode = ShadingMode::Material;
        state.activeRightTab = RightPanelTab::Properties;
        state.activePropsCat = PropertiesCategory::Tool;
        break;
    case WorkspaceTab::Shading:
        state.show3DOverlays = true;
        state.shadingMode = ShadingMode::Material;
        state.activeRightTab = RightPanelTab::Properties;
        state.activePropsCat = PropertiesCategory::Material;
        break;
    case WorkspaceTab::Rendering:
        state.show3DOverlays = false;
        state.shadingMode = ShadingMode::Rendered;
        state.activeRightTab = RightPanelTab::Properties;
        state.activePropsCat = PropertiesCategory::Lights;
        break;
    }
}

} // namespace

void drawTopWorkspaceBar(StudioUiState& state, UvViewportState& uvState,
                         CanvasEngine& canvas, ObjMesh& mesh,
                         const StudioCallbacks& callbacks,
                         float x, float y, float width, float height) {
    const float row1H = 32.0f;
    state.headerBarRect = ImVec4(x, y, x + width, y + row1H);
    state.headerItemHovered = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);

    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, StudioColors::ToVec4(StudioColors::HeaderBg));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoDocking;
    if (ImGui::Begin("##TopWorkspaceBar", nullptr, flags)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7.0f, 4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3.0f, 2.0f));

        // --- ROW 1: Unified Caption-Embedded Chrome ([Cr] 20x20 Squircle + 20x20 House + Menus + Actions + _ [] X) ---
        ImVec2 logoMin(x + 8.0f, y + 6.0f);
        ImVec2 logoMax(logoMin.x + 20.0f, logoMin.y + 20.0f);
        if (state.appLogoTex != 0) {
            dl->AddImage(state.appLogoTex, logoMin, logoMax);
        } else {
            drawAppMonogramLogo(dl, logoMin, logoMax);
        }

        // Illustrator-style House (Home Screen toggle) icon button beside [Cr]
        ImVec2 hmMin(logoMax.x + 5.0f, y + 6.0f);
        ImVec2 hmMax(hmMin.x + 20.0f, hmMin.y + 20.0f);
        ImGui::SetCursorScreenPos(hmMin);
        bool hmClick = ImGui::InvisibleButton("##top_home_btn", ImVec2(20.0f, 20.0f));
        bool hmHov = ImGui::IsItemHovered();
        if (hmHov) state.headerItemHovered = true;
        if (state.showHomeScreen || hmHov) {
            dl->AddRectFilled(hmMin, hmMax,
                              state.showHomeScreen ? StudioColors::ControlHover : StudioColors::ControlBg,
                              3.0f);
        }
        drawSmallIcon(dl, SmallIconKind::Home, hmMin + ImVec2(3.0f, 3.0f), hmMax - ImVec2(3.0f, 3.0f),
                      state.showHomeScreen ? StudioColors::TextPrimary : StudioColors::TextSecondary);
        if (hmHov) {
            ImGui::SetTooltip("%s", state.showHomeScreen ? (state.hasActiveDocument ? "Return to Editor" : "Home")
                                                         : "Home Screen");
        }
        if (hmClick) {
            if (state.showHomeScreen) {
                if (state.hasActiveDocument) {
                    state.showHomeScreen = false;
                    if (callbacks.onReturnToEditor) callbacks.onReturnToEditor();
                }
            } else {
                state.showHomeScreen = true;
            }
        }

        ImGui::SetCursorScreenPos(ImVec2(hmMax.x + 6.0f, y + 5.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, StudioColors::ToVec4(StudioColors::ControlHover));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, StudioColors::ToVec4(StudioColors::ToolActiveBg));

        auto trackHdr = [&]() {
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)) {
                state.headerItemHovered = true;
            }
        };
        auto menuBtn = [&](const char* label, const char* popupId) -> bool {
            if (ImGui::Button(label)) ImGui::OpenPopup(popupId);
            trackHdr();
            return ImGui::BeginPopup(popupId);
        };

        if (menuBtn("File", "##MenuFile")) {
            if (ImGui::MenuItem("New Project (4K)", "Ctrl+N"))            { if (callbacks.onNewProject) callbacks.onNewProject(); }
            if (ImGui::MenuItem("Open Project (.cr)...", "Ctrl+L"))       { if (callbacks.onLoadProject) callbacks.onLoadProject(); }
            if (ImGui::MenuItem("Open Car (.obj)...", "Ctrl+O"))          { if (callbacks.onOpenObj) callbacks.onOpenObj(); }
            if (ImGui::MenuItem("Porsche 992 Template", "F5"))            { if (callbacks.onQuickLoadPorsche) callbacks.onQuickLoadPorsche(); }
            ImGui::Separator();
            if (ImGui::MenuItem("Open Livery Texture...", "Ctrl+Shift+O")){ if (callbacks.onOpenLiveryPng) callbacks.onOpenLiveryPng(); }
            if (ImGui::MenuItem("Load AO / Shading Map..."))              { if (callbacks.onLoadAoMap) callbacks.onLoadAoMap(); }
            if (ImGui::MenuItem("Import Image...", "Ctrl+I")) {
                if (callbacks.onImportImage) callbacks.onImportImage();
                else if (callbacks.onImportDecal) callbacks.onImportDecal();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save (.cr)", "Ctrl+S"))                  { if (callbacks.onSaveProject) callbacks.onSaveProject(); }
            if (ImGui::MenuItem("Save As (.cr)...", "Ctrl+Shift+S"))      { if (callbacks.onSaveProject) callbacks.onSaveProject(); }
            ImGui::Separator();
            if (ImGui::MenuItem("Export 4K Livery PNG...", "Ctrl+Shift+E")){ if (callbacks.onExportPng) callbacks.onExportPng(4096); }
            if (ImGui::BeginMenu("Export Livery Size")) {
                if (ImGui::MenuItem("4096 x 4096 (4K)..."))               { if (callbacks.onExportPng) callbacks.onExportPng(4096); }
                if (ImGui::MenuItem("2048 x 2048 (2K)..."))               { if (callbacks.onExportPng) callbacks.onExportPng(2048); }
                if (ImGui::MenuItem("1024 x 1024 (1K)..."))               { if (callbacks.onExportPng) callbacks.onExportPng(1024); }
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine();

        if (menuBtn("Edit", "##MenuEdit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, canvas.canUndo())) {
                if (callbacks.onUndo) callbacks.onUndo(); else canvas.undo();
            }
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, canvas.canRedo())) {
                if (callbacks.onRedo) callbacks.onRedo(); else canvas.redo();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Copy", "Ctrl+C")) {
                if (callbacks.onCopy) callbacks.onCopy();
                else state.requestCopySelection = true;
            }
            if (ImGui::MenuItem("Cut", "Ctrl+X")) {
                if (callbacks.onCut) callbacks.onCut();
                else state.requestCutSelection = true;
            }
            if (ImGui::MenuItem("Paste", "Ctrl+V")) {
                if (callbacks.onPaste) callbacks.onPaste();
                else state.requestPasteClipboard = true;
            }
            ImGui::Separator();
            if (ImGui::BeginMenu("Canvas Resolution")) {
                for (int res : {4096, 2048, 1024}) {
                    char lbl[24];
                    std::snprintf(lbl, sizeof(lbl), "%d x %d", res, res);
                    if (ImGui::MenuItem(lbl, nullptr, canvas.width == res)) {
                        state.canvasResolution = res;
                        if (callbacks.onResizeCanvas) callbacks.onResizeCanvas(res);
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Mirror", "Shift+M", &state.brush.mirrorX)) {
                canvas.setMirrorOnActiveItem(state.brush.mirrorX, &mesh);
                if (callbacks.onToggleMirror) callbacks.onToggleMirror(state.brush.mirrorX);
                if (callbacks.onCanvasModified) callbacks.onCanvasModified();
            }
            if (ImGui::MenuItem("3D Cross-Seam Projection", nullptr, &state.brush.project3DBrush)) {}
            ImGui::EndPopup();
        }
        ImGui::SameLine();

        if (menuBtn("Object", "##MenuObject")) {
            if (ImGui::MenuItem("Texture Paint Mode", nullptr, state.activeMode == StudioMode::TexturePaint)) {
                state.activeMode = StudioMode::TexturePaint;
                if (state.activeTool == StudioTool::VertexEdit || state.activeTool == StudioTool::FacePaintMask) {
                    state.activeTool = StudioTool::Brush;
                }
            }
            if (ImGui::MenuItem("Object Mode", nullptr, state.activeMode == StudioMode::Object)) {
                state.activeMode = StudioMode::Object;
                state.activeTool = StudioTool::SelectMove;
            }
            if (ImGui::MenuItem("Edit Mesh Mode", "Tab", state.activeMode == StudioMode::EditMesh)) {
                state.activeMode = StudioMode::EditMesh;
                state.activeTool = StudioTool::VertexEdit;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Grab / Move Vertices", "G"))   { state.editSubTool = EditSubTool::Grab; state.activeMode = StudioMode::EditMesh; state.activeTool = StudioTool::VertexEdit; }
            if (ImGui::MenuItem("Rotate Vertices", "R"))        { state.editSubTool = EditSubTool::Rotate; state.activeMode = StudioMode::EditMesh; state.activeTool = StudioTool::VertexEdit; }
            if (ImGui::MenuItem("Scale Vertices", "S"))         { state.editSubTool = EditSubTool::Scale; state.activeMode = StudioMode::EditMesh; state.activeTool = StudioTool::VertexEdit; }
            if (ImGui::MenuItem("Select Face Paint Mask", "F")) { state.activeMode = StudioMode::EditMesh; state.activeTool = StudioTool::FacePaintMask; }
            if (ImGui::MenuItem("Clear Selection / Mask", "Alt+A") && mesh.loaded) {
                mesh.clearSelection();
                state.meshGpuDirty = true;
                if (callbacks.onMeshModified) callbacks.onMeshModified();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Show All Sub-Objects") && mesh.loaded) {
                for (SubGroup& g : mesh.groups) g.visible = true;
                state.meshGpuDirty = true;
                if (callbacks.onMeshModified) callbacks.onMeshModified();
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine();

        if (menuBtn("Type", "##MenuType")) {
            if (ImGui::MenuItem("New Point Type", "T")) {
                TextLayerProps tp;
                tp.text = "Lorem ipsum";
                tp.uvPos = Vec2{0.5f, 0.5f};
                tp.anchoredIn3D = false;
                tp.fontFamily = "Arial";
                tp.fontStyle = "Bold";
                tp.fontPath = canvas.findFontPath("", tp.fontFamily, tp.fontStyle);
                tp.fillColor = state.brush.color;
                tp.outlineColor = state.secondaryColor;
                tp.mirrorX = state.brush.mirrorX;
                tp.mirrorMode = state.brush.mirrorX ? MirrorTextMode::ReadableBothSides : MirrorTextMode::None;
                tp.syncCharStyles();
                int itemIdx = canvas.addTextItem("Text Layer", tp, &mesh);
                canvas.selectedItemIdx = itemIdx;
                if (VectorItem* created = canvas.activeVectorItem()) {
                    std::snprintf(state.textEditBuf, sizeof(state.textEditBuf), "%s", created->text.text.c_str());
                    state.textEditSyncedLayerId = created->id;
                }
                state.activeTool = StudioTool::Text;
                state.activeTypeToolVariant = 0;
                state.activePropsCat = PropertiesCategory::Text;
                if (callbacks.onCanvasModified) callbacks.onCanvasModified();
            }
            if (ImGui::MenuItem("Touch Type Tool", "Shift+T", state.activeTool == StudioTool::TouchType)) {
                state.activeTool = StudioTool::TouchType;
                state.activeTypeToolVariant = 1;
                state.activePropsCat = PropertiesCategory::Text;
            }
            if (ImGui::MenuItem("Fill with Placeholder Text")) {
                triggerFillPlaceholderText(state, canvas, mesh, callbacks);
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine();

        if (menuBtn("Layer", "##MenuLayer")) {
            if (ImGui::MenuItem("New Layer", "Ctrl+Shift+N")) {
                if (callbacks.onNewLayer) {
                    callbacks.onNewLayer();
                } else {
                    char nm[32];
                    std::snprintf(nm, sizeof(nm), "Layer %zu", canvas.layers.size());
                    canvas.addLayer(nm, false, {0, 0, 0, 0});
                    if (callbacks.onCanvasModified) callbacks.onCanvasModified();
                }
            }
            if (ImGui::MenuItem("New Pen Path", "P")) {
                RulerStripeProps rp;
                rp.smoothCurve = true;
                rp.fillColor = state.brush.color;
                rp.color = state.secondaryColor;
                rp.mirrorX = state.brush.mirrorX;
                int itemIdx = canvas.addPenItem("Pen Path", rp, &mesh);
                canvas.selectedItemIdx = itemIdx;
                state.activeTool = StudioTool::Pen;
                state.activePenToolVariant = 0;
                state.activePropsCat = PropertiesCategory::Ruler;
                if (callbacks.onCanvasModified) callbacks.onCanvasModified();
            }
            if (ImGui::MenuItem("Import Image...", "Ctrl+I")) {
                if (callbacks.onImportImage) callbacks.onImportImage();
                else if (callbacks.onImportDecal) callbacks.onImportDecal();
            }
            ImGui::Separator();
            int idx = canvas.activeLayerIdx;
            int nLayers = static_cast<int>(canvas.layers.size());
            bool hasVec = (idx >= 0 && idx < nLayers &&
                           (canvas.layers[idx].kind != LayerKind::RasterPaint || !canvas.layers[idx].items.empty()));
            if (ImGui::MenuItem("Rasterize Vector Items", nullptr, false, hasVec)) {
                if (canvas.rasterizeLayerToPaint(idx, &mesh) && callbacks.onCanvasModified) {
                    callbacks.onCanvasModified();
                }
            }
            if (ImGui::MenuItem("Bake AO Overlay", nullptr, false, mesh.loaded)) {
                if (callbacks.onBakeMeshAo) {
                    callbacks.onBakeMeshAo();
                } else {
                    canvas.bakeMeshAoOverlay(mesh, 1024, true);
                    if (callbacks.onCanvasModified) callbacks.onCanvasModified();
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Duplicate Layer", "Ctrl+J", false, idx >= 0 && idx < nLayers)) {
                if (callbacks.onDuplicateLayer) callbacks.onDuplicateLayer(idx);
                else {
                    canvas.duplicateLayer(idx);
                    if (callbacks.onCanvasModified) callbacks.onCanvasModified();
                }
            }
            bool canMerge = (idx > 0 && idx < nLayers && canvas.layers[idx - 1].kind == LayerKind::RasterPaint);
            if (ImGui::MenuItem("Flatten Selected", "Ctrl+E", false, canMerge)) {
                canvas.mergeDown(idx);
                if (callbacks.onCanvasModified) callbacks.onCanvasModified();
            }
            if (ImGui::MenuItem("Delete Layer", "Del", false, nLayers > 1 && idx >= 0)) {
                if (callbacks.onDeleteLayer) callbacks.onDeleteLayer(idx);
                else {
                    canvas.removeLayer(idx);
                    if (callbacks.onCanvasModified) callbacks.onCanvasModified();
                }
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine();

        if (menuBtn("View", "##MenuView")) {
            ImGui::MenuItem("Show Rulers", "Ctrl+R", &state.showRulers);
            ImGui::MenuItem("Show Pixel Grid", nullptr, &state.uvShowGridRulers);
            ImGui::MenuItem("Show UV Wireframe", nullptr, &state.uvShowWireframe);
            if (ImGui::MenuItem("Clear Ruler Guides")) {
                uvState.guidesX.clear();
                uvState.guidesY.clear();
            }
            ImGui::Separator();
            ImGui::MenuItem("Show Studio Floor Grid", nullptr, &state.material.showFloorGrid);
            ImGui::MenuItem("Show Symmetry Plane (X=0)", nullptr, &state.material.showSymmetryPlane);
            ImGui::MenuItem("Show 3D Overlays", nullptr, &state.show3DOverlays);
            ImGui::MenuItem("Rich Tooltips", nullptr, &state.showRichTooltips);
            ImGui::Separator();
            if (ImGui::BeginMenu("Shading")) {
                if (ImGui::MenuItem("Wireframe", nullptr, state.shadingMode == ShadingMode::Wireframe)) state.shadingMode = ShadingMode::Wireframe;
                if (ImGui::MenuItem("Solid Clay", nullptr, state.shadingMode == ShadingMode::Clay))     state.shadingMode = ShadingMode::Clay;
                if (ImGui::MenuItem("Material Preview", nullptr, state.shadingMode == ShadingMode::Material)) state.shadingMode = ShadingMode::Material;
                if (ImGui::MenuItem("Showroom Rendered", nullptr, state.shadingMode == ShadingMode::Rendered)) state.shadingMode = ShadingMode::Rendered;
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Camera: Front", "1"))      { if (callbacks.onSetCameraPreset) callbacks.onSetCameraPreset(CameraPreset::Front); }
            if (ImGui::MenuItem("Camera: Side", "3"))       { if (callbacks.onSetCameraPreset) callbacks.onSetCameraPreset(CameraPreset::Side); }
            if (ImGui::MenuItem("Camera: Top", "7"))        { if (callbacks.onSetCameraPreset) callbacks.onSetCameraPreset(CameraPreset::Top); }
            if (ImGui::MenuItem("Camera: Rear"))            { if (callbacks.onSetCameraPreset) callbacks.onSetCameraPreset(CameraPreset::Rear); }
            if (ImGui::MenuItem("Camera: 3/4 Beauty"))      { if (callbacks.onSetCameraPreset) callbacks.onSetCameraPreset(CameraPreset::ThreeQuarterBeauty); }
            if (ImGui::MenuItem("Perspective / Ortho", "5", state.isOrthoCamera)) { if (callbacks.onToggleOrtho) callbacks.onToggleOrtho(); }
            ImGui::Separator();
            if (ImGui::MenuItem("Fit 2D UV Canvas", "Ctrl+0")) { uvState.needsInitialFit = true; }
            if (ImGui::MenuItem("Reset Workspace Layout"))     { state.dockLayoutResetRequested = true; }
            ImGui::EndPopup();
        }
        ImGui::SameLine();

        if (menuBtn("Help", "##MenuHelp")) {
            if (ImGui::MenuItem("Keyboard Shortcuts...")) state.showShortcutsModal = true;
            ImGui::Separator();
            const char* updLbl = state.otaUpdateAvailable ? "Check for Updates...      " : "Check for Updates...";
            bool chkUpd = ImGui::MenuItem(updLbl);
            if (state.otaUpdateAvailable) {
                ImDrawList* mdl = ImGui::GetWindowDrawList();
                ImVec2 rMin = ImGui::GetItemRectMin();
                ImVec2 rMax = ImGui::GetItemRectMax();
                ImVec2 c(rMax.x - 10.0f, (rMin.y + rMax.y) * 0.5f);
                mdl->AddCircleFilled(c, 6.5f, IM_COL32(0xB8, 0x7D, 0x14, 255), 16);
                mdl->AddCircle(c, 6.5f, IM_COL32(0xD9, 0x9B, 0x26, 255), 16, 1.0f);
                mdl->AddRectFilled(ImVec2(c.x - 0.9f, c.y - 3.6f), ImVec2(c.x + 0.9f, c.y + 0.8f), IM_COL32(255, 255, 255, 255));
                mdl->AddCircleFilled(ImVec2(c.x, c.y + 2.9f), 1.0f, IM_COL32(255, 255, 255, 255), 8);
            }
            if (chkUpd) {
                state.requestOpenUpdatesModal = true;
                if (callbacks.onCheckUpdates) callbacks.onCheckUpdates();
            }
            if (ImGui::MenuItem("Terms of Use...")) {
                state.requestOpenTermsModal = true;
                if (callbacks.onOpenTerms) callbacks.onOpenTerms();
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(); // FrameBorderSize for borderless menus

        // Document badge (.cr) beside Help menu
        std::string docName = state.activeProjectPath.empty()
                              ? "Untitled.cr"
                              : std::filesystem::path(state.activeProjectPath).filename().string();
        ImGui::SameLine(0.0f, 10.0f);
        ImGui::TextColored(StudioColors::ToVec4(StudioColors::TextMuted), "%s", docName.c_str());

        // Right-aligned Workspace / Back to Editor + [!] Update Badge + Render Photo + Caption Window Buttons (_, [], X)
        const ImGuiStyle& st = ImGui::GetStyle();
        const float capBtnW = 34.0f;
        const float capBlockW = capBtnW * 3.0f + 4.0f;
        float updBadgeW = state.otaUpdateAvailable ? 27.0f : 0.0f;
        float prevRightX = ImGui::GetItemRectMax().x - x;

        auto drawOtaBadgeBtn = [&]() {
            if (!state.otaUpdateAvailable) return;
            ImGui::SameLine(0.0f, 5.0f);
            ImVec2 bMin = ImGui::GetCursorScreenPos();
            const float bSz = 22.0f;
            bool updClick = ImGui::InvisibleButton("##ota_upd_badge", ImVec2(bSz, bSz));
            bool updHov = ImGui::IsItemHovered();
            trackHdr();
            ImVec2 c(bMin.x + bSz * 0.5f, bMin.y + bSz * 0.5f);
            dl->AddCircleFilled(c, 9.5f, updHov ? IM_COL32(0xD9, 0x9B, 0x26, 255) : IM_COL32(0xB8, 0x7D, 0x14, 255), 20);
            dl->AddCircle(c, 9.5f, IM_COL32(0xD9, 0x9B, 0x26, 255), 20, 1.2f);
            dl->AddRectFilled(ImVec2(c.x - 1.1f, c.y - 5.2f), ImVec2(c.x + 1.1f, c.y + 1.4f), IM_COL32(255, 255, 255, 255));
            dl->AddCircleFilled(ImVec2(c.x, c.y + 4.1f), 1.25f, IM_COL32(255, 255, 255, 255), 10);
            if (updHov) {
                std::string tip = state.otaLatestVersion.empty()
                                  ? "Update available - Click to view update"
                                  : ("Coatroom v" + state.otaLatestVersion + " available - Click to view update");
                ImGui::SetTooltip("%s", tip.c_str());
            }
            if (updClick) {
                state.requestOpenUpdatesModal = true;
            }
        };

        if (state.showHomeScreen) {
            float backW = state.hasActiveDocument
                          ? (ImGui::CalcTextSize("Back to Editor").x + st.FramePadding.x * 2.0f + 4.0f)
                          : 0.0f;
            float homeRightW = backW + updBadgeW + 10.0f + capBlockW;
            if (width - homeRightW > prevRightX + 6.0f && (backW > 0.0f || updBadgeW > 0.0f)) {
                ImGui::SameLine(width - homeRightW);
                if (state.hasActiveDocument) {
                    if (flatTogglePill("Back to Editor", false, false)) {
                        state.showHomeScreen = false;
                        if (callbacks.onReturnToEditor) callbacks.onReturnToEditor();
                    }
                    trackHdr();
                } else {
                    ImGui::Dummy(ImVec2(1.0f, 1.0f));
                }
                drawOtaBadgeBtn();
            }
        } else {
            char wsBtnLbl[48];
            std::snprintf(wsBtnLbl, sizeof(wsBtnLbl), "%s  v", workspaceDisplayName(state.activeWorkspace));
            float wsW = std::max(108.0f, ImGui::CalcTextSize(wsBtnLbl).x + st.FramePadding.x * 2.0f);
            float photoW = ImGui::CalcTextSize("Render Photo").x + st.FramePadding.x * 2.0f;
            float rightActionW = wsW + updBadgeW + photoW + 12.0f + capBlockW;
            if (width - rightActionW > prevRightX + 6.0f) {
                ImGui::SameLine(width - rightActionW);
                if (ImGui::Button(wsBtnLbl, ImVec2(wsW, 0.0f))) {
                    ImGui::OpenPopup("##WorkspaceDrop");
                }
                trackHdr();
                itemTooltip("Switch Studio Workspace Layout");
                if (ImGui::BeginPopup("##WorkspaceDrop")) {
                    struct WsDropItem { const char* name; WorkspaceTab tab; };
                    static constexpr WsDropItem kDrops[] = {
                        {"Essentials",     WorkspaceTab::Layout},
                        {"2D UV Artboard", WorkspaceTab::UvEditing},
                        {"3D Paint",       WorkspaceTab::TexturePaint},
                        {"Showroom",       WorkspaceTab::Rendering},
                    };
                    for (const WsDropItem& di : kDrops) {
                        if (ImGui::MenuItem(di.name, nullptr, state.activeWorkspace == di.tab)) {
                            applyWorkspacePreset(state, di.tab);
                        }
                    }
                    ImGui::EndPopup();
                }

                drawOtaBadgeBtn();

                ImGui::SameLine();
                if (flatTogglePill("Render Photo", false, false)) {
                    if (callbacks.onRenderPhoto) callbacks.onRenderPhoto();
                    else if (callbacks.onRenderBeautyShot) callbacks.onRenderBeautyShot();
                }
                trackHdr();
                itemTooltip("Render Studio Photo PNG (F12)");
            }
        }

        // Caption Minimize / Maximize / Close embedded on far right of Row 1
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        const float capStartX = x + width - capBtnW * 3.0f;
        struct CapBtnSpec { const char* id; SmallIconKind icon; const char* tip; bool isClose; };
        CapBtnSpec capSpecs[3] = {
            {"##win_min", SmallIconKind::WindowMin, "Minimize", false},
            {"##win_max", state.windowMaximized ? SmallIconKind::WindowRestore : SmallIconKind::WindowMax,
                          state.windowMaximized ? "Restore Down" : "Maximize", false},
            {"##win_cls", SmallIconKind::WindowClose, "Close Coatroom", true},
        };
        for (int ci = 0; ci < 3; ++ci) {
            ImVec2 cMin(capStartX + ci * capBtnW, y);
            ImVec2 cMax(cMin.x + capBtnW, y + row1H);
            ImGui::SetCursorScreenPos(cMin);
            bool clk = ImGui::InvisibleButton(capSpecs[ci].id, ImVec2(capBtnW, row1H));
            bool hov = ImGui::IsItemHovered();
            if (hov) {
                state.headerItemHovered = true;
                dl->AddRectFilled(cMin, cMax, capSpecs[ci].isClose ? StudioColors::BrandAccent : StudioColors::ControlHover);
                ImGui::SetTooltip("%s", capSpecs[ci].tip);
            }
            ImVec2 icoMin(cMin.x + 11.0f, cMin.y + 10.0f);
            ImVec2 icoMax(icoMin.x + 12.0f, icoMin.y + 12.0f);
            drawSmallIcon(dl, capSpecs[ci].icon, icoMin, icoMax,
                          hov ? IM_COL32(255, 255, 255, 255) : StudioColors::TextSecondary);
            if (clk) {
                if (ci == 0 && callbacks.onWindowMinimize) callbacks.onWindowMinimize();
                else if (ci == 1 && callbacks.onWindowToggleMaximize) callbacks.onWindowToggleMaximize();
                else if (ci == 2 && callbacks.onWindowClose) callbacks.onWindowClose();
            }
        }
        ImGui::PopStyleVar();

        // Double-click empty unified caption bar toggles maximize cleanly
        if (!state.headerItemHovered &&
            ImGui::IsMouseHoveringRect(ImVec2(x + 36.0f, y), ImVec2(x + width - capBtnW * 3.0f, y + row1H), false) &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
            callbacks.onWindowToggleMaximize) {
            callbacks.onWindowToggleMaximize();
        }

        // Divider between Unified Row 1 Chrome and Row 2 Contextual Taskbar
        ImGui::PopStyleVar(2); // balance outer FramePadding & ItemSpacing
        float row2Y = y + row1H;
        dl->AddLine(ImVec2(x, row2Y), ImVec2(x + width, row2Y), StudioColors::Border, 1.0f);

        if (height > row1H + 4.0f) {
            dl->AddRectFilled(ImVec2(x, row2Y + 1.0f), ImVec2(x + width, y + height), StudioColors::PanelBody);

            // --- ROW 2: Adobe Illustrator CC Contextual Taskbar (26px) ---
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.0f, 2.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 2.0f));

            ImGui::SetCursorScreenPos(ImVec2(x + 8.0f, row2Y + 3.0f));
        ImGui::TextColored(StudioColors::ToVec4(StudioColors::TextSecondary), "%s (%s)",
                           toolDisplayName(state.activeTool), toolShortcutHint(state.activeTool));

        ImGui::SameLine(0.0f, 8.0f);
        if (colorEdit4u8("##CtxPrimary", state.brush.color, true)) {
            syncActiveLayerColors(state, canvas, mesh, callbacks, false);
        }
        itemTooltip("Primary Fill Color");
        ImGui::SameLine(0.0f, 4.0f);
        if (colorEdit4u8("##CtxStroke", state.secondaryColor, true)) {
            syncActiveLayerColors(state, canvas, mesh, callbacks, true);
        }
        itemTooltip("Secondary / Stroke Color");

        ImGui::SameLine(0.0f, 8.0f);
        ImGui::TextColored(StudioColors::ToVec4(StudioColors::BorderLight), "|");
        ImGui::SameLine(0.0f, 8.0f);

        Layer* actL = (canvas.activeLayerIdx >= 0 && canvas.activeLayerIdx < static_cast<int>(canvas.layers.size()))
                      ? &canvas.layers[canvas.activeLayerIdx]
                      : nullptr;
        VectorItem* actItem = canvas.activeVectorItem();

        const bool showTextBar = (state.activeTool == StudioTool::Text || state.activeTool == StudioTool::TouchType) ||
                                 (state.activeTool == StudioTool::Select &&
                                  ((actItem && actItem->kind == VectorItemKind::Text) ||
                                   (!actItem && actL && actL->kind == LayerKind::Text)));
        const bool showPenBar = (state.activeTool == StudioTool::Line || state.activeTool == StudioTool::Pen ||
                                 state.activeTool == StudioTool::DirectSelect) ||
                                (state.activeTool == StudioTool::Select &&
                                 ((actItem && actItem->kind == VectorItemKind::PenPath) ||
                                  (!actItem && actL && actL->kind == LayerKind::RulerStripe)));
        const bool showRectBar = (state.activeTool == StudioTool::Rect) ||
                                 (state.activeTool == StudioTool::Select && actItem && actItem->kind == VectorItemKind::RectShape);

        if (showTextBar) {
            TextLayerProps* pTx = (actItem && actItem->kind == VectorItemKind::Text) ? &actItem->text :
                                  (actL && actL->kind == LayerKind::Text) ? &actL->text : nullptr;
            uint32_t tUid = (actItem && actItem->kind == VectorItemKind::Text) ? actItem->id : (actL ? actL->id : 0);
            if (pTx && actL && !actL->locked) {
                TextLayerProps& tx = *pTx;
                tx.syncCharStyles();
                if (state.textEditSyncedLayerId != tUid) {
                    std::snprintf(state.textEditBuf, sizeof(state.textEditBuf), "%s", tx.text.c_str());
                    state.textEditSyncedLayerId = tUid;
                }
                bool ch = false;
                ImGui::SetNextItemWidth(118.0f);
                if (ImGui::InputText("##CtxTxtBuf", state.textEditBuf, sizeof(state.textEditBuf))) {
                    tx.text = state.textEditBuf;
                    tx.syncCharStyles();
                    ch = true;
                }
                if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();
                itemTooltip("Edit Active Text String");
                ImGui::SameLine();
                ch |= drawGroupedFontPickers(state, canvas, tx, 108.0f, 82.0f, true, "CtxF");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(58.0f);
                ch |= ImGui::DragFloat("##CtxSz", &tx.fontSizePx, 1.0f, 8.0f, 512.0f, "%.0f px");
                if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();
                itemTooltip("Font Size (px)");
                ImGui::SameLine();
                if (flatTogglePill("Box", tx.isAreaBox)) {
                    canvas.pushUndoSnapshot();
                    tx.isAreaBox = !tx.isAreaBox;
                    ch = true;
                }
                itemTooltip("Toggle Wrappable Area Type Box");
                ImGui::SameLine();
                if (flatTogglePill("Touch", state.activeTool == StudioTool::TouchType)) {
                    state.activeTool = StudioTool::TouchType;
                    state.activeTypeToolVariant = 1;
                }
                itemTooltip("Touch Type Tool (Shift+T)");
                if (tx.selectedCharIdx >= 0 && tx.selectedCharIdx < static_cast<int>(tx.text.size())) {
                    ImGui::SameLine();
                    char chLbl[24];
                    char cc = tx.text[tx.selectedCharIdx];
                    std::snprintf(chLbl, sizeof(chLbl), "#%d '%c' x", tx.selectedCharIdx + 1, cc == ' ' ? '_' : cc);
                    if (flatTogglePill(chLbl, true, true)) {
                        tx.selectedCharIdx = -1;
                    }
                    itemTooltip("Click to Clear Single-Letter Selection");
                }
                if (ch) {
                    canvas.rasterizeLayer(canvas.activeLayerIdx, &mesh);
                    if (callbacks.onCanvasModified) callbacks.onCanvasModified();
                }
            } else {
                if (ImGui::Button("+ Add Text (Lorem ipsum)")) {
                    triggerFillPlaceholderText(state, canvas, mesh, callbacks);
                }
                itemTooltip("Create Placeholder Typography Item");
            }
        } else if (showPenBar) {
            RulerStripeProps* pRu = (actItem && actItem->kind == VectorItemKind::PenPath) ? &actItem->pen :
                                    (actL && actL->kind == LayerKind::RulerStripe) ? &actL->ruler : nullptr;
            if (pRu) {
                RulerStripeProps& pen = *pRu;
                bool ch = false;
                if (colorEdit4u8("##CtxPenFill", pen.fillColor, true)) ch = true;
                if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();
                itemTooltip("Path Fill Color");
                ImGui::SameLine(0.0f, 3.0f);
                if (colorEdit4u8("##CtxPenStroke", pen.color, true)) ch = true;
                if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();
                itemTooltip("Path Stroke Color");
                ImGui::SameLine();
                if (flatTogglePill("Filled", pen.filled)) {
                    canvas.pushUndoSnapshot();
                    pen.filled = !pen.filled;
                    if (pen.filled && pen.nodes.size() >= 3) pen.closed = true;
                    ch = true;
                }
                itemTooltip("Toggle Solid Interior Fill");
                ImGui::SameLine();
                if (flatTogglePill("Closed", pen.closed)) {
                    canvas.pushUndoSnapshot();
                    pen.closed = !pen.closed;
                    ch = true;
                }
                itemTooltip("Toggle Closed Loop Path");
                ImGui::SameLine();
                if (flatTogglePill("Stroke", pen.hasStroke)) {
                    canvas.pushUndoSnapshot();
                    pen.hasStroke = !pen.hasStroke;
                    ch = true;
                }
                itemTooltip("Toggle Path Stroke Outline");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(68.0f);
                ch |= ImGui::DragFloat("##CtxPenW", &pen.widthPx, 0.5f, 1.0f, 200.0f, "%.0f px");
                if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();
                itemTooltip("Stroke Width (px)");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(60.0f);
                ch |= ImGui::DragFloat("##CtxPenTap", &pen.endTaper, 0.01f, 0.0f, 1.0f, "%.2f tap");
                if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();
                itemTooltip("Endpoint Taper");
                if (ch) {
                    canvas.rasterizeLayer(canvas.activeLayerIdx, &mesh);
                    if (callbacks.onCanvasModified) callbacks.onCanvasModified();
                }
            }
        } else if (showRectBar) {
            if (actItem && actItem->kind == VectorItemKind::RectShape) {
                RectShapeProps& rc = actItem->rect;
                bool ch = false;
                if (flatTogglePill("Filled", rc.filled)) {
                    canvas.pushUndoSnapshot();
                    rc.filled = !rc.filled;
                    ch = true;
                }
                ImGui::SameLine();
                if (flatTogglePill("Stroke", rc.hasStroke)) {
                    canvas.pushUndoSnapshot();
                    rc.hasStroke = !rc.hasStroke;
                    ch = true;
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(66.0f);
                ch |= ImGui::DragFloat("Edge", &rc.strokeWidthPx, 0.5f, 0.0f, 48.0f, "%.0f px");
                if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();
                ImGui::SameLine();
                ImGui::SetNextItemWidth(66.0f);
                ch |= ImGui::DragFloat("Corner", &rc.cornerRadiusPx, 0.5f, 0.0f, 128.0f, "%.0f px");
                if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();
                if (ch) {
                    canvas.rasterizeLayer(canvas.activeLayerIdx, &mesh);
                    if (callbacks.onCanvasModified) callbacks.onCanvasModified();
                }
            } else {
                ImGui::SetNextItemWidth(74.0f);
                ImGui::DragFloat("Stroke", &state.rectStrokeWidthPx, 0.5f, 0.0f, 32.0f, "%.0f px");
                itemTooltip("Rectangle Stroke Width (0 = Solid Fill)");
                ImGui::SameLine();
                int opPct = static_cast<int>(std::round(state.brush.opacity * 100.0f));
                ImGui::SetNextItemWidth(68.0f);
                if (ImGui::DragInt("Opac", &opPct, 1.0f, 1, 100, "%d%%")) {
                    state.brush.opacity = std::clamp(opPct / 100.0f, 0.01f, 1.0f);
                }
                itemTooltip("Opacity (%)");
            }
        } else if (state.activeTool == StudioTool::VertexEdit || state.activeTool == StudioTool::FacePaintMask) {
            ImGui::SetNextItemWidth(74.0f);
            ImGui::DragFloat("Radius", &state.vertexSelectRadius, 0.01f, 0.02f, 1.0f, "%.2f m");
            itemTooltip("3D Selection Radius");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(68.0f);
            ImGui::DragFloat("Angle", &state.faceMaskAngleDeg, 1.0f, 5.0f, 90.0f, "%.0f deg");
            itemTooltip("Connected Face Mask Angle");
        } else {
            ImGui::SetNextItemWidth(74.0f);
            ImGui::DragFloat("Size", &state.brush.radiusPx, 1.0f, 1.0f, 256.0f, "%.0f px");
            itemTooltip("Brush Radius (px)");
            ImGui::SameLine();
            int opPct = static_cast<int>(std::round(state.brush.opacity * 100.0f));
            ImGui::SetNextItemWidth(64.0f);
            if (ImGui::DragInt("Opac", &opPct, 1.0f, 1, 100, "%d%%")) {
                state.brush.opacity = std::clamp(opPct / 100.0f, 0.01f, 1.0f);
            }
            itemTooltip("Opacity (%)");
            ImGui::SameLine();
            int hardPct = static_cast<int>(std::round(state.brush.hardness * 100.0f));
            ImGui::SetNextItemWidth(64.0f);
            if (ImGui::DragInt("Hard", &hardPct, 1.0f, 0, 100, "%d%%")) {
                state.brush.hardness = std::clamp(hardPct / 100.0f, 0.0f, 1.0f);
            }
            itemTooltip("Hardness (%)");
        }

        ImGui::SameLine(0.0f, 8.0f);
        if (flatTogglePill("Mirror", state.brush.mirrorX)) {
            state.brush.mirrorX = !state.brush.mirrorX;
            canvas.setMirrorOnActiveItem(state.brush.mirrorX, &mesh);
            if (callbacks.onToggleMirror) callbacks.onToggleMirror(state.brush.mirrorX);
            if (callbacks.onCanvasModified) callbacks.onCanvasModified();
        }
        if (ImGui::IsItemHovered()) {
            if (state.showRichTooltips) {
                state.hoveredRichTooltipId = kRichTooltipMirrorIdx;
                state.richTooltipAnchor = ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + 6.0f);
            } else {
                ImGui::SetTooltip("Mirror Symmetry (Shift+M)");
            }
        }

        char resLbl[24];
        std::snprintf(resLbl, sizeof(resLbl), "%d x %d", canvas.width, canvas.height);
        float ctxRightW = ImGui::CalcTextSize(resLbl).x + 12.0f;
        float prevCtxRight = ImGui::GetItemRectMax().x - x;
        if (width - ctxRightW > prevCtxRight + 8.0f) {
            ImGui::SameLine(width - ctxRightW);
            ImGui::TextColored(StudioColors::ToVec4(StudioColors::TextMuted), "%s", resLbl);
        }

            ImGui::PopStyleVar(2);
            dl->AddLine(ImVec2(x, y + height - 1.0f), ImVec2(x + width, y + height - 1.0f), StudioColors::Border, 1.0f);
        } else {
            dl->AddLine(ImVec2(x, y + height - 1.0f), ImVec2(x + width, y + height - 1.0f), StudioColors::Border, 1.0f);
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    drawShortcutsHelpModal(state, callbacks);
}

void drawToolRail(StudioUiState& state, const StudioCallbacks& callbacks,
                  float x, float y, float width, float height,
                  CanvasEngine* canvas, const ObjMesh* mesh) {
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(3.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, StudioColors::ToVec4(StudioColors::PanelBody));

    auto activateTool = [&](StudioTool tool) {
        state.activeTool = tool;
        state.brush.eraser = (tool == StudioTool::Eraser);
        if (tool == StudioTool::Pen) state.activePenToolVariant = 0;
        else if (tool == StudioTool::Line) state.activePenToolVariant = 1;
        else if (tool == StudioTool::Text) state.activeTypeToolVariant = 0;
        else if (tool == StudioTool::TouchType) state.activeTypeToolVariant = 1;
        else if (tool == StudioTool::Hand) state.activeNavToolVariant = 0;
        else if (tool == StudioTool::Zoom) state.activeNavToolVariant = 1;

        if (tool == StudioTool::VertexEdit || tool == StudioTool::FacePaintMask) {
            state.activeMode = StudioMode::EditMesh;
            state.activePropsCat = PropertiesCategory::MeshEdit;
        } else if (tool == StudioTool::Text || tool == StudioTool::TouchType) {
            state.activeMode = StudioMode::TexturePaint;
            state.activePropsCat = PropertiesCategory::Text;
        } else if (tool == StudioTool::Image) {
            state.activeMode = StudioMode::TexturePaint;
            state.activePropsCat = PropertiesCategory::Decal;
        } else if (tool == StudioTool::Line || tool == StudioTool::Pen) {
            state.activeMode = StudioMode::TexturePaint;
            state.activePropsCat = PropertiesCategory::Ruler;
        } else if (tool != StudioTool::Select && tool != StudioTool::Orbit &&
                   tool != StudioTool::Hand && tool != StudioTool::Zoom) {
            state.activeMode = StudioMode::TexturePaint;
            state.activePropsCat = PropertiesCategory::Tool;
        }
    };

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoDocking;
    if (ImGui::Begin("##LeftToolPalette", nullptr, flags)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Single-column Illustrator Pro vertical rail (46px width, 32x32 slots, 20x20 icons)
        struct RailSlot {
            StudioTool primary;
            StudioTool secondary;
            bool hasFlyout;
            bool dividerBefore;
            int groupKind; // 0=none, 1=Pen/Line, 2=Hand/Zoom, 3=Vertex/Mask
        };
        const StudioTool penCur = (state.activePenToolVariant == 1) ? StudioTool::Line : StudioTool::Pen;
        const StudioTool typeCur = (state.activeTypeToolVariant == 1) ? StudioTool::TouchType : StudioTool::Text;
        const StudioTool navCur = (state.activeNavToolVariant == 1) ? StudioTool::Zoom : StudioTool::Hand;
        const StudioTool edit3DCur = (state.activeTool == StudioTool::FacePaintMask)
                                     ? StudioTool::FacePaintMask : StudioTool::VertexEdit;
        const RailSlot slots[] = {
            {StudioTool::Select,       StudioTool::Select,        false, false, 0},
            {StudioTool::DirectSelect, StudioTool::DirectSelect,  false, false, 0},
            {penCur,                   StudioTool::Line,          true,  false, 1},
            {StudioTool::Rect,         StudioTool::Rect,          false, false, 0},
            {typeCur,                  StudioTool::TouchType,     true,  false, 4},
            {StudioTool::Brush,        StudioTool::Brush,         false, true,  0},
            {StudioTool::Eraser,       StudioTool::Eraser,        false, false, 0},
            {StudioTool::Fill,         StudioTool::Fill,          false, false, 0},
            {StudioTool::Eyedropper,   StudioTool::Eyedropper,    false, false, 0},
            {StudioTool::Image,        StudioTool::Image,         false, false, 0},
            {navCur,                   StudioTool::Zoom,          true,  true,  2},
            {edit3DCur,                StudioTool::FacePaintMask, true,  false, 3},
        };

        const float btnSz = 32.0f;
        const float slotGap = 3.0f;
        const float leftPad = std::floor((width - btnSz) * 0.5f);
        float curY = y + 6.0f;

        for (int i = 0; i < 12; ++i) {
            const RailSlot& sl = slots[i];
            if (sl.dividerBefore) {
                dl->AddLine(ImVec2(x + 8.0f, curY + 2.0f), ImVec2(x + width - 8.0f, curY + 2.0f),
                            StudioColors::Border, 1.0f);
                curY += 6.0f;
            }
            StudioTool tool = sl.primary;
            ImVec2 bMin(x + leftPad, curY);
            ImVec2 bMax(bMin.x + btnSz, bMin.y + btnSz);

            ImGui::SetCursorScreenPos(bMin);
            ImGui::PushID(i);
            bool clicked = ImGui::InvisibleButton("##il_tool", ImVec2(btnSz, btnSz),
                                                  ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
            bool hovered = ImGui::IsItemHovered();
            bool isActive = (state.activeTool == tool) ||
                            (sl.groupKind == 1 && (state.activeTool == StudioTool::Pen || state.activeTool == StudioTool::Line)) ||
                            (sl.groupKind == 4 && (state.activeTool == StudioTool::Text || state.activeTool == StudioTool::TouchType)) ||
                            (sl.groupKind == 2 && (state.activeTool == StudioTool::Hand || state.activeTool == StudioTool::Zoom)) ||
                            (sl.groupKind == 3 && (state.activeTool == StudioTool::VertexEdit || state.activeTool == StudioTool::FacePaintMask));
            bool is3DEditTool = (tool == StudioTool::VertexEdit || tool == StudioTool::FacePaintMask);

            if (isActive) {
                dl->AddRectFilled(bMin, bMax,
                                  is3DEditTool ? StudioColors::AmberAccentBg : StudioColors::ToolActiveBg,
                                  3.0f);
                dl->AddRect(bMin, bMax,
                            is3DEditTool ? StudioColors::AmberAccent : StudioColors::BorderLight,
                            3.0f, 1.0f);
            } else if (hovered) {
                dl->AddRectFilled(bMin, bMax, StudioColors::ControlHover, 3.0f);
            }

            ImU32 iconCol = isActive ? StudioColors::TextPrimary :
                            hovered  ? StudioColors::TextPrimary : StudioColors::TextSecondary;
            drawToolIcon(dl, tool, bMin + ImVec2(6.0f, 6.0f), bMax - ImVec2(6.0f, 6.0f), iconCol, isActive);

            if (sl.hasFlyout) {
                dl->AddTriangleFilled(ImVec2(bMax.x - 6.0f, bMax.y - 2.0f),
                                      ImVec2(bMax.x - 2.0f, bMax.y - 2.0f),
                                      ImVec2(bMax.x - 2.0f, bMax.y - 6.0f),
                                      StudioColors::TextSecondary);
            }

            if (hovered) {
                if (state.showRichTooltips) {
                    state.hoveredRichTooltipId = static_cast<int>(tool);
                    state.richTooltipAnchor = ImVec2(bMax.x + 8.0f, bMin.y);
                } else {
                    ImGui::SetTooltip("%s (%s)", toolDisplayName(tool), toolShortcutHint(tool));
                }
            }
            if (sl.hasFlyout && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                ImGui::OpenPopup("##ToolFlyout");
            }
            if (clicked) {
                activateTool(tool);
            }

            if (sl.hasFlyout) {
                ImGui::SetNextWindowPos(ImVec2(bMax.x + 3.0f, bMin.y), ImGuiCond_Appearing);
                if (ImGui::BeginPopup("##ToolFlyout")) {
                    if (sl.groupKind == 1) {
                        if (ImGui::MenuItem("Pen Tool", "P", state.activeTool == StudioTool::Pen)) activateTool(StudioTool::Pen);
                        if (ImGui::MenuItem("Line Tool", "\\", state.activeTool == StudioTool::Line)) activateTool(StudioTool::Line);
                    } else if (sl.groupKind == 4) {
                        if (ImGui::MenuItem("Type Tool", "T", state.activeTool == StudioTool::Text)) activateTool(StudioTool::Text);
                        if (ImGui::MenuItem("Touch Type Tool", "Shift+T", state.activeTool == StudioTool::TouchType)) activateTool(StudioTool::TouchType);
                    } else if (sl.groupKind == 2) {
                        if (ImGui::MenuItem("Hand Tool", "H", state.activeTool == StudioTool::Hand)) activateTool(StudioTool::Hand);
                        if (ImGui::MenuItem("Zoom Tool", "Z", state.activeTool == StudioTool::Zoom)) activateTool(StudioTool::Zoom);
                    } else if (sl.groupKind == 3) {
                        if (ImGui::MenuItem("Vertex Edit", "Tab", state.activeTool == StudioTool::VertexEdit)) activateTool(StudioTool::VertexEdit);
                        if (ImGui::MenuItem("Face Paint Mask", "F", state.activeTool == StudioTool::FacePaintMask)) activateTool(StudioTool::FacePaintMask);
                    }
                    ImGui::EndPopup();
                }
            }
            ImGui::PopID();
            curY += btnSz + slotGap;
        }

        // Large 22x22 Illustrator Fill & Stroke Swatches (single-column centered inside 46px rail)
        curY += 4.0f;
        dl->AddLine(ImVec2(x + 8.0f, curY), ImVec2(x + width - 8.0f, curY), StudioColors::Border, 1.0f);
        curY += 8.0f;

        if (curY + 46.0f < y + height - 34.0f) {
            ImVec2 priMin(x + 6.0f, curY);
            ImVec2 priMax(priMin.x + 22.0f, priMin.y + 22.0f);
            ImVec2 secMin(x + 17.0f, curY + 12.0f);
            ImVec2 secMax(secMin.x + 22.0f, secMin.y + 22.0f);

            dl->AddRectFilled(secMin, secMax, toImU32(state.secondaryColor), 1.5f);
            dl->AddRectFilled(secMin + ImVec2(6.0f, 6.0f), secMax - ImVec2(6.0f, 6.0f), StudioColors::PanelBody);
            dl->AddRect(secMin, secMax,
                        state.editingStrokeColor ? StudioColors::GuideCyan : StudioColors::BorderLight,
                        1.5f, state.editingStrokeColor ? 1.6f : 1.0f);

            dl->AddRectFilled(priMin, priMax, toImU32(state.brush.color), 1.5f);
            dl->AddRect(priMin, priMax,
                        !state.editingStrokeColor ? StudioColors::GuideCyan : StudioColors::BorderLight,
                        1.5f, !state.editingStrokeColor ? 1.6f : 1.0f);

            ImVec2 swMin(x + 30.0f, curY - 1.0f);
            ImGui::SetCursorScreenPos(swMin);
            if (ImGui::InvisibleButton("##swap_colors", ImVec2(13.0f, 13.0f))) {
                std::swap(state.brush.color, state.secondaryColor);
                if (canvas && mesh) syncBothColorsToActiveLayer(state, *canvas, *mesh, callbacks);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Swap Fill & Stroke (X)");
            drawSmallIcon(dl, SmallIconKind::SwapArrows, swMin, swMin + ImVec2(12.0f, 12.0f), StudioColors::TextSecondary);

            ImVec2 defMin(x + 4.0f, curY + 24.0f);
            ImGui::SetCursorScreenPos(defMin);
            if (ImGui::InvisibleButton("##def_colors", ImVec2(11.0f, 11.0f))) {
                state.brush.color = {244, 246, 249, 255};
                state.secondaryColor = {22, 24, 28, 255};
                if (canvas && mesh) syncBothColorsToActiveLayer(state, *canvas, *mesh, callbacks);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Default Colors (D)");
            dl->AddRectFilled(defMin + ImVec2(4.0f, 4.0f), defMin + ImVec2(10.0f, 10.0f), IM_COL32(22, 24, 28, 255));
            dl->AddRect(defMin + ImVec2(4.0f, 4.0f), defMin + ImVec2(10.0f, 10.0f), StudioColors::TextMuted, 0.0f, 1.0f);
            dl->AddRectFilled(defMin, defMin + ImVec2(6.0f, 6.0f), IM_COL32(244, 246, 249, 255));

            curY += 42.0f;

            auto swatchPopupPicker = [&](const char* popupId, Color4u8& col, bool isStroke) {
                if (ImGui::BeginPopup(popupId)) {
                    float f4[4];
                    color4ToFloat4(col, f4);
                    ImGui::SetNextItemWidth(164.0f);
                    if (ImGui::ColorPicker4("##LeftSwPop", f4,
                                            ImGuiColorEditFlags_PickerHueBar |
                                            ImGuiColorEditFlags_NoSidePreview |
                                            ImGuiColorEditFlags_NoSmallPreview |
                                            ImGuiColorEditFlags_NoInputs |
                                            ImGuiColorEditFlags_NoLabel |
                                            ImGuiColorEditFlags_NoAlpha)) {
                        col = float4ToColor4(f4);
                        col.a = 255;
                        if (canvas && mesh) syncActiveLayerColors(state, *canvas, *mesh, callbacks, isStroke);
                    }
                    ImGui::EndPopup();
                }
            };

            ImGui::SetCursorScreenPos(priMin);
            if (ImGui::InvisibleButton("##pri_pick_trig", ImVec2(22.0f, 22.0f))) {
                state.editingStrokeColor = false;
                state.activeRightTab = RightPanelTab::Color;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Fill Color (Double-click to pick)");
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) ImGui::OpenPopup("##LeftFillPop");
            }
            swatchPopupPicker("##LeftFillPop", state.brush.color, false);

            ImGui::SetCursorScreenPos(secMin);
            if (ImGui::InvisibleButton("##sec_pick_trig", ImVec2(22.0f, 22.0f))) {
                state.editingStrokeColor = true;
                state.activeRightTab = RightPanelTab::Color;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Stroke Color (Double-click to pick)");
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) ImGui::OpenPopup("##LeftStrokePop");
            }
            swatchPopupPicker("##LeftStrokePop", state.secondaryColor, true);
        }

        // Bottom F12 Render Photo shortcut icon
        float botY = y + height - 38.0f;
        if (botY > curY + 4.0f) {
            ImVec2 camMin(x + leftPad, botY);
            ImVec2 camMax(camMin.x + btnSz, camMin.y + btnSz);
            ImGui::SetCursorScreenPos(camMin);
            bool camClick = ImGui::InvisibleButton("##rail_f12", ImVec2(btnSz, btnSz));
            if (ImGui::IsItemHovered()) {
                dl->AddRectFilled(camMin, camMax, StudioColors::ControlHover, 3.0f);
                if (state.showRichTooltips) {
                    state.hoveredRichTooltipId = static_cast<int>(StudioTool::BeautyRender);
                    state.richTooltipAnchor = ImVec2(camMax.x + 8.0f, camMin.y - 160.0f);
                } else {
                    ImGui::SetTooltip("Render Photo (F12)");
                }
            }
            drawToolIcon(dl, StudioTool::BeautyRender, camMin + ImVec2(6.0f, 6.0f), camMax - ImVec2(6.0f, 6.0f),
                         StudioColors::TextSecondary, false);
            if (camClick) {
                if (callbacks.onRenderPhoto) callbacks.onRenderPhoto();
                else if (callbacks.onRenderBeautyShot) callbacks.onRenderBeautyShot();
            }
        }

        dl->AddLine(ImVec2(x + width - 1.0f, y), ImVec2(x + width - 1.0f, y + height),
                    StudioColors::Border, 1.0f);
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

void drawViewport3DPanel(StudioUiState& state, const ObjMesh& mesh,
                         const StudioCallbacks& callbacks) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::Begin("3D Viewport", nullptr,
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImVec2 avail = ImGui::GetContentRegionAvail();
        avail.x = std::max(avail.x, 16.0f);
        avail.y = std::max(avail.y, 16.0f);

        state.viewport3DPanelMin = Vec2{pos.x, pos.y};
        state.viewport3DPanelSize = Vec2{avail.x, avail.y};

        dl->AddRectFilled(pos, pos + avail, StudioColors::CanvasTrough);
        if (state.viewport3DTex != 0) {
            dl->AddImage(state.viewport3DTex, pos, pos + avail);
        }

        // Allow overlay controls (header, XYZ axis gizmo, floating nav buttons) to intercept clicks cleanly
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##3d_vp_canvas", avail,
                               ImGuiButtonFlags_MouseButtonLeft |
                               ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
        state.viewport3DHovered = ImGui::IsItemHovered();
        state.viewport3DActive = ImGui::IsItemActive();

        if (callbacks.onDraw3DViewportOverlay) {
            callbacks.onDraw3DViewportOverlay();
        }

        // 1. Blender-style translucent 3D Viewport Header Bar (26px)
        const float hdrH = 26.0f;
        dl->AddRectFilled(pos, ImVec2(pos.x + avail.x, pos.y + hdrH), IM_COL32(30, 30, 30, 210));
        dl->AddLine(ImVec2(pos.x, pos.y + hdrH), ImVec2(pos.x + avail.x, pos.y + hdrH),
                    IM_COL32(56, 56, 56, 190), 1.0f);

        ImGui::SetCursorScreenPos(ImVec2(pos.x + 6.0f, pos.y + 3.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 2.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(46, 46, 46, 220));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(62, 62, 62, 235));

        char modeLbl[48];
        std::snprintf(modeLbl, sizeof(modeLbl), "%s  v", modeDisplayName(state.activeMode));
        if (ImGui::Button(modeLbl)) ImGui::OpenPopup("##Vp3DModePopup");
        if (ImGui::BeginPopup("##Vp3DModePopup")) {
            if (ImGui::MenuItem("Texture Paint", nullptr, state.activeMode == StudioMode::TexturePaint)) {
                state.activeMode = StudioMode::TexturePaint;
                if (state.activeTool == StudioTool::VertexEdit || state.activeTool == StudioTool::FacePaintMask) {
                    state.activeTool = StudioTool::Brush;
                }
            }
            if (ImGui::MenuItem("Object Mode", nullptr, state.activeMode == StudioMode::Object)) {
                state.activeMode = StudioMode::Object;
                state.activeTool = StudioTool::SelectMove;
            }
            if (ImGui::MenuItem("Edit Mesh", "Tab", state.activeMode == StudioMode::EditMesh)) {
                state.activeMode = StudioMode::EditMesh;
                state.activeTool = StudioTool::VertexEdit;
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine();

        if (ImGui::Button("View")) ImGui::OpenPopup("##Vp3DViewPopup");
        if (ImGui::BeginPopup("##Vp3DViewPopup")) {
            if (ImGui::MenuItem("Front", "1"))         { if (callbacks.onSetCameraPreset) callbacks.onSetCameraPreset(CameraPreset::Front); }
            if (ImGui::MenuItem("Side", "3"))          { if (callbacks.onSetCameraPreset) callbacks.onSetCameraPreset(CameraPreset::Side); }
            if (ImGui::MenuItem("Top", "7"))           { if (callbacks.onSetCameraPreset) callbacks.onSetCameraPreset(CameraPreset::Top); }
            if (ImGui::MenuItem("Rear"))               { if (callbacks.onSetCameraPreset) callbacks.onSetCameraPreset(CameraPreset::Rear); }
            if (ImGui::MenuItem("3/4 Beauty"))         { if (callbacks.onSetCameraPreset) callbacks.onSetCameraPreset(CameraPreset::ThreeQuarterBeauty); }
            ImGui::Separator();
            if (ImGui::MenuItem("Perspective / Ortho", "5", state.isOrthoCamera)) {
                if (callbacks.onToggleOrtho) callbacks.onToggleOrtho();
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine();

        if (ImGui::Button("Overlays")) ImGui::OpenPopup("##Vp3DOverlayPopup");
        if (ImGui::BeginPopup("##Vp3DOverlayPopup")) {
            ImGui::MenuItem("Show 3D Overlays", nullptr, &state.show3DOverlays);
            ImGui::MenuItem("Studio Floor Grid", nullptr, &state.material.showFloorGrid);
            ImGui::MenuItem("Symmetry Plane (X=0)", nullptr, &state.material.showSymmetryPlane);
            ImGui::EndPopup();
        }

        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();

        // Top-right 4 Blender Shading Spheres inside 3D header
        const float sphR = 7.0f;
        const float sphGap = 22.0f;
        float sphStartX = pos.x + avail.x - 16.0f - sphGap * 3.0f;
        float sphY = pos.y + hdrH * 0.5f;
        const ShadingMode shModes[4] = {ShadingMode::Wireframe, ShadingMode::Clay, ShadingMode::Material, ShadingMode::Rendered};
        const char* shNames[4] = {"Wireframe", "Solid Clay", "Material Preview", "Showroom Rendered"};
        for (int si = 0; si < 4; ++si) {
            ImVec2 c(sphStartX + si * sphGap, sphY);
            ImGui::SetCursorScreenPos(c - ImVec2(10.0f, 10.0f));
            ImGui::PushID(300 + si);
            if (ImGui::InvisibleButton("##sh_sph", ImVec2(20.0f, 20.0f))) {
                state.shadingMode = shModes[si];
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shading: %s", shNames[si]);
            drawShadingSphereIcon(dl, shModes[si], c, sphR, state.shadingMode == shModes[si]);
            ImGui::PopID();
        }

        // 2. Top-left subtle borderless Blender status readout
        char info[128];
        std::string meshNm = mesh.loaded ? std::filesystem::path(mesh.sourcePath).filename().string() : "No Mesh";
        std::snprintf(info, sizeof(info), "User %s  |  %s (%zu tris)",
                      state.isOrthoCamera ? "Orthographic" : "Perspective", meshNm.c_str(), mesh.tris.size());
        dl->AddText(ImVec2(pos.x + 12.0f, pos.y + hdrH + 8.0f), IM_COL32(215, 215, 215, 205), info);

        // 3. Top-right Interactive Blender XYZ Axis Gizmo
        if (state.show3DOverlays && avail.x > 160.0f && avail.y > 220.0f) {
            ImVec2 gizC(pos.x + avail.x - 42.0f, pos.y + hdrH + 44.0f);
            float gizR = 26.0f;

            // Project world axes onto view plane using camRight, camUp, camForward
            auto projAxis = [&](Vec3 a) -> Vec3 {
                return Vec3{dot(a, state.camRight), -dot(a, state.camUp), dot(a, state.camForward)};
            };
            struct AxisHandle {
                Vec3 dir;
                const char* lbl;
                ImU32 col;
                CameraPreset preset;
                bool hasPreset;
            };
            static constexpr AxisHandle kAxes[3] = {
                {{1, 0, 0}, "X", IM_COL32(224, 82, 82, 255),  CameraPreset::Side,  true},
                {{0, 1, 0}, "Y", IM_COL32(105, 179, 76, 255), CameraPreset::Top,   true},
                {{0, 0, 1}, "Z", IM_COL32(75, 137, 220, 255), CameraPreset::Front, true},
            };

            ImGui::SetCursorScreenPos(gizC - ImVec2(gizR + 8.0f, gizR + 8.0f));
            ImGui::InvisibleButton("##xyz_gizmo_bg", ImVec2((gizR + 8.0f) * 2.0f, (gizR + 8.0f) * 2.0f));
            bool gizHover = ImGui::IsItemHovered();
            if (gizHover) {
                dl->AddCircleFilled(gizC, gizR + 8.0f, IM_COL32(255, 255, 255, 18), 28);
            }

            // Sort back-to-front by view Z
            int order[3] = {0, 1, 2};
            Vec3 pAxes[3];
            for (int k = 0; k < 3; ++k) pAxes[k] = projAxis(kAxes[k].dir);
            std::sort(order, order + 3, [&](int a, int b) { return pAxes[a].z < pAxes[b].z; });

            for (int oi = 0; oi < 3; ++oi) {
                int k = order[oi];
                ImVec2 tip(gizC.x + pAxes[k].x * gizR, gizC.y + pAxes[k].y * gizR);
                ImVec2 negTip(gizC.x - pAxes[k].x * gizR * 0.85f, gizC.y - pAxes[k].y * gizR * 0.85f);
                dl->AddLine(gizC, tip, kAxes[k].col, 1.8f);
                dl->AddCircleFilled(negTip, 4.0f, (kAxes[k].col & 0x00FFFFFF) | 0x66000000, 12);
                dl->AddCircle(negTip, 4.0f, kAxes[k].col, 12, 1.0f);
                dl->AddCircleFilled(tip, 7.0f, kAxes[k].col, 14);
                ImVec2 ts = ImGui::CalcTextSize(kAxes[k].lbl);
                dl->AddText(ImVec2(tip.x - ts.x * 0.5f, tip.y - ts.y * 0.5f), IM_COL32(18, 18, 18, 255), kAxes[k].lbl);

                if (gizHover && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    ImVec2 mp = ImGui::GetIO().MousePos;
                    if (std::hypot(mp.x - tip.x, mp.y - tip.y) <= 9.0f && callbacks.onSetCameraPreset) {
                        callbacks.onSetCameraPreset(kAxes[k].preset);
                    } else if (k == 2 && std::hypot(mp.x - negTip.x, mp.y - negTip.y) <= 8.0f &&
                               callbacks.onSetCameraPreset) {
                        callbacks.onSetCameraPreset(CameraPreset::Rear);
                    }
                }
            }

            // 4. Floating translucent circular navigation pills below XYZ gizmo
            float navX = pos.x + avail.x - 36.0f;
            float navY = gizC.y + gizR + 18.0f;
            for (int ni = 0; ni < 4; ++ni) {
                ImVec2 c(navX, navY + ni * 28.0f);
                ImGui::SetCursorScreenPos(c - ImVec2(11.0f, 11.0f));
                ImGui::PushID(400 + ni);
                bool click = ImGui::InvisibleButton("##nav_pill", ImVec2(22.0f, 22.0f));
                bool hov = ImGui::IsItemHovered();
                dl->AddCircleFilled(c, 10.5f, hov ? IM_COL32(72, 72, 72, 220) : IM_COL32(44, 44, 44, 185), 18);
                ImU32 fg = hov ? StudioColors::TextPrimary : StudioColors::TextSecondary;
                if (ni == 0) {
                    drawToolIcon(dl, StudioTool::Zoom, c - ImVec2(9, 9), c + ImVec2(9, 9), fg, false);
                    if (hov) ImGui::SetTooltip("Zoom 3D View (Click or Drag Up/Down)");
                    ImGuiIO& vpIo = ImGui::GetIO();
                    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f)) {
                        if (std::fabs(vpIo.MouseDelta.y) > 0.0f && callbacks.onZoom3D) {
                            callbacks.onZoom3D(-vpIo.MouseDelta.y * 0.04f);
                        }
                    } else if (click && !ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f) && callbacks.onZoom3D) {
                        callbacks.onZoom3D(1.0f);
                    }
                } else if (ni == 1) {
                    drawToolIcon(dl, StudioTool::HandPan, c - ImVec2(9, 9), c + ImVec2(9, 9), fg, ImGui::IsItemActive());
                    if (hov) ImGui::SetTooltip("Pan 3D View (Drag)");
                    ImGuiIO& vpIo = ImGui::GetIO();
                    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.5f) && callbacks.onPan3D) {
                        if (std::fabs(vpIo.MouseDelta.x) > 0.0f || std::fabs(vpIo.MouseDelta.y) > 0.0f) {
                            callbacks.onPan3D(vpIo.MouseDelta.x, vpIo.MouseDelta.y);
                        }
                    }
                } else if (ni == 2) {
                    drawSmallIcon(dl, SmallIconKind::Camera, c - ImVec2(7, 7), c + ImVec2(7, 7), fg);
                    if (hov) ImGui::SetTooltip("3/4 Beauty Camera");
                    if (click && callbacks.onSetCameraPreset) {
                        callbacks.onSetCameraPreset(CameraPreset::ThreeQuarterBeauty);
                    }
                } else {
                    drawSmallIcon(dl, SmallIconKind::OrthoGrid, c - ImVec2(7, 7), c + ImVec2(7, 7),
                                  state.isOrthoCamera ? StudioColors::StudioBlue : fg);
                    if (hov) ImGui::SetTooltip("Toggle Perspective / Orthographic (5)");
                    if (click && callbacks.onToggleOrtho) callbacks.onToggleOrtho();
                }
                ImGui::PopID();
            }
        }
        if (state.request3DContextMenu) {
            ImGui::OpenPopup("##Vp3DCtxMenu");
            state.request3DContextMenu = false;
        }
        if (ImGui::BeginPopup("##Vp3DCtxMenu")) {
            if (ImGui::MenuItem("Copy Selected", "Ctrl+C")) {
                if (callbacks.onCopy) callbacks.onCopy();
                else state.requestCopySelection = true;
            }
            if (ImGui::MenuItem("Cut Selected", "Ctrl+X")) {
                if (callbacks.onCut) callbacks.onCut();
                else state.requestCutSelection = true;
            }
            if (ImGui::MenuItem("Paste", "Ctrl+V")) {
                if (callbacks.onPaste) callbacks.onPaste();
                else state.requestPasteClipboard = true;
            }
            if (ImGui::MenuItem("Delete Selected", "Del")) {
                if (callbacks.onClearSelection) callbacks.onClearSelection();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Mirror", "Shift+M", &state.brush.mirrorX)) {
                if (callbacks.onToggleMirror) callbacks.onToggleMirror(state.brush.mirrorX);
            }
            if (ImGui::MenuItem("3/4 Beauty Camera")) {
                if (callbacks.onSetCameraPreset) callbacks.onSetCameraPreset(CameraPreset::ThreeQuarterBeauty);
            }
            if (ImGui::MenuItem("Render Photo...", "F12")) {
                if (callbacks.onRenderPhoto) callbacks.onRenderPhoto();
                else if (callbacks.onRenderBeautyShot) callbacks.onRenderBeautyShot();
            }
            ImGui::EndPopup();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void drawLayersPanel(StudioUiState& state, CanvasEngine& canvas,
                     const ObjMesh& mesh, const StudioCallbacks& callbacks) {
    if (ImGui::Begin("Layers")) {
        float availW = ImGui::GetContentRegionAvail().x;
        if (canvas.activeLayerIdx >= 0 && canvas.activeLayerIdx < static_cast<int>(canvas.layers.size())) {
            Layer& act = canvas.layers[canvas.activeLayerIdx];
            ImGui::SetNextItemWidth(availW * 0.44f);
            if (ImGui::BeginCombo("##Blend", blendModeName(act.blend))) {
                BlendMode modes[] = {BlendMode::Normal, BlendMode::Multiply, BlendMode::Screen, BlendMode::Overlay};
                for (BlendMode bm : modes) {
                    bool sel = (act.blend == bm);
                    if (ImGui::Selectable(blendModeName(bm), sel)) {
                        canvas.pushUndoSnapshot();
                        act.blend = bm;
                        canvas.recomposite();
                        if (callbacks.onCanvasModified) callbacks.onCanvasModified();
                    }
                    if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1.0f);
            float opPct = act.opacity * 100.0f;
            if (ImGui::DragFloat("##LayerOpacity", &opPct, 0.5f, 0.0f, 100.0f, "Opac %.0f%%")) {
                act.opacity = std::clamp(opPct / 100.0f, 0.0f, 1.0f);
                canvas.recomposite();
                if (callbacks.onCanvasModified) callbacks.onCanvasModified();
            }
            if (ImGui::IsItemActivated()) canvas.pushUndoSnapshot();
            itemTooltip("Active Layer Opacity (%)");
        }

        float listH = std::max(72.0f, ImGui::GetContentRegionAvail().y);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, StudioColors::ToVec4(StudioColors::PanelHeader));
        if (ImGui::BeginChild("##LayerStack", ImVec2(-1.0f, listH), ImGuiChildFlags_None)) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImGuiIO& io = ImGui::GetIO();
            static int s_renamingIdx = -1;
            static char s_inlineRename[128] = {};
            static bool s_focusRename = false;
            static int s_pendingDragIdx = -1;
            static float s_dragStartY = 0.0f;
            static std::vector<uint32_t> s_collapsedIds;

            auto isCollapsed = [&](uint32_t id) {
                return std::find(s_collapsedIds.begin(), s_collapsedIds.end(), id) != s_collapsedIds.end();
            };
            auto toggleCollapsed = [&](uint32_t id) {
                auto it = std::find(s_collapsedIds.begin(), s_collapsedIds.end(), id);
                if (it != s_collapsedIds.end()) s_collapsedIds.erase(it);
                else s_collapsedIds.push_back(id);
            };

            int nLayers = static_cast<int>(canvas.layers.size());
            int dropGapK = -1;
            float dropLineY = 0.0f;
            float dropLineX0 = 0.0f, dropLineX1 = 0.0f;

            for (int i = nLayers - 1; i >= 0; --i) {
                Layer& l = canvas.layers[i];
                bool isSel = (i == canvas.activeLayerIdx);
                bool hasItems = !l.items.empty();
                bool collapsed = isCollapsed(l.id);
                ImGui::PushID(static_cast<int>(l.id));

                ImVec2 rowMin = ImGui::GetCursorScreenPos();
                float rowW = ImGui::GetContentRegionAvail().x;
                float rowH = 25.0f;
                ImVec2 rowMax(rowMin.x + rowW, rowMin.y + rowH);

                if (isSel && canvas.selectedItemIdx < 0) {
                    dl->AddRectFilled(rowMin, rowMax, StudioColors::ControlActive);
                    dl->AddRectFilled(rowMin, ImVec2(rowMin.x + 2.5f, rowMax.y), StudioColors::StudioBlue);
                } else if (isSel) {
                    dl->AddRectFilled(rowMin, rowMax, IM_COL32(34, 38, 46, 255));
                }

                // Expand/Collapse triangle when layer has child VectorItems
                if (hasItems) {
                    ImGui::SetCursorScreenPos(ImVec2(rowMin.x + 2.0f, rowMin.y + 4.0f));
                    if (ImGui::InvisibleButton("##exp", ImVec2(14.0f, 17.0f))) {
                        toggleCollapsed(l.id);
                    }
                    ImVec2 c(rowMin.x + 9.0f, rowMin.y + 12.5f);
                    if (collapsed) {
                        dl->AddTriangleFilled(c + ImVec2(-2.5f, -4.0f), c + ImVec2(3.5f, 0.0f), c + ImVec2(-2.5f, 4.0f),
                                              StudioColors::TextSecondary);
                    } else {
                        dl->AddTriangleFilled(c + ImVec2(-4.0f, -2.5f), c + ImVec2(4.0f, -2.5f), c + ImVec2(0.0f, 3.5f),
                                              StudioColors::TextSecondary);
                    }
                }

                // Eye toggle (Alt+Click solos / un-solos layer visibility)
                ImVec2 eyeMin(rowMin.x + 17.0f, rowMin.y + 4.0f);
                ImGui::SetCursorScreenPos(eyeMin);
                if (ImGui::InvisibleButton("##eye", ImVec2(17.0f, 17.0f))) {
                    canvas.pushUndoSnapshot();
                    if (io.KeyAlt) {
                        bool onlyMe = l.visible;
                        for (int oi = 0; oi < nLayers; ++oi) {
                            if (oi != i && canvas.layers[oi].visible) { onlyMe = false; break; }
                        }
                        for (int oi = 0; oi < nLayers; ++oi) {
                            canvas.layers[oi].visible = onlyMe ? true : (oi == i);
                        }
                    } else {
                        l.visible = !l.visible;
                    }
                    canvas.recomposite();
                    if (callbacks.onCanvasModified) callbacks.onCanvasModified();
                }
                itemTooltip("Toggle Visibility (Alt+Click to Solo)");
                drawSmallIcon(dl, l.visible ? SmallIconKind::EyeOpen : SmallIconKind::EyeClosed,
                              eyeMin, eyeMin + ImVec2(17.0f, 17.0f),
                              l.visible ? StudioColors::TextPrimary : StudioColors::TextMuted);

                ImVec2 lockMin(rowMin.x + 36.0f, rowMin.y + 4.0f);
                ImGui::SetCursorScreenPos(lockMin);
                if (ImGui::InvisibleButton("##lock", ImVec2(17.0f, 17.0f))) {
                    canvas.pushUndoSnapshot();
                    l.locked = !l.locked;
                }
                itemTooltip("Lock / Unlock Layer");
                drawSmallIcon(dl, l.locked ? SmallIconKind::Locked : SmallIconKind::Unlocked,
                              lockMin, lockMin + ImVec2(17.0f, 17.0f),
                              l.locked ? StudioColors::AmberAccent : StudioColors::TextMuted);

                const char* tag = hasItems                             ? "L" :
                                  (l.kind == LayerKind::RasterPaint) ? "P" :
                                  (l.kind == LayerKind::Text)        ? "T" :
                                  (l.kind == LayerKind::Decal)       ? "I" : "V";
                ImU32 tagCol = (l.kind == LayerKind::RasterPaint && !hasItems) ? StudioColors::TextSecondary : StudioColors::StudioBlue;
                ImVec2 chipMin(rowMin.x + 56.0f, rowMin.y + 4.0f);
                ImVec2 chipMax(chipMin.x + 15.0f, chipMin.y + 17.0f);
                dl->AddRectFilled(chipMin, chipMax, StudioColors::ControlBg, 2.0f);
                dl->AddText(ImVec2(chipMin.x + 4.0f, chipMin.y + 1.5f), tagCol, tag);

                char opBuf[16];
                std::snprintf(opBuf, sizeof(opBuf), "%d%%", static_cast<int>(std::round(l.opacity * 100.0f)));
                ImVec2 opSz = ImGui::CalcTextSize(opBuf);

                if (s_renamingIdx == i) {
                    ImGui::SetCursorScreenPos(ImVec2(rowMin.x + 75.0f, rowMin.y + 2.0f));
                    ImGui::SetNextItemWidth(std::max(40.0f, rowW - 81.0f - opSz.x));
                    if (s_focusRename) {
                        ImGui::SetKeyboardFocusHere();
                        s_focusRename = false;
                    }
                    bool commit = ImGui::InputText("##InlineRen", s_inlineRename, sizeof(s_inlineRename),
                                                   ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                    if (commit || (!ImGui::IsItemActive() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))) {
                        if (s_inlineRename[0] != '\0') {
                            canvas.pushUndoSnapshot();
                            l.name = s_inlineRename;
                        }
                        s_renamingIdx = -1;
                    } else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                        s_renamingIdx = -1;
                    }
                } else {
                    ImGui::SetCursorScreenPos(ImVec2(rowMin.x + 75.0f, rowMin.y));
                    float selW = std::max(20.0f, rowW - 75.0f);
                    if (ImGui::Selectable("##sel", isSel, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(selW, rowH))) {
                        canvas.activeLayerIdx = i;
                        canvas.selectedItemIdx = -1;
                        state.textEditSyncedLayerId = 0;
                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                            s_renamingIdx = i;
                            std::snprintf(s_inlineRename, sizeof(s_inlineRename), "%s", l.name.c_str());
                            s_focusRename = true;
                        } else if (l.kind == LayerKind::Text) {
                            state.activeTool = StudioTool::Text;
                            state.activePropsCat = PropertiesCategory::Text;
                        } else if (l.kind == LayerKind::Decal) {
                            state.activeTool = StudioTool::Image;
                            state.activePropsCat = PropertiesCategory::Decal;
                        } else if (l.kind == LayerKind::RulerStripe) {
                            state.activeTool = l.ruler.smoothCurve ? StudioTool::Pen : StudioTool::Line;
                            state.activePenToolVariant = l.ruler.smoothCurve ? 0 : 1;
                            state.activePropsCat = PropertiesCategory::Ruler;
                        }
                    }
                    if (ImGui::IsItemActive() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        s_pendingDragIdx = i;
                        s_dragStartY = io.MousePos.y;
                    }
                    if (s_pendingDragIdx == i && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
                        std::fabs(io.MousePos.y - s_dragStartY) >= 4.0f) {
                        state.layerDragFromIdx = i;
                    }

                    dl->PushClipRect(ImVec2(rowMin.x + 77.0f, rowMin.y),
                                     ImVec2(std::max(rowMin.x + 79.0f, rowMax.x - opSz.x - 10.0f), rowMax.y), true);
                    dl->AddText(ImVec2(rowMin.x + 77.0f, rowMin.y + 4.5f),
                                l.visible ? StudioColors::TextPrimary : StudioColors::TextMuted, l.name.c_str());
                    dl->PopClipRect();
                }

                if (state.layerDragFromIdx >= 0 && io.MousePos.x >= rowMin.x && io.MousePos.x <= rowMax.x &&
                    io.MousePos.y >= rowMin.y && io.MousePos.y <= rowMax.y) {
                    bool topHalf = (io.MousePos.y < (rowMin.y + rowH * 0.5f));
                    dropGapK = topHalf ? (i + 1) : i;
                    dropLineY = topHalf ? rowMin.y : rowMax.y;
                    dropLineX0 = rowMin.x;
                    dropLineX1 = rowMax.x;
                }

                if (ImGui::BeginPopupContextItem("##layer_ctx")) {
                    canvas.activeLayerIdx = i;
                    static char renameBuf[128] = {};
                    if (ImGui::IsWindowAppearing()) {
                        std::snprintf(renameBuf, sizeof(renameBuf), "%s", l.name.c_str());
                    }
                    ImGui::SetNextItemWidth(160.0f);
                    if (ImGui::InputText("##rename", renameBuf, sizeof(renameBuf), ImGuiInputTextFlags_EnterReturnsTrue)) {
                        if (renameBuf[0] != '\0') {
                            canvas.pushUndoSnapshot();
                            l.name = renameBuf;
                        }
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("New Layer", "Ctrl+Shift+N")) {
                        if (callbacks.onNewLayer) {
                            callbacks.onNewLayer();
                        } else {
                            char nm[32];
                            std::snprintf(nm, sizeof(nm), "Layer %zu", canvas.layers.size());
                            canvas.addLayer(nm, false, {0, 0, 0, 0});
                            if (callbacks.onCanvasModified) callbacks.onCanvasModified();
                        }
                    }
                    if (ImGui::MenuItem("Duplicate Layer", "Ctrl+J")) {
                        if (callbacks.onDuplicateLayer) callbacks.onDuplicateLayer(i);
                        else {
                            canvas.duplicateLayer(i);
                            if (callbacks.onCanvasModified) callbacks.onCanvasModified();
                        }
                    }
                    if (ImGui::MenuItem("Paste", "Ctrl+V", false, canvas.hasClipboardItem)) {
                        if (callbacks.onPaste) callbacks.onPaste();
                        else state.requestPasteClipboard = true;
                    }
                    if ((l.kind != LayerKind::RasterPaint || !l.items.empty()) &&
                        ImGui::MenuItem("Rasterize Vector Items")) {
                        if (canvas.rasterizeLayerToPaint(i, &mesh) && callbacks.onCanvasModified) {
                            callbacks.onCanvasModified();
                        }
                    }
                    bool canFlat = (i > 0 && canvas.layers[i - 1].kind == LayerKind::RasterPaint);
                    if (ImGui::MenuItem("Flatten Selected", "Ctrl+E", false, canFlat)) {
                        if (canvas.mergeDown(i) && callbacks.onCanvasModified) callbacks.onCanvasModified();
                    }
                    if (ImGui::MenuItem("Bake AO Overlay", nullptr, false, mesh.loaded)) {
                        if (callbacks.onBakeMeshAo) callbacks.onBakeMeshAo();
                        else {
                            canvas.bakeMeshAoOverlay(mesh, 1024, true);
                            if (callbacks.onCanvasModified) callbacks.onCanvasModified();
                        }
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Delete Layer", "Del", false, canvas.layers.size() > 1)) {
                        if (callbacks.onDeleteLayer) callbacks.onDeleteLayer(i);
                        else {
                            canvas.removeLayer(i);
                            if (callbacks.onCanvasModified) callbacks.onCanvasModified();
                        }
                    }
                    ImGui::EndPopup();
                }

                dl->AddText(ImVec2(rowMax.x - opSz.x - 6.0f, rowMin.y + 4.5f), StudioColors::TextMuted, opBuf);
                dl->AddLine(ImVec2(rowMin.x, rowMax.y), rowMax, StudioColors::Border, 1.0f);

                // Expandable child VectorItems inside container layer
                if (hasItems && !collapsed) {
                    int nItems = static_cast<int>(l.items.size());
                    for (int ii = nItems - 1; ii >= 0; --ii) {
                        VectorItem& it = l.items[ii];
                        bool itSel = (i == canvas.activeLayerIdx && ii == canvas.selectedItemIdx);
                        ImGui::PushID(static_cast<int>(it.id) + 10000);
                        ImVec2 subMin = ImGui::GetCursorScreenPos();
                        float subH = 21.0f;
                        ImVec2 subMax(subMin.x + rowW, subMin.y + subH);

                        if (itSel) {
                            dl->AddRectFilled(subMin, subMax, StudioColors::ControlActive);
                            dl->AddRectFilled(subMin, ImVec2(subMin.x + 2.5f, subMax.y), StudioColors::AmberAccent);
                        }
                        dl->AddLine(ImVec2(subMin.x + 12.0f, subMin.y), ImVec2(subMin.x + 12.0f, subMax.y),
                                    StudioColors::BorderLight, 1.0f);
                        dl->AddLine(ImVec2(subMin.x + 12.0f, subMin.y + subH * 0.5f),
                                    ImVec2(subMin.x + 20.0f, subMin.y + subH * 0.5f),
                                    StudioColors::BorderLight, 1.0f);

                        ImVec2 subEye(subMin.x + 22.0f, subMin.y + 2.5f);
                        ImGui::SetCursorScreenPos(subEye);
                        if (ImGui::InvisibleButton("##it_eye", ImVec2(15.0f, 15.0f))) {
                            canvas.pushUndoSnapshot();
                            it.visible = !it.visible;
                            canvas.rasterizeLayer(i, &mesh);
                            if (callbacks.onCanvasModified) callbacks.onCanvasModified();
                        }
                        drawSmallIcon(dl, it.visible ? SmallIconKind::EyeOpen : SmallIconKind::EyeClosed,
                                      subEye, subEye + ImVec2(15.0f, 15.0f),
                                      it.visible ? StudioColors::TextPrimary : StudioColors::TextMuted);

                        ImVec2 subLock(subMin.x + 39.0f, subMin.y + 2.5f);
                        ImGui::SetCursorScreenPos(subLock);
                        if (ImGui::InvisibleButton("##it_lock", ImVec2(15.0f, 15.0f))) {
                            canvas.pushUndoSnapshot();
                            it.locked = !it.locked;
                        }
                        drawSmallIcon(dl, it.locked ? SmallIconKind::Locked : SmallIconKind::Unlocked,
                                      subLock, subLock + ImVec2(15.0f, 15.0f),
                                      it.locked ? StudioColors::AmberAccent : StudioColors::TextMuted);

                        const char* itTag = (it.kind == VectorItemKind::PenPath)   ? "P" :
                                            (it.kind == VectorItemKind::RectShape) ? "R" :
                                            (it.kind == VectorItemKind::Text)      ? "T" : "I";
                        ImVec2 icMin(subMin.x + 56.0f, subMin.y + 3.0f);
                        ImVec2 icMax(icMin.x + 13.0f, icMin.y + 15.0f);
                        dl->AddRectFilled(icMin, icMax, StudioColors::ControlBg, 2.0f);
                        dl->AddText(ImVec2(icMin.x + 3.0f, icMin.y + 0.5f), StudioColors::AmberAccent, itTag);

                        ImGui::SetCursorScreenPos(ImVec2(subMin.x + 73.0f, subMin.y));
                        if (ImGui::Selectable("##it_sel", itSel, ImGuiSelectableFlags_None,
                                              ImVec2(std::max(20.0f, rowW - 73.0f), subH))) {
                            canvas.activeLayerIdx = i;
                            canvas.selectedItemIdx = ii;
                            state.textEditSyncedLayerId = 0;
                            if (it.kind == VectorItemKind::Text) {
                                state.activePropsCat = PropertiesCategory::Text;
                            } else if (it.kind == VectorItemKind::Image) {
                                state.activePropsCat = PropertiesCategory::Decal;
                            } else if (it.kind == VectorItemKind::PenPath) {
                                state.activePropsCat = PropertiesCategory::Ruler;
                            } else if (it.kind == VectorItemKind::RectShape) {
                                state.activeTool = StudioTool::Rect;
                                state.activePropsCat = PropertiesCategory::Tool;
                            }
                        }
                        dl->PushClipRect(ImVec2(subMin.x + 75.0f, subMin.y), ImVec2(subMax.x - 6.0f, subMax.y), true);
                        dl->AddText(ImVec2(subMin.x + 75.0f, subMin.y + 3.0f),
                                    it.visible ? StudioColors::TextSecondary : StudioColors::TextMuted,
                                    it.name.c_str());
                        dl->PopClipRect();

                        if (ImGui::BeginPopupContextItem("##vitem_ctx")) {
                            canvas.activeLayerIdx = i;
                            canvas.selectedItemIdx = ii;
                            if (ImGui::MenuItem("Copy", "Ctrl+C")) {
                                if (callbacks.onCopy) callbacks.onCopy();
                                else {
                                    canvas.copySelectedItem();
                                }
                            }
                            if (ImGui::MenuItem("Cut", "Ctrl+X")) {
                                if (callbacks.onCut) callbacks.onCut();
                                else {
                                    canvas.cutSelectedItem(&mesh);
                                    if (callbacks.onCanvasModified) callbacks.onCanvasModified();
                                }
                            }
                            if (ImGui::MenuItem("Toggle Mirror")) {
                                bool curM = (it.kind == VectorItemKind::PenPath)   ? it.pen.mirrorX :
                                            (it.kind == VectorItemKind::RectShape) ? it.rect.mirrorX :
                                            (it.kind == VectorItemKind::Text)      ? it.text.mirrorX : it.image.mirrorX;
                                canvas.setMirrorOnActiveItem(!curM, &mesh);
                                if (callbacks.onCanvasModified) callbacks.onCanvasModified();
                            }
                            if (ImGui::MenuItem("Delete Item", "Del")) {
                                if (canvas.deleteSelectedItem(&mesh) && callbacks.onCanvasModified) {
                                    callbacks.onCanvasModified();
                                }
                            }
                            ImGui::EndPopup();
                        }

                        dl->AddLine(ImVec2(subMin.x + 18.0f, subMax.y), subMax, IM_COL32(34, 34, 34, 180), 1.0f);
                        ImGui::PopID();
                    }
                }

                ImGui::PopID();
            }

            if (ImGui::BeginPopupContextWindow("##layers_win_ctx",
                                                ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
                if (ImGui::MenuItem("New Layer", "Ctrl+Shift+N")) {
                    if (callbacks.onNewLayer) callbacks.onNewLayer();
                    else {
                        char nm[32];
                        std::snprintf(nm, sizeof(nm), "Layer %zu", canvas.layers.size());
                        canvas.addLayer(nm, false, {0, 0, 0, 0});
                        if (callbacks.onCanvasModified) callbacks.onCanvasModified();
                    }
                }
                if (ImGui::MenuItem("Paste", "Ctrl+V", false, canvas.hasClipboardItem)) {
                    if (callbacks.onPaste) callbacks.onPaste();
                    else state.requestPasteClipboard = true;
                }
                if (ImGui::MenuItem("Bake AO Overlay", nullptr, false, mesh.loaded)) {
                    if (callbacks.onBakeMeshAo) callbacks.onBakeMeshAo();
                    else {
                        canvas.bakeMeshAoOverlay(mesh, 1024, true);
                        if (callbacks.onCanvasModified) callbacks.onCanvasModified();
                    }
                }
                ImGui::EndPopup();
            }

            // Render floating pill + 2.5px #00B8FF cyan insertion bar during layer drag
            if (state.layerDragFromIdx >= 0 && state.layerDragFromIdx < nLayers) {
                if (dropGapK >= 0) {
                    dl->AddLine(ImVec2(dropLineX0, dropLineY), ImVec2(dropLineX1, dropLineY),
                                StudioColors::GuideCyan, 2.5f);
                }
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    const char* dragName = canvas.layers[state.layerDragFromIdx].name.c_str();
                    ImVec2 ts = ImGui::CalcTextSize(dragName);
                    ImVec2 p0(io.MousePos.x + 12.0f, io.MousePos.y - 10.0f);
                    ImVec2 p1(p0.x + ts.x + 18.0f, p0.y + 20.0f);
                    ImGui::GetForegroundDrawList()->AddRectFilled(p0, p1, IM_COL32(36, 40, 48, 220), 4.0f);
                    ImGui::GetForegroundDrawList()->AddRect(p0, p1, StudioColors::GuideCyan, 4.0f, 1.2f);
                    ImGui::GetForegroundDrawList()->AddText(ImVec2(p0.x + 9.0f, p0.y + 3.0f),
                                                            StudioColors::TextPrimary, dragName);
                } else {
                    if (dropGapK >= 0) {
                        int fromIdx = state.layerDragFromIdx;
                        if (io.KeyAlt) {
                            int dupIdx = canvas.duplicateLayer(fromIdx);
                            int targetIdx = std::clamp((dupIdx < dropGapK) ? (dropGapK - 1) : dropGapK,
                                                       0, static_cast<int>(canvas.layers.size()) - 1);
                            canvas.reorderLayer(dupIdx, targetIdx);
                        } else {
                            int targetIdx = std::clamp((fromIdx < dropGapK) ? (dropGapK - 1) : dropGapK,
                                                       0, nLayers - 1);
                            if (targetIdx != fromIdx) {
                                canvas.reorderLayer(fromIdx, targetIdx);
                            }
                        }
                        if (callbacks.onCanvasModified) callbacks.onCanvasModified();
                    }
                    state.layerDragFromIdx = -1;
                    s_pendingDragIdx = -1;
                }
            } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                s_pendingDragIdx = -1;
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    ImGui::End();
}

void drawInspectorPanel(StudioUiState& state, CanvasEngine& canvas,
                        ObjMesh& mesh, const StudioCallbacks& callbacks) {
    if (ImGui::Begin("Properties")) {
        // Illustrator-style Top Tabs: Color | Properties | Outliner (NO context menu on Properties)
        float tabW = (ImGui::GetContentRegionAvail().x - 8.0f) / 3.0f;
        if (flatTogglePill("Color", state.activeRightTab == RightPanelTab::Color, false, ImVec2(tabW, 21.0f))) {
            state.activeRightTab = RightPanelTab::Color;
        }
        ImGui::SameLine();
        if (flatTogglePill("Properties", state.activeRightTab == RightPanelTab::Properties, false, ImVec2(tabW, 21.0f))) {
            state.activeRightTab = RightPanelTab::Properties;
        }
        ImGui::SameLine();
        if (flatTogglePill("Outliner", state.activeRightTab == RightPanelTab::Outliner, false, ImVec2(tabW, 21.0f))) {
            state.activeRightTab = RightPanelTab::Outliner;
        }
        ImGui::Separator();

        if (state.activeRightTab == RightPanelTab::Color) {
            drawColorTabContent(state, canvas, mesh, callbacks);
        } else if (state.activeRightTab == RightPanelTab::Outliner) {
            drawOutlinerTabContent(state, canvas, mesh, callbacks);
        } else {
            const Layer* actL = (canvas.activeLayerIdx >= 0 &&
                                 canvas.activeLayerIdx < static_cast<int>(canvas.layers.size()))
                                ? &canvas.layers[canvas.activeLayerIdx]
                                : nullptr;
            const VectorItem* actItem = canvas.activeVectorItem();

            auto sectionFlags = [&](PropertiesCategory cat, bool extraCondition = false) -> ImGuiTreeNodeFlags {
                if (state.forceOpenPropsCat == static_cast<int>(cat)) {
                    ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                }
                return (state.activePropsCat == cat || extraCondition)
                       ? ImGuiTreeNodeFlags_DefaultOpen
                       : ImGuiTreeNodeFlags_None;
            };

            if (ImGui::CollapsingHeader("Brush & Paint", sectionFlags(PropertiesCategory::Tool,
                state.activeTool == StudioTool::Brush || state.activeTool == StudioTool::Eraser ||
                state.activeTool == StudioTool::Fill))) {
                drawBrushSubpanel(state);
            }
            if (ImGui::CollapsingHeader("Rectangle Shape", sectionFlags(PropertiesCategory::Tool,
                state.activeTool == StudioTool::Rect ||
                (actItem && actItem->kind == VectorItemKind::RectShape)))) {
                drawRectSubpanel(state, canvas, mesh, callbacks);
            }
            if (ImGui::CollapsingHeader("Typography", sectionFlags(PropertiesCategory::Text,
                state.activeTool == StudioTool::Text || state.activeTool == StudioTool::TouchType ||
                (actItem && actItem->kind == VectorItemKind::Text) ||
                (!actItem && actL && actL->kind == LayerKind::Text)))) {
                drawTextSubpanel(state, canvas, mesh, callbacks);
            }
            if (ImGui::CollapsingHeader("Image", sectionFlags(PropertiesCategory::Decal,
                state.activeTool == StudioTool::Image ||
                (actItem && actItem->kind == VectorItemKind::Image) ||
                (!actItem && actL && actL->kind == LayerKind::Decal)))) {
                drawDecalSubpanel(state, canvas, mesh, callbacks);
            }
            if (ImGui::CollapsingHeader("Pen & Path", sectionFlags(PropertiesCategory::Ruler,
                state.activeTool == StudioTool::Line || state.activeTool == StudioTool::Pen ||
                state.activeTool == StudioTool::DirectSelect ||
                (actItem && actItem->kind == VectorItemKind::PenPath) ||
                (!actItem && actL && actL->kind == LayerKind::RulerStripe)))) {
                drawRulerSubpanel(state, canvas, mesh, callbacks);
            }
            if (ImGui::CollapsingHeader("3D Mesh Edit & Face Mask", sectionFlags(PropertiesCategory::MeshEdit,
                state.activeMode == StudioMode::EditMesh))) {
                drawMeshEditSubpanel(state, mesh, callbacks);
            }
            if (ImGui::CollapsingHeader("Material Shader", sectionFlags(PropertiesCategory::Material))) {
                drawMaterialSubpanel(state);
            }
            if (ImGui::CollapsingHeader("Studio Lights & Render", sectionFlags(PropertiesCategory::Lights))) {
                drawLightsSubpanel(state, callbacks);
            }
            state.forceOpenPropsCat = -1;
        }
    }
    ImGui::End();
}

void drawStatusBar(const StudioUiState& state, const CanvasEngine& canvas,
                   const ObjMesh& mesh, float x, float y, float width, float height) {
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 2.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, StudioColors::ToVec4(StudioColors::MenuBarBg));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoDocking;
    if (ImGui::Begin("##StudioStatusBar", nullptr, flags)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddLine(ImVec2(x, y), ImVec2(x + width, y), StudioColors::Border, 1.0f);

        const char* symStr = state.brush.mirrorX ? "Mirror" : "Symmetry Off";
        char left[256];
        std::snprintf(left, sizeof(left),
                      "LMB: %s  |  RMB: Orbit/Ctx  |  Shift: Snap  |  Ctrl+R: Rulers  |  %s  |  %dx%d  |  %zu tris",
                      toolDisplayName(state.activeTool), symStr, canvas.width, canvas.height, mesh.tris.size());
        ImGui::TextColored(StudioColors::ToVec4(StudioColors::TextSecondary), "%s", left);

        ImVec2 stSz = ImGui::CalcTextSize(state.statusText.c_str());
        float prevRightX = ImGui::GetItemRectMax().x - x;
        if (prevRightX + stSz.x + 16.0f < width - 12.0f) {
            ImGui::SameLine(width - stSz.x - 12.0f);
            ImGui::TextColored(StudioColors::ToVec4(StudioColors::TextPrimary), "%s", state.statusText.c_str());
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

void drawHomeScreen(StudioUiState& state, CanvasEngine& canvas, const StudioCallbacks& callbacks,
                    float x, float y, float width, float height) {
    (void)canvas;
    const float navW = std::min(172.0f, width * 0.21f);
    const ImU32 homeLeftBg = IM_COL32(0x1E, 0x1E, 0x1E, 255);
    const ImU32 homeMainBg = IM_COL32(0x26, 0x26, 0x26, 255);
    const ImU32 cardBg     = IM_COL32(0x2E, 0x2E, 0x2E, 255);
    const ImU32 cardBorder = IM_COL32(0x3B, 0x3B, 0x3B, 255);
    const ImU32 adobeBlue  = IM_COL32(0x14, 0x73, 0xE6, 255);
    const ImU32 adobeBlueH = IM_COL32(0x26, 0x80, 0xEB, 255);
    const ImU32 adobeBlueA = IM_COL32(0x0D, 0x66, 0xD0, 255);

    auto invokePreset = [&](int idx) {
        if (callbacks.onStartPreset) {
            callbacks.onStartPreset(idx);
        } else {
            if (idx == 0 && callbacks.onLoadProject) callbacks.onLoadProject();
            else if (idx == 1 && callbacks.onOpenObj) callbacks.onOpenObj();
            else if (idx == 2 && callbacks.onOpenLiveryPng) callbacks.onOpenLiveryPng();
            else if (callbacks.onNewProject) callbacks.onNewProject();
            state.showHomeScreen = false;
        }
    };

    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, StudioColors::ToVec4(homeMainBg));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoDocking;
    if (ImGui::Begin("##CoatroomHomeScreen", nullptr, flags)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // 1. Compact Left Navigation Rail (#1E1E1E)
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + navW, y + height), homeLeftBg);
        dl->AddLine(ImVec2(x + navW, y), ImVec2(x + navW, y + height), StudioColors::Border, 1.0f);

        float pillW = std::min(120.0f, std::max(84.0f, navW - 36.0f));
        ImVec2 newMin(x + std::floor((navW - pillW) * 0.5f), y + 18.0f);
        ImGui::SetCursorScreenPos(newMin);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, StudioColors::ToVec4(adobeBlue));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, StudioColors::ToVec4(adobeBlueH));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, StudioColors::ToVec4(adobeBlueA));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
        if (ImGui::Button("New project", ImVec2(pillW, 24.0f))) {
            invokePreset(3);
        }
        ImGui::PopStyleColor(4);

        ImGui::SetCursorScreenPos(ImVec2(newMin.x, newMin.y + 30.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, StudioColors::ToVec4(StudioColors::ControlHover));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, StudioColors::ToVec4(StudioColors::ControlActive));
        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0x4A, 0x4A, 0x4A, 255));
        if (ImGui::Button("Open...", ImVec2(pillW, 24.0f))) {
            if (callbacks.onLoadProject) callbacks.onLoadProject();
            else if (callbacks.onOpenObj) callbacks.onOpenObj();
        }
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(3);

        float divY = newMin.y + 66.0f;
        dl->AddLine(ImVec2(x + 14.0f, divY), ImVec2(x + navW - 14.0f, divY), IM_COL32(0x33, 0x33, 0x33, 255), 1.0f);

        struct NavItem { const char* lbl; SmallIconKind icon; int tabId; };
        static constexpr NavItem kNavs[] = {
            {"Home",      SmallIconKind::Home,      0},
            {"Templates", SmallIconKind::Templates, 1},
        };
        float navY = divY + 10.0f;
        for (int ni = 0; ni < 2; ++ni) {
            bool act = (state.homeNavTab == kNavs[ni].tabId);
            ImVec2 rMin(x, navY + ni * 32.0f);
            ImVec2 rMax(x + navW - 1.0f, rMin.y + 30.0f);
            ImGui::SetCursorScreenPos(rMin);
            ImGui::PushID(ni);
            bool clk = ImGui::InvisibleButton("##home_nav", ImVec2(navW - 1.0f, 30.0f));
            bool hov = ImGui::IsItemHovered();
            ImGui::PopID();
            if (act) {
                dl->AddRectFilled(rMin, rMax, IM_COL32(0x2A, 0x2A, 0x2A, 255));
                dl->AddRectFilled(ImVec2(x, rMin.y + 6.0f), ImVec2(x + 3.0f, rMax.y - 6.0f), IM_COL32(238, 238, 238, 255), 1.5f);
            } else if (hov) {
                dl->AddRectFilled(rMin, rMax, IM_COL32(0x26, 0x26, 0x26, 255));
            }
            ImVec2 iMin(x + 18.0f, rMin.y + 7.0f);
            ImU32 fg = (act || hov) ? StudioColors::TextPrimary : StudioColors::TextSecondary;
            drawSmallIcon(dl, kNavs[ni].icon, iMin, iMin + ImVec2(16.0f, 16.0f), fg);
            dl->AddText(ImVec2(x + 42.0f, rMin.y + 8.0f), fg, kNavs[ni].lbl);
            if (clk) state.homeNavTab = kNavs[ni].tabId;
        }

        if (state.hasActiveDocument) {
            ImVec2 bkPos(newMin.x, y + height - 42.0f);
            ImGui::SetCursorScreenPos(bkPos);
            if (flatTogglePill("Back to Editor", false, false, ImVec2(pillW, 24.0f))) {
                state.showHomeScreen = false;
                if (callbacks.onReturnToEditor) callbacks.onReturnToEditor();
            }
        }

        // 2. Main Welcome & Recent Canvas (#262626)
        ImGui::SetCursorScreenPos(ImVec2(x + navW, y));
        if (ImGui::BeginChild("##HomeMainContent", ImVec2(width - navW, height), ImGuiChildFlags_None)) {
            ImDrawList* mdl = ImGui::GetWindowDrawList();
            float areaW = ImGui::GetContentRegionAvail().x;
            float colW = std::clamp(areaW - 56.0f, 320.0f, 880.0f);
            float colLeft = std::floor((areaW - colW) * 0.5f);

            ImGui::SetCursorPos(ImVec2(colLeft, 18.0f));

            // Optional Crash Recovery Banner
            if (state.hasRecoveryBanner) {
                ImVec2 bMin = ImGui::GetCursorScreenPos();
                float bH = 44.0f;
                ImVec2 bMax(bMin.x + colW, bMin.y + bH);
                mdl->AddRectFilled(bMin, bMax, IM_COL32(0x33, 0x27, 0x15, 255), 6.0f);
                mdl->AddRect(bMin, bMax, IM_COL32(0xD9, 0x9B, 0x26, 255), 6.0f, 1.0f);

                ImVec2 ic(bMin.x + 22.0f, bMin.y + bH * 0.5f);
                mdl->AddCircleFilled(ic, 9.0f, IM_COL32(0xB8, 0x7D, 0x14, 255), 16);
                mdl->AddRectFilled(ImVec2(ic.x - 1.0f, ic.y - 4.8f), ImVec2(ic.x + 1.0f, ic.y + 1.2f), IM_COL32(255, 255, 255, 255));
                mdl->AddCircleFilled(ImVec2(ic.x, ic.y + 3.8f), 1.15f, IM_COL32(255, 255, 255, 255), 8);

                mdl->AddText(ImVec2(bMin.x + 40.0f, bMin.y + 7.0f), IM_COL32(245, 232, 200, 255),
                             "Recovered Unsaved Session Found");
                const char* recSub = state.recoveryInfoText.empty()
                                     ? "An autosave snapshot from an earlier session can be restored."
                                     : state.recoveryInfoText.c_str();
                mdl->AddText(ImVec2(bMin.x + 40.0f, bMin.y + 23.0f), StudioColors::TextSecondary, recSub);

                ImGui::SetCursorScreenPos(ImVec2(bMax.x - 172.0f, bMin.y + 10.0f));
                ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0xB8, 0x7D, 0x14, 255));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0xD9, 0x9B, 0x26, 255));
                if (ImGui::Button("Recover", ImVec2(76.0f, 24.0f))) {
                    state.hasRecoveryBanner = false;
                    state.showHomeScreen = false;
                    if (callbacks.onRestoreRecovery) callbacks.onRestoreRecovery();
                }
                ImGui::PopStyleColor(2);
                ImGui::SameLine(0.0f, 8.0f);
                if (ImGui::Button("Dismiss", ImVec2(76.0f, 24.0f))) {
                    state.hasRecoveryBanner = false;
                    if (callbacks.onDismissRecovery) callbacks.onDismissRecovery();
                }
                ImGui::SetCursorPos(ImVec2(colLeft, 76.0f));
            } else {
                ImGui::SetCursorPos(ImVec2(colLeft, 24.0f));
            }

            // Welcome Title ("Welcome to Coatroom")
            ImGui::SetWindowFontScale(1.42f);
            const char* welcomeTxt = (state.homeNavTab == 1) ? "Automotive Templates" : "Welcome to Coatroom";
            float titleW = ImGui::CalcTextSize(welcomeTxt).x;
            ImGui::SetCursorPosX(colLeft + std::max(0.0f, (colW - titleW) * 0.5f));
            ImGui::TextColored(ImVec4(0.93f, 0.93f, 0.94f, 1.0f), "%s", welcomeTxt);
            ImGui::SetWindowFontScale(1.0f);
            ImGui::Dummy(ImVec2(0.0f, 12.0f));

            // Compact Generic Start / Open Card ("Start or Open")
            float heroLocalY = ImGui::GetCursorPosY();
            ImVec2 heroMin(ImGui::GetWindowPos().x + colLeft, ImGui::GetCursorScreenPos().y);
            const float heroH = 148.0f;
            ImVec2 heroMax(heroMin.x + colW, heroMin.y + heroH);
            mdl->AddRectFilled(heroMin, heroMax, cardBg, 8.0f);
            mdl->AddRect(heroMin, heroMax, cardBorder, 8.0f, 1.0f);

            const char* subHead = "Start or Open";
            float subW = ImGui::CalcTextSize(subHead).x;
            mdl->AddText(ImVec2(heroMin.x + (colW - subW) * 0.5f, heroMin.y + 12.0f),
                         StudioColors::TextPrimary, subHead);

            struct PresetSpec { const char* title; const char* spec; int id; };
            static constexpr PresetSpec kPresets[4] = {
                {"Open Project (.cr)", "Resume saved work",   0},
                {"Open Car (.obj)",    "Load any 3D mesh",    1},
                {"Open Livery (.png)", "Load livery texture", 2},
                {"New 4K Blank",       "4096 x 4096 px",      3},
            };

            float tileW = std::min(170.0f, (colW - 40.0f) / 4.0f);
            float tileGap = (colW - tileW * 4.0f) / 5.0f;
            float tileTop = heroMin.y + 36.0f;
            float tileH = 98.0f;

            for (int k = 0; k < 4; ++k) {
                ImVec2 tMin(heroMin.x + tileGap + k * (tileW + tileGap), tileTop);
                ImVec2 tMax(tMin.x + tileW, tMin.y + tileH);
                ImGui::SetCursorScreenPos(tMin);
                ImGui::PushID(900 + k);
                bool tClick = ImGui::InvisibleButton("##preset_tile", ImVec2(tileW, tileH));
                bool tHov = ImGui::IsItemHovered();
                ImGui::PopID();

                if (tHov) {
                    mdl->AddRectFilled(tMin, tMax, IM_COL32(0x39, 0x39, 0x39, 255), 6.0f);
                    mdl->AddRect(tMin, tMax, StudioColors::BorderLight, 6.0f, 1.0f);
                }

                // Centered crisp generic wireframe icon
                ImVec2 ic(tMin.x + tileW * 0.5f, tMin.y + 28.0f);
                ImU32 lineCol = tHov ? IM_COL32(240, 240, 242, 255) : IM_COL32(204, 206, 212, 255);
                if (k == 0) {
                    // Stacked layers / .cr project file icon
                    mdl->AddRect(ImVec2(ic.x - 14.0f, ic.y - 16.0f), ImVec2(ic.x + 14.0f, ic.y + 16.0f), lineCol, 2.0f, 1.3f);
                    mdl->AddLine(ImVec2(ic.x + 6.0f, ic.y - 16.0f), ImVec2(ic.x + 14.0f, ic.y - 8.0f), lineCol, 1.2f);
                    mdl->AddText(ImVec2(ic.x - 9.0f, ic.y - 3.0f), StudioColors::TextSecondary, ".cr");
                } else if (k == 1) {
                    // Generic 3D mesh front-3/4 car silhouette
                    const ImVec2 meshCar[7] = {
                        {ic.x - 22.0f, ic.y + 7.0f}, {ic.x - 19.0f, ic.y - 1.0f},
                        {ic.x - 8.0f,  ic.y - 9.0f}, {ic.x + 10.0f, ic.y - 9.0f},
                        {ic.x + 21.0f, ic.y + 0.0f}, {ic.x + 22.0f, ic.y + 7.0f},
                        {ic.x - 22.0f, ic.y + 7.0f},
                    };
                    mdl->AddPolyline(meshCar, 7, lineCol, 1.35f, ImDrawFlags_None);
                    mdl->AddCircleFilled(ImVec2(ic.x - 12.0f, ic.y + 8.0f), 4.0f, cardBg, 12);
                    mdl->AddCircle(ImVec2(ic.x - 12.0f, ic.y + 8.0f), 4.0f, lineCol, 12, 1.25f);
                    mdl->AddCircleFilled(ImVec2(ic.x + 12.0f, ic.y + 8.0f), 4.0f, cardBg, 12);
                    mdl->AddCircle(ImVec2(ic.x + 12.0f, ic.y + 8.0f), 4.0f, lineCol, 12, 1.25f);
                } else if (k == 2) {
                    // Livery PNG frame icon
                    mdl->AddRect(ImVec2(ic.x - 18.0f, ic.y - 13.0f), ImVec2(ic.x + 18.0f, ic.y + 13.0f), lineCol, 2.0f, 1.3f);
                    const ImVec2 mt[4] = {
                        {ic.x - 14.0f, ic.y + 9.0f}, {ic.x - 4.0f, ic.y - 1.0f},
                        {ic.x + 4.0f,  ic.y + 5.0f}, {ic.x + 14.0f, ic.y - 4.0f},
                    };
                    mdl->AddPolyline(mt, 4, StudioColors::TextSecondary, 1.2f, ImDrawFlags_None);
                } else {
                    // New 4K blank canvas outline
                    mdl->AddRect(ImVec2(ic.x - 15.0f, ic.y - 15.0f), ImVec2(ic.x + 15.0f, ic.y + 15.0f), lineCol, 2.0f, 1.3f);
                    mdl->AddText(ImVec2(ic.x - 7.0f, ic.y - 6.0f), StudioColors::TextSecondary, "4K");
                }

                float tw = ImGui::CalcTextSize(kPresets[k].title).x;
                mdl->AddText(ImVec2(tMin.x + (tileW - tw) * 0.5f, tMin.y + 54.0f),
                             StudioColors::TextPrimary, kPresets[k].title);
                float sw = ImGui::CalcTextSize(kPresets[k].spec).x;
                mdl->AddText(ImVec2(tMin.x + (tileW - sw) * 0.5f, tMin.y + 72.0f),
                             StudioColors::TextMuted, kPresets[k].spec);

                if (tClick) {
                    invokePreset(kPresets[k].id);
                }
            }

            // Recent / Templates Body below Hero Card
            float recentTop = heroLocalY + heroH + 22.0f;
            ImGui::SetCursorPos(ImVec2(colLeft, recentTop));

            if (state.homeNavTab == 1) {
                ImGui::TextColored(StudioColors::ToVec4(StudioColors::TextPrimary), "Template Quick Start");
                ImGui::SetCursorPosX(colLeft);
                ImGui::TextColored(StudioColors::ToVec4(StudioColors::TextSecondary),
                                   "Select any template above to initialize a 4K/2K UV artboard and 3D showroom.\n"
                                   "All exported PNG textures and native .cr projects are saved cleanly with AO panel shading.");
            } else {
                ImGui::TextColored(StudioColors::ToVec4(StudioColors::TextPrimary), "Recent");
                ImGui::SameLine(colLeft + colW - 56.0f);
                if (flatTogglePill("##GridBtn", state.homeGridView, false, ImVec2(24.0f, 22.0f))) {
                    state.homeGridView = true;
                }
                drawSmallIcon(mdl, SmallIconKind::ViewGrid,
                              ImGui::GetItemRectMin() + ImVec2(4.0f, 3.0f),
                              ImGui::GetItemRectMax() - ImVec2(4.0f, 3.0f),
                              state.homeGridView ? StudioColors::TextPrimary : StudioColors::TextSecondary);
                itemTooltip("Thumbnail Grid View");
                ImGui::SameLine(0.0f, 4.0f);
                if (flatTogglePill("##ListBtn", !state.homeGridView, false, ImVec2(24.0f, 22.0f))) {
                    state.homeGridView = false;
                }
                drawSmallIcon(mdl, SmallIconKind::ViewList,
                              ImGui::GetItemRectMin() + ImVec2(4.0f, 3.0f),
                              ImGui::GetItemRectMax() - ImVec2(4.0f, 3.0f),
                              !state.homeGridView ? StudioColors::TextPrimary : StudioColors::TextSecondary);
                itemTooltip("Compact List View");

                // Sort & Filter row
                ImGui::SetCursorPosX(colLeft);
                ImGui::TextColored(StudioColors::ToVec4(StudioColors::TextMuted), "Sort");
                ImGui::SameLine(0.0f, 10.0f);
                ImGui::SetNextItemWidth(92.0f);
                const char* sortLbl = (state.homeSortMode == 1) ? "Name" : "Recent";
                if (ImGui::BeginCombo("##HomeSort", sortLbl)) {
                    if (ImGui::Selectable("Recent", state.homeSortMode == 0)) state.homeSortMode = 0;
                    if (ImGui::Selectable("Name",   state.homeSortMode == 1)) state.homeSortMode = 1;
                    ImGui::EndCombo();
                }

                ImGui::SameLine(colLeft + colW - 224.0f);
                ImGui::TextColored(StudioColors::ToVec4(StudioColors::TextMuted), "Filter");
                ImGui::SameLine(0.0f, 8.0f);
                ImGui::SetNextItemWidth(178.0f);
                ImGui::InputTextWithHint("##HomeFilter", "Filter Recent Files", state.homeFilterBuf,
                                         sizeof(state.homeFilterBuf));

                ImGui::Dummy(ImVec2(0.0f, 6.0f));
                ImGui::SetCursorPosX(colLeft);

                std::vector<int> indices;
                indices.reserve(state.recentProjects.size());
                for (int ri = 0; ri < static_cast<int>(state.recentProjects.size()); ++ri) {
                    const RecentProjectEntry& e = state.recentProjects[ri];
                    if (state.homeFilterBuf[0] != '\0' &&
                        !containsCaseInsensitive(e.name, state.homeFilterBuf) &&
                        !containsCaseInsensitive(e.path, state.homeFilterBuf)) {
                        continue;
                    }
                    indices.push_back(ri);
                }
                if (state.homeSortMode == 1) {
                    std::sort(indices.begin(), indices.end(), [&](int a, int b) {
                        return state.recentProjects[a].name < state.recentProjects[b].name;
                    });
                }

                float listH = std::max(80.0f, ImGui::GetContentRegionAvail().y - 14.0f);
                if (ImGui::BeginChild("##RecentScroll", ImVec2(colW, listH), ImGuiChildFlags_None)) {
                    ImDrawList* rdl = ImGui::GetWindowDrawList();
                    if (indices.empty()) {
                        ImVec2 eMin = ImGui::GetCursorScreenPos();
                        ImVec2 eMax(eMin.x + colW, eMin.y + 84.0f);
                        rdl->AddRectFilled(eMin, eMax, IM_COL32(0x2A, 0x2A, 0x2A, 255), 6.0f);
                        rdl->AddRect(eMin, eMax, cardBorder, 6.0f, 1.0f);
                        const char* emptyMsg = state.recentProjects.empty()
                                               ? "No recent projects yet. Open a .cr or .obj file or pick a template above."
                                               : "No recent projects match your filter.";
                        float ew = ImGui::CalcTextSize(emptyMsg).x;
                        rdl->AddText(ImVec2(eMin.x + std::max(12.0f, (colW - ew) * 0.5f), eMin.y + 34.0f),
                                     StudioColors::TextMuted, emptyMsg);
                        ImGui::Dummy(ImVec2(colW, 84.0f));
                    } else if (state.homeGridView) {
                        const int cols = std::max(2, static_cast<int>((colW + 12.0f) / 208.0f));
                        const float gap = 12.0f;
                        const float cW = std::min(220.0f, (colW - gap * (cols - 1)) / cols);
                        const float cH = 152.0f;
                        for (size_t k = 0; k < indices.size(); ++k) {
                            const RecentProjectEntry& e = state.recentProjects[indices[k]];
                            int col = static_cast<int>(k) % cols;
                            if (col > 0) ImGui::SameLine(0.0f, gap);

                            ImVec2 cMin = ImGui::GetCursorScreenPos();
                            ImVec2 cMax(cMin.x + cW, cMin.y + cH);
                            ImGui::PushID(static_cast<int>(k));
                            bool clk = ImGui::InvisibleButton("##rec_card", ImVec2(cW, cH));
                            bool hov = ImGui::IsItemHovered();
                            ImGui::PopID();

                            rdl->AddRectFilled(cMin, cMax, cardBg, 6.0f);
                            rdl->AddRect(cMin, cMax, hov ? StudioColors::BorderLight : cardBorder, 6.0f, 1.0f);

                            ImVec2 thMin(cMin.x + 4.0f, cMin.y + 4.0f);
                            ImVec2 thMax(cMax.x - 4.0f, cMin.y + 108.0f);
                            rdl->AddRectFilled(thMin, thMax, IM_COL32(0x1B, 0x1B, 0x1B, 255), 4.0f);
                            if (e.thumbTex != 0) {
                                rdl->AddImageRounded(e.thumbTex, thMin, thMax, ImVec2(0, 0), ImVec2(1, 1),
                                                     IM_COL32_WHITE, 4.0f);
                            } else {
                                // Front 3/4th perspective wireframe car preview when thumbnail is absent
                                ImVec2 tc((thMin.x + thMax.x) * 0.5f, (thMin.y + thMax.y) * 0.5f + 4.0f);
                                ImU32 wf = IM_COL32(105, 112, 126, 195);
                                const ImVec2 wfBody[8] = {
                                    {tc.x - 36.0f, tc.y + 10.0f}, {tc.x - 34.0f, tc.y - 2.0f},
                                    {tc.x - 20.0f, tc.y - 6.0f},  {tc.x - 6.0f,  tc.y - 16.0f},
                                    {tc.x + 18.0f, tc.y - 16.0f}, {tc.x + 33.0f, tc.y - 4.0f},
                                    {tc.x + 35.0f, tc.y + 8.0f},  {tc.x - 36.0f, tc.y + 10.0f},
                                };
                                rdl->AddPolyline(wfBody, 8, wf, 1.25f, ImDrawFlags_None);
                                rdl->AddLine(ImVec2(tc.x - 20.0f, tc.y - 6.0f), ImVec2(tc.x + 4.0f, tc.y - 5.0f), wf, 1.0f);
                                rdl->AddLine(ImVec2(tc.x - 6.0f, tc.y - 16.0f), ImVec2(tc.x + 4.0f, tc.y - 5.0f), wf, 1.0f);
                                rdl->AddCircleFilled(ImVec2(tc.x - 22.0f, tc.y + 11.0f), 5.5f, IM_COL32(0x1B, 0x1B, 0x1B, 255), 14);
                                rdl->AddCircle(ImVec2(tc.x - 22.0f, tc.y + 11.0f), 5.5f, wf, 14, 1.25f);
                                rdl->AddCircleFilled(ImVec2(tc.x + 18.0f, tc.y + 9.0f), 5.0f, IM_COL32(0x1B, 0x1B, 0x1B, 255), 14);
                                rdl->AddCircle(ImVec2(tc.x + 18.0f, tc.y + 9.0f), 5.0f, wf, 14, 1.25f);
                            }
                            if (!e.kind.empty()) {
                                ImVec2 bMin(thMax.x - 32.0f, thMin.y + 4.0f);
                                ImVec2 bMax(thMax.x - 4.0f, thMin.y + 20.0f);
                                rdl->AddRectFilled(bMin, bMax, IM_COL32(0x22, 0x24, 0x29, 235), 3.0f);
                                rdl->AddText(ImVec2(bMin.x + 5.0f, bMin.y + 1.0f), StudioColors::StudioBlue, e.kind.c_str());
                            }

                            rdl->PushClipRect(ImVec2(cMin.x + 8.0f, cMin.y + 111.0f), ImVec2(cMax.x - 8.0f, cMax.y - 4.0f), true);
                            rdl->AddText(ImVec2(cMin.x + 8.0f, cMin.y + 112.0f), StudioColors::TextPrimary, e.name.c_str());
                            const char* sub = e.detail.empty() ? e.path.c_str() : e.detail.c_str();
                            rdl->AddText(ImVec2(cMin.x + 8.0f, cMin.y + 130.0f), StudioColors::TextMuted, sub);
                            rdl->PopClipRect();

                            if (hov && !e.path.empty()) ImGui::SetTooltip("%s", e.path.c_str());
                            if (clk && callbacks.onOpenRecent) {
                                state.showHomeScreen = false;
                                callbacks.onOpenRecent(e.path);
                            }
                        }
                    } else {
                        for (size_t k = 0; k < indices.size(); ++k) {
                            const RecentProjectEntry& e = state.recentProjects[indices[k]];
                            ImVec2 rMin = ImGui::GetCursorScreenPos();
                            ImVec2 rMax(rMin.x + colW, rMin.y + 30.0f);
                            ImGui::PushID(static_cast<int>(k) + 2000);
                            bool clk = ImGui::InvisibleButton("##rec_row", ImVec2(colW, 30.0f));
                            bool hov = ImGui::IsItemHovered();
                            ImGui::PopID();
                            rdl->AddRectFilled(rMin, rMax, hov ? IM_COL32(0x36, 0x36, 0x36, 255) : cardBg, 4.0f);
                            rdl->AddLine(ImVec2(rMin.x, rMax.y), rMax, StudioColors::Border, 1.0f);
                            rdl->AddText(ImVec2(rMin.x + 10.0f, rMin.y + 7.0f), StudioColors::StudioBlue,
                                         e.kind.empty() ? ".cr" : e.kind.c_str());
                            rdl->AddText(ImVec2(rMin.x + 54.0f, rMin.y + 7.0f), StudioColors::TextPrimary, e.name.c_str());
                            const char* detailStr = e.detail.empty() ? e.path.c_str() : e.detail.c_str();
                            rdl->AddText(ImVec2(rMin.x + colW * 0.48f, rMin.y + 7.0f), StudioColors::TextMuted, detailStr);
                            if (hov && !e.path.empty()) ImGui::SetTooltip("%s", e.path.c_str());
                            if (clk && callbacks.onOpenRecent) {
                                state.showHomeScreen = false;
                                callbacks.onOpenRecent(e.path);
                            }
                        }
                    }
                }
                ImGui::EndChild();
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

void drawStudioWorkspace(StudioUiState& state, UvViewportState& uvState,
                         CanvasEngine& canvas, ObjMesh& mesh,
                         const StudioCallbacks& callbacks) {
    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput && io.KeyCtrl && !io.KeyShift && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_R)) {
        state.showRulers = !state.showRulers;
    }

    ImGuiViewport* vp = ImGui::GetMainViewport();
    const float topBarH = state.showHomeScreen ? 32.0f : 58.0f;
    const float statusH = 22.0f;
    const float leftRailW = 46.0f;
    const float rightRailW = 0.0f;

    int prevHoveredRichId = state.hoveredRichTooltipId;
    state.hoveredRichTooltipId = -1;

    drawTopWorkspaceBar(state, uvState, canvas, mesh, callbacks,
                        vp->WorkPos.x, vp->WorkPos.y, vp->WorkSize.x, topBarH);

    float midY = vp->WorkPos.y + topBarH;
    float midH = std::max(64.0f, vp->WorkSize.y - topBarH - statusH);

    if (state.showHomeScreen) {
        drawHomeScreen(state, canvas, callbacks, vp->WorkPos.x, midY, vp->WorkSize.x, midH);
        drawStatusBar(state, canvas, mesh, vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - statusH,
                      vp->WorkSize.x, statusH);
        return;
    }

    drawToolRail(state, callbacks, vp->WorkPos.x, midY, leftRailW, midH, &canvas, &mesh);

    if (state.hoveredRichTooltipId >= 0) {
        if (state.hoveredRichTooltipId == prevHoveredRichId) {
            state.richTooltipTimer += io.DeltaTime;
        } else {
            state.richTooltipTimer = 0.0f;
        }
    } else {
        state.richTooltipTimer = 0.0f;
    }

    ImVec2 dockPos(vp->WorkPos.x + leftRailW, midY);
    ImVec2 dockSize(std::max(128.0f, vp->WorkSize.x - leftRailW - rightRailW), midH);

    ImGui::SetNextWindowPos(dockPos);
    ImGui::SetNextWindowSize(dockSize);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, StudioColors::ToVec4(StudioColors::CanvasTrough));

    ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                                 ImGuiWindowFlags_NoDocking;
    ImGui::Begin("##StudioDockHost", nullptr, hostFlags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    ImGuiID dockId = ImGui::GetID("CoatroomDockSpace");
    if (!state.dockLayoutInitialized || state.dockLayoutResetRequested) {
        state.dockLayoutInitialized = true;
        state.dockLayoutResetRequested = false;

        ImGui::DockBuilderRemoveNode(dockId);
        ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodePos(dockId, dockPos);
        ImGui::DockBuilderSetNodeSize(dockId, dockSize);

        float rightRatio = std::clamp(264.0f / std::max(dockSize.x, 640.0f), 0.19f, 0.25f);
        ImGuiID centerId = 0, rightId = 0;
        ImGui::DockBuilderSplitNode(dockId, ImGuiDir_Right, rightRatio, &rightId, &centerId);

        ImGuiID propsId = 0, layersId = 0;
        ImGui::DockBuilderSplitNode(rightId, ImGuiDir_Down, 0.43f, &layersId, &propsId);

        float uvSplitRatio = 0.38f;
        if (state.activeWorkspace == WorkspaceTab::UvEditing) uvSplitRatio = 0.72f;
        else if (state.activeWorkspace == WorkspaceTab::TexturePaint) uvSplitRatio = 0.24f;
        else if (state.activeWorkspace == WorkspaceTab::Rendering) uvSplitRatio = 0.01f;

        ImGuiID vp3dId = 0, uv2dId = 0;
        ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Right, uvSplitRatio, &uv2dId, &vp3dId);

        ImGui::DockBuilderDockWindow("3D Viewport", vp3dId);
        ImGui::DockBuilderDockWindow("2D UV Canvas", uv2dId);
        ImGui::DockBuilderDockWindow("Properties", propsId);
        ImGui::DockBuilderDockWindow("Layers", layersId);
        ImGui::DockBuilderFinish(dockId);
    }

    ImGui::DockSpace(dockId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
    ImGui::End();

    drawViewport3DPanel(state, mesh, callbacks);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::Begin("2D UV Canvas", nullptr,
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        drawUvViewport(state, uvState, canvas, mesh, callbacks);
    }
    ImGui::End();
    ImGui::PopStyleVar();

    drawInspectorPanel(state, canvas, mesh, callbacks);
    drawLayersPanel(state, canvas, mesh, callbacks);

    drawStatusBar(state, canvas, mesh, vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - statusH,
                  vp->WorkSize.x, statusH);

    // Rich Illustrator-style animated tooltip overlay (after 0.40s dwell or forced screenshot preview)
    int showRichId = -1;
    ImVec2 anchorPos = state.richTooltipAnchor;
    if (state.forcePreviewRichTooltip >= 0) {
        showRichId = state.forcePreviewRichTooltip;
        anchorPos = ImVec2(vp->WorkPos.x + leftRailW + 10.0f, vp->WorkPos.y + topBarH + 84.0f);
    } else if (state.showRichTooltips && state.hoveredRichTooltipId >= 0 && state.richTooltipTimer >= 0.40f &&
               !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
        showRichId = state.hoveredRichTooltipId;
    }
    if (showRichId >= 0) {
        float maxX = std::max(vp->WorkPos.x + 8.0f, vp->WorkPos.x + vp->WorkSize.x - 270.0f);
        float maxY = std::max(vp->WorkPos.y + 8.0f, vp->WorkPos.y + vp->WorkSize.y - 216.0f);
        anchorPos.x = std::clamp(anchorPos.x, vp->WorkPos.x + 6.0f, maxX);
        anchorPos.y = std::clamp(anchorPos.y, vp->WorkPos.y + 6.0f, maxY);
        drawRichTooltipCard(ImGui::GetForegroundDrawList(), anchorPos, showRichId, ImGui::GetTime());
    }
}

} // namespace lps
