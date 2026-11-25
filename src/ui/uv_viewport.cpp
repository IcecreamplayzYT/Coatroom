#define IMGUI_DEFINE_MATH_OPERATORS
#include "ui/uv_viewport.hpp"
#include "ui/studio_theme.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace lps {

namespace {

inline Vec2 toVec2(const ImVec2& v) { return Vec2{v.x, v.y}; }

inline ImU32 toImU32(Color4u8 c) {
    return IM_COL32(c.r, c.g, c.b, c.a);
}

struct Obb2D {
    ImVec2 center;
    ImVec2 axisR;
    ImVec2 axisD;
    float halfW = 20.0f;
    float halfH = 12.0f;
    ImVec2 corners[4];
    ImVec2 rotHandle;
};

Obb2D buildScreenObb(Vec2 centerUv, Vec2 halfUv, float rotDeg,
                     const ImVec2& vpCenter, Vec2 viewCenterUv, float pixelsPerUv) {
    auto uv2scr = [&](Vec2 uv) {
        return ImVec2(vpCenter.x + (uv.x - viewCenterUv.x) * pixelsPerUv,
                      vpCenter.y + (uv.y - viewCenterUv.y) * pixelsPerUv);
    };
    float rad = rotDeg * kDeg2Rad;
    float cs = std::cos(rad), sn = std::sin(rad);
    Obb2D obb;
    obb.center = uv2scr(centerUv);
    obb.axisR = ImVec2(cs, sn);
    obb.axisD = ImVec2(-sn, cs);
    obb.halfW = std::max(10.0f, halfUv.x * pixelsPerUv);
    obb.halfH = std::max(8.0f, halfUv.y * pixelsPerUv);
    obb.corners[0] = obb.center - obb.axisR * obb.halfW - obb.axisD * obb.halfH;
    obb.corners[1] = obb.center + obb.axisR * obb.halfW - obb.axisD * obb.halfH;
    obb.corners[2] = obb.center + obb.axisR * obb.halfW + obb.axisD * obb.halfH;
    obb.corners[3] = obb.center - obb.axisR * obb.halfW + obb.axisD * obb.halfH;
    obb.rotHandle = obb.center - obb.axisD * (obb.halfH + 22.0f);
    return obb;
}

bool pointInObb(const ImVec2& p, const Obb2D& obb) {
    ImVec2 d = p - obb.center;
    float rx = d.x * obb.axisR.x + d.y * obb.axisR.y;
    float ry = d.x * obb.axisD.x + d.y * obb.axisD.y;
    return std::fabs(rx) <= obb.halfW && std::fabs(ry) <= obb.halfH;
}

void drawHudBadge(ImDrawList* dl, const ImVec2& anchor, const char* text, ImU32 borderCol = StudioColors::StudioBlue) {
    ImVec2 ts = ImGui::CalcTextSize(text);
    ImVec2 p0(std::round(anchor.x + 14.0f), std::round(anchor.y - ts.y - 14.0f));
    ImVec2 p1(p0.x + ts.x + 12.0f, p0.y + ts.y + 8.0f);
    dl->AddRectFilled(p0, p1, IM_COL32(30, 30, 30, 235), 2.0f);
    dl->AddRect(p0, p1, borderCol, 2.0f, 1.0f);
    dl->AddText(ImVec2(p0.x + 6.0f, p0.y + 4.0f), StudioColors::TextPrimary, text);
}

void syncRulerUse3DFlag(RulerStripeProps& ruler) {
    if (ruler.nodes.empty()) return;
    for (const StripeNode& n : ruler.nodes) {
        if (!n.has3D) {
            ruler.use3DSpace = false;
            return;
        }
    }
}

struct GuideSnapResult {
    Vec2 uv;
    int snappedXIdx = -1;
    int snappedYIdx = -1;
    bool snappedCenterX = false;
    bool snappedCenterY = false;
};

GuideSnapResult snapUvToGuides(Vec2 rawUv, const UvViewportState& uvState, bool enabled) {
    GuideSnapResult res{rawUv};
    if (!enabled) return res;
    const float tolUv = 8.0f / std::max(64.0f, uvState.pixelsPerUv);

    float bestDx = tolUv;
    if (std::fabs(rawUv.x - 0.5f) < bestDx) {
        bestDx = std::fabs(rawUv.x - 0.5f);
        res.uv.x = 0.5f;
        res.snappedCenterX = true;
    }
    for (int i = 0; i < static_cast<int>(uvState.guidesX.size()); ++i) {
        float d = std::fabs(rawUv.x - uvState.guidesX[i]);
        if (d < bestDx) {
            bestDx = d;
            res.uv.x = uvState.guidesX[i];
            res.snappedXIdx = i;
            res.snappedCenterX = false;
        }
    }

    float bestDy = tolUv;
    if (std::fabs(rawUv.y - 0.5f) < bestDy) {
        bestDy = std::fabs(rawUv.y - 0.5f);
        res.uv.y = 0.5f;
        res.snappedCenterY = true;
    }
    for (int i = 0; i < static_cast<int>(uvState.guidesY.size()); ++i) {
        float d = std::fabs(rawUv.y - uvState.guidesY[i]);
        if (d < bestDy) {
            bestDy = d;
            res.uv.y = uvState.guidesY[i];
            res.snappedYIdx = i;
            res.snappedCenterY = false;
        }
    }
    return res;
}

inline Vec2 evalCubic2(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, float t) {
    float u = 1.0f - t;
    float uu = u * u, tt = t * t;
    return p0 * (uu * u) + p1 * (3.0f * uu * t) + p2 * (3.0f * u * tt) + p3 * (tt * t);
}

inline Vec2 lerp2(Vec2 a, Vec2 b, float t) { return a + (b - a) * t; }

void populateNode3DAndHandles(StripeNode& node, const ObjMesh& mesh) {
    if (!mesh.loaded) {
        node.has3D = false;
        return;
    }
    Vec3 p3, n3;
    int32_t tri = -1;
    if (!mesh.queryUvAtPixel(node.uv.x, node.uv.y, p3, n3, tri)) {
        node.has3D = false;
        return;
    }
    node.pos3D = p3;
    node.nrm3D = n3;
    node.has3D = true;

    auto mapHandleUvTo3D = [&](Vec2 hUv) -> Vec3 {
        Vec3 hp, hn;
        int32_t hTri = -1;
        if (mesh.queryUvAtPixel(hUv.x, hUv.y, hp, hn, hTri) && length(hp - node.pos3D) < 1.25f) {
            return hp;
        }
        // Fallback: project UV tangent delta cleanly on triangle tangent frame
        if (tri >= 0 && tri < static_cast<int32_t>(mesh.tris.size())) {
            const Triangle& t = mesh.tris[tri];
            Vec2 dU1 = t.uv1 - t.uv0;
            Vec2 dU2 = t.uv2 - t.uv0;
            float det = dU1.x * dU2.y - dU2.x * dU1.y;
            if (std::fabs(det) > 1e-8f) {
                Vec3 dP1 = mesh.uniquePositions[t.p1] - mesh.uniquePositions[t.p0];
                Vec3 dP2 = mesh.uniquePositions[t.p2] - mesh.uniquePositions[t.p0];
                Vec3 dPdu = (dP1 * dU2.y - dP2 * dU1.y) / det;
                Vec3 dPdv = (dP2 * dU1.x - dP1 * dU2.x) / det;
                Vec2 duv = hUv - node.uv;
                Vec3 est = node.pos3D + dPdu * duv.x + dPdv * duv.y;
                // Keep reasonable tangent length
                if (length(est - node.pos3D) < 1.5f) return est;
            }
        }
        return node.pos3D;
    };

    if (node.hasHandles) {
        node.handleIn3D = mapHandleUvTo3D(node.handleInUv);
        node.handleOut3D = mapHandleUvTo3D(node.handleOutUv);
    } else {
        node.handleIn3D = node.pos3D;
        node.handleOut3D = node.pos3D;
    }
}

Vec2 snapStepAngleUv(Vec2 anchorUv, Vec2 rawUv, bool shiftHeld, float stepDeg,
                     int canvasW, int canvasH, float& outDeg, float& outLenPx) {
    float dx = (rawUv.x - anchorUv.x) * static_cast<float>(canvasW);
    float dy = (rawUv.y - anchorUv.y) * static_cast<float>(canvasH);
    outLenPx = std::hypot(dx, dy);
    if (outLenPx < 1e-4f) {
        outDeg = 0.0f;
        return rawUv;
    }
    float rad = std::atan2(-dy, dx);
    if (shiftHeld) {
        float snapStep = stepDeg * kDeg2Rad;
        rad = std::round(rad / snapStep) * snapStep;
    }
    outDeg = rad * kRad2Deg;
    if (outDeg < 0.0f) outDeg += 360.0f;
    if (!shiftHeld) return rawUv;
    return Vec2{
        anchorUv.x + (std::cos(rad) * outLenPx) / static_cast<float>(canvasW),
        anchorUv.y - (std::sin(rad) * outLenPx) / static_cast<float>(canvasH)
    };
}

Vec2 computeTextHalfUv(const TextLayerProps& tx, int canvasW, int canvasH) {
    float uvScale = static_cast<float>(canvasW) / 2048.0f;
    if (tx.isAreaBox) {
        float estW = (std::max(40.0f, tx.boxWidthPx) + tx.outlineWidth * 2.0f + 24.0f) * uvScale;
        float estH = (std::max(24.0f, tx.boxHeightPx) + tx.outlineWidth * 2.0f + 24.0f) * uvScale;
        return Vec2{(estW * 0.5f) / canvasW, (estH * 0.5f) / canvasH};
    }
    float nChars = std::max(1.0f, static_cast<float>(tx.text.size()));
    float estW = (nChars * (tx.fontSizePx * 0.58f + tx.tracking) + tx.outlineWidth * 2.0f + 16.0f) * uvScale;
    float estH = (tx.fontSizePx * 1.18f + tx.outlineWidth * 2.0f + 16.0f) * uvScale;
    return Vec2{(estW * 0.5f) / canvasW, (estH * 0.5f) / canvasH};
}

void computeItemCenterAndHalf(const VectorItem& it, int canvasW, int canvasH, Vec2& outCenter, Vec2& outHalf) {
    bool anc3D = (it.kind == VectorItemKind::Text)      ? it.text.anchoredIn3D :
                 (it.kind == VectorItemKind::Image)     ? it.image.anchoredIn3D :
                 (it.kind == VectorItemKind::RectShape) ? it.rect.anchoredIn3D : false;
    if (anc3D && it.hasPrimaryUvBox) {
        outCenter = it.primaryUvCenter;
        outHalf = it.primaryUvHalf;
        return;
    }
    if (it.kind == VectorItemKind::Text) {
        outCenter = it.text.uvPos;
        outHalf = computeTextHalfUv(it.text, canvasW, canvasH);
    } else if (it.kind == VectorItemKind::Image) {
        outCenter = it.image.uvPos;
        outHalf = it.image.uvScale * 0.5f;
    } else if (it.kind == VectorItemKind::RectShape) {
        outCenter = it.rect.uvPos;
        outHalf = it.rect.uvScale * 0.5f;
    } else {
        outCenter = it.primaryUvCenter;
        outHalf = it.primaryUvHalf;
    }
}

void computeLegacyLayerCenterAndHalf(const Layer& l, int canvasW, int canvasH, Vec2& outCenter, Vec2& outHalf) {
    bool isText = (l.kind == LayerKind::Text);
    bool anc3D = isText ? l.text.anchoredIn3D : l.decal.anchoredIn3D;
    if (anc3D && l.hasPrimaryUvBox) {
        outCenter = l.primaryUvCenter;
        outHalf = l.primaryUvHalf;
    } else {
        outCenter = isText ? l.text.uvPos : l.decal.uvPos;
        outHalf = isText ? computeTextHalfUv(l.text, canvasW, canvasH) : (l.decal.uvScale * 0.5f);
    }
}

bool penPathNearScreenPoint(const RulerStripeProps& pen, const ImVec2& mousePos,
                            const ImVec2& vpCenter, Vec2 viewCenterUv, float pixelsPerUv) {
    int nN = static_cast<int>(pen.nodes.size());
    if (nN == 0) return false;
    auto uv2scr = [&](Vec2 uv) {
        return ImVec2(vpCenter.x + (uv.x - viewCenterUv.x) * pixelsPerUv,
                      vpCenter.y + (uv.y - viewCenterUv.y) * pixelsPerUv);
    };
    float hitDist = std::max(8.0f, pen.widthPx * 0.5f * (pixelsPerUv / 2048.0f) + 5.0f);
    int segCount = (pen.closed && nN >= 3) ? nN : std::max(0, nN - 1);
    for (int si = 0; si < segCount; ++si) {
        const StripeNode& nA = pen.nodes[si];
        const StripeNode& nB = pen.nodes[(si + 1) % nN];
        Vec2 p0 = nA.uv, p1 = nA.hasHandles ? nA.handleOutUv : nA.uv;
        Vec2 p2 = nB.hasHandles ? nB.handleInUv : nB.uv, p3 = nB.uv;
        for (int k = 0; k <= 12; ++k) {
            Vec2 pt = evalCubic2(p0, p1, p2, p3, k / 12.0f);
            if (length(toVec2(mousePos - uv2scr(pt))) <= hitDist) return true;
        }
    }
    if (nN == 1 && length(toVec2(mousePos - uv2scr(pen.nodes[0].uv))) <= 10.0f) return true;
    return false;
}

bool findTopVectorTargetAtPoint(const CanvasEngine& canvas, const ImVec2& mousePos,
                                const ImVec2& vpCenter, Vec2 viewCenterUv, float pixelsPerUv,
                                int& outLayerIdx, int& outItemIdx) {
    for (int i = static_cast<int>(canvas.layers.size()) - 1; i >= 0; --i) {
        const Layer& l = canvas.layers[i];
        if (!l.visible || l.locked) continue;
        for (int ii = static_cast<int>(l.items.size()) - 1; ii >= 0; --ii) {
            const VectorItem& it = l.items[ii];
            if (!it.visible || it.locked) continue;
            if (it.kind == VectorItemKind::PenPath) {
                if (penPathNearScreenPoint(it.pen, mousePos, vpCenter, viewCenterUv, pixelsPerUv)) {
                    outLayerIdx = i;
                    outItemIdx = ii;
                    return true;
                }
                continue;
            }
            Vec2 centerUv{}, halfUv{};
            computeItemCenterAndHalf(it, canvas.width, canvas.height, centerUv, halfUv);
            float rotDeg = (it.kind == VectorItemKind::Text)      ? it.text.rotationDeg :
                           (it.kind == VectorItemKind::Image)     ? it.image.rotationDeg :
                                                                    it.rect.rotationDeg;
            Obb2D obb = buildScreenObb(centerUv, halfUv, rotDeg, vpCenter, viewCenterUv, pixelsPerUv);
            if (pointInObb(mousePos, obb)) {
                outLayerIdx = i;
                outItemIdx = ii;
                return true;
            }
        }
        if (l.kind == LayerKind::Text || l.kind == LayerKind::Decal) {
            Vec2 centerUv{}, halfUv{};
            computeLegacyLayerCenterAndHalf(l, canvas.width, canvas.height, centerUv, halfUv);
            float rotDeg = (l.kind == LayerKind::Text) ? l.text.rotationDeg : l.decal.rotationDeg;
            Obb2D obb = buildScreenObb(centerUv, halfUv, rotDeg, vpCenter, viewCenterUv, pixelsPerUv);
            if (pointInObb(mousePos, obb)) {
                outLayerIdx = i;
                outItemIdx = -1;
                return true;
            }
        }
    }
    return false;
}

} // namespace

