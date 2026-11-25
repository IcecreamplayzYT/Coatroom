#pragma once

#include "math/geom.hpp"
#include "mesh/obj_mesh.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lps {

enum class LayerKind {
    RasterPaint = 0,
    Text = 1,
    Decal = 2,
    RulerStripe = 3,
};

enum class BlendMode {
    Normal = 0,
    Multiply = 1,
    Screen = 2,
    Overlay = 3,
};

enum class MirrorTextMode {
    None = 0,
    ReadableBothSides = 1, // mirrors L<->R 3D position while keeping left->right glyph/decal orientation!
    TrueFlip = 2,          // strict geometric mirror
};

struct Color4u8 {
    uint8_t r = 255, g = 255, b = 255, a = 255;
};

struct BrushSettings {
    float radiusPx = 24.0f;   // radius in canvas pixels OR projected 3D brush world size
    float worldRadius = 0.08f; // meters when painting directly in 3D projection mode
    float hardness = 0.85f;
    float opacity = 1.0f;
    float spacing = 0.18f;
    Color4u8 color{205, 232, 32, 255};
    bool eraser = false;
    bool mirrorX = true;
    bool project3DBrush = true; // true = project cylinder around hit normal across seams; false = UV circle
    bool maskSelectedFacesOnly = false;
};

struct StripeNode {
    Vec2 uv{0.5f, 0.5f};
    Vec2 handleInUv{0.5f, 0.5f};
    Vec2 handleOutUv{0.5f, 0.5f};
    Vec3 pos3D{0, 0, 0};
    Vec3 handleIn3D{0, 0, 0};
    Vec3 handleOut3D{0, 0, 0};
    Vec3 nrm3D{0, 1, 0};
    bool has3D = false;
    bool hasHandles = false;
    bool smooth = true;
};

struct TextLayerProps {
    std::string text = "Lorem ipsum";
    std::string fontPath;
    float fontSizePx = 110.0f; // height relative to 2048/4096 space or UV/3D scale
    float worldHeight = 0.22f; // when placed in 3D surface tangent space
    float tracking = 0.0f;
    float italicShear = 0.0f;
    float outlineWidth = 0.0f;
    Color4u8 fillColor{245, 247, 250, 255};
    Color4u8 outlineColor{16, 18, 22, 255};
    Vec2 uvPos{0.5f, 0.5f};
    Vec3 pos3D{0, 0, 0};
    Vec3 nrm3D{1, 0, 0};
    Vec3 right3D{0, 0, 1};
    bool anchoredIn3D = false;
    float rotationDeg = 0.0f;
    MirrorTextMode mirrorMode = MirrorTextMode::ReadableBothSides;
    bool mirrorX = false;
};

struct DecalLayerProps {
    std::string sourcePath;
    int imgW = 0, imgH = 0;
    std::vector<uint8_t> imgRgba; // original imported decal bitmap (legacy/direct)
    std::shared_ptr<const std::vector<uint8_t>> imgRgbaShared; // copy-on-write shared bitmap
    Vec2 uvPos{0.5f, 0.5f};
    Vec2 uvScale{0.18f, 0.18f};
    Vec3 pos3D{0, 0, 0};
    Vec3 nrm3D{1, 0, 0};
    Vec3 right3D{0, 0, 1};
    float worldWidth = 0.45f;
    bool anchoredIn3D = false;
    float rotationDeg = 0.0f;
    Color4u8 tint{255, 255, 255, 255};
    MirrorTextMode mirrorMode = MirrorTextMode::ReadableBothSides;
    bool mirrorX = false;

    const std::vector<uint8_t>& imagePixels() const {
        if (imgRgbaShared && !imgRgbaShared->empty()) return *imgRgbaShared;
        return imgRgba;
    }
};

struct RulerStripeProps {
    std::vector<StripeNode> nodes;
    float widthPx = 18.0f;
    float worldWidth = 0.045f;
    float endTaper = 0.0f;   // 0..1 taper thickness at endpoints for speed-blade look!
    bool smoothCurve = false; // false = sharp straight ruler segments, true = Catmull-Rom smooth blade
    bool use3DSpace = true;   // project cleanly across UV seams along surface!
    bool mirrorX = true;
    Color4u8 color{232, 236, 242, 255};
    Color4u8 outlineColor{15, 17, 21, 255};
    float outlineWidth = 0.0f;
    bool closed = false;
    bool filled = false;
    bool hasStroke = true;
    Color4u8 fillColor{205, 232, 32, 255};
};

