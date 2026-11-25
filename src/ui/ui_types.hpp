#pragma once

#include "math/geom.hpp"
#include "mesh/obj_mesh.hpp"
#include "paint/canvas_engine.hpp"
#include "imgui.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace lps {

enum class StudioTool {
    SelectMove = 0,
    NodeEdit,
    Orbit,
    Brush,
    StraightRuler,
    CurveBlade,
    RectBox,
    Eraser,
    FloodFill,
    Eyedropper,
    Text,
    SponsorDecal,
    VertexEdit,
    FacePaintMask,
    HandPan,
    Zoom,
    BeautyRender,
    TouchType,
    COUNT,
    // Clean Coatroom v1.2 Adobe tool aliases (share underlying enum indices)
    Select = SelectMove,
    DirectSelect = NodeEdit,
    Line = StraightRuler,
    Pen = CurveBlade,
    Rect = RectBox,
    Fill = FloodFill,
    Image = SponsorDecal,
    Hand = HandPan
};

enum class StudioMode {
    TexturePaint = 0,
    Object = 1,
    EditMesh = 2
};

enum class ShadingMode {
    Wireframe = 0,
    Clay = 1,
    Material = 2,
    Rendered = 3
};

enum class CameraPreset {
    Front = 0,
    Side = 1,
    Top = 2,
    Rear = 3,
    ThreeQuarterBeauty = 4
};

enum class EditSubTool {
    Grab = 0,
    Rotate = 1,
    Scale = 2
};

enum class WorkspaceTab {
    Layout = 0,
    UvEditing,
    TexturePaint,
    Shading,
    Rendering
};

enum class RightPanelTab {
    Color = 0,
    Properties,
    Outliner
};

enum class PropertiesCategory {
    Tool = 0,
    Text,
    Decal,
    Ruler,
    MeshEdit,
    Material,
    Lights
};

struct StudioLightUi {
    std::string label;
    bool enabled = true;
    Vec3 position{3.0f, 3.5f, 3.0f};
    Vec3 color{1.0f, 0.98f, 0.95f};
    float intensity = 2.0f;
};

struct MaterialUiSettings {
    float metallic = 0.14f;
    float roughness = 0.22f;
    float clearcoat = 0.94f;
    float clearcoatRoughness = 0.04f;
    float envIntensity = 1.0f;
    bool showFloorGrid = true;
    bool showSymmetryPlane = false;
};

struct UvViewportState {
    Vec2 viewCenterUv{0.5f, 0.5f};
    float pixelsPerUv = 512.0f;
    bool needsInitialFit = true;

    // Interactive painting state
    bool isPainting = false;
    Vec2 prevStrokeUv{0.5f, 0.5f};

    // Interactive ruler/Bezier pen & node editing state
    bool rulerDrawing = false;
    bool rulerDraggingNewSegment = false;
    Vec2 rulerDragStartUv{0.5f, 0.5f};
    int rulerDragNodeIdx = -1;
    int penDragMode = 0; // 0=none, 1=pulling handleOut/handleIn, 2=move anchor, 3=move handleIn, 4=move handleOut
    int penDragNodeIdx = -1;
    Vec2 penAnchorDownUv{0.5f, 0.5f};
    Vec2 penPrevDragUv{0.5f, 0.5f};

    // Interactive RectBox & Type Area Box tool states
    bool rectDrawing = false;
    Vec2 rectStartUv{0.5f, 0.5f};
    Vec2 rectEndUv{0.5f, 0.5f};
    bool typeBoxDrawing = false;
    Vec2 typeBoxStartUv{0.5f, 0.5f};
    Vec2 typeBoxEndUv{0.5f, 0.5f};

    // Illustrator-style draggable ruler guides (normalized UV positions)
    std::vector<float> guidesX;
    std::vector<float> guidesY;
    int draggingGuideAxis = 0; // 0=none, 1=X(vertical guide), 2=Y(horizontal guide)
    int draggingGuideIdx = -1;
    bool guideDraggingFromRuler = false;