void uvFitToView(UvViewportState& uvState, float viewportW, float viewportH) {
    float minDim = std::max(64.0f, std::min(viewportW - 44.0f, viewportH - 44.0f));
    uvState.pixelsPerUv = minDim;
    uvState.viewCenterUv = Vec2{0.5f, 0.5f};
    uvState.needsInitialFit = false;
}

void drawUvViewport(StudioUiState& state, UvViewportState& uvState,
                    CanvasEngine& canvas, const ObjMesh& mesh,
                    const StudioCallbacks& callbacks) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 winPos = ImGui::GetCursorScreenPos();
    const ImVec2 winAvail = ImGui::GetContentRegionAvail();
    if (winAvail.x < 16.0f || winAvail.y < 32.0f) return;

    // 1. Clean Illustrator artboard layout (Pixel Rulers on Ctrl+R, no duplicate top button clutter)
    const float rulerTop = state.showRulers ? 20.0f : 0.0f;
    const float rulerLeft = state.showRulers ? 34.0f : 0.0f;
    const float troughW = std::max(32.0f, winAvail.x - rulerLeft);
    const float troughH = std::max(32.0f, winAvail.y - rulerTop);

    if (uvState.needsInitialFit) {
        uvFitToView(uvState, troughW, troughH);
    }

    // 2. Canvas desk area (Illustrator neutral #2C2C2C artboard surround)
    ImVec2 cMin(winPos.x + rulerLeft, winPos.y + rulerTop);
    ImVec2 cMax(winPos.x + winAvail.x, winPos.y + winAvail.y);
    ImVec2 cSize = cMax - cMin;
    ImVec2 vpCenter(cMin.x + cSize.x * 0.5f, cMin.y + cSize.y * 0.5f);

    // Bottom-left Illustrator-style zoom pill region (e.g. "40% v")
    int zoomPct = static_cast<int>(std::round((uvState.pixelsPerUv / std::max(1, canvas.width)) * 100.0f));
    char zoomPillLbl[24];
    std::snprintf(zoomPillLbl, sizeof(zoomPillLbl), "%d%%  v", zoomPct);
    ImVec2 zpPos(cMin.x + 8.0f, cMax.y - 25.0f);
    ImVec2 zpSize(std::max(58.0f, ImGui::CalcTextSize(zoomPillLbl).x + 14.0f), 18.0f);
    bool overZoomOverlay = ImGui::IsPopupOpen("##UvZoomPopup") ||
                           ImGui::IsMouseHoveringRect(zpPos, zpPos + zpSize, false);

    auto uvToScreen = [&](Vec2 uv) -> ImVec2 {
        return ImVec2(vpCenter.x + (uv.x - uvState.viewCenterUv.x) * uvState.pixelsPerUv,
                      vpCenter.y + (uv.y - uvState.viewCenterUv.y) * uvState.pixelsPerUv);
    };
    auto screenToUv = [&](const ImVec2& sp) -> Vec2 {
        return Vec2{
            uvState.viewCenterUv.x + (sp.x - vpCenter.x) / uvState.pixelsPerUv,
            uvState.viewCenterUv.y + (sp.y - vpCenter.y) / uvState.pixelsPerUv
        };
    };

    dl->AddRectFilled(cMin, cMax, StudioColors::UvDesk);

    // Interactive canvas hit area (allows bottom-left zoom pill to take priority cleanly)
    ImGui::SetCursorScreenPos(cMin);
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##uv_canvas_hit", cSize,
                           ImGuiButtonFlags_MouseButtonLeft |
                           ImGuiButtonFlags_MouseButtonRight |
                           ImGuiButtonFlags_MouseButtonMiddle);
    bool isHovered = ImGui::IsItemHovered() && !overZoomOverlay && (uvState.draggingGuideAxis == 0);
    bool isActive = ImGui::IsItemActive();
    ImGuiIO& io = ImGui::GetIO();

    // Pan & Zoom (Wheel, Middle-mouse, Alt+LMB, or HandPan tool)
    if (isHovered && std::fabs(io.MouseWheel) > 0.0f) {
        Vec2 uvBefore = screenToUv(io.MousePos);
        float factor = std::pow(1.14f, io.MouseWheel);
        uvState.pixelsPerUv = std::clamp(uvState.pixelsPerUv * factor, 64.0f, 32768.0f);
        Vec2 uvAfter = screenToUv(io.MousePos);
        uvState.viewCenterUv.x += (uvBefore.x - uvAfter.x);
        uvState.viewCenterUv.y += (uvBefore.y - uvAfter.y);
    }

    bool panDragging = (isActive || isHovered) &&
                       (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f) ||
                        (io.KeyAlt && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) ||
                        (state.activeTool == StudioTool::HandPan && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)));
    if (panDragging) {
        uvState.viewCenterUv.x -= io.MouseDelta.x / uvState.pixelsPerUv;
        uvState.viewCenterUv.y -= io.MouseDelta.y / uvState.pixelsPerUv;
    }

    // Zoom tool (Z)
    if (state.activeTool == StudioTool::Zoom && isHovered && !io.KeyAlt &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        Vec2 uvBefore = screenToUv(io.MousePos);
        float zFactor = io.KeyShift ? (1.0f / 1.35f) : 1.35f;
        uvState.pixelsPerUv = std::clamp(uvState.pixelsPerUv * zFactor, 64.0f, 32768.0f);
        Vec2 uvAfter = screenToUv(io.MousePos);
        uvState.viewCenterUv.x += (uvBefore.x - uvAfter.x);
        uvState.viewCenterUv.y += (uvBefore.y - uvAfter.y);
    }

    Vec2 hoverUv = screenToUv(io.MousePos);
    if (isHovered) {
        state.hoverUvValid = (hoverUv.x >= 0.0f && hoverUv.x <= 1.0f && hoverUv.y >= 0.0f && hoverUv.y <= 1.0f);
        state.lastHoverUv = hoverUv;
    }

    dl->PushClipRect(cMin, cMax, true);

    ImVec2 imgMin = uvToScreen({0.0f, 0.0f});
    ImVec2 imgMax = uvToScreen({1.0f, 1.0f});

    dl->AddRectFilled(imgMin, imgMax, IM_COL32(24, 24, 24, 255));
    if (state.canvas2DTex != 0) {
        dl->AddImage(state.canvas2DTex, imgMin, imgMax);
    }

    // 3. Optional pixel grid overlay
    if (state.uvShowGridRulers) {
        float pxScreen = uvState.pixelsPerUv / std::max(1, canvas.width);
        int stepPx = 256;
        if (pxScreen * 32.0f >= 28.0f) stepPx = 32;
        else if (pxScreen * 64.0f >= 28.0f) stepPx = 64;
        else if (pxScreen * 128.0f >= 28.0f) stepPx = 128;
        else if (pxScreen * 256.0f >= 28.0f) stepPx = 256;
        else stepPx = 512;

        int halfCanvas = canvas.width / 2;
        for (int p = stepPx; p < canvas.width; p += stepPx) {
            float u = static_cast<float>(p) / canvas.width;
            ImVec2 a = uvToScreen({u, 0.0f});
            ImVec2 b = uvToScreen({u, 1.0f});
            ImU32 gcol = (p == halfCanvas) ? IM_COL32(255, 255, 255, 34) : IM_COL32(255, 255, 255, 13);
            dl->AddLine(a, b, gcol, 1.0f);

            ImVec2 c = uvToScreen({0.0f, u});
            ImVec2 d = uvToScreen({1.0f, u});
            dl->AddLine(c, d, gcol, 1.0f);
        }
    }

    // 4. Optional UV Mesh overlay (clean smooth white filled islands + crisp boundary contour)
    if (state.uvShowWireframe && mesh.loaded && !mesh.tris.empty()) {
        struct UvBoundaryEdge {
            Vec2 a, b;
            int32_t groupIdx;
        };
        static const ObjMesh* s_cachedMesh = nullptr;
        static size_t s_cachedTriCount = 0;
        static std::string s_cachedPath;
        static std::vector<UvBoundaryEdge> s_boundaryEdges;

        if (s_cachedMesh != &mesh || s_cachedTriCount != mesh.tris.size() || s_cachedPath != mesh.sourcePath) {
            s_cachedMesh = &mesh;
            s_cachedTriCount = mesh.tris.size();
            s_cachedPath = mesh.sourcePath;
            s_boundaryEdges.clear();

            struct EdgeKeyRec {
                uint64_t k0, k1;
                Vec2 a, b;
                int32_t groupIdx;
            };
            std::vector<EdgeKeyRec> edges;
            edges.reserve(mesh.tris.size() * 3);

            auto quantKey = [](Vec2 uv) -> uint64_t {
                uint32_t qx = static_cast<uint32_t>(std::clamp(static_cast<int>(std::lround(uv.x * 16384.0f)), 0, 16384));
                uint32_t qy = static_cast<uint32_t>(std::clamp(static_cast<int>(std::lround(uv.y * 16384.0f)), 0, 16384));
                return (static_cast<uint64_t>(qx) << 32) | qy;
            };

            for (const Triangle& t : mesh.tris) {
                const Vec2 uvs[3] = {t.uv0, t.uv1, t.uv2};
                for (int e = 0; e < 3; ++e) {
                    Vec2 va = uvs[e];
                    Vec2 vb = uvs[(e + 1) % 3];
                    uint64_t ka = quantKey(va);
                    uint64_t kb = quantKey(vb);
                    if (ka == kb) continue;
                    if (ka > kb) { std::swap(ka, kb); std::swap(va, vb); }
                    edges.push_back({ka, kb, va, vb, t.groupIdx});
                }
            }
            std::sort(edges.begin(), edges.end(), [](const EdgeKeyRec& x, const EdgeKeyRec& y) {
                return (x.k0 != y.k0) ? (x.k0 < y.k0) : (x.k1 < y.k1);
            });
            for (size_t i = 0; i < edges.size();) {
                size_t j = i + 1;
                while (j < edges.size() && edges[j].k0 == edges[i].k0 && edges[j].k1 == edges[i].k1) {
                    ++j;
                }
                if (j - i == 1) {
                    s_boundaryEdges.push_back({edges[i].a, edges[i].b, edges[i].groupIdx});
                }
                i = j;
            }
        }

        Vec2 visMin = screenToUv(cMin);
        Vec2 visMax = screenToUv(cMax);
        const ImU32 fillWhiteCol = IM_COL32(248, 250, 255, 20);
        const ImU32 selFillCol   = IM_COL32(217, 130, 43, 120);
        const ImU32 boundaryCol  = IM_COL32(255, 255, 255, 200);
        const ImU32 zoomWireCol  = IM_COL32(255, 255, 255, 24);
        const ImU32 selWireCol   = IM_COL32(240, 155, 55, 220);

        // Disable AA fringe during triangle fill so adjacent shared edges never double-blend into static
        ImDrawListFlags savedFlags = dl->Flags;
        dl->Flags &= ~ImDrawListFlags_AntiAliasedFill;

        int nTris = static_cast<int>(mesh.tris.size());
        for (int i = 0; i < nTris; ++i) {
            const Triangle& t = mesh.tris[i];
            if (t.groupIdx >= 0 && t.groupIdx < static_cast<int>(mesh.groups.size()) &&
                !mesh.groups[t.groupIdx].visible) {
                continue;
            }
            float minU = std::min({t.uv0.x, t.uv1.x, t.uv2.x});
            float maxU = std::max({t.uv0.x, t.uv1.x, t.uv2.x});
            float minV = std::min({t.uv0.y, t.uv1.y, t.uv2.y});
            float maxV = std::max({t.uv0.y, t.uv1.y, t.uv2.y});
            if (maxU < visMin.x || minU > visMax.x || maxV < visMin.y || minV > visMax.y) continue;

            ImVec2 p0 = uvToScreen(t.uv0);
            ImVec2 p1 = uvToScreen(t.uv1);
            ImVec2 p2 = uvToScreen(t.uv2);
            float area2 = std::fabs((p1.x - p0.x) * (p2.y - p0.y) - (p1.y - p0.y) * (p2.x - p0.x));
            if (area2 < 0.05f && !t.selected) continue;

            dl->AddTriangleFilled(p0, p1, p2, t.selected ? selFillCol : fillWhiteCol);
        }
        dl->Flags = savedFlags;

        // Crisp white island boundary outlines (eliminates dense internal Moire noise)
        for (const UvBoundaryEdge& be : s_boundaryEdges) {
            if (be.groupIdx >= 0 && be.groupIdx < static_cast<int>(mesh.groups.size()) &&
                !mesh.groups[be.groupIdx].visible) {
                continue;
            }
            if (std::max(be.a.x, be.b.x) < visMin.x || std::min(be.a.x, be.b.x) > visMax.x ||
                std::max(be.a.y, be.b.y) < visMin.y || std::min(be.a.y, be.b.y) > visMax.y) {
                continue;
            }
            dl->AddLine(uvToScreen(be.a), uvToScreen(be.b), boundaryCol, 1.0f);
        }

        // Show subtle internal wireframe only when zoomed in closely or on selected faces
        bool showInternalWire = (uvState.pixelsPerUv > 1400.0f);
        for (int i = 0; i < nTris; ++i) {
            const Triangle& t = mesh.tris[i];
            if (!t.selected && !showInternalWire) continue;
            if (t.groupIdx >= 0 && t.groupIdx < static_cast<int>(mesh.groups.size()) &&
                !mesh.groups[t.groupIdx].visible) {
                continue;
            }
            float minU = std::min({t.uv0.x, t.uv1.x, t.uv2.x});
            float maxU = std::max({t.uv0.x, t.uv1.x, t.uv2.x});
            float minV = std::min({t.uv0.y, t.uv1.y, t.uv2.y});
            float maxV = std::max({t.uv0.y, t.uv1.y, t.uv2.y});
            if (maxU < visMin.x || minU > visMax.x || maxV < visMin.y || minV > visMax.y) continue;

            ImVec2 p0 = uvToScreen(t.uv0);
            ImVec2 p1 = uvToScreen(t.uv1);
            ImVec2 p2 = uvToScreen(t.uv2);
            dl->AddTriangle(p0, p1, p2, t.selected ? selWireCol : zoomWireCol, t.selected ? 1.2f : 1.0f);
        }
    }

    // Illustrator Bright Cyan (#00B8FF) Guide Lines + Hover Highlight & Re-drag Support
    bool clickedExistingGuide = false;
    if (state.showRulers) {
        int hovXIdx = -1, hovYIdx = -1;
        if (isHovered && uvState.draggingGuideAxis == 0) {
            for (int i = 0; i < static_cast<int>(uvState.guidesX.size()); ++i) {
                float sx = uvToScreen({uvState.guidesX[i], 0.0f}).x;
                if (std::fabs(io.MousePos.x - sx) <= 5.0f) { hovXIdx = i; break; }
            }
            if (hovXIdx < 0) {
                for (int i = 0; i < static_cast<int>(uvState.guidesY.size()); ++i) {
                    float sy = uvToScreen({0.0f, uvState.guidesY[i]}).y;
                    if (std::fabs(io.MousePos.y - sy) <= 5.0f) { hovYIdx = i; break; }
                }
            }
            bool allowGrabGuide = (state.activeTool == StudioTool::SelectMove ||
                                   state.activeTool == StudioTool::NodeEdit);
            if (allowGrabGuide && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt) {
                if (hovXIdx >= 0) {
                    uvState.draggingGuideAxis = 1;
                    uvState.draggingGuideIdx = hovXIdx;
                    uvState.guideDraggingFromRuler = false;
                    clickedExistingGuide = true;
                } else if (hovYIdx >= 0) {
                    uvState.draggingGuideAxis = 2;
                    uvState.draggingGuideIdx = hovYIdx;
                    uvState.guideDraggingFromRuler = false;
                    clickedExistingGuide = true;
                }
            }
        }
        for (int i = 0; i < static_cast<int>(uvState.guidesX.size()); ++i) {
            float sx = uvToScreen({uvState.guidesX[i], 0.0f}).x;
            bool act = (i == hovXIdx) || (uvState.draggingGuideAxis == 1 && uvState.draggingGuideIdx == i);
            dl->AddLine(ImVec2(sx, cMin.y), ImVec2(sx, cMax.y),
                        act ? IM_COL32(0x33, 0xD1, 0xFF, 255) : StudioColors::GuideCyan,
                        act ? 1.8f : 1.1f);
        }
        for (int i = 0; i < static_cast<int>(uvState.guidesY.size()); ++i) {
            float sy = uvToScreen({0.0f, uvState.guidesY[i]}).y;
            bool act = (i == hovYIdx) || (uvState.draggingGuideAxis == 2 && uvState.draggingGuideIdx == i);
            dl->AddLine(ImVec2(cMin.x, sy), ImVec2(cMax.x, sy),
                        act ? IM_COL32(0x33, 0xD1, 0xFF, 255) : StudioColors::GuideCyan,
                        act ? 1.8f : 1.1f);
        }
    }

    // Subtle 1px artboard frame
    dl->AddRect(imgMin - ImVec2(1.0f, 1.0f), imgMax + ImVec2(1.0f, 1.0f), StudioColors::BorderLight, 0.0f, 1.0f);

    // 5. Interactive Tool Handlers & Live Reticles / Gizmos
    bool toolInputAllowed = !io.KeyAlt && !ImGui::IsMouseDown(ImGuiMouseButton_Middle) &&
                            !clickedExistingGuide && (uvState.draggingGuideAxis == 0) &&
                            state.activeTool != StudioTool::Hand &&
                            state.activeTool != StudioTool::Zoom;
    Layer* activeLayer = (canvas.activeLayerIdx >= 0 && canvas.activeLayerIdx < static_cast<int>(canvas.layers.size()))
                         ? &canvas.layers[canvas.activeLayerIdx]
                         : nullptr;
    bool rmbConsumedByPen = false;

    // -- BRUSH / ERASER --
    if (state.activeTool == StudioTool::Brush || state.activeTool == StudioTool::Eraser) {
        if (isHovered && toolInputAllowed) {
            float rScr = std::max(2.0f, (state.brush.radiusPx / std::max(1, canvas.width)) * uvState.pixelsPerUv);
            dl->AddCircle(io.MousePos, rScr, IM_COL32(18, 18, 18, 210), 32, 2.0f);
            dl->AddCircle(io.MousePos, rScr, IM_COL32(238, 238, 238, 220), 32, 1.0f);
            if (state.brush.hardness < 0.98f) {
                dl->AddCircle(io.MousePos, std::max(1.0f, rScr * state.brush.hardness),
                              IM_COL32(71, 114, 179, 140), 24, 1.0f);
            }
            if (state.brush.mirrorX && mesh.loaded && state.hoverUvValid) {
                Vec3 p3, n3; int32_t tri = -1;
                if (mesh.queryUvAtPixel(hoverUv.x, hoverUv.y, p3, n3, tri)) {
                    Vec3 mp{2.0f * mesh.centerSymmetryX - p3.x, p3.y, p3.z};
                    Vec3 mn{-n3.x, n3.y, n3.z};
                    RayHit mh = mesh.projectPointToSurface(mp, mn, 0.25f);
                    if (mh.hit && mh.triIdx != tri) {
                        ImVec2 ms = uvToScreen(mh.uv);
                        dl->AddCircle(ms, rScr, IM_COL32(71, 114, 179, 150), 24, 1.0f);
                    }
                }
            }
        }

        if (isHovered && toolInputAllowed && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (activeLayer && activeLayer->locked) {
                state.statusText = "Active layer is locked";
            } else {
                int paintLayerIdx = (activeLayer && activeLayer->kind == LayerKind::RasterPaint)
                                    ? canvas.activeLayerIdx
                                    : canvas.ensureEditableLayer();
                activeLayer = (paintLayerIdx >= 0 && paintLayerIdx < static_cast<int>(canvas.layers.size()))
                              ? &canvas.layers[paintLayerIdx] : nullptr;
                BrushSettings bs = state.brush;
                bs.eraser = (state.activeTool == StudioTool::Eraser);
                canvas.beginStroke(paintLayerIdx);
                canvas.paintDabAtUV(paintLayerIdx, &mesh, hoverUv, bs, nullptr);
                uvState.isPainting = true;
                uvState.prevStrokeUv = hoverUv;
                if (callbacks.onCanvasModified) callbacks.onCanvasModified();
            }
        }
        if (uvState.isPainting) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                float movedPx = length((hoverUv - uvState.prevStrokeUv) * static_cast<float>(canvas.width));
                if (movedPx >= 0.4f) {
                    BrushSettings bs = state.brush;
                    bs.eraser = (state.activeTool == StudioTool::Eraser);
                    canvas.paintDabAtUV(canvas.activeLayerIdx, &mesh, hoverUv, bs, &uvState.prevStrokeUv);
                    uvState.prevStrokeUv = hoverUv;
                    if (callbacks.onCanvasModified) callbacks.onCanvasModified();
                }
            } else {
                canvas.endStroke(canvas.activeLayerIdx);
                uvState.isPainting = false;
            }
        }
    }

    // -- RECTANGLE SHAPE TOOL (M) --
    if (state.activeTool == StudioTool::Rect) {
        if (isHovered && toolInputAllowed && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (activeLayer && activeLayer->locked) {
                state.statusText = "Active layer is locked";
            } else {
                uvState.rectDrawing = true;
                uvState.rectStartUv = snapUvToGuides(hoverUv, uvState, state.showRulers).uv;
                uvState.rectEndUv = uvState.rectStartUv;
            }
        }
        if (uvState.rectDrawing) {
            Vec2 endUv = snapUvToGuides(hoverUv, uvState, state.showRulers).uv;
            if (io.KeyShift) {
                float du = endUv.x - uvState.rectStartUv.x;
                float dv = endUv.y - uvState.rectStartUv.y;
                float s = std::max(std::fabs(du), std::fabs(dv));
                endUv.x = uvState.rectStartUv.x + (du >= 0.0f ? s : -s);
                endUv.y = uvState.rectStartUv.y + (dv >= 0.0f ? s : -s);
            }
            uvState.rectEndUv = endUv;
            ImVec2 rA = uvToScreen(uvState.rectStartUv);
            ImVec2 rB = uvToScreen(uvState.rectEndUv);
            ImVec2 rMin(std::min(rA.x, rB.x), std::min(rA.y, rB.y));
            ImVec2 rMax(std::max(rA.x, rB.x), std::max(rA.y, rB.y));
            dl->AddRectFilled(rMin, rMax, (toImU32(state.brush.color) & 0x00FFFFFF) | 0x44000000);
            dl->AddRect(rMin, rMax, StudioColors::StudioBlue, 0.0f, 1.5f);

            if (state.brush.mirrorX) {
                ImVec2 mA = uvToScreen(Vec2{1.0f - uvState.rectStartUv.x, uvState.rectStartUv.y});
                ImVec2 mB = uvToScreen(Vec2{1.0f - uvState.rectEndUv.x, uvState.rectEndUv.y});
                ImVec2 mMin(std::min(mA.x, mB.x), std::min(mA.y, mB.y));
                ImVec2 mMax(std::max(mA.x, mB.x), std::max(mA.y, mB.y));
                dl->AddRectFilled(mMin, mMax, (toImU32(state.brush.color) & 0x00FFFFFF) | 0x22000000);
                dl->AddRect(mMin, mMax, IM_COL32(0x00, 0xB8, 0xFF, 110), 0.0f, 1.0f);
            }

            float wPx = std::fabs(uvState.rectEndUv.x - uvState.rectStartUv.x) * canvas.width;
            float hPx = std::fabs(uvState.rectEndUv.y - uvState.rectStartUv.y) * canvas.height;
            char hud[64];
            std::snprintf(hud, sizeof(hud), "%.0f x %.0f px%s", wPx, hPx, io.KeyShift ? " [1:1]" : "");
            drawHudBadge(dl, io.MousePos, hud, StudioColors::StudioBlue);

            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                uvState.rectDrawing = false;
                if (wPx >= 2.0f && hPx >= 2.0f) {
                    float minU = std::clamp(std::min(uvState.rectStartUv.x, uvState.rectEndUv.x), 0.0f, 1.0f);
                    float maxU = std::clamp(std::max(uvState.rectStartUv.x, uvState.rectEndUv.x), 0.0f, 1.0f);
                    float minV = std::clamp(std::min(uvState.rectStartUv.y, uvState.rectEndUv.y), 0.0f, 1.0f);
                    float maxV = std::clamp(std::max(uvState.rectStartUv.y, uvState.rectEndUv.y), 0.0f, 1.0f);

                    RectShapeProps rp;
                    rp.uvPos = Vec2{(minU + maxU) * 0.5f, (minV + maxV) * 0.5f};
                    rp.uvScale = Vec2{std::max(0.004f, maxU - minU), std::max(0.004f, maxV - minV)};
                    rp.anchoredIn3D = false;
                    rp.fillColor = state.brush.color;
                    rp.fillColor.a = static_cast<uint8_t>(std::clamp(static_cast<int>(std::round(state.brush.opacity * state.brush.color.a)), 0, 255));
                    rp.strokeColor = state.secondaryColor;
                    rp.strokeWidthPx = state.rectStrokeWidthPx;
                    rp.filled = true;
                    rp.hasStroke = (state.rectStrokeWidthPx > 0.0f);
                    rp.mirrorX = state.brush.mirrorX;
                    int newItemIdx = canvas.addRectItem("Rectangle", rp, &mesh);
                    canvas.selectedItemIdx = newItemIdx;
                    activeLayer = (canvas.activeLayerIdx >= 0 && canvas.activeLayerIdx < static_cast<int>(canvas.layers.size()))
                                  ? &canvas.layers[canvas.activeLayerIdx] : nullptr;
                    if (callbacks.onCanvasModified) callbacks.onCanvasModified();
                }
            }
        }
    }

    // -- ILLUSTRATOR CUBIC BEZIER PEN (P) / LINE (\) / DIRECT SELECT (A / Ctrl) --
    const bool isRulerOrNodeTool = (state.activeTool == StudioTool::Line ||
                                    state.activeTool == StudioTool::Pen ||
                                    state.activeTool == StudioTool::DirectSelect);
    if (isRulerOrNodeTool) {
        if (state.rulerFinishRequested) {
            uvState.rulerDrawing = false;
            uvState.rulerDraggingNewSegment = false;
            uvState.penDragMode = 0;
            uvState.penDragNodeIdx = -1;
            state.rulerFinishRequested = false;
        }

        const bool directSelect = (state.activeTool == StudioTool::DirectSelect) || io.KeyCtrl;

        VectorItem* actItem = canvas.activeVectorItem();
        int penLayerIdx = canvas.activeLayerIdx;
        RulerStripeProps* pPen = nullptr;
        bool penLocked = false;
        if (actItem && actItem->kind == VectorItemKind::PenPath) {
            pPen = &actItem->pen;
            penLocked = (activeLayer && activeLayer->locked) || actItem->locked;
        } else if (activeLayer && activeLayer->kind == LayerKind::RulerStripe) {
            pPen = &activeLayer->ruler;
            penLocked = activeLayer->locked;
        }

        int hoverSegIdx = -1;
        float hoverSegT = 0.5f;
        Vec2 hoverSegUv{0.5f, 0.5f};
        int hoverAnchorIdx = -1;
        int hoverHandleKind = 0; // 3=in, 4=out
        int hoverHandleNode = -1;

        static bool s_segBendMoved = false;
        static bool s_segBendUndoPushed = false;
        static ImVec2 s_segBendDownPos{0.0f, 0.0f};
        static float s_segClickT = 0.5f;

        if (pPen) {
            const RulerStripeProps& ruler = *pPen;
            int nN = static_cast<int>(ruler.nodes.size());

            // 1. Draw Bezier / Straight segments (+ live translucent mirrored preview when mirrorX)
            int segCount = (ruler.closed && nN >= 3) ? nN : std::max(0, nN - 1);
            for (int si = 0; si < segCount; ++si) {
                const StripeNode& nA = ruler.nodes[si];
                const StripeNode& nB = ruler.nodes[(si + 1) % nN];
                Vec2 p0 = nA.uv;
                Vec2 p1 = nA.hasHandles ? nA.handleOutUv : nA.uv;
                Vec2 p2 = nB.hasHandles ? nB.handleInUv : nB.uv;
                Vec2 p3 = nB.uv;
                if (!ruler.smoothCurve && !nA.hasHandles && !nB.hasHandles) {
                    dl->AddLine(uvToScreen(p0), uvToScreen(p3), StudioColors::GuideCyan, 1.4f);
                } else {
                    dl->AddBezierCubic(uvToScreen(p0), uvToScreen(p1), uvToScreen(p2), uvToScreen(p3),
                                       StudioColors::GuideCyan, 1.5f, 20);
                }
                if (ruler.mirrorX) {
                    auto mirUv = [](Vec2 u) { return Vec2{1.0f - u.x, u.y}; };
                    const ImU32 mirCol = IM_COL32(0x00, 0xB8, 0xFF, 96);
                    if (!ruler.smoothCurve && !nA.hasHandles && !nB.hasHandles) {
                        dl->AddLine(uvToScreen(mirUv(p0)), uvToScreen(mirUv(p3)), mirCol, 1.2f);
                    } else {
                        dl->AddBezierCubic(uvToScreen(mirUv(p0)), uvToScreen(mirUv(p1)),
                                           uvToScreen(mirUv(p2)), uvToScreen(mirUv(p3)),
                                           mirCol, 1.2f, 20);
                    }
                }
            }

            // 2. Draw Handles and Anchors + Hit Test
            float bestAnchorD = 9.0f;
            float bestHandleD = 8.0f;
            for (int i = 0; i < nN; ++i) {
                const StripeNode& nd = ruler.nodes[i];
                ImVec2 sp = uvToScreen(nd.uv);
                if (isHovered && nd.hasHandles) {
                    float dIn = length(toVec2(io.MousePos - uvToScreen(nd.handleInUv)));
                    float dOut = length(toVec2(io.MousePos - uvToScreen(nd.handleOutUv)));
                    if (dOut < bestHandleD) { bestHandleD = dOut; hoverHandleKind = 4; hoverHandleNode = i; }
                    if (dIn  < bestHandleD) { bestHandleD = dIn;  hoverHandleKind = 3; hoverHandleNode = i; }
                }
                if (nd.hasHandles) {
                    ImVec2 hInS = uvToScreen(nd.handleInUv);
                    ImVec2 hOutS = uvToScreen(nd.handleOutUv);
                    dl->AddLine(sp, hInS, StudioColors::GuideCyan, 1.0f);
                    dl->AddLine(sp, hOutS, StudioColors::GuideCyan, 1.0f);
                    bool actIn = (hoverHandleNode == i && hoverHandleKind == 3) ||
                                 (uvState.penDragNodeIdx == i && uvState.penDragMode == 3);
                    bool actOut = (hoverHandleNode == i && hoverHandleKind == 4) ||
                                  (uvState.penDragNodeIdx == i && uvState.penDragMode == 4);
                    dl->AddCircleFilled(hInS, 3.6f, actIn ? StudioColors::AmberAccent : StudioColors::GuideCyan, 12);
                    dl->AddCircleFilled(hOutS, 3.6f, actOut ? StudioColors::AmberAccent : StudioColors::GuideCyan, 12);
                }

                float dAnc = isHovered ? length(toVec2(io.MousePos - sp)) : 999.0f;
                if (dAnc < bestAnchorD) { bestAnchorD = dAnc; hoverAnchorIdx = i; }
                bool hNode = (hoverAnchorIdx == i) || (uvState.penDragNodeIdx == i && uvState.penDragMode != 0);
                ImU32 nCol = hNode ? StudioColors::AmberAccent : StudioColors::GuideCyan;
                dl->AddRectFilled(sp - ImVec2(4.0f, 4.0f), sp + ImVec2(4.0f, 4.0f),
                                  nd.smooth ? IM_COL32(22, 26, 34, 240) : StudioColors::GuideCyan);
                dl->AddRect(sp - ImVec2(4.0f, 4.0f), sp + ImVec2(4.0f, 4.0f), nCol, 0.0f, 1.5f);
            }

            // Close loop 'o' badge / RMB '-' remove badge / handle collapse / segment hint
            if (isHovered && !directSelect && uvState.penDragMode == 0 &&
                uvState.rulerDrawing && !ruler.closed && nN >= 2 && hoverAnchorIdx == 0) {
                ImVec2 sp0 = uvToScreen(ruler.nodes[0].uv);
                dl->AddCircle(sp0, 8.5f, StudioColors::AmberAccent, 16, 1.6f);
                drawHudBadge(dl, io.MousePos, "o Close Path", StudioColors::AmberAccent);
            } else if (isHovered && uvState.penDragMode == 0 && hoverAnchorIdx >= 0) {
                drawHudBadge(dl, io.MousePos, "- RMB Delete  |  Drag Move", StudioColors::AmberAccent);
            } else if (isHovered && uvState.penDragMode == 0 && hoverHandleKind != 0) {
                drawHudBadge(dl, io.MousePos, "Handle: Drag / RMB Reset", StudioColors::GuideCyan);
            } else if (isHovered && uvState.penDragMode == 0 && hoverAnchorIdx < 0 &&
                       hoverHandleKind == 0 && segCount > 0) {
                float bestSegD = 7.5f;
                for (int si = 0; si < segCount; ++si) {
                    const StripeNode& nA = ruler.nodes[si];
                    const StripeNode& nB = ruler.nodes[(si + 1) % nN];
                    Vec2 p0 = nA.uv, p1 = nA.hasHandles ? nA.handleOutUv : nA.uv;
                    Vec2 p2 = nB.hasHandles ? nB.handleInUv : nB.uv, p3 = nB.uv;
                    for (int k = 1; k < 20; ++k) {
                        float t = k / 20.0f;
                        Vec2 pt = evalCubic2(p0, p1, p2, p3, t);
                        float d = length(toVec2(io.MousePos - uvToScreen(pt)));
                        if (d < bestSegD) {
                            bestSegD = d;
                            hoverSegIdx = si;
                            hoverSegT = t;
                            hoverSegUv = pt;
                        }
                    }
                }
                if (hoverSegIdx >= 0) {
                    ImVec2 spSeg = uvToScreen(hoverSegUv);
                    dl->AddCircle(spSeg, 5.0f, StudioColors::GuideCyan, 12, 1.4f);
                    drawHudBadge(dl, io.MousePos,
                                 directSelect ? "Drag to Bend Segment" : "+ Add Anchor / Drag Bend",
                                 StudioColors::GuideCyan);
                }
            }
        }

        // Right-click & Keyboard shortcuts on Pen
        if (isHovered && pPen && !penLocked) {
            if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                uvState.rulerDrawing = false;
                uvState.penDragMode = 0;
                uvState.penDragNodeIdx = -1;
            }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                if (hoverHandleKind != 0 && hoverHandleNode >= 0 &&
                    hoverHandleNode < static_cast<int>(pPen->nodes.size())) {
                    canvas.pushUndoSnapshot();
                    StripeNode& nd = pPen->nodes[hoverHandleNode];
                    if (hoverHandleKind == 3) nd.handleInUv = nd.uv;
                    else nd.handleOutUv = nd.uv;
                    nd.smooth = false;
                    if (length(nd.handleInUv - nd.uv) < 1e-6f && length(nd.handleOutUv - nd.uv) < 1e-6f) {
                        nd.hasHandles = false;
                    }
                    populateNode3DAndHandles(nd, mesh);
                    canvas.rasterizeLayer(penLayerIdx, &mesh);
                    if (callbacks.onCanvasModified) callbacks.onCanvasModified();
                    uvState.penDragMode = 0;
                    uvState.penDragNodeIdx = -1;
                    rmbConsumedByPen = true;
                } else if (hoverAnchorIdx >= 0 && hoverAnchorIdx < static_cast<int>(pPen->nodes.size())) {
                    if (actItem && actItem->kind == VectorItemKind::PenPath && callbacks.onRemovePenPoint) {
                        callbacks.onRemovePenPoint(canvas.selectedItemIdx, hoverAnchorIdx);
                    } else {
                        canvas.pushUndoSnapshot();
                        pPen->nodes.erase(pPen->nodes.begin() + hoverAnchorIdx);
                        if (pPen->nodes.size() < 3) pPen->closed = false;
                        canvas.rasterizeLayer(penLayerIdx, &mesh);
                        if (callbacks.onCanvasModified) callbacks.onCanvasModified();
                    }
                    uvState.penDragMode = 0;
                    uvState.penDragNodeIdx = -1;
                    rmbConsumedByPen = true;
                } else if (uvState.rulerDrawing) {
                    uvState.rulerDrawing = false;
                    uvState.penDragMode = 0;
                    uvState.penDragNodeIdx = -1;
                    rmbConsumedByPen = true;
                }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && !pPen->nodes.empty()) {
                canvas.pushUndoSnapshot();
                pPen->nodes.pop_back();
                if (pPen->nodes.size() < 3) pPen->closed = false;
                canvas.rasterizeLayer(penLayerIdx, &mesh);
                if (callbacks.onCanvasModified) callbacks.onCanvasModified();
            }
        }

        // 3. Mouse Down Action
        if (isHovered && toolInputAllowed && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (pPen && !penLocked && uvState.rulerDrawing && !directSelect && !pPen->closed &&
                pPen->nodes.size() >= 2 && hoverAnchorIdx == 0) {
                canvas.pushUndoSnapshot();
                pPen->closed = true;
                uvState.penDragMode = 1;
                uvState.penDragNodeIdx = 0;
                uvState.penAnchorDownUv = pPen->nodes[0].uv;
                uvState.penPrevDragUv = hoverUv;
                uvState.rulerDrawing = false;
                canvas.rasterizeLayer(penLayerIdx, &mesh);
                if (callbacks.onCanvasModified) callbacks.onCanvasModified();
            } else if (pPen && !penLocked && hoverHandleKind != 0 && hoverHandleNode >= 0) {
                canvas.pushUndoSnapshot();
                uvState.penDragMode = hoverHandleKind;
                uvState.penDragNodeIdx = hoverHandleNode;
                uvState.penPrevDragUv = hoverUv;
            } else if (pPen && !penLocked && hoverAnchorIdx >= 0) {
                if (io.KeyAlt || ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    canvas.pushUndoSnapshot();
                    StripeNode& nd = pPen->nodes[hoverAnchorIdx];
                    nd.smooth = !nd.smooth;
                    if (!nd.smooth) {
                        nd.hasHandles = false;
                        nd.handleInUv = nd.handleOutUv = nd.uv;
                    } else {
                        nd.hasHandles = true;
                        Vec2 dir{0.025f, 0.0f};
                        if (pPen->nodes.size() >= 2) {
                            int nN = static_cast<int>(pPen->nodes.size());
                            int pI = (hoverAnchorIdx + nN - 1) % nN;
                            int nxI = (hoverAnchorIdx + 1) % nN;
                            Vec2 chord = pPen->nodes[nxI].uv - pPen->nodes[pI].uv;
                            if (length(chord) > 1e-4f) dir = normalized(chord) * 0.03f;
                        }
                        nd.handleOutUv = nd.uv + dir;
                        nd.handleInUv = nd.uv - dir;
                    }
                    populateNode3DAndHandles(nd, mesh);
                    syncRulerUse3DFlag(*pPen);
                    canvas.rasterizeLayer(penLayerIdx, &mesh);
                    if (callbacks.onCanvasModified) callbacks.onCanvasModified();
                } else {
                    canvas.pushUndoSnapshot();
                    uvState.penDragMode = 2;
                    uvState.penDragNodeIdx = hoverAnchorIdx;
                    uvState.penPrevDragUv = hoverUv;
                }
            } else if (pPen && !penLocked && hoverSegIdx >= 0) {
                uvState.penDragMode = 5; // direct segment bend OR single-click De Casteljau insert
                uvState.penDragNodeIdx = hoverSegIdx;
                uvState.penPrevDragUv = hoverUv;
                s_segBendMoved = false;
                s_segBendUndoPushed = false;
                s_segBendDownPos = io.MousePos;
                s_segClickT = hoverSegT;
            } else if (directSelect) {
                int pLayer = -1, pItem = -1;
                if (findTopVectorTargetAtPoint(canvas, io.MousePos, vpCenter, uvState.viewCenterUv, uvState.pixelsPerUv,
                                               pLayer, pItem) &&
                    pItem >= 0) {
                    canvas.activeLayerIdx = pLayer;
                    canvas.selectedItemIdx = pItem;
                    state.textEditSyncedLayerId = 0;
                } else if (uvState.rulerDrawing) {
                    uvState.rulerDrawing = false;
                }
            } else {
                // Empty canvas click in Pen/Line tool: reuse empty PenPath item or add new PenPath item inside container layer
                bool needNewItem = (!pPen) || (!uvState.rulerDrawing && !pPen->nodes.empty());
                if (needNewItem) {
                    RulerStripeProps rp;
                    rp.smoothCurve = (state.activeTool == StudioTool::Pen);
                    rp.fillColor = state.brush.color;
                    rp.color = state.secondaryColor;
                    rp.outlineColor = state.secondaryColor;
                    rp.hasStroke = true;
                    rp.filled = false;
                    rp.closed = false;
                    rp.mirrorX = state.brush.mirrorX;
                    int itemIdx = canvas.addPenItem(
                        state.activeTool == StudioTool::Pen ? "Pen Path" : "Line Path", rp, &mesh);
                    canvas.selectedItemIdx = itemIdx;
                    penLayerIdx = canvas.activeLayerIdx;
                    activeLayer = (penLayerIdx >= 0 && penLayerIdx < static_cast<int>(canvas.layers.size()))
                                  ? &canvas.layers[penLayerIdx] : nullptr;
                    actItem = canvas.activeVectorItem();
                    pPen = (actItem && actItem->kind == VectorItemKind::PenPath) ? &actItem->pen : nullptr;
                    penLocked = false;
                } else if (!penLocked && pPen) {
                    canvas.pushUndoSnapshot();
                }

                if (pPen && !penLocked) {
                    pPen->smoothCurve = (state.activeTool == StudioTool::Pen);
                    Vec2 placeUv = snapUvToGuides(hoverUv, uvState, state.showRulers).uv;
                    if (uvState.rulerDrawing && !pPen->nodes.empty()) {
                        float ang = 0.0f, lenPx = 0.0f;
                        float snapStep = (state.activeTool == StudioTool::Pen) ? 45.0f : 15.0f;
                        placeUv = snapStepAngleUv(pPen->nodes.back().uv, placeUv, io.KeyShift,
                                                  snapStep, canvas.width, canvas.height, ang, lenPx);
                    }
                    StripeNode sn{};
                    sn.uv = placeUv;
                    sn.handleInUv = placeUv;
                    sn.handleOutUv = placeUv;
                    sn.hasHandles = false;
                    sn.smooth = (state.activeTool == StudioTool::Pen);
                    populateNode3DAndHandles(sn, mesh);
                    pPen->nodes.push_back(sn);
                    syncRulerUse3DFlag(*pPen);

                    uvState.penDragMode = 1;
                    uvState.penDragNodeIdx = static_cast<int>(pPen->nodes.size()) - 1;
                    uvState.penAnchorDownUv = placeUv;
                    uvState.penPrevDragUv = hoverUv;
                    uvState.rulerDrawing = true;
                    if (pPen->nodes.size() >= 2) {
                        canvas.rasterizeLayer(penLayerIdx, &mesh);
                        if (callbacks.onCanvasModified) callbacks.onCanvasModified();
                    }
                }
            }
        }

        // 4. Active Pen / Direct-Select Dragging (NO duplicate node added on release!)
        if (uvState.penDragMode != 0 && pPen) {
            int idx = uvState.penDragNodeIdx;
            int nN = static_cast<int>(pPen->nodes.size());
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && idx >= 0 && idx < nN) {
                if (uvState.penDragMode == 5) {
                    // Direct segment bend
                    if (!s_segBendMoved && length(toVec2(io.MousePos - s_segBendDownPos)) >= 3.5f) {
                        s_segBendMoved = true;
                        if (!s_segBendUndoPushed) {
                            canvas.pushUndoSnapshot();
                            s_segBendUndoPushed = true;
                        }
                    }
                    if (s_segBendMoved) {
                        int iB = (idx + 1) % nN;
                        StripeNode& nA = pPen->nodes[idx];
                        StripeNode& nB = pPen->nodes[iB];
                        if (!nA.hasHandles) { nA.hasHandles = true; nA.handleInUv = nA.uv; nA.handleOutUv = lerp2(nA.uv, nB.uv, 0.33f); }
                        if (!nB.hasHandles) { nB.hasHandles = true; nB.handleOutUv = nB.uv; nB.handleInUv = lerp2(nB.uv, nA.uv, 0.33f); }
                        float t = std::clamp(s_segClickT, 0.12f, 0.88f);
                        float scale = 1.0f / std::max(0.25f, 3.0f * (1.0f - t) * t);
                        Vec2 dUv = hoverUv - uvState.penPrevDragUv;
                        nA.handleOutUv += dUv * ((1.0f - t) * scale);
                        nB.handleInUv += dUv * (t * scale);
                        nA.smooth = false;
                        nB.smooth = false;
                        populateNode3DAndHandles(nA, mesh);
                        populateNode3DAndHandles(nB, mesh);
                        syncRulerUse3DFlag(*pPen);
                        uvState.penPrevDragUv = hoverUv;
                        double now = ImGui::GetTime();
                        if (now - uvState.lastLiveRasterTime >= 0.015) {
                            canvas.rasterizeLayer(penLayerIdx, &mesh);
                            if (callbacks.onCanvasModified) callbacks.onCanvasModified();
                            uvState.lastLiveRasterTime = now;
                        }
                    }
                } else {
                    StripeNode& nd = pPen->nodes[idx];
                    if (uvState.penDragMode == 1) {
                        if (ImGui::IsKeyDown(ImGuiKey_Space)) {
                            Vec2 d = hoverUv - uvState.penPrevDragUv;
                            nd.uv += d;
                            nd.handleInUv += d;
                            nd.handleOutUv += d;
                        } else if (state.activeTool == StudioTool::Pen) {
                            float dragPx = length(toVec2(io.MousePos - uvToScreen(nd.uv)));
                            if (dragPx >= 4.0f) {
                                float ang = 0.0f, len = 0.0f;
                                Vec2 hOut = snapStepAngleUv(nd.uv, hoverUv, io.KeyShift, 45.0f,
                                                            canvas.width, canvas.height, ang, len);
                                nd.hasHandles = true;
                                nd.handleOutUv = hOut;
                                if (io.KeyAlt) {
                                    nd.smooth = false;
                                } else {
                                    nd.smooth = true;
                                    nd.handleInUv = nd.uv * 2.0f - nd.handleOutUv;
                                }
                            }
                        }
                    } else if (uvState.penDragMode == 2) {
                        Vec2 targetUv = snapUvToGuides(hoverUv, uvState, state.showRulers).uv;
                        if (io.KeyShift && pPen->nodes.size() >= 2) {
                            int refIdx = (idx > 0) ? (idx - 1) : 1;
                            float ang = 0.0f, len = 0.0f;
                            targetUv = snapStepAngleUv(pPen->nodes[refIdx].uv, targetUv, true,
                                                       45.0f, canvas.width, canvas.height, ang, len);
                        }
                        Vec2 delta = targetUv - nd.uv;
                        nd.uv = targetUv;
                        nd.handleInUv += delta;
                        nd.handleOutUv += delta;
                    } else if (uvState.penDragMode == 3 || uvState.penDragMode == 4) {
                        float ang = 0.0f, len = 0.0f;
                        Vec2 hUv = snapStepAngleUv(nd.uv, hoverUv, io.KeyShift, 45.0f,
                                                   canvas.width, canvas.height, ang, len);
                        nd.hasHandles = true;
                        if (uvState.penDragMode == 3) {
                            nd.handleInUv = hUv;
                            if (io.KeyAlt) {
                                nd.smooth = false;
                            } else if (nd.smooth) {
                                Vec2 dir = nd.uv - nd.handleInUv;
                                float dLen = length(dir);
                                float oppLen = length(nd.handleOutUv - nd.uv);
                                if (dLen > 1e-5f) nd.handleOutUv = nd.uv + dir * (oppLen > 1e-4f ? (oppLen / dLen) : 1.0f);
                            }
                        } else {
                            nd.handleOutUv = hUv;
                            if (io.KeyAlt) {
                                nd.smooth = false;
                            } else if (nd.smooth) {
                                Vec2 dir = nd.uv - nd.handleOutUv;
                                float dLen = length(dir);
                                float oppLen = length(nd.handleInUv - nd.uv);
                                if (dLen > 1e-5f) nd.handleInUv = nd.uv + dir * (oppLen > 1e-4f ? (oppLen / dLen) : 1.0f);
                            }
                        }
                    }
                    uvState.penPrevDragUv = hoverUv;
                    populateNode3DAndHandles(nd, mesh);
                    syncRulerUse3DFlag(*pPen);

                    double now = ImGui::GetTime();
                    if (pPen->nodes.size() >= 2 && (now - uvState.lastLiveRasterTime >= 0.015)) {
                        canvas.rasterizeLayer(penLayerIdx, &mesh);
                        if (callbacks.onCanvasModified) callbacks.onCanvasModified();
                        uvState.lastLiveRasterTime = now;
                    }
                }
            } else {
                // Mouse released
                if (uvState.penDragMode == 5 && !s_segBendMoved && !directSelect &&
                    state.activeTool == StudioTool::Pen && idx >= 0 && idx < nN) {
                    // Clean click on segment inserts De Casteljau anchor
                    canvas.pushUndoSnapshot();
                    int iB = (idx + 1) % nN;
                    StripeNode nA = pPen->nodes[idx];
                    StripeNode nB = pPen->nodes[iB];
                    Vec2 p0 = nA.uv, p1 = nA.hasHandles ? nA.handleOutUv : nA.uv;
                    Vec2 p2 = nB.hasHandles ? nB.handleInUv : nB.uv, p3 = nB.uv;
                    float t = s_segClickT;
                    Vec2 q0 = lerp2(p0, p1, t), q1 = lerp2(p1, p2, t), q2 = lerp2(p2, p3, t);
                    Vec2 r0 = lerp2(q0, q1, t), r1 = lerp2(q1, q2, t);
                    Vec2 mid = lerp2(r0, r1, t);

                    pPen->nodes[idx].hasHandles = true;
                    pPen->nodes[idx].handleOutUv = q0;
                    pPen->nodes[iB].hasHandles = true;
                    pPen->nodes[iB].handleInUv = q2;

                    StripeNode ins{};
                    ins.uv = mid;
                    ins.handleInUv = r0;
                    ins.handleOutUv = r1;
                    ins.hasHandles = true;
                    ins.smooth = true;
                    populateNode3DAndHandles(ins, mesh);
                    populateNode3DAndHandles(pPen->nodes[idx], mesh);
                    populateNode3DAndHandles(pPen->nodes[iB], mesh);
                    pPen->nodes.insert(pPen->nodes.begin() + idx + 1, ins);
                    syncRulerUse3DFlag(*pPen);
                    canvas.rasterizeLayer(penLayerIdx, &mesh);
                    if (callbacks.onCanvasModified) callbacks.onCanvasModified();
                } else if (pPen->nodes.size() >= 2) {
                    canvas.rasterizeLayer(penLayerIdx, &mesh);
                    if (callbacks.onCanvasModified) callbacks.onCanvasModified();
                }
                uvState.penDragMode = 0;
                uvState.penDragNodeIdx = -1;
                s_segBendMoved = false;
                s_segBendUndoPushed = false;
            }
        }

        // 5. #00B8FF Live Cubic Bezier Rubber-Band Preview (+ translucent mirrored segment!)
        if (!directSelect && uvState.rulerDrawing && pPen &&
            !pPen->nodes.empty() && !pPen->closed && uvState.penDragMode == 0 && !io.KeyCtrl) {
            const StripeNode& lastN = pPen->nodes.back();
            float angDeg = 0.0f, lenPx = 0.0f;
            float snapStep = (state.activeTool == StudioTool::Pen) ? 45.0f : 15.0f;
            Vec2 snappedHover = snapUvToGuides(hoverUv, uvState, state.showRulers).uv;
            Vec2 previewUv = snapStepAngleUv(lastN.uv, snappedHover, io.KeyShift, snapStep,
                                             canvas.width, canvas.height, angDeg, lenPx);
            if (isHovered) {
                ImVec2 pA = uvToScreen(lastN.uv);
                ImVec2 pOut = uvToScreen(lastN.hasHandles ? lastN.handleOutUv : lastN.uv);
                ImVec2 pB = uvToScreen(previewUv);
                if (lastN.hasHandles && state.activeTool == StudioTool::Pen) {
                    dl->AddBezierCubic(pA, pOut, pB, pB, StudioColors::GuideCyan, 1.6f, 20);
                } else {
                    dl->AddLine(pA, pB, StudioColors::GuideCyan, 1.6f);
                }
                dl->AddCircleFilled(pB, 3.5f, StudioColors::GuideCyan, 12);

                if (pPen->mirrorX) {
                    auto mirUv = [](Vec2 u) { return Vec2{1.0f - u.x, u.y}; };
                    ImVec2 mA = uvToScreen(mirUv(lastN.uv));
                    ImVec2 mOut = uvToScreen(mirUv(lastN.hasHandles ? lastN.handleOutUv : lastN.uv));
                    ImVec2 mB = uvToScreen(mirUv(previewUv));
                    const ImU32 mCol = IM_COL32(0x00, 0xB8, 0xFF, 110);
                    if (lastN.hasHandles && state.activeTool == StudioTool::Pen) {
                        dl->AddBezierCubic(mA, mOut, mB, mB, mCol, 1.3f, 20);
                    } else {
                        dl->AddLine(mA, mB, mCol, 1.3f);
                    }
                    dl->AddCircle(mB, 3.0f, mCol, 12, 1.0f);
                }

                if (hoverAnchorIdx != 0) {
                    char hud[80];
                    std::snprintf(hud, sizeof(hud), "%.1f px  |  %.1f deg%s",
                                  lenPx, angDeg, io.KeyShift ? " [SNAP]" : "");
                    drawHudBadge(dl, pB, hud, io.KeyShift ? StudioColors::AmberAccent : StudioColors::GuideCyan);
                }
            }
        }
    }

    // -- TEXT / TOUCH TYPE / IMAGE / RECTANGLE / SELECT-MOVE PICKING + AREA BOX & TOUCH TYPE GIZMOS --
    const bool isTouchTypeTool = (state.activeTool == StudioTool::TouchType);
    const bool isTransformTool = (state.activeTool == StudioTool::Text ||
                                  isTouchTypeTool ||
                                  state.activeTool == StudioTool::Image ||
                                  state.activeTool == StudioTool::Select);
    static bool s_gizmoStartedAnchored3D = false;
    static float s_gizmoOrigWorldSize = 0.25f;
    static float s_gizmoOrigWorldH = 0.25f;

    auto resolveTransformTarget = [&](VectorItem*& outItem, Layer*& outLegacyLayer, int& outLayerIdx) -> bool {
        outItem = canvas.activeVectorItem();
        outLayerIdx = canvas.activeLayerIdx;
        if (outItem && (outItem->kind == VectorItemKind::Text ||
                        outItem->kind == VectorItemKind::Image ||
                        outItem->kind == VectorItemKind::RectShape)) {
            outLegacyLayer = nullptr;
            return true;
        }
        outItem = nullptr;
        if (activeLayer && (activeLayer->kind == LayerKind::Text || activeLayer->kind == LayerKind::Decal)) {
            outLegacyLayer = activeLayer;
            return true;
        }
        outLegacyLayer = nullptr;
        return false;
    };

    if (isTransformTool && !isTouchTypeTool && isHovered && toolInputAllowed &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        uvState.gizmoMode == UvViewportState::GizmoDragMode::None) {
        VectorItem* tItem = nullptr;
        Layer* tLeg = nullptr;
        int tLIdx = -1;
        bool hitCurrentHandle = false;
        if (resolveTransformTarget(tItem, tLeg, tLIdx) && !(activeLayer && activeLayer->locked) &&
            !(tItem && tItem->locked)) {
            Vec2 centerUv{}, halfUv{};
            float rotDeg = 0.0f;
            bool isAreaTxt = false;
            if (tItem) {
                computeItemCenterAndHalf(*tItem, canvas.width, canvas.height, centerUv, halfUv);
                rotDeg = (tItem->kind == VectorItemKind::Text)      ? tItem->text.rotationDeg :
                         (tItem->kind == VectorItemKind::Image)     ? tItem->image.rotationDeg : tItem->rect.rotationDeg;
                isAreaTxt = (tItem->kind == VectorItemKind::Text && tItem->text.isAreaBox);
            } else {
                computeLegacyLayerCenterAndHalf(*tLeg, canvas.width, canvas.height, centerUv, halfUv);
                rotDeg = (tLeg->kind == LayerKind::Text) ? tLeg->text.rotationDeg : tLeg->decal.rotationDeg;
                isAreaTxt = (tLeg->kind == LayerKind::Text && tLeg->text.isAreaBox);
            }
            Obb2D curObb = buildScreenObb(centerUv, halfUv, rotDeg, vpCenter, uvState.viewCenterUv, uvState.pixelsPerUv);
            if (length(toVec2(io.MousePos - curObb.rotHandle)) <= 9.0f) {
                hitCurrentHandle = true;
            } else {
                for (int ci = 0; ci < 4; ++ci) {
                    if (length(toVec2(io.MousePos - curObb.corners[ci])) <= 8.5f) {
                        hitCurrentHandle = true;
                        break;
                    }
                    if (isAreaTxt) {
                        ImVec2 edgeMid = (curObb.corners[ci] + curObb.corners[(ci + 1) & 3]) * 0.5f;
                        if (length(toVec2(io.MousePos - edgeMid)) <= 8.5f) {
                            hitCurrentHandle = true;
                            break;
                        }
                    }
                }
            }
        }
        if (!hitCurrentHandle) {
            int pickedL = -1, pickedI = -1;
            if (findTopVectorTargetAtPoint(canvas, io.MousePos, vpCenter, uvState.viewCenterUv, uvState.pixelsPerUv,
                                           pickedL, pickedI)) {
                canvas.activeLayerIdx = pickedL;
                canvas.selectedItemIdx = pickedI;
                activeLayer = (pickedL >= 0 && pickedL < static_cast<int>(canvas.layers.size()))
                              ? &canvas.layers[pickedL] : nullptr;
                state.textEditSyncedLayerId = 0;
                if (VectorItem* pi = canvas.activeVectorItem()) {
                    if (pi->kind == VectorItemKind::PenPath && state.activeTool == StudioTool::Select) {
                        state.activePropsCat = PropertiesCategory::Ruler;
                    } else if (pi->kind == VectorItemKind::Text) {
                        state.activePropsCat = PropertiesCategory::Text;
                    } else if (pi->kind == VectorItemKind::Image) {
                        state.activePropsCat = PropertiesCategory::Decal;
                    }
                }
            } else if (state.activeTool == StudioTool::Text) {
                uvState.typeBoxDrawing = true;
                uvState.typeBoxStartUv = snapUvToGuides(hoverUv, uvState, state.showRulers).uv;
                uvState.typeBoxEndUv = uvState.typeBoxStartUv;
            } else if (state.activeTool == StudioTool::Image) {
                if (callbacks.onImportImage) callbacks.onImportImage();
                else if (callbacks.onImportDecal) callbacks.onImportDecal();
            }
        }
    }

    if (uvState.typeBoxDrawing) {
        Vec2 endUv = snapUvToGuides(hoverUv, uvState, state.showRulers).uv;
        uvState.typeBoxEndUv = endUv;
        ImVec2 rA = uvToScreen(uvState.typeBoxStartUv);
        ImVec2 rB = uvToScreen(endUv);
        ImVec2 rMin(std::min(rA.x, rB.x), std::min(rA.y, rB.y));
        ImVec2 rMax(std::max(rA.x, rB.x), std::max(rA.y, rB.y));
        dl->AddRectFilled(rMin, rMax, IM_COL32(255, 255, 255, 22));
        dl->AddRect(rMin, rMax, StudioColors::GuideCyan, 0.0f, 1.3f);
        float boxW = std::fabs(rMax.x - rMin.x);
        float boxH = std::fabs(rMax.y - rMin.y);
        if (boxW >= 12.0f && boxH >= 12.0f) {
            dl->AddText(ImVec2(rMin.x + 5.0f, rMin.y + 4.0f), StudioColors::TextPrimary, "Area Type Box");
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            uvState.typeBoxDrawing = false;
            TextLayerProps tp;
            tp.text = "Lorem ipsum";
            tp.fontFamily = "Arial";
            tp.fontStyle = "Bold";
            tp.fontPath = canvas.findFontPath("", tp.fontFamily, tp.fontStyle);
            tp.uvPos = (boxW >= 14.0f && boxH >= 12.0f)
                       ? Vec2{(uvState.typeBoxStartUv.x + endUv.x) * 0.5f,
                              (uvState.typeBoxStartUv.y + endUv.y) * 0.5f}
                       : uvState.typeBoxStartUv;
            tp.anchoredIn3D = false;
            tp.fillColor = state.brush.color;
            tp.outlineColor = state.secondaryColor;
            tp.mirrorX = state.brush.mirrorX;
            tp.mirrorMode = state.brush.mirrorX ? MirrorTextMode::ReadableBothSides : MirrorTextMode::None;
            float uvScale = std::max(0.25f, static_cast<float>(canvas.width) / 2048.0f);
            if (boxW >= 20.0f && boxH >= 14.0f) {
                float pxW = std::fabs(endUv.x - uvState.typeBoxStartUv.x) * canvas.width;
                float pxH = std::fabs(endUv.y - uvState.typeBoxStartUv.y) * canvas.height;
                tp.isAreaBox = true;
                tp.boxWidthPx = std::clamp(pxW / uvScale, 48.0f, 4096.0f);
                tp.boxHeightPx = std::clamp(pxH / uvScale, 28.0f, 4096.0f);
                tp.fontSizePx = std::clamp(tp.boxHeightPx * 0.44f, 36.0f, 120.0f);
            }
            tp.syncCharStyles();
            int itemIdx = canvas.addTextItem("Text Layer", tp, &mesh);
            canvas.selectedItemIdx = itemIdx;
            activeLayer = (canvas.activeLayerIdx >= 0 && canvas.activeLayerIdx < static_cast<int>(canvas.layers.size()))
                          ? &canvas.layers[canvas.activeLayerIdx] : nullptr;
            state.textEditSyncedLayerId = 0;
            state.activePropsCat = PropertiesCategory::Text;
            if (callbacks.onCanvasModified) callbacks.onCanvasModified();
        }
    }

    // -- TOUCH TYPE INTERACTIVE PER-LETTER CANVAS HANDLER (Shift+T) --
    if (isTouchTypeTool) {
        VectorItem* tItem = nullptr;
        Layer* tLeg = nullptr;
        int tLIdx = -1;
        resolveTransformTarget(tItem, tLeg, tLIdx);
        TextLayerProps* pTx = (tItem && tItem->kind == VectorItemKind::Text) ? &tItem->text :
                              (tLeg && tLeg->kind == LayerKind::Text) ? &tLeg->text : nullptr;
        bool txLocked = (activeLayer && activeLayer->locked) || (tItem && tItem->locked);

        if (isHovered && toolInputAllowed && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            uvState.gizmoMode == UvViewportState::GizmoDragMode::None) {
            bool consumed = false;
            if (pTx && !txLocked && pTx->selectedCharIdx >= 0 &&
                pTx->selectedCharIdx < static_cast<int>(pTx->lastCharBoxesUv.size())) {
                const CharBoxUv& selB = pTx->lastCharBoxesUv[pTx->selectedCharIdx];
                ImVec2 sMin = uvToScreen(selB.uvMin);
                ImVec2 sMax = uvToScreen(selB.uvMax);
                ImVec2 sRot((sMin.x + sMax.x) * 0.5f, sMin.y - 16.0f);
                pTx->syncCharStyles();
                CharStyle& cs = pTx->charStyles[pTx->selectedCharIdx];
                if (length(toVec2(io.MousePos - sRot)) <= 8.5f) {
                    canvas.pushUndoSnapshot();
                    uvState.gizmoMode = UvViewportState::GizmoDragMode::TouchCharRotate;
                    uvState.gizmoOrigRotDeg = cs.rotDeg;
                    consumed = true;
                } else {
                    ImVec2 corners[4] = {sMin, {sMax.x, sMin.y}, sMax, {sMin.x, sMax.y}};
                    for (int ci = 0; ci < 4; ++ci) {
                        if (length(toVec2(io.MousePos - corners[ci])) <= 8.0f) {
                            canvas.pushUndoSnapshot();
                            uvState.gizmoMode = UvViewportState::GizmoDragMode::TouchCharScale;
                            uvState.gizmoDragStartUv = hoverUv;
                            uvState.gizmoOrigCharScale = cs.scale;
                            consumed = true;
                            break;
                        }
                    }
                }
            }
            if (!consumed && pTx) {
                for (int ci = static_cast<int>(pTx->lastCharBoxesUv.size()) - 1; ci >= 0; --ci) {
                    const CharBoxUv& b = pTx->lastCharBoxesUv[ci];
                    if (hoverUv.x >= b.uvMin.x && hoverUv.x <= b.uvMax.x &&
                        hoverUv.y >= b.uvMin.y && hoverUv.y <= b.uvMax.y) {
                        pTx->selectedCharIdx = ci;
                        if (!txLocked) {
                            pTx->syncCharStyles();
                            canvas.pushUndoSnapshot();
                            uvState.gizmoMode = UvViewportState::GizmoDragMode::TouchCharMove;
                            uvState.gizmoDragStartUv = hoverUv;
                            uvState.gizmoOrigCharHOffset = pTx->charStyles[ci].hOffsetPx;
                            uvState.gizmoOrigCharBaseline = pTx->charStyles[ci].baselinePx;
                        }
                        consumed = true;
                        break;
                    }
                }
            }
            if (!consumed) {
                int pickedL = -1, pickedI = -1;
                if (findTopVectorTargetAtPoint(canvas, io.MousePos, vpCenter, uvState.viewCenterUv, uvState.pixelsPerUv,
                                               pickedL, pickedI)) {
                    canvas.activeLayerIdx = pickedL;
                    canvas.selectedItemIdx = pickedI;
                    state.textEditSyncedLayerId = 0;
                    resolveTransformTarget(tItem, tLeg, tLIdx);
                    pTx = (tItem && tItem->kind == VectorItemKind::Text) ? &tItem->text :
                          (tLeg && tLeg->kind == LayerKind::Text) ? &tLeg->text : nullptr;
                } else if (pTx) {
                    pTx->selectedCharIdx = -1;
                }
            }
        }

        if (pTx) {
            for (int ci = 0; ci < static_cast<int>(pTx->lastCharBoxesUv.size()); ++ci) {
                const CharBoxUv& b = pTx->lastCharBoxesUv[ci];
                ImVec2 sMin = uvToScreen(b.uvMin);
                ImVec2 sMax = uvToScreen(b.uvMax);
                if (sMax.x - sMin.x < 1.0f || sMax.y - sMin.y < 1.0f) continue;
                bool isSelChar = (ci == pTx->selectedCharIdx);
                dl->AddRect(sMin, sMax, isSelChar ? StudioColors::AmberAccent : IM_COL32(0x00, 0xB8, 0xFF, 110),
                            0.0f, isSelChar ? 1.8f : 1.0f);
                if (isSelChar) {
                    ImVec2 corners[4] = {sMin, {sMax.x, sMin.y}, sMax, {sMin.x, sMax.y}};
                    for (const ImVec2& cp : corners) {
                        dl->AddRectFilled(cp - ImVec2(3.5f, 3.5f), cp + ImVec2(3.5f, 3.5f), StudioColors::PanelBody);
                        dl->AddRect(cp - ImVec2(3.5f, 3.5f), cp + ImVec2(3.5f, 3.5f), StudioColors::AmberAccent, 0.0f, 1.3f);
                    }
                    ImVec2 topM((sMin.x + sMax.x) * 0.5f, sMin.y);
                    ImVec2 rotP(topM.x, topM.y - 16.0f);
                    dl->AddLine(topM, rotP, StudioColors::AmberAccent, 1.3f);
                    dl->AddCircleFilled(rotP, 4.0f, StudioColors::PanelBody, 12);
                    dl->AddCircle(rotP, 4.0f, StudioColors::AmberAccent, 12, 1.4f);
                }
            }

            if (uvState.gizmoMode == UvViewportState::GizmoDragMode::TouchCharMove ||
                uvState.gizmoMode == UvViewportState::GizmoDragMode::TouchCharScale ||
                uvState.gizmoMode == UvViewportState::GizmoDragMode::TouchCharRotate) {
                pTx->syncCharStyles();
                int ci = pTx->selectedCharIdx;
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && ci >= 0 &&
                    ci < static_cast<int>(pTx->charStyles.size())) {
                    CharStyle& cs = pTx->charStyles[ci];
                    float uvScale = std::max(0.25f, static_cast<float>(canvas.width) / 2048.0f);
                    if (uvState.gizmoMode == UvViewportState::GizmoDragMode::TouchCharMove) {
                        Vec2 dPx = (hoverUv - uvState.gizmoDragStartUv) * static_cast<float>(canvas.width);
                        cs.hOffsetPx = std::clamp(uvState.gizmoOrigCharHOffset + dPx.x / uvScale, -400.0f, 400.0f);
                        cs.baselinePx = std::clamp(uvState.gizmoOrigCharBaseline - dPx.y / uvScale, -400.0f, 400.0f);
                        char cHud[48];
                        std::snprintf(cHud, sizeof(cHud), "X %.0f  Base %.0f", cs.hOffsetPx, cs.baselinePx);
                        drawHudBadge(dl, io.MousePos, cHud, StudioColors::AmberAccent);
                    } else if (uvState.gizmoMode == UvViewportState::GizmoDragMode::TouchCharScale) {
                        ImVec2 ctr(0.0f, 0.0f);
                        if (ci < static_cast<int>(pTx->lastCharBoxesUv.size())) {
                            ctr = (uvToScreen(pTx->lastCharBoxesUv[ci].uvMin) + uvToScreen(pTx->lastCharBoxesUv[ci].uvMax)) * 0.5f;
                        }
                        float d0 = std::max(6.0f, length(toVec2(uvToScreen(uvState.gizmoDragStartUv) - ctr)));
                        float d1 = std::max(3.0f, length(toVec2(io.MousePos - ctr)));
                        cs.scale = std::clamp(uvState.gizmoOrigCharScale * (d1 / d0), 0.20f, 3.50f);
                        char cHud[32];
                        std::snprintf(cHud, sizeof(cHud), "%.0f%%", cs.scale * 100.0f);
                        drawHudBadge(dl, io.MousePos, cHud, StudioColors::AmberAccent);
                    } else {
                        ImVec2 ctr = (ci < static_cast<int>(pTx->lastCharBoxesUv.size()))
                                     ? (uvToScreen(pTx->lastCharBoxesUv[ci].uvMin) + uvToScreen(pTx->lastCharBoxesUv[ci].uvMax)) * 0.5f
                                     : io.MousePos;
                        ImVec2 d = io.MousePos - ctr;
                        float deg = std::atan2(d.y, d.x) * kRad2Deg + 90.0f;
                        if (io.KeyShift) deg = std::round(deg / 15.0f) * 15.0f;
                        while (deg > 180.0f) deg -= 360.0f;
                        while (deg < -180.0f) deg += 360.0f;
                        cs.rotDeg = deg;
                        char cHud[32];
                        std::snprintf(cHud, sizeof(cHud), "%.0f deg", deg);
                        drawHudBadge(dl, io.MousePos, cHud, StudioColors::AmberAccent);
                    }
                    double now = ImGui::GetTime();
                    if (now - uvState.lastLiveRasterTime >= 0.015) {
                        canvas.rasterizeLayer(tLIdx, &mesh);
                        if (callbacks.onCanvasModified) callbacks.onCanvasModified();
                        uvState.lastLiveRasterTime = now;
                    }
                } else {
                    canvas.rasterizeLayer(tLIdx, &mesh);
                    if (callbacks.onCanvasModified) callbacks.onCanvasModified();
                    uvState.gizmoMode = UvViewportState::GizmoDragMode::None;
                }
            }
        }
    }

    if (isTransformTool && !isTouchTypeTool && !uvState.typeBoxDrawing) {
        VectorItem* tItem = nullptr;
        Layer* tLeg = nullptr;
        int tLIdx = -1;
        if (resolveTransformTarget(tItem, tLeg, tLIdx)) {
            bool itemLocked = (activeLayer && activeLayer->locked) || (tItem && tItem->locked);
            Vec2 centerUv{}, halfUv{};
            float rotDeg = 0.0f;
            bool isText = false, isImage = false, isRect = false;
            bool isAreaTxt = false;
            bool isMirrored = false;
            if (tItem) {
                isText = (tItem->kind == VectorItemKind::Text);
                isImage = (tItem->kind == VectorItemKind::Image);
                isRect = (tItem->kind == VectorItemKind::RectShape);
                isAreaTxt = (isText && tItem->text.isAreaBox);
                computeItemCenterAndHalf(*tItem, canvas.width, canvas.height, centerUv, halfUv);
                rotDeg = isText ? tItem->text.rotationDeg : isImage ? tItem->image.rotationDeg : tItem->rect.rotationDeg;
                isMirrored = isText ? tItem->text.mirrorX : isImage ? tItem->image.mirrorX : tItem->rect.mirrorX;
            } else {
                isText = (tLeg->kind == LayerKind::Text);
                isImage = !isText;
                isAreaTxt = (isText && tLeg->text.isAreaBox);
                computeLegacyLayerCenterAndHalf(*tLeg, canvas.width, canvas.height, centerUv, halfUv);
                rotDeg = isText ? tLeg->text.rotationDeg : tLeg->decal.rotationDeg;
                isMirrored = isText ? (tLeg->text.mirrorX || tLeg->text.mirrorMode != MirrorTextMode::None)
                                    : (tLeg->decal.mirrorX || tLeg->decal.mirrorMode != MirrorTextMode::None);
            }

            Obb2D obb = buildScreenObb(centerUv, halfUv, rotDeg, vpCenter, uvState.viewCenterUv, uvState.pixelsPerUv);

            dl->AddPolyline(obb.corners, 4, StudioColors::GuideCyan, 1.3f, ImDrawFlags_Closed);
            ImVec2 topMid = (obb.corners[0] + obb.corners[1]) * 0.5f;
            dl->AddLine(topMid, obb.rotHandle, StudioColors::GuideCyan, 1.3f);
            dl->AddCircleFilled(obb.rotHandle, 4.5f, StudioColors::PanelBody, 12);
            dl->AddCircle(obb.rotHandle, 4.5f, StudioColors::AmberAccent, 12, 1.4f);

            for (int ci = 0; ci < 4; ++ci) {
                dl->AddRectFilled(obb.corners[ci] - ImVec2(4.0f, 4.0f), obb.corners[ci] + ImVec2(4.0f, 4.0f),
                                  StudioColors::PanelBody);
                dl->AddRect(obb.corners[ci] - ImVec2(4.0f, 4.0f), obb.corners[ci] + ImVec2(4.0f, 4.0f),
                            StudioColors::GuideCyan, 0.0f, 1.4f);
                if (isAreaTxt) {
                    ImVec2 edgeMid = (obb.corners[ci] + obb.corners[(ci + 1) & 3]) * 0.5f;
                    dl->AddRectFilled(edgeMid - ImVec2(3.5f, 3.5f), edgeMid + ImVec2(3.5f, 3.5f), StudioColors::AmberAccent);
                    dl->AddRect(edgeMid - ImVec2(3.5f, 3.5f), edgeMid + ImVec2(3.5f, 3.5f), StudioColors::PanelBody, 0.0f, 1.0f);
                }
            }

            if (isMirrored) {
                Obb2D mirObb = buildScreenObb(Vec2{1.0f - centerUv.x, centerUv.y}, halfUv, -rotDeg,
                                              vpCenter, uvState.viewCenterUv, uvState.pixelsPerUv);
                dl->AddPolyline(mirObb.corners, 4, IM_COL32(0x00, 0xB8, 0xFF, 90), 1.0f, ImDrawFlags_Closed);
            }

            if (isHovered && toolInputAllowed && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !itemLocked) {
                bool was3D = tItem
                             ? (isText ? tItem->text.anchoredIn3D : isImage ? tItem->image.anchoredIn3D : tItem->rect.anchoredIn3D)
                             : (isText ? tLeg->text.anchoredIn3D : tLeg->decal.anchoredIn3D);
                float origWorldSz = tItem
                                    ? (isText ? tItem->text.worldHeight : isImage ? tItem->image.worldWidth : tItem->rect.worldWidth)
                                    : (isText ? tLeg->text.worldHeight : tLeg->decal.worldWidth);
                float origWorldH = (tItem && isRect) ? tItem->rect.worldHeight : origWorldSz;

                if (length(toVec2(io.MousePos - obb.rotHandle)) <= 9.0f) {
                    canvas.pushUndoSnapshot();
                    uvState.gizmoMode = UvViewportState::GizmoDragMode::Rotate;
                    uvState.gizmoOrigRotDeg = rotDeg;
                    s_gizmoStartedAnchored3D = was3D;
                    s_gizmoOrigWorldSize = origWorldSz;
                    s_gizmoOrigWorldH = origWorldH;
                } else {
                    int hitEdge = -1;
                    if (isAreaTxt) {
                        for (int ci = 0; ci < 4; ++ci) {
                            ImVec2 edgeMid = (obb.corners[ci] + obb.corners[(ci + 1) & 3]) * 0.5f;
                            if (length(toVec2(io.MousePos - edgeMid)) <= 8.5f) {
                                hitEdge = ci; // 0=top, 1=right, 2=bottom, 3=left
                                break;
                            }
                        }
                    }
                    int hitCorner = -1;
                    for (int ci = 0; ci < 4 && hitEdge < 0; ++ci) {
                        if (length(toVec2(io.MousePos - obb.corners[ci])) <= 8.5f) {
                            hitCorner = ci;
                            break;
                        }
                    }
                    if (hitEdge >= 0) {
                        canvas.pushUndoSnapshot();
                        uvState.gizmoMode = UvViewportState::GizmoDragMode::BoxResizeEdge;
                        uvState.gizmoCornerIdx = hitEdge;
                        uvState.gizmoDragStartUv = hoverUv;
                        uvState.gizmoOrigUvPos = centerUv;
                        uvState.gizmoOrigBoxW = tItem ? tItem->text.boxWidthPx : tLeg->text.boxWidthPx;
                        uvState.gizmoOrigBoxH = tItem ? tItem->text.boxHeightPx : tLeg->text.boxHeightPx;
                    } else if (hitCorner >= 0) {
                        canvas.pushUndoSnapshot();
                        uvState.gizmoMode = UvViewportState::GizmoDragMode::ScaleCorner;
                        uvState.gizmoCornerIdx = hitCorner;
                        uvState.gizmoDragStartUv = hoverUv;
                        uvState.gizmoOrigUvScale = tItem
                                                   ? (isImage ? tItem->image.uvScale : isRect ? tItem->rect.uvScale : Vec2{0, 0})
                                                   : (isImage ? tLeg->decal.uvScale : Vec2{0, 0});
                        uvState.gizmoOrigFontSize = tItem ? (isText ? tItem->text.fontSizePx : 0.0f)
                                                          : (isText ? tLeg->text.fontSizePx : 0.0f);
                        s_gizmoStartedAnchored3D = was3D;
                        s_gizmoOrigWorldSize = origWorldSz;
                        s_gizmoOrigWorldH = origWorldH;
                    } else if (pointInObb(io.MousePos, obb)) {
                        canvas.pushUndoSnapshot();
                        uvState.gizmoMode = UvViewportState::GizmoDragMode::Move;
                        uvState.gizmoDragStartUv = hoverUv;
                        uvState.gizmoOrigUvPos = centerUv;
                        s_gizmoStartedAnchored3D = was3D;
                        s_gizmoOrigWorldSize = origWorldSz;
                        s_gizmoOrigWorldH = origWorldH;
                    }
                }
            }

            if (uvState.gizmoMode != UvViewportState::GizmoDragMode::None) {
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    if (uvState.gizmoMode == UvViewportState::GizmoDragMode::BoxResizeEdge && isAreaTxt) {
                        TextLayerProps& tx = tItem ? tItem->text : tLeg->text;
                        float uvScale = std::max(0.25f, static_cast<float>(canvas.width) / 2048.0f);
                        ImVec2 dScr = io.MousePos - uvToScreen(uvState.gizmoDragStartUv);
                        float dAlongR = (dScr.x * obb.axisR.x + dScr.y * obb.axisR.y) / std::max(1.0f, uvState.pixelsPerUv);
                        float dAlongD = (dScr.x * obb.axisD.x + dScr.y * obb.axisD.y) / std::max(1.0f, uvState.pixelsPerUv);
                        float dPxX = (dAlongR * canvas.width) / uvScale;
                        float dPxY = (dAlongD * canvas.height) / uvScale;
                        int edge = uvState.gizmoCornerIdx;
                        if (edge == 1 || edge == 3) {
                            float s = (edge == 1) ? 1.0f : -1.0f;
                            float newW = std::clamp(uvState.gizmoOrigBoxW + s * dPxX, 40.0f, 4096.0f);
                            float appliedU = ((newW - uvState.gizmoOrigBoxW) * uvScale * s * 0.5f) / canvas.width;
                            tx.boxWidthPx = newW;
                            tx.uvPos = uvState.gizmoOrigUvPos + Vec2{obb.axisR.x * appliedU, obb.axisR.y * appliedU};
                        } else {
                            float s = (edge == 2) ? 1.0f : -1.0f;
                            float newH = std::clamp(uvState.gizmoOrigBoxH + s * dPxY, 24.0f, 4096.0f);
                            float appliedV = ((newH - uvState.gizmoOrigBoxH) * uvScale * s * 0.5f) / canvas.height;
                            tx.boxHeightPx = newH;
                            tx.uvPos = uvState.gizmoOrigUvPos + Vec2{obb.axisD.x * appliedV, obb.axisD.y * appliedV};
                        }
                        tx.anchoredIn3D = false;
                        if (tItem) canvas.syncItem2DTo3D(*tItem, &mesh);
                        else canvas.syncLayer2DTo3D(tLIdx, &mesh);
                        char bHud[48];
                        std::snprintf(bHud, sizeof(bHud), "Box %.0f x %.0f", tx.boxWidthPx, tx.boxHeightPx);
                        drawHudBadge(dl, io.MousePos, bHud, StudioColors::AmberAccent);
                    } else if (uvState.gizmoMode == UvViewportState::GizmoDragMode::Move) {
                        Vec2 rawPos = uvState.gizmoOrigUvPos + (hoverUv - uvState.gizmoDragStartUv);
                        GuideSnapResult sn = snapUvToGuides(rawPos, uvState, state.showRulers);
                        Vec2 newPos = sn.uv;
                        if (sn.snappedXIdx >= 0 || sn.snappedCenterX) {
                            float sx = uvToScreen({newPos.x, 0.0f}).x;
                            dl->AddLine(ImVec2(sx, cMin.y), ImVec2(sx, cMax.y), IM_COL32(0x66, 0xDF, 0xFF, 230), 1.6f);
                        }
                        if (sn.snappedYIdx >= 0 || sn.snappedCenterY) {
                            float sy = uvToScreen({0.0f, newPos.y}).y;
                            dl->AddLine(ImVec2(cMin.x, sy), ImVec2(cMax.x, sy), IM_COL32(0x66, 0xDF, 0xFF, 230), 1.6f);
                        }
                        Vec3 p3, n3;
                        int32_t tri = -1;
                        bool hit3D = s_gizmoStartedAnchored3D && mesh.loaded &&
                                     mesh.queryUvAtPixel(newPos.x, newPos.y, p3, n3, tri);
                        if (tItem) {
                            Vec2* dstPos = isText ? &tItem->text.uvPos : isImage ? &tItem->image.uvPos : &tItem->rect.uvPos;
                            bool* dstAnc = isText ? &tItem->text.anchoredIn3D : isImage ? &tItem->image.anchoredIn3D : &tItem->rect.anchoredIn3D;
                            Vec3* dstP3  = isText ? &tItem->text.pos3D : isImage ? &tItem->image.pos3D : &tItem->rect.pos3D;
                            Vec3* dstN3  = isText ? &tItem->text.nrm3D : isImage ? &tItem->image.nrm3D : &tItem->rect.nrm3D;
                            *dstPos = newPos;
                            if (hit3D) {
                                *dstAnc = true;
                                *dstP3 = p3;
                                *dstN3 = n3;
                            } else {
                                *dstAnc = false;
                                canvas.syncItem2DTo3D(*tItem, &mesh);
                            }
                        } else if (tLeg) {
                            if (isText) {
                                tLeg->text.uvPos = newPos;
                                if (hit3D) {
                                    tLeg->text.anchoredIn3D = true;
                                    tLeg->text.pos3D = p3;
                                    tLeg->text.nrm3D = n3;
                                } else {
                                    tLeg->text.anchoredIn3D = false;
                                    canvas.syncLayer2DTo3D(tLIdx, &mesh);
                                }
                            } else {
                                tLeg->decal.uvPos = newPos;
                                if (hit3D) {
                                    tLeg->decal.anchoredIn3D = true;
                                    tLeg->decal.pos3D = p3;
                                    tLeg->decal.nrm3D = n3;
                                } else {
                                    tLeg->decal.anchoredIn3D = false;
                                    canvas.syncLayer2DTo3D(tLIdx, &mesh);
                                }
                            }
                        }
                    } else if (uvState.gizmoMode == UvViewportState::GizmoDragMode::Rotate) {
                        ImVec2 d = io.MousePos - obb.center;
                        float deg = std::atan2(d.y, d.x) * kRad2Deg + 90.0f;
                        if (io.KeyShift) deg = std::round(deg / 15.0f) * 15.0f;
                        while (deg > 180.0f) deg -= 360.0f;
                        while (deg < -180.0f) deg += 360.0f;
                        if (tItem) {
                            if (isText) tItem->text.rotationDeg = deg;
                            else if (isImage) tItem->image.rotationDeg = deg;
                            else tItem->rect.rotationDeg = deg;
                            if (!s_gizmoStartedAnchored3D) {
                                if (isText) tItem->text.anchoredIn3D = false;
                                else if (isImage) tItem->image.anchoredIn3D = false;
                                else tItem->rect.anchoredIn3D = false;
                                canvas.syncItem2DTo3D(*tItem, &mesh);
                            }
                        } else if (tLeg) {
                            if (isText) {
                                tLeg->text.rotationDeg = deg;
                                if (!s_gizmoStartedAnchored3D) {
                                    tLeg->text.anchoredIn3D = false;
                                    canvas.syncLayer2DTo3D(tLIdx, &mesh);
                                }
                            } else {
                                tLeg->decal.rotationDeg = deg;
                                if (!s_gizmoStartedAnchored3D) {
                                    tLeg->decal.anchoredIn3D = false;
                                    canvas.syncLayer2DTo3D(tLIdx, &mesh);
                                }
                            }
                        }
                        char rHud[32];
                        std::snprintf(rHud, sizeof(rHud), "%.1f deg", deg);
                        drawHudBadge(dl, io.MousePos, rHud, StudioColors::AmberAccent);
                    } else if (uvState.gizmoMode == UvViewportState::GizmoDragMode::ScaleCorner) {
                        float d0 = std::max(8.0f, length(toVec2(uvToScreen(uvState.gizmoDragStartUv) - obb.center)));
                        float d1 = std::max(4.0f, length(toVec2(io.MousePos - obb.center)));
                        float ratio = std::clamp(d1 / d0, 0.15f, 8.0f);
                        if (tItem) {
                            if (s_gizmoStartedAnchored3D) {
                                if (isText) {
                                    tItem->text.anchoredIn3D = true;
                                    tItem->text.worldHeight = std::clamp(s_gizmoOrigWorldSize * ratio, 0.03f, 2.50f);
                                } else if (isImage) {
                                    tItem->image.anchoredIn3D = true;
                                    tItem->image.worldWidth = std::clamp(s_gizmoOrigWorldSize * ratio, 0.05f, 3.50f);
                                } else {
                                    tItem->rect.anchoredIn3D = true;
                                    tItem->rect.worldWidth = std::clamp(s_gizmoOrigWorldSize * ratio, 0.02f, 3.50f);
                                    tItem->rect.worldHeight = std::clamp(s_gizmoOrigWorldH * ratio, 0.02f, 3.50f);
                                }
                            } else if (isText) {
                                tItem->text.fontSizePx = std::clamp(uvState.gizmoOrigFontSize * ratio, 12.0f, 512.0f);
                                tItem->text.anchoredIn3D = false;
                                canvas.syncItem2DTo3D(*tItem, &mesh);
                            } else {
                                Vec2* dstScale = isImage ? &tItem->image.uvScale : &tItem->rect.uvScale;
                                if ((isImage && state.decalLockAspect) || io.KeyShift || !io.KeyAlt) {
                                    *dstScale = Vec2{
                                        std::clamp(uvState.gizmoOrigUvScale.x * ratio, 0.01f, 1.5f),
                                        std::clamp(uvState.gizmoOrigUvScale.y * ratio, 0.01f, 1.5f)
                                    };
                                } else {
                                    ImVec2 deltaScr = io.MousePos - obb.center;
                                    float rx = std::fabs(deltaScr.x * obb.axisR.x + deltaScr.y * obb.axisR.y);
                                    float ry = std::fabs(deltaScr.x * obb.axisD.x + deltaScr.y * obb.axisD.y);
                                    *dstScale = Vec2{
                                        std::clamp((rx * 2.0f) / uvState.pixelsPerUv, 0.01f, 1.5f),
                                        std::clamp((ry * 2.0f) / uvState.pixelsPerUv, 0.01f, 1.5f)
                                    };
                                }
                                if (isImage) tItem->image.anchoredIn3D = false;
                                else tItem->rect.anchoredIn3D = false;
                                canvas.syncItem2DTo3D(*tItem, &mesh);
                            }
                        } else if (tLeg) {
                            if (s_gizmoStartedAnchored3D) {
                                if (isText) {
                                    tLeg->text.anchoredIn3D = true;
                                    tLeg->text.worldHeight = std::clamp(s_gizmoOrigWorldSize * ratio, 0.03f, 2.50f);
                                } else {
                                    tLeg->decal.anchoredIn3D = true;
                                    tLeg->decal.worldWidth = std::clamp(s_gizmoOrigWorldSize * ratio, 0.05f, 3.50f);
                                }
                            } else if (isText) {
                                tLeg->text.fontSizePx = std::clamp(uvState.gizmoOrigFontSize * ratio, 12.0f, 512.0f);
                                tLeg->text.anchoredIn3D = false;
                                canvas.syncLayer2DTo3D(tLIdx, &mesh);
                            } else {
                                if (state.decalLockAspect || !io.KeyAlt) {
                                    tLeg->decal.uvScale = Vec2{
                                        std::clamp(uvState.gizmoOrigUvScale.x * ratio, 0.02f, 1.5f),
                                        std::clamp(uvState.gizmoOrigUvScale.y * ratio, 0.02f, 1.5f)
                                    };
                                } else {
                                    ImVec2 deltaScr = io.MousePos - obb.center;
                                    float rx = std::fabs(deltaScr.x * obb.axisR.x + deltaScr.y * obb.axisR.y);
                                    float ry = std::fabs(deltaScr.x * obb.axisD.x + deltaScr.y * obb.axisD.y);
                                    tLeg->decal.uvScale = Vec2{
                                        std::clamp((rx * 2.0f) / uvState.pixelsPerUv, 0.02f, 1.5f),
                                        std::clamp((ry * 2.0f) / uvState.pixelsPerUv, 0.02f, 1.5f)
                                    };
                                }
                                tLeg->decal.anchoredIn3D = false;
                                canvas.syncLayer2DTo3D(tLIdx, &mesh);
                            }
                        }
                    }
                    double now = ImGui::GetTime();
                    if (now - uvState.lastLiveRasterTime >= 0.015) {
                        canvas.rasterizeLayer(tLIdx, &mesh);
                        if (callbacks.onCanvasModified) callbacks.onCanvasModified();
                        uvState.lastLiveRasterTime = now;
                    }
                } else {
                    canvas.rasterizeLayer(tLIdx, &mesh);
                    if (callbacks.onCanvasModified) callbacks.onCanvasModified();
                    uvState.gizmoMode = UvViewportState::GizmoDragMode::None;
                }
            }
        }
    }

    // -- FLOOD FILL (G) --
    if (state.activeTool == StudioTool::Fill && isHovered && toolInputAllowed &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (activeLayer && activeLayer->locked) {
            state.statusText = "Active layer is locked";
        } else {
            int fillLayerIdx = (activeLayer && activeLayer->kind == LayerKind::RasterPaint)
                               ? canvas.activeLayerIdx
                               : canvas.ensureEditableLayer();
            canvas.floodFillAtUV(fillLayerIdx, hoverUv, state.brush.color,
                                 state.floodFillTolerance, state.brush.mirrorX, &mesh);
            if (callbacks.onCanvasModified) callbacks.onCanvasModified();
        }
    }

    // -- EYEDROPPER (I) --
    if (state.activeTool == StudioTool::Eyedropper && isHovered && toolInputAllowed) {
        Color4u8 peek = canvas.sampleCompositeAtUV(hoverUv);
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (io.KeyAlt) state.secondaryColor = peek;
            else state.brush.color = peek;
            char msg[64];
            std::snprintf(msg, sizeof(msg), "Picked RGB(%d, %d, %d)", peek.r, peek.g, peek.b);
            state.statusText = msg;
        }
        ImVec2 swMin(io.MousePos.x + 14.0f, io.MousePos.y + 14.0f);
        ImVec2 swMax(swMin.x + 20.0f, swMin.y + 20.0f);
        dl->AddRectFilled(swMin, swMax, toImU32(peek), 2.0f);
        dl->AddRect(swMin, swMax, StudioColors::TextPrimary, 2.0f, 1.0f);
    }

    dl->PopClipRect();

    // -- 2D UV CANVAS RIGHT-CLICK CONTEXT MENU (only when RMB wasn't consumed by Pen anchor/handle/finish!) --
    static Vec2 s_ctxUv{0.5f, 0.5f};
    if (isHovered && !rmbConsumedByPen && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !overZoomOverlay) {
        s_ctxUv = hoverUv;
        ImGui::OpenPopup("##UvCanvasCtxMenu");
    }
    if (ImGui::BeginPopup("##UvCanvasCtxMenu")) {
        bool hasActItem = (canvas.activeVectorItem() != nullptr);
        if (ImGui::MenuItem("Copy", "Ctrl+C", false, hasActItem)) {
            if (callbacks.onCopy) callbacks.onCopy();
            else canvas.copySelectedItem();
        }
        if (ImGui::MenuItem("Cut", "Ctrl+X", false, hasActItem)) {
            if (callbacks.onCut) callbacks.onCut();
            else if (canvas.cutSelectedItem(&mesh) && callbacks.onCanvasModified) callbacks.onCanvasModified();
        }
        if (ImGui::MenuItem("Paste Here", "Ctrl+V")) {
            if (canvas.hasClipboardItem) {
                if (canvas.pasteClipboardItem(&mesh, &s_ctxUv, nullptr) >= 0 && callbacks.onCanvasModified) {
                    callbacks.onCanvasModified();
                }
            } else if (callbacks.onPaste) {
                callbacks.onPaste();
            } else {
                state.requestPasteClipboard = true;
            }
        }
        if (ImGui::MenuItem("Delete Selected", "Del", false, hasActItem)) {
            if (canvas.deleteSelectedItem(&mesh) && callbacks.onCanvasModified) callbacks.onCanvasModified();
        }
        if (hasActItem && ImGui::MenuItem("Toggle Mirror on Item")) {
            const VectorItem* vi = canvas.activeVectorItem();
            bool curM = (vi->kind == VectorItemKind::PenPath)   ? vi->pen.mirrorX :
                        (vi->kind == VectorItemKind::RectShape) ? vi->rect.mirrorX :
                        (vi->kind == VectorItemKind::Text)      ? vi->text.mirrorX : vi->image.mirrorX;
            canvas.setMirrorOnActiveItem(!curM, &mesh);
            if (callbacks.onCanvasModified) callbacks.onCanvasModified();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Fit Artboard", "Ctrl+0")) {
            uvFitToView(uvState, troughW, troughH);
        }
        ImGui::MenuItem("Show UV Wireframe", nullptr, &state.uvShowWireframe);
        ImGui::MenuItem("Show Pixel Grid", nullptr, &state.uvShowGridRulers);
        ImGui::EndPopup();
    }

    // 6. Illustrator Pixel Rulers (Ctrl+R) & Draggable Cyan Guide Pulling
    if (state.showRulers) {
        ImVec2 topRMin(cMin.x, winPos.y);
        ImVec2 topRMax(cMax.x, cMin.y);
        dl->AddRectFilled(topRMin, topRMax, StudioColors::PanelHeader);
        dl->AddLine(ImVec2(topRMin.x, topRMax.y), topRMax, StudioColors::Border, 1.0f);

        ImVec2 leftRMin(winPos.x, cMin.y);
        ImVec2 leftRMax(cMin.x, cMax.y);
        dl->AddRectFilled(leftRMin, leftRMax, StudioColors::PanelHeader);
        dl->AddLine(ImVec2(leftRMax.x, leftRMin.y), leftRMax, StudioColors::Border, 1.0f);

        // Corner px unit badge
        dl->AddRectFilled(winPos, cMin, StudioColors::PanelHeader);
        dl->AddRect(winPos, cMin, StudioColors::Border, 0.0f, 1.0f);
        dl->AddText(ImVec2(winPos.x + 8.0f, winPos.y + 3.0f), StudioColors::TextMuted, "px");

        // Drag out new guides from rulers
        ImGui::SetCursorScreenPos(topRMin);
        ImGui::InvisibleButton("##top_ruler_drag", topRMax - topRMin);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            uvState.guidesY.push_back(std::clamp(hoverUv.y, 0.0f, 1.0f));
            uvState.draggingGuideAxis = 2;
            uvState.draggingGuideIdx = static_cast<int>(uvState.guidesY.size()) - 1;
            uvState.guideDraggingFromRuler = true;
        } else if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            uvState.guidesY.clear();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip("Drag down for Horizontal Guide (Right-click clears)");
        }

        ImGui::SetCursorScreenPos(leftRMin);
        ImGui::InvisibleButton("##left_ruler_drag", leftRMax - leftRMin);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            uvState.guidesX.push_back(std::clamp(hoverUv.x, 0.0f, 1.0f));
            uvState.draggingGuideAxis = 1;
            uvState.draggingGuideIdx = static_cast<int>(uvState.guidesX.size()) - 1;
            uvState.guideDraggingFromRuler = true;
        } else if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            uvState.guidesX.clear();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip("Drag right for Vertical Guide (Right-click clears)");
        }

        if (uvState.draggingGuideAxis != 0) {
            bool backOverRuler = (uvState.draggingGuideAxis == 1 && io.MousePos.x <= cMin.x) ||
                                 (uvState.draggingGuideAxis == 2 && io.MousePos.y <= cMin.y);
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                if (uvState.draggingGuideAxis == 1 && uvState.draggingGuideIdx >= 0 &&
                    uvState.draggingGuideIdx < static_cast<int>(uvState.guidesX.size())) {
                    float gx = std::clamp(hoverUv.x, 0.0f, 1.0f);
                    uvState.guidesX[uvState.draggingGuideIdx] = gx;
                    dl->AddLine(ImVec2(io.MousePos.x, cMin.y), ImVec2(io.MousePos.x, cMax.y), StudioColors::GuideCyan, 1.8f);
                    char gHud[48];
                    if (backOverRuler) std::snprintf(gHud, sizeof(gHud), "Drop on ruler to remove");
                    else std::snprintf(gHud, sizeof(gHud), "X: %.0f px", gx * canvas.width);
                    drawHudBadge(dl, io.MousePos, gHud, StudioColors::GuideCyan);
                } else if (uvState.draggingGuideAxis == 2 && uvState.draggingGuideIdx >= 0 &&
                           uvState.draggingGuideIdx < static_cast<int>(uvState.guidesY.size())) {
                    float gy = std::clamp(hoverUv.y, 0.0f, 1.0f);
                    uvState.guidesY[uvState.draggingGuideIdx] = gy;
                    dl->AddLine(ImVec2(cMin.x, io.MousePos.y), ImVec2(cMax.x, io.MousePos.y), StudioColors::GuideCyan, 1.8f);
                    char gHud[48];
                    if (backOverRuler) std::snprintf(gHud, sizeof(gHud), "Drop on ruler to remove");
                    else std::snprintf(gHud, sizeof(gHud), "Y: %.0f px", gy * canvas.height);
                    drawHudBadge(dl, io.MousePos, gHud, StudioColors::GuideCyan);
                }
            } else {
                // Erase guide if dragged back onto ruler (unless simple click on empty ruler -> discard empty drag)
                if (backOverRuler) {
                    if (uvState.draggingGuideAxis == 1 && uvState.draggingGuideIdx >= 0 &&
                        uvState.draggingGuideIdx < static_cast<int>(uvState.guidesX.size())) {
                        uvState.guidesX.erase(uvState.guidesX.begin() + uvState.draggingGuideIdx);
                    } else if (uvState.draggingGuideAxis == 2 && uvState.draggingGuideIdx >= 0 &&
                               uvState.draggingGuideIdx < static_cast<int>(uvState.guidesY.size())) {
                        uvState.guidesY.erase(uvState.guidesY.begin() + uvState.draggingGuideIdx);
                    }
                }
                uvState.draggingGuideAxis = 0;
                uvState.draggingGuideIdx = -1;
                uvState.guideDraggingFromRuler = false;
            }
        }

        float pxScreen = uvState.pixelsPerUv / std::max(1, canvas.width);
        int labelStep = 512;
        if (pxScreen * 128.0f >= 64.0f) labelStep = 128;
        else if (pxScreen * 256.0f >= 64.0f) labelStep = 256;
        else if (pxScreen * 512.0f >= 64.0f) labelStep = 512;
        else labelStep = 1024;
        int subStep = std::max(32, labelStep / 4);

        dl->PushClipRect(topRMin, topRMax, true);
        for (int p = 0; p <= canvas.width; p += subStep) {
            float sx = uvToScreen({static_cast<float>(p) / canvas.width, 0.0f}).x;
            bool major = (p % labelStep) == 0;
            dl->AddLine(ImVec2(sx, topRMax.y - (major ? 6.0f : 3.0f)), ImVec2(sx, topRMax.y),
                        major ? StudioColors::TextSecondary : StudioColors::TextMuted, 1.0f);
            if (major) {
                char lbl[16];
                std::snprintf(lbl, sizeof(lbl), "%d", p);
                float tw = ImGui::CalcTextSize(lbl).x;
                float tx = sx + 3.0f;
                if (tx + tw + 3.0f > topRMax.x) tx = topRMax.x - tw - 3.0f;
                if (tx >= topRMin.x + 2.0f) {
                    dl->AddText(ImVec2(tx, topRMin.y + 2.0f), StudioColors::TextSecondary, lbl);
                }
            }
        }
        if (isHovered || uvState.draggingGuideAxis == 1) {
            dl->AddLine(ImVec2(io.MousePos.x, topRMin.y), ImVec2(io.MousePos.x, topRMax.y),
                        StudioColors::GuideCyan, 1.5f);
        }
        dl->PopClipRect();

        dl->PushClipRect(leftRMin, leftRMax, true);
        for (int p = 0; p <= canvas.height; p += subStep) {
            float sy = uvToScreen({0.0f, static_cast<float>(p) / canvas.height}).y;
            bool major = (p % labelStep) == 0;
            dl->AddLine(ImVec2(leftRMax.x - (major ? 6.0f : 3.0f), sy), ImVec2(leftRMax.x, sy),
                        major ? StudioColors::TextSecondary : StudioColors::TextMuted, 1.0f);
            if (major) {
                char lbl[16];
                std::snprintf(lbl, sizeof(lbl), "%d", p);
                ImVec2 ts = ImGui::CalcTextSize(lbl);
                float tx = leftRMax.x - ts.x - 7.0f;
                float ty = sy + 2.0f;
                if (ty + ts.y + 2.0f > leftRMax.y) ty = leftRMax.y - ts.y - 2.0f;
                if (ty >= leftRMin.y + 1.0f) {
                    dl->AddText(ImVec2(std::max(leftRMin.x + 2.0f, tx), ty), StudioColors::TextSecondary, lbl);
                }
            }
        }
        if (isHovered || uvState.draggingGuideAxis == 2) {
            dl->AddLine(ImVec2(leftRMin.x, io.MousePos.y), ImVec2(leftRMax.x, io.MousePos.y),
                        StudioColors::GuideCyan, 1.5f);
        }
        dl->PopClipRect();
    }

    // 7. Illustrator-style Bottom-Left Zoom Pill (e.g. "14% v") + Hover Pixel Coords
    ImGui::SetCursorScreenPos(zpPos);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 2.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, StudioColors::ToVec4(StudioColors::PanelHeader));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, StudioColors::ToVec4(StudioColors::ControlHover));
    if (ImGui::Button(zoomPillLbl, zpSize)) {
        ImGui::OpenPopup("##UvZoomPopup");
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("2D Canvas Zoom Presets");
    }
    if (ImGui::BeginPopup("##UvZoomPopup")) {
        if (ImGui::MenuItem("Fit Artboard", "Ctrl+0")) {
            uvFitToView(uvState, troughW, troughH);
        }
        ImGui::Separator();
        for (int pct : {25, 50, 100, 200}) {
            char pl[24];
            std::snprintf(pl, sizeof(pl), "%d%%%s", pct, pct == 100 ? " (1:1)" : "");
            if (ImGui::MenuItem(pl, nullptr, std::abs(zoomPct - pct) <= 1)) {
                uvState.pixelsPerUv = std::max(64.0f, canvas.width * (pct * 0.01f));
                uvState.viewCenterUv = Vec2{0.5f, 0.5f};
            }
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();

    if (state.hoverUvValid && cSize.x > 240.0f) {
        char buf[48];
        int px = std::clamp(static_cast<int>(state.lastHoverUv.x * canvas.width), 0, canvas.width - 1);
        int py = std::clamp(static_cast<int>(state.lastHoverUv.y * canvas.height), 0, canvas.height - 1);
        std::snprintf(buf, sizeof(buf), "%d, %d px", px, py);
        dl->AddText(ImVec2(zpPos.x + zpSize.x + 10.0f, zpPos.y + 2.0f), StudioColors::TextSecondary, buf);
    }
}

} // namespace lps