struct RectShapeProps {
    Vec2 uvPos{0.5f, 0.5f};
    Vec2 uvScale{0.15f, 0.15f};
    Vec3 pos3D{0, 0, 0};
    Vec3 nrm3D{0, 1, 0};
    Vec3 right3D{1, 0, 0};
    float worldWidth = 0.25f;
    float worldHeight = 0.25f;
    float rotationDeg = 0.0f;
    float cornerRadiusPx = 0.0f;
    float strokeWidthPx = 4.0f;
    Color4u8 fillColor{255, 255, 255, 255};
    Color4u8 strokeColor{18, 18, 22, 255};
    bool filled = true;
    bool hasStroke = false;
    bool anchoredIn3D = false;
    bool mirrorX = false;
};

enum class VectorItemKind {
    PenPath = 0,
    RectShape = 1,
    Text = 2,
    Image = 3,
};

struct VectorItem {
    uint32_t id = 0;
    std::string name;
    VectorItemKind kind = VectorItemKind::PenPath;
    bool visible = true;
    bool locked = false;
    Vec2 primaryUvCenter{0.5f, 0.5f};
    Vec2 primaryUvHalf{0.0f, 0.0f};
    bool hasPrimaryUvBox = false;
    RulerStripeProps pen;
    RectShapeProps rect;
    TextLayerProps text;
    DecalLayerProps image;
};

struct FontFaceInfo {
    std::string displayName;
    std::string filePath;
};

struct DirtyRect {
    int x0 = 0;
    int y0 = 0;
    int x1 = -1;
    int y1 = -1;
    bool valid = false;

    void reset() {
        x0 = 0; y0 = 0; x1 = -1; y1 = -1;
        valid = false;
    }
    void expand(int x, int y) {
        if (!valid) { x0 = x1 = x; y0 = y1 = y; valid = true; return; }
        if (x < x0) x0 = x;
        if (y < y0) y0 = y;
        if (x > x1) x1 = x;
        if (y > y1) y1 = y;
    }
    void expandRect(int rx0, int ry0, int rx1, int ry1) {
        if (rx0 > rx1 || ry0 > ry1) return;
        if (!valid) { x0 = rx0; y0 = ry0; x1 = rx1; y1 = ry1; valid = true; return; }
        if (rx0 < x0) x0 = rx0;
        if (ry0 < y0) y0 = ry0;
        if (rx1 > x1) x1 = rx1;
        if (ry1 > y1) y1 = ry1;
    }
};

struct Layer {
    uint32_t id = 0;
    std::string name;
    LayerKind kind = LayerKind::RasterPaint;
    BlendMode blend = BlendMode::Normal;
    float opacity = 1.0f;
    bool visible = true;
    bool locked = false;

    // Paint bitmap (also acts as cached rasterized output for vector layers & container layers)
    std::vector<uint8_t> rgba; // width * height * 4
    std::shared_ptr<std::vector<uint8_t>> paintBuffer;
    std::vector<VectorItem> items;
    bool dirtyVector = true;
    int prevMinX = 0, prevMinY = 0, prevMaxX = -1, prevMaxY = -1;
    bool hasPrevBounds = false;
    Vec2 primaryUvCenter{0.5f, 0.5f};
    Vec2 primaryUvHalf{0.09f, 0.09f};
    bool hasPrimaryUvBox = false;

    TextLayerProps text;
    DecalLayerProps decal;
    RulerStripeProps ruler;

    std::vector<uint8_t>& ensureUniquePaintBuffer(int w, int h) {
        const size_t expected = static_cast<size_t>(std::max(1, w)) * std::max(1, h) * 4;
        if (!paintBuffer) {
            paintBuffer = std::make_shared<std::vector<uint8_t>>(expected, 0);
        } else if (paintBuffer.use_count() > 1 || paintBuffer->size() != expected) {
            auto copy = std::make_shared<std::vector<uint8_t>>(*paintBuffer);
            copy->resize(expected, 0);
            paintBuffer = std::move(copy);
        }
        return *paintBuffer;
    }
};