    // Interactive Text / Decal gizmo state
    enum class GizmoDragMode {
        None = 0,
        Move,
        ScaleCorner,
        Rotate,
        BoxResizeEdge,
        TouchCharMove,
        TouchCharScale,
        TouchCharRotate
    };
    GizmoDragMode gizmoMode = GizmoDragMode::None;
    int gizmoCornerIdx = -1;
    Vec2 gizmoDragStartUv{0.5f, 0.5f};
    Vec2 gizmoOrigUvPos{0.5f, 0.5f};
    Vec2 gizmoOrigUvScale{0.18f, 0.18f};
    float gizmoOrigFontSize = 110.0f;
    float gizmoOrigRotDeg = 0.0f;
    float gizmoOrigBoxW = 420.0f;
    float gizmoOrigBoxH = 160.0f;
    float gizmoOrigCharScale = 1.0f;
    float gizmoOrigCharBaseline = 0.0f;
    float gizmoOrigCharHOffset = 0.0f;
    double lastLiveRasterTime = 0.0;
};

struct RecentProjectEntry {
    std::string name;
    std::string path;
    std::string kind;      // ".cr" or ".obj"
    std::string detail;    // e.g. "4096x4096 | Modified ..."
    std::string thumbPath; // cached Front 3/4th beauty thumbnail PNG on disk
    ImTextureID thumbTex = 0;
};

struct StudioUiState {
    StudioTool activeTool = StudioTool::Brush;
    StudioMode activeMode = StudioMode::TexturePaint;
    ShadingMode shadingMode = ShadingMode::Rendered;
    EditSubTool editSubTool = EditSubTool::Grab;

    WorkspaceTab activeWorkspace = WorkspaceTab::Layout;
    RightPanelTab activeRightTab = RightPanelTab::Properties;
    PropertiesCategory activePropsCat = PropertiesCategory::Tool;

    BrushSettings brush;
    Color4u8 secondaryColor{22, 24, 28, 255}; // Illustrator-style Stroke / Outline swatch
    MaterialUiSettings material;
    StudioLightUi lights[3] = {
        {"Key Softbox",   true, { 3.4f, 3.8f,  3.2f}, {1.00f, 0.97f, 0.93f}, 2.4f},
        {"Rim Strip",     true, {-3.6f, 2.5f, -3.4f}, {0.78f, 0.88f, 1.00f}, 1.8f},
        {"Top Diffuser",  true, { 0.0f, 4.8f,  0.4f}, {0.96f, 0.98f, 1.00f}, 1.3f},
    };

    // Viewport textures (provided by renderer via app shell)
    ImTextureID viewport3DTex = 0;
    ImTextureID canvas2DTex = 0;

    // 3D viewport tracking updated each frame by panels.cpp
    bool viewport3DHovered = false;
    bool viewport3DActive = false;
    Vec2 viewport3DPanelMin{0.0f, 0.0f};
    Vec2 viewport3DPanelSize{800.0f, 600.0f};

    // Camera basis for Blender XYZ Axis Gizmo (updated optionally by app shell)
    Vec3 camRight{1.0f, 0.0f, 0.0f};
    Vec3 camUp{0.0f, 1.0f, 0.0f};
    Vec3 camForward{0.0f, 0.0f, 1.0f};

    // 2D UV Canvas & Rulers toggles
    bool showRulers = true;        // Toggled by Ctrl+R / View > Show Rulers
    bool uvShowWireframe = true;
    bool uvShowGridRulers = true;  // Pixel grid inside UV canvas
    int canvasResolution = 4096;

    // Tool-specific UI parameters
    float floodFillTolerance = 0.08f;
    float rectStrokeWidthPx = 0.0f; // 0 = filled plate, >0 = stroked frame
    float vertexSelectRadius = 0.18f;
    float faceMaskAngleDeg = 28.0f;
    bool faceMaskConnectedAngle = true;
    float nudgeStep = 0.01f;
    float scaleStepFactor = 1.03f;
    float smoothStrength = 0.5f;
    int smoothIterations = 2;
    bool decalLockAspect = true;
    bool isOrthoCamera = false;
    bool show3DOverlays = true;
    bool showShortcutsModal = false;

    // Text / Font search & hex color buffers
    char fontSearchBuf[128] = {};
    char textEditBuf[512] = {};
    char outlinerSearchBuf[128] = {};
    char hexColorBuf[16] = "cde820";
    uint32_t textEditSyncedLayerId = 0;
    bool rulerFinishRequested = false;

    // Hover / Status info
    bool hoverUvValid = false;
    Vec2 lastHoverUv{0.5f, 0.5f};
    std::string statusText = "Ready";
    std::string activeMeshPath;
    std::string activeProjectPath;