struct LayerSnapshot {
    uint32_t id = 0;
    std::string name;
    LayerKind kind = LayerKind::RasterPaint;
    BlendMode blend = BlendMode::Normal;
    float opacity = 1.0f;
    bool visible = true;
    bool locked = false;
    Vec2 primaryUvCenter{0.5f, 0.5f};
    Vec2 primaryUvHalf{0.09f, 0.09f};
    bool hasPrimaryUvBox = false;
    TextLayerProps text;
    DecalLayerProps decal;
    RulerStripeProps ruler;
    std::shared_ptr<std::vector<uint8_t>> paintBuffer;
    std::vector<VectorItem> items;
};

struct DocumentSnapshot {
    int activeLayerIdx = 0;
    int selectedItemIdx = -1;
    std::vector<LayerSnapshot> layers;
};

// Render Coatroom Adobe-style mathematical squircle logo into RGBA bitmap
std::vector<uint8_t> renderBrandLogoRgba(int sizePx);

class CanvasEngine {
public:
    static constexpr size_t kMaxUndoSteps = 200;

    void init(int size, Color4u8 bgColor = {22, 24, 29, 255});
    void resizeCanvas(int newSize);

    // Layer management (container Layer + VectorItem model)
    int addLayer(const std::string& name, bool fillBg = false, Color4u8 bg = {0, 0, 0, 0});
    int addRasterLayer(const std::string& name, bool fillBg = false, Color4u8 bg = {0, 0, 0, 0});
    int addTextLayer(const std::string& name);
    int addDecalLayer(const std::string& name, const std::string& imgPath);
    int addRulerStripeLayer(const std::string& name);
    bool removeLayer(int index);
    void deleteLayer(int index);
    void moveLayer(int index, int delta);
    void reorderLayer(int fromIdx, int toIdx);
    int duplicateLayer(int index);
    bool mergeDown(int index);
    bool rasterizeLayerToPaint(int index, const ObjMesh* mesh = nullptr);
    void syncLayer3DTo2D(int layerIdx, const ObjMesh* mesh);
    void syncLayer2DTo3D(int layerIdx, const ObjMesh* mesh);

    // VectorItem container helpers (add into active editable layer without spawning layers)
    int ensureEditableLayer();
    VectorItem* activeVectorItem();
    const VectorItem* activeVectorItem() const;
    VectorItem* getVectorItem(int layerIdx, int itemIdx);
    const VectorItem* getVectorItem(int layerIdx, int itemIdx) const;
    int addPenItem(const std::string& name, const RulerStripeProps& props, const ObjMesh* mesh = nullptr);
    int addRectItem(const std::string& name, const RectShapeProps& props, const ObjMesh* mesh = nullptr);
    int addTextItem(const std::string& name, const TextLayerProps& props, const ObjMesh* mesh = nullptr);
    int addImageItemFromRgba(const std::string& name, std::vector<uint8_t> rgba, int w, int h,
                             const DecalLayerProps& props, const ObjMesh* mesh = nullptr);
    void syncItem3DTo2D(VectorItem& item, const ObjMesh* mesh);
    void syncItem2DTo3D(VectorItem& item, const ObjMesh* mesh);

    // Clipboard & Item Actions (with Undo)
    bool copySelectedItem();
    bool cutSelectedItem(const ObjMesh* mesh = nullptr);
    int pasteClipboardItem(const ObjMesh* mesh = nullptr, const Vec2* optUv = nullptr, const RayHit* optHit3D = nullptr);
    int pasteExternalImageRgba(const std::string& name, std::vector<uint8_t> rgba, int w, int h,
                               const ObjMesh* mesh = nullptr, const Vec2* optUv = nullptr, const RayHit* optHit3D = nullptr);
    bool deleteSelectedItem(const ObjMesh* mesh = nullptr);
    void setMirrorOnActiveItem(bool mirror, const ObjMesh* mesh = nullptr);

    // Base texture / AO shading overlay / PNG I/O
    bool loadBaseTexture(const std::string& path);
    bool loadAoOverlay(const std::string& path, bool makeVisible = true);
    bool bakeMeshAoOverlay(const ObjMesh& mesh, int res = 1024, bool makeVisible = true);
    int findAoLayerIndex() const;
    bool exportFlattenedPng(const std::string& path, int outSize, const ObjMesh* optDilateMesh = nullptr);