    // Unified caption-embedded header chrome & interactive dock state
    ImVec4 headerBarRect{0.0f, 0.0f, 0.0f, 0.0f};
    bool headerItemHovered = false;
    bool windowMaximized = false;
    bool editingStrokeColor = false;
    int forceOpenPropsCat = -1;
    int layerDragFromIdx = -1;

    // Coatroom v1.2 UI features (brand bitmap, rich tooltips, context menus, clipboard, modals)
    ImTextureID appLogoTex = 0;
    bool showRichTooltips = true;
    int forcePreviewRichTooltip = -1; // StudioTool cast (or kRichTooltipMirrorIdx) for --screenshot-tooltip
    int hoveredRichTooltipId = -1;
    float richTooltipTimer = 0.0f;
    ImVec2 richTooltipAnchor{0.0f, 0.0f};
    bool request3DContextMenu = false;
    bool requestOpenTermsModal = false;
    bool requestOpenUpdatesModal = false;
    bool requestPasteClipboard = false;
    bool requestCopySelection = false;
    bool requestCutSelection = false;
    bool showUpdatesModal = false;
    bool showTermsModal = false;
    bool otaUpdateAvailable = false;
    std::string otaLatestVersion;
    int activePenToolVariant = 0; // 0=Pen (CurveBlade), 1=Line (StraightRuler)
    int activeTypeToolVariant = 0; // 0=Type (Text), 1=Touch Type (TouchType)
    int activeNavToolVariant = 0; // 0=Hand (HandPan), 1=Zoom
    std::vector<Color4u8> customSwatches;

    // Adobe Illustrator-style Home / Welcome Screen & Crash Recovery Banner
    bool showHomeScreen = true;
    bool hasActiveDocument = false;
    int homeNavTab = 0;       // 0 = Home, 1 = Templates
    bool homeGridView = true; // true = Grid cards, false = Compact List
    int homeSortMode = 0;     // 0 = Recent, 1 = Name
    char homeFilterBuf[128] = {};
    bool hasRecoveryBanner = false;
    std::string recoveryInfoText;
    std::vector<RecentProjectEntry> recentProjects;

    // Layout & dirty flags
    bool dockLayoutInitialized = false;
    bool dockLayoutResetRequested = false;
    bool meshGpuDirty = false;
};

struct StudioCallbacks {
    std::function<void()> onNewProject;
    std::function<void()> onOpenObj;
    std::function<void()> onOpenLiveryPng;
    std::function<void()> onLoadAoMap;
    std::function<void()> onBakeMeshAo;
    std::function<void()> onImportDecal;
    std::function<void()> onImportImage;
    std::function<void(int resolution)> onExportPng;
    std::function<void()> onSaveProject;
    std::function<void()> onLoadProject;
    std::function<void()> onQuickLoadPorsche;
    std::function<void()> onRenderBeautyShot;
    std::function<void()> onRenderPhoto;

    std::function<void(bool mirror)> onToggleMirror;
    std::function<void()> onCopy;
    std::function<void()> onCut;
    std::function<void()> onPaste;
    std::function<void()> onCheckUpdates;
    std::function<void()> onOpenTerms;
    std::function<void()> onNewLayer;
    std::function<void(int layerIdx)> onDuplicateLayer;
    std::function<void(int layerIdx)> onDeleteLayer;
    std::function<void(int itemIdx, int ptIdx)> onRemovePenPoint;
    std::function<void()> onFillPlaceholderText;

    // Home Screen & Crash Recovery actions
    std::function<void(int presetIdx)> onStartPreset;
    std::function<void(const std::string& path)> onOpenRecent;
    std::function<void()> onReturnToEditor;
    std::function<void()> onRestoreRecovery;
    std::function<void()> onDismissRecovery;

    std::function<void()> onWindowMinimize;
    std::function<void()> onWindowToggleMaximize;
    std::function<void()> onWindowClose;

    std::function<void(CameraPreset)> onSetCameraPreset;
    std::function<void()> onToggleOrtho;
    std::function<void()> onUndo;
    std::function<void()> onRedo;
    std::function<void(int newRes)> onResizeCanvas;
    std::function<void(float zoomDelta)> onZoom3D;
    std::function<void(float dx, float dy)> onPan3D;
    std::function<void()> onClearSelection;

    std::function<void()> onMeshModified;
    std::function<void()> onCanvasModified;

    // Called inside the 3D Viewport window right after the 3D render image is drawn
    std::function<void()> onDraw3DViewportOverlay;
};

} // namespace lps