    // Font scanning from C:\Windows\Fonts
    void scanSystemFonts();
    const std::vector<FontFaceInfo>& fonts() const { return systemFonts; }

    // Painting tools
    void beginStroke(int layerIdx);
    void paintDabAt3D(int layerIdx, const ObjMesh& mesh, const RayHit& hit, const BrushSettings& bs,
                      const RayHit* prevHit = nullptr);
    void paintDabAtUV(int layerIdx, const ObjMesh* mesh, Vec2 uv, const BrushSettings& bs,
                      const Vec2* prevUv = nullptr);
    void endStroke(int layerIdx);

    void floodFillAtUV(int layerIdx, Vec2 uv, Color4u8 fillCol, float tolerance, bool mirrorX, const ObjMesh* mesh);
    Color4u8 sampleCompositeAtUV(Vec2 uv) const;

    // Vector Layer & Container Item Rasterization
    void rasterizeLayer(int layerIdx, const ObjMesh* mesh);
    void rasterizeVectorLayer(int layerIdx, const ObjMesh* mesh);
    void rasterizeAllDirty(const ObjMesh* mesh);

    // 200-step Copy-on-Write Undo / Redo for all document edits
    void pushUndoSnapshot();
    void undo(const ObjMesh* mesh = nullptr);
    void redo(const ObjMesh* mesh = nullptr);
    bool canUndo() const { return !undoStack.empty(); }
    bool canRedo() const { return !redoStack.empty(); }

    // Composite all visible layers into compositeRgba and mark GPU upload dirty
    void recomposite();

    DirtyRect consumeDirtyRect(bool& outFullUpload) {
        DirtyRect r = dirtyRect;
        outFullUpload = fullUpload;
        dirtyRect.reset();
        fullUpload = false;
        return r;
    }

    int width = 2048;
    int height = 2048;
    int activeLayerIdx = 0;
    int selectedItemIdx = -1;
    std::vector<Layer> layers;
    std::vector<uint8_t> compositeRgba;
    bool gpuDirty = true;
    DirtyRect dirtyRect;
    bool fullUpload = true;

    bool hasClipboardItem = false;
    VectorItem clipboardItem;

    // Save/Load native editable .cr (CRM1) & legacy .lps (LPS1/LPS2) projects!
    bool saveProject(const std::string& path, const std::string& meshPath) const;
    bool loadProject(const std::string& path, std::string& outMeshPath);

private:
    int upsertTopAoLayer(bool makeVisible);
    DocumentSnapshot captureSnapshot() const;
    void restoreSnapshot(const DocumentSnapshot& snap, const ObjMesh* mesh);
    void stampUvCircle(std::vector<uint8_t>& buf, Vec2 uv, float radiusPx, float hardness, Color4u8 col,
                       float opacity, bool eraser, const ObjMesh* maskMesh, bool maskFacesOnly);
    void stampSurfaceBrush3D(Layer& layer, const ObjMesh& mesh, const Vec3& centerPos, const Vec3& centerNrm,
                             const BrushSettings& bs);
    void projectBitmap3D(Layer& layer, const ObjMesh& mesh, const uint8_t* srcRgba, int srcW, int srcH,
                         const Vec3& centerPos, const Vec3& centerNrm, const Vec3& rightHint,
                         float worldW, float worldH, float rotDeg, bool flipX, Color4u8 tint,
                         bool mirrorSecondPass, MirrorTextMode mirrorMode, DirtyRect* outDirty = nullptr,
                         Vec2* outPrimaryUvCenter = nullptr, Vec2* outPrimaryUvHalf = nullptr,
                         bool* outHasPrimaryUvBox = nullptr);

    uint32_t nextLayerId = 1;
    uint32_t nextItemId = 1;
    const ObjMesh* cachedMesh = nullptr;
    std::vector<FontFaceInfo> systemFonts;
    DocumentSnapshot pendingStrokeSnap;
    int strokeLayerIdx = -1;
    std::vector<DocumentSnapshot> undoStack;
    std::vector<DocumentSnapshot> redoStack;
};

} // namespace lps
