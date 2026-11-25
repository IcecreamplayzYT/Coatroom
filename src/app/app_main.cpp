#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include "math/geom.hpp"
#include "mesh/obj_mesh.hpp"
#include "paint/canvas_engine.hpp"
#include "render/d3d11_renderer.hpp"
#include "ui/ui_types.hpp"
#include "ui/studio_theme.hpp"
#include "ui/uv_viewport.hpp"
#include "ui/panels.hpp"
#include "app/file_dialogs.hpp"
#include "app/automation_mcp.hpp"
#include "app/ota_updater.hpp"
#include "stb/stb_image.h"
#include "stb/stb_image_write.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

constexpr const char* kCoatroomVersion = "1.2.0";
constexpr int kIdIconCoatroom = 101;
constexpr const char* kPorscheObjPath = "C:\\Users\\MyHP _)\\Downloads\\Porsche 992 GT3 R - 2023.obj";
constexpr const char* kPorscheLiveryPath =
    "C:\\Users\\MyHP _)\\.gemini\\antigravity\\scratch\\Porsche_992_GT3R_Livery_Template\\4K\\Stann #3 - Livery (4K).png";
constexpr const char* kPorscheAoPanelsPath =
    "C:\\Users\\MyHP _)\\.gemini\\antigravity\\scratch\\Porsche_992_GT3R_Livery_Template\\Shared_Templates_And_Previews\\Porsche_992_GT3R_Panels_4K.png";

enum class GizmoDrag3D {
    None = 0,
    Move,
    ScaleCorner,
    Rotate,
};

struct AppState {
    HWND hwnd = nullptr;
    bool running = true;

    lps::D3D11Renderer renderer;
    lps::ObjMesh mesh;
    lps::CanvasEngine canvas;
    lps::StudioUiState uiState;
    lps::UvViewportState uvState;
    lps::RenderSceneParams sceneParams;

    std::string cliAoOverridePath;
    bool preferBakedAoOnBlank = true;

    // 3D Orbit Camera
    float camYaw = 0.72f;
    float camPitch = 0.24f;
    float camDist = 5.15f;
    lps::Vec3 camTarget{0.0f, 0.52f, 0.0f};

    // Navigation drag tracking
    bool rmbDragging = false;
    float rmbAccumDrag = 0.0f;

    // 3D Viewport tool interaction state
    lps::RayHit hoverHit3D{};
    bool painting3D = false;
    lps::RayHit prevHit3D{};
    double lastStrokeUploadTime = 0.0;

    bool rulerDrawing3D = false;
    int rulerDragNodeIdx3D = -1;
    int penDragMode3D = 0; // 0=none, 1=anchor, 2=handleIn, 3=handleOut, 4=pullNewHandles
    int penDragNodeIdx3D = -1;
    lps::Vec3 penDragStartPos3D{0.0f, 0.0f, 0.0f};
    lps::Vec2 penDragMouseDownPx{0.0f, 0.0f};
    GizmoDrag3D gizmoDrag3D = GizmoDrag3D::None;
    int gizmoLayerIdx3D = -1;
    int gizmoItemIdx3D = -1;
    lps::Vec2 gizmoStartMouse3D{0.0f, 0.0f};
    float gizmoOrigSize3D = 0.22f;
    float gizmoOrigRot3D = 0.0f;
    float gizmoStartScaleDist3D = 1.0f;
    float gizmoStartAngle3D = 0.0f;
    lps::Vec3 gizmoPivotPos3D{0.0f, 0.0f, 0.0f};
    double lastVectorRasterTime = 0.0;

    ID3D11Texture2D* appLogoGpuTex = nullptr;
    ID3D11ShaderResourceView* appLogoSrv = nullptr;
    std::string otaManifestUrl;

    bool rectDrawing3D = false;
    lps::Vec3 rectStartPos3D{0.0f, 0.0f, 0.0f};
    lps::Vec3 rectStartNrm3D{0.0f, 1.0f, 0.0f};
    lps::Vec3 rectRight3D{1.0f, 0.0f, 0.0f};
    lps::Vec3 rectDown3D{0.0f, -1.0f, 0.0f};
    lps::Vec3 rectCurrPos3D{0.0f, 0.0f, 0.0f};
    bool zoomScrub3D = false;

    // 3D Edit Mesh interactive G/R/S state
    bool editDraggingVerts3D = false;
    lps::Vec2 editDragStartMouse{0.0f, 0.0f};
    std::vector<lps::Vec3> editStartPositions;
    lps::Vec3 editSelCenter{0.0f, 0.0f, 0.0f};
    std::string editHudBadge;

    // Deferred beauty shot request (handled outside ImGui frame cleanly)
    bool pendingBeautyDialog = false;
    std::string pendingBeautyPath;

    // AppData Recent Projects & Crash Protection (.cr autosave + SEH recovery)
    std::filesystem::path coatroomDataDir;
    std::filesystem::path recentJsonPath;
    std::filesystem::path thumbsDir;
    std::filesystem::path recoveryDir;
    std::filesystem::path recoveryCrPath;
    std::filesystem::path recoveryMetaPath;
    std::unordered_map<std::string, std::pair<ID3D11Texture2D*, ID3D11ShaderResourceView*>> thumbGpuCache;
    bool documentDirty = false;
    bool suppressRecentTracking = false;
    bool returnToHomeIfNoDoc = false;
    double lastAutosaveTime = 0.0;
};

AppState* g_app = nullptr;
std::atomic_flag g_crashHandlerEntered = ATOMIC_FLAG_INIT;

lps::RenderShadingMode toRenderShading(lps::ShadingMode m) {
    switch (m) {
    case lps::ShadingMode::Wireframe: return lps::RenderShadingMode::Wireframe;
    case lps::ShadingMode::Clay:      return lps::RenderShadingMode::SolidClay;
    case lps::ShadingMode::Material:  return lps::RenderShadingMode::MaterialPreview;
    case lps::ShadingMode::Rendered:  return lps::RenderShadingMode::ShowroomRendered;
    default:                          return lps::RenderShadingMode::ShowroomRendered;
    }
}

void computeCameraBasis(const AppState& app, lps::Vec3& outEye, lps::Vec3& outForward,
                        lps::Vec3& outRight, lps::Vec3& outUp) {
    float cp = std::cos(app.camPitch);
    float sp = std::sin(app.camPitch);
    float cy = std::cos(app.camYaw);
    float sy = std::sin(app.camYaw);
    lps::Vec3 offset{cp * sy * app.camDist, sp * app.camDist, cp * cy * app.camDist};
    outEye = app.camTarget + offset;
    outForward = lps::normalized(app.camTarget - outEye);
    lps::Vec3 worldUp{0.0f, 1.0f, 0.0f};
    outRight = lps::normalized(lps::cross(worldUp, outForward));
    outUp = lps::normalized(lps::cross(outForward, outRight));
}

void applyCameraPreset(AppState& app, lps::CameraPreset preset) {
    lps::Vec3 center{0.0f, 0.52f, 0.0f};
    float fitDist = 6.10f;
    if (app.mesh.loaded) {
        center = app.mesh.bounds.center();
        float maxExt = std::max({app.mesh.bounds.extent().x, app.mesh.bounds.extent().y,
                                 app.mesh.bounds.extent().z, 1.0f});
        fitDist = maxExt * 1.32f;
    }
    app.camTarget = center;
    app.camDist = fitDist;

    switch (preset) {
    case lps::CameraPreset::Front:
        app.camYaw = 0.0f;
        app.camPitch = 0.07f;
        app.uiState.statusText = "Camera: Front (1)";
        break;
    case lps::CameraPreset::Rear:
        app.camYaw = lps::kPi;
        app.camPitch = 0.10f;
        app.uiState.statusText = "Camera: Rear";
        break;
    case lps::CameraPreset::Side:
        app.camYaw = lps::kPi * 0.5f;
        app.camPitch = 0.06f;
        app.uiState.statusText = "Camera: Side (3)";
        break;
    case lps::CameraPreset::Top:
        app.camYaw = lps::kPi;
        app.camPitch = 1.50f; // slight -Z bias keeps +Z nose pointing up in Top view
        app.uiState.statusText = "Camera: Top (7)";
        break;
    case lps::CameraPreset::ThreeQuarterBeauty:
    default:
        app.camYaw = 0.72f;
        app.camPitch = 0.24f;
        app.uiState.statusText = "Camera: 3/4 Beauty";
        break;
    }
    app.sceneParams.orthoHalfHeight = std::clamp(
        app.camDist * std::tan(app.sceneParams.fovYDeg * 0.5f * lps::kDeg2Rad), 0.35f, 15.0f);
}

void syncSceneParams(AppState& app) {
    lps::Vec3 eye, fwd, right, up;
    computeCameraBasis(app, eye, fwd, right, up);
    app.uiState.camRight = right;
    app.uiState.camUp = up;
    app.uiState.camForward = -fwd;

    app.sceneParams.camEye = eye;
    app.sceneParams.camTarget = app.camTarget;
    app.sceneParams.camUp = lps::Vec3{0.0f, 1.0f, 0.0f};
    app.sceneParams.orthographic = app.uiState.isOrthoCamera;
    app.sceneParams.shadingMode = toRenderShading(app.uiState.shadingMode);

    app.sceneParams.metallic = app.uiState.material.metallic;
    app.sceneParams.roughness = app.uiState.material.roughness;
    app.sceneParams.clearcoat = app.uiState.material.clearcoat;
    app.sceneParams.clearcoatRoughness = app.uiState.material.clearcoatRoughness;
    app.sceneParams.exposure = 1.05f * app.uiState.material.envIntensity;
    app.sceneParams.showStudioFloor = app.uiState.material.showFloorGrid;
    app.sceneParams.showSelectionOverlay = (app.uiState.activeMode == lps::StudioMode::EditMesh) ||
                                           app.uiState.brush.maskSelectedFacesOnly;
    app.sceneParams.showWireframeOverlay = false;
    app.sceneParams.symmetryPlaneX = app.mesh.loaded ? app.mesh.centerSymmetryX : 0.0f;

    for (int i = 0; i < 3; ++i) {
        app.sceneParams.lights[i].position = app.uiState.lights[i].position;
        app.sceneParams.lights[i].color = app.uiState.lights[i].color;
        app.sceneParams.lights[i].intensity = app.uiState.lights[i].intensity;
        app.sceneParams.lights[i].enabled = app.uiState.lights[i].enabled;
    }
}

lps::Ray screenPosToRay3D(const AppState& app, ImVec2 screenPos) {
    float vpW = std::max(app.uiState.viewport3DPanelSize.x, 16.0f);
    float vpH = std::max(app.uiState.viewport3DPanelSize.y, 16.0f);
    float aspect = vpW / vpH;
    lps::Mat4 vp = app.sceneParams.viewMatrix() * app.sceneParams.projMatrix(aspect);
    lps::Mat4 invVp = lps::inverseMat4(vp);

    float ndcX = ((screenPos.x - app.uiState.viewport3DPanelMin.x) / vpW) * 2.0f - 1.0f;
    float ndcY = 1.0f - ((screenPos.y - app.uiState.viewport3DPanelMin.y) / vpH) * 2.0f;

    lps::Vec4 pN4 = lps::transformPoint4(invVp, lps::Vec3{ndcX, ndcY, 0.0f});
    lps::Vec4 pF4 = lps::transformPoint4(invVp, lps::Vec3{ndcX, ndcY, 1.0f});
    lps::Vec3 pN{pN4.x / pN4.w, pN4.y / pN4.w, pN4.z / pN4.w};
    lps::Vec3 pF{pF4.x / pF4.w, pF4.y / pF4.w, pF4.z / pF4.w};
    return lps::Ray{pN, lps::normalized(pF - pN)};
}

bool projectWorldToViewport3D(const AppState& app, const lps::Vec3& p, ImVec2& outScreen) {
    float vpW = std::max(app.uiState.viewport3DPanelSize.x, 16.0f);
    float vpH = std::max(app.uiState.viewport3DPanelSize.y, 16.0f);
    lps::Mat4 vp = app.sceneParams.viewMatrix() * app.sceneParams.projMatrix(vpW / vpH);
    lps::Vec4 c = lps::transformPoint4(vp, p);
    if (c.w <= 1e-5f) return false;
    float ndcX = c.x / c.w;
    float ndcY = c.y / c.w;
    float ndcZ = c.z / c.w;
    if (ndcZ < 0.0f || ndcZ > 1.0f) return false;
    outScreen.x = app.uiState.viewport3DPanelMin.x + (ndcX * 0.5f + 0.5f) * vpW;
    outScreen.y = app.uiState.viewport3DPanelMin.y + (0.5f - ndcY * 0.5f) * vpH;
    return true;
}

void syncCanvasGpu(AppState& app, bool genMips = true) {
    app.canvas.rasterizeAllDirty(&app.mesh);
    if (app.canvas.gpuDirty) {
        if (app.uiState.hasActiveDocument) app.documentDirty = true;
        bool fullUpload = false;
        lps::DirtyRect dr = app.canvas.consumeDirtyRect(fullUpload);
        ID3D11ShaderResourceView* srv = nullptr;
        if (!fullUpload && dr.valid) {
            srv = app.renderer.updateCanvasRegion(
                app.canvas.compositeRgba.data(), app.canvas.width, app.canvas.height,
                dr.x0, dr.y0, dr.x1, dr.y1, genMips);
        } else {
            srv = app.renderer.createOrUpdateCanvasTexture(
                app.canvas.compositeRgba.data(), app.canvas.width, app.canvas.height);
        }
        app.uiState.canvas2DTex = reinterpret_cast<ImTextureID>(srv);
        app.canvas.gpuDirty = false;
    }
}

void syncMeshGpu(AppState& app) {
    if (app.uiState.meshGpuDirty) {
        app.renderer.uploadMesh(app.mesh);
        app.uiState.meshGpuDirty = false;
    }
}

std::string meshStemOrDefault(const AppState& app, const char* fallback = "car") {
    if (!app.uiState.activeMeshPath.empty()) {
        std::string stem = std::filesystem::path(app.uiState.activeMeshPath).stem().string();
        if (!stem.empty()) return stem;
    }
    return fallback;
}

std::string formatLocalTimestamp() {
    std::time_t now = std::time(nullptr);
    std::tm tmLocal{};
    localtime_s(&tmLocal, &now);
    char buf[32] = {};
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d",
                  tmLocal.tm_year + 1900, tmLocal.tm_mon + 1, tmLocal.tm_mday,
                  tmLocal.tm_hour, tmLocal.tm_min);
    return buf;
}

std::string jsonEscapeStr(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (unsigned char c : in) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char hex[8];
                std::snprintf(hex, sizeof(hex), "\\u%04x", c);
                out += hex;
            } else {
                out.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    return out;
}

std::string extractJsonStringField(const std::string& objText, const std::string& key) {
    std::string pat = "\"" + key + "\"";
    size_t pos = objText.find(pat);
    if (pos == std::string::npos) return {};
    pos = objText.find(':', pos + pat.size());
    if (pos == std::string::npos) return {};
    pos = objText.find('"', pos + 1);
    if (pos == std::string::npos) return {};
    std::string out;
    for (size_t i = pos + 1; i < objText.size(); ++i) {
        char c = objText[i];
        if (c == '"') return out;
        if (c == '\\' && i + 1 < objText.size()) {
            char nxt = objText[++i];
            if (nxt == 'n') out.push_back('\n');
            else if (nxt == 'r') out.push_back('\r');
            else if (nxt == 't') out.push_back('\t');
            else out.push_back(nxt);
        } else {
            out.push_back(c);
        }
    }
    return {};
}

std::filesystem::path getCoatroomDataDir() {
    wchar_t* localApp = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localApp)) && localApp) {
        std::filesystem::path p = std::filesystem::path(localApp) / L"Coatroom";
        CoTaskMemFree(localApp);
        return p;
    }
    if (const wchar_t* envApp = _wgetenv(L"LOCALAPPDATA")) {
        return std::filesystem::path(envApp) / L"Coatroom";
    }
    return std::filesystem::temp_directory_path() / L"Coatroom";
}

bool createRgbaTextureSrv(ID3D11Device* dev, const uint8_t* rgba, int w, int h,
                          ID3D11Texture2D** outTex, ID3D11ShaderResourceView** outSrv) {
    if (!dev || !rgba || w <= 0 || h <= 0 || !outTex || !outSrv) return false;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = static_cast<UINT>(w);
    td.Height = static_cast<UINT>(h);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData{};
    initData.pSysMem = rgba;
    initData.SysMemPitch = static_cast<UINT>(w) * 4u;

    ID3D11Texture2D* tex = nullptr;
    if (FAILED(dev->CreateTexture2D(&td, &initData, &tex)) || !tex) return false;

    ID3D11ShaderResourceView* srv = nullptr;
    D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
    srvd.Format = td.Format;
    srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvd.Texture2D.MipLevels = 1;
    if (FAILED(dev->CreateShaderResourceView(tex, &srvd, &srv)) || !srv) {
        tex->Release();
        return false;
    }
    *outTex = tex;
    *outSrv = srv;
    return true;
}

void evictThumbSrv(AppState& app, const std::string& pngPath) {
    auto it = app.thumbGpuCache.find(pngPath);
    if (it != app.thumbGpuCache.end()) {
        if (it->second.second) it->second.second->Release();
        if (it->second.first) it->second.first->Release();
        app.thumbGpuCache.erase(it);
    }
}

void releaseAllThumbSrvs(AppState& app) {
    for (auto& kv : app.thumbGpuCache) {
        if (kv.second.second) kv.second.second->Release();
        if (kv.second.first) kv.second.first->Release();
    }
    app.thumbGpuCache.clear();
    for (auto& r : app.uiState.recentProjects) r.thumbTex = 0;
}

ImTextureID loadThumbPngToSrv(AppState& app, const std::string& pngPath) {
    if (pngPath.empty() || !app.renderer.device()) return 0;
    auto it = app.thumbGpuCache.find(pngPath);
    if (it != app.thumbGpuCache.end()) {
        return reinterpret_cast<ImTextureID>(it->second.second);
    }
    std::error_code ec;
    if (!std::filesystem::exists(pngPath, ec)) return 0;
    int w = 0, h = 0, ch = 0;
    uint8_t* rgba = stbi_load(pngPath.c_str(), &w, &h, &ch, 4);
    if (!rgba || w <= 0 || h <= 0) {
        if (rgba) stbi_image_free(rgba);
        return 0;
    }
    ID3D11Texture2D* tex = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    bool ok = createRgbaTextureSrv(app.renderer.device(), rgba, w, h, &tex, &srv);
    stbi_image_free(rgba);
    if (!ok) return 0;
    app.thumbGpuCache[pngPath] = {tex, srv};
    return reinterpret_cast<ImTextureID>(srv);
}

std::string fnv1a64HexLower(const std::string& s) {
    uint64_t h = 14695981039346656037ull;
    for (unsigned char ch : s) {
        unsigned char lc = static_cast<unsigned char>(std::tolower(ch));
        h ^= lc;
        h *= 1099511628211ull;
    }
    char buf[20] = {};
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
    return buf;
}

std::string captureFront34Thumbnail(AppState& app, const std::string& targetPath) {
    if (!app.mesh.loaded || app.thumbsDir.empty() || targetPath.empty()) return {};
    std::error_code ec;
    std::filesystem::create_directories(app.thumbsDir, ec);
    std::string outPng = (app.thumbsDir / (fnv1a64HexLower(targetPath) + ".png")).string();

    syncCanvasGpu(app, true);
    syncMeshGpu(app);

    lps::RenderSceneParams tp = app.sceneParams;
    lps::Vec3 center = app.mesh.bounds.center();
    float maxExt = std::max({app.mesh.bounds.extent().x, app.mesh.bounds.extent().y,
                             app.mesh.bounds.extent().z, 1.0f});
    float fitDist = maxExt * 1.24f;
    const float yaw = 0.72f;
    const float pitch = 0.24f;
    float cp = std::cos(pitch), sp = std::sin(pitch);
    float cy = std::cos(yaw), sy = std::sin(yaw);
    tp.camTarget = center;
    tp.camEye = center + lps::Vec3{cp * sy * fitDist, sp * fitDist, cp * cy * fitDist};
    tp.camUp = lps::Vec3{0.0f, 1.0f, 0.0f};
    tp.orthographic = false;
    tp.shadingMode = lps::RenderShadingMode::ShowroomRendered;
    tp.showSelectionOverlay = false;
    tp.showWireframeOverlay = false;

    if (!app.renderer.renderBeautyShotToPng(app.mesh, tp, outPng, 320, 180)) {
        return {};
    }
    evictThumbSrv(app, outPng);
    return outPng;
}

void initCoatroomStoragePaths(AppState& app, const std::filesystem::path& baseDir) {
    app.coatroomDataDir = baseDir;
    app.recentJsonPath = baseDir / "recent.json";
    app.thumbsDir = baseDir / "thumbs";
    app.recoveryDir = baseDir / "recovery";
    app.recoveryCrPath = app.recoveryDir / "autosave_recovery.cr";
    app.recoveryMetaPath = app.recoveryDir / "recovery_meta.json";
}

bool saveRecentProjectsFile(const std::filesystem::path& jsonPath,
                            const std::vector<lps::RecentProjectEntry>& items) {
    std::error_code ec;
    if (!jsonPath.parent_path().empty()) {
        std::filesystem::create_directories(jsonPath.parent_path(), ec);
    }
    std::ofstream ofs(jsonPath, std::ios::binary | std::ios::trunc);
    if (!ofs) return false;
    ofs << "{\n  \"recent\": [\n";
    for (size_t i = 0; i < items.size(); ++i) {
        const auto& r = items[i];
        ofs << "    {\"name\":\"" << jsonEscapeStr(r.name)
            << "\",\"path\":\"" << jsonEscapeStr(r.path)
            << "\",\"kind\":\"" << jsonEscapeStr(r.kind)
            << "\",\"detail\":\"" << jsonEscapeStr(r.detail)
            << "\",\"thumbPath\":\"" << jsonEscapeStr(r.thumbPath) << "\"}"
            << (i + 1 < items.size() ? "," : "") << "\n";
    }
    ofs << "  ]\n}\n";
    return ofs.good();
}

std::vector<lps::RecentProjectEntry> loadRecentProjectsFile(const std::filesystem::path& jsonPath) {
    std::vector<lps::RecentProjectEntry> out;
    std::ifstream ifs(jsonPath, std::ios::binary);
    if (!ifs) return out;
    std::string text((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    bool inStr = false, esc = false;
    int braceDepth = 0;
    size_t objStart = std::string::npos;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (inStr) {
            if (esc) esc = false;
            else if (c == '\\') esc = true;
            else if (c == '"') inStr = false;
            continue;
        }
        if (c == '"') { inStr = true; continue; }
        if (c == '{') {
            if (braceDepth == 1) objStart = i;
            ++braceDepth;
        } else if (c == '}') {
            --braceDepth;
            if (braceDepth == 1 && objStart != std::string::npos) {
                std::string obj = text.substr(objStart, i - objStart + 1);
                lps::RecentProjectEntry e{};
                e.name = extractJsonStringField(obj, "name");
                e.path = extractJsonStringField(obj, "path");
                e.kind = extractJsonStringField(obj, "kind");
                e.detail = extractJsonStringField(obj, "detail");
                e.thumbPath = extractJsonStringField(obj, "thumbPath");
                if (!e.path.empty()) {
                    if (e.name.empty()) e.name = std::filesystem::path(e.path).stem().string();
                    out.push_back(std::move(e));
                }
                objStart = std::string::npos;
            }
        }
    }
    return out;
}

void pushRecentProject(AppState& app, const std::string& rawPath, const std::string& kind,
                       const std::string& detail = "") {
    if (app.suppressRecentTracking || rawPath.empty()) return;
    std::string absPath = rawPath;
    std::error_code ec;
    std::filesystem::path p(rawPath);
    if (std::filesystem::exists(p, ec)) {
        absPath = std::filesystem::weakly_canonical(p, ec).string();
        if (absPath.empty()) absPath = p.string();
    }
    auto eqInsensitive = [](const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a[i])) !=
                std::tolower(static_cast<unsigned char>(b[i]))) return false;
        }
        return true;
    };

    lps::RecentProjectEntry entry{};
    entry.path = absPath;
    entry.name = std::filesystem::path(absPath).stem().string();
    if (entry.name.empty()) entry.name = absPath;
    entry.kind = kind;
    entry.detail = detail.empty() ? formatLocalTimestamp() : detail;
    if (app.mesh.loaded) {
        entry.thumbPath = captureFront34Thumbnail(app, absPath);
        if (!entry.thumbPath.empty()) {
            entry.thumbTex = loadThumbPngToSrv(app, entry.thumbPath);
        }
    }

    auto& list = app.uiState.recentProjects;
    list.erase(std::remove_if(list.begin(), list.end(), [&](const lps::RecentProjectEntry& r) {
        return eqInsensitive(r.path, absPath);
    }), list.end());
    list.insert(list.begin(), std::move(entry));
    if (list.size() > 12) list.resize(12);
    if (!app.recentJsonPath.empty()) saveRecentProjectsFile(app.recentJsonPath, list);
}

bool writeRecoverySnapshot(AppState& app, const char* reason) {
    if (app.recoveryCrPath.empty() || !app.uiState.hasActiveDocument) return false;
    std::error_code ec;
    std::filesystem::create_directories(app.recoveryDir, ec);
    if (!app.canvas.saveProject(app.recoveryCrPath.string(), app.uiState.activeMeshPath)) return false;
    std::string stamp = formatLocalTimestamp();
    std::ofstream mfs(app.recoveryMetaPath, std::ios::binary | std::ios::trunc);
    if (mfs) {
        std::string meshStem = meshStemOrDefault(app, "Blank Canvas");
        mfs << "{\n"
            << "  \"reason\": \"" << jsonEscapeStr(reason ? reason : "Autosave") << "\",\n"
            << "  \"savedAt\": \"" << jsonEscapeStr(stamp) << "\",\n"
            << "  \"meshName\": \"" << jsonEscapeStr(meshStem) << "\",\n"
            << "  \"resolution\": " << app.canvas.width << ",\n"
            << "  \"layers\": " << app.canvas.layers.size() << "\n"
            << "}\n";
    }
    app.documentDirty = false;
    app.lastAutosaveTime = ImGui::GetTime();
    return true;
}

std::string readRecoverySummary(const AppState& app) {
    std::ifstream ifs(app.recoveryMetaPath, std::ios::binary);
    if (!ifs) return "Recovered unsaved session";
    std::string text((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    std::string savedAt = extractJsonStringField(text, "savedAt");
    std::string meshNm = extractJsonStringField(text, "meshName");
    std::string reason = extractJsonStringField(text, "reason");
    std::string msg = "Unsaved session detected";
    if (!meshNm.empty()) msg += " (" + meshNm + ")";
    if (!savedAt.empty()) msg += " - " + savedAt;
    if (!reason.empty()) msg += " [" + reason + "]";
    return msg;
}

void clearRecoverySnapshot(const AppState& app) {
    std::error_code ec;
    if (!app.recoveryCrPath.empty()) std::filesystem::remove(app.recoveryCrPath, ec);
    if (!app.recoveryMetaPath.empty()) std::filesystem::remove(app.recoveryMetaPath, ec);
}

LONG WINAPI coatroomCrashFilter(EXCEPTION_POINTERS* /*exInfo*/) {
    if (!g_crashHandlerEntered.test_and_set() && g_app &&
        g_app->uiState.hasActiveDocument && !g_app->recoveryCrPath.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(g_app->recoveryDir, ec);
        g_app->canvas.saveProject(g_app->recoveryCrPath.string(), g_app->uiState.activeMeshPath);
        std::ofstream mfs(g_app->recoveryMetaPath, std::ios::binary | std::ios::trunc);
        if (mfs) {
            mfs << "{\"reason\":\"Crash Recovery\",\"meshName\":\""
                << jsonEscapeStr(meshStemOrDefault(*g_app, "Canvas"))
                << "\",\"savedAt\":\"Emergency Snapshot\"}\n";
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

std::filesystem::path findBundledFileW(const wchar_t* relName) {
    wchar_t exeBuf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exeBuf, MAX_PATH);
    std::filesystem::path exeDir = std::filesystem::path(exeBuf).parent_path();
    const std::filesystem::path candidates[] = {
        exeDir / relName,
        exeDir.parent_path() / relName,
        std::filesystem::current_path() / relName,
    };
    std::error_code ec;
    for (const auto& c : candidates) {
        if (std::filesystem::exists(c, ec)) return c;
    }
    return {};
}

bool createAppLogoSrv(AppState& app) {
    ID3D11Device* dev = app.renderer.device();
    if (!dev) return false;
    std::vector<uint8_t> rgba = lps::renderBrandLogoRgba(20);
    if (rgba.size() != static_cast<size_t>(20) * 20 * 4) return false;
    if (!createRgbaTextureSrv(dev, rgba.data(), 20, 20, &app.appLogoGpuTex, &app.appLogoSrv)) return false;
    app.uiState.appLogoTex = reinterpret_cast<ImTextureID>(app.appLogoSrv);
    return true;
}

void releaseAppLogoSrv(AppState& app) {
    app.uiState.appLogoTex = 0;
    if (app.appLogoSrv) {
        app.appLogoSrv->Release();
        app.appLogoSrv = nullptr;
    }
    if (app.appLogoGpuTex) {
        app.appLogoGpuTex->Release();
        app.appLogoGpuTex = nullptr;
    }
}

bool tryReadClipboardRgba(HWND hwnd, std::vector<uint8_t>& outRgba, int& outW, int& outH) {
    outRgba.clear();
    outW = outH = 0;
    if (!OpenClipboard(hwnd)) return false;

    bool ok = false;
    static const UINT cfPng = RegisterClipboardFormatW(L"PNG");
    if (cfPng != 0 && IsClipboardFormatAvailable(cfPng)) {
        HANDLE hData = GetClipboardData(cfPng);
        if (hData) {
            const void* ptr = GlobalLock(hData);
            SIZE_T bytes = GlobalSize(hData);
            if (ptr && bytes > 8 && bytes <= 64u * 1024u * 1024u) {
                int w = 0, h = 0, ch = 0;
                uint8_t* decoded = stbi_load_from_memory(
                    static_cast<const stbi_uc*>(ptr), static_cast<int>(bytes), &w, &h, &ch, 4);
                if (decoded && w > 0 && h > 0) {
                    outW = w;
                    outH = h;
                    outRgba.assign(decoded, decoded + static_cast<size_t>(w) * h * 4);
                    ok = true;
                }
                if (decoded) stbi_image_free(decoded);
            }
            if (ptr) GlobalUnlock(hData);
        }
    }

    if (!ok && IsClipboardFormatAvailable(CF_DIB)) {
        HANDLE hDib = GetClipboardData(CF_DIB);
        if (hDib) {
            const uint8_t* base = static_cast<const uint8_t*>(GlobalLock(hDib));
            SIZE_T dibBytes = GlobalSize(hDib);
            if (base && dibBytes >= sizeof(BITMAPINFOHEADER)) {
                BITMAPINFOHEADER hdr{};
                std::memcpy(&hdr, base, sizeof(BITMAPINFOHEADER));
                const int w = hdr.biWidth;
                const bool topDown = (hdr.biHeight < 0);
                const int h = topDown ? -hdr.biHeight : hdr.biHeight;
                const int bpp = hdr.biBitCount;
                if (hdr.biSize >= sizeof(BITMAPINFOHEADER) && w > 0 && h > 0 &&
                    w <= 16384 && h <= 16384 && (bpp == 24 || bpp == 32) &&
                    (hdr.biCompression == BI_RGB || hdr.biCompression == BI_BITFIELDS)) {
                    size_t palBytes = static_cast<size_t>(hdr.biClrUsed) * sizeof(RGBQUAD);
                    if (hdr.biCompression == BI_BITFIELDS && hdr.biSize == sizeof(BITMAPINFOHEADER)) {
                        palBytes += 3 * sizeof(DWORD);
                    }
                    size_t pixelOff = hdr.biSize + palBytes;
                    size_t rowStride = ((static_cast<size_t>(w) * bpp + 31u) / 32u) * 4u;
                    if (pixelOff + rowStride * static_cast<size_t>(h) <= dibBytes) {
                        const uint8_t* pxBase = base + pixelOff;
                        outRgba.resize(static_cast<size_t>(w) * h * 4u);
                        bool anyNonZeroAlpha = false;
                        for (int y = 0; y < h; ++y) {
                            int srcY = topDown ? y : (h - 1 - y);
                            const uint8_t* sRow = pxBase + static_cast<size_t>(srcY) * rowStride;
                            uint8_t* dRow = outRgba.data() + static_cast<size_t>(y) * w * 4u;
                            for (int x = 0; x < w; ++x) {
                                const uint8_t b = sRow[x * (bpp / 8) + 0];
                                const uint8_t g = sRow[x * (bpp / 8) + 1];
                                const uint8_t r = sRow[x * (bpp / 8) + 2];
                                const uint8_t a = (bpp == 32) ? sRow[x * 4 + 3] : 255;
                                if (bpp == 32 && a != 0) anyNonZeroAlpha = true;
                                dRow[x * 4 + 0] = r;
                                dRow[x * 4 + 1] = g;
                                dRow[x * 4 + 2] = b;
                                dRow[x * 4 + 3] = a;
                            }
                        }
                        if (bpp == 32 && !anyNonZeroAlpha) {
                            for (size_t i = 3; i < outRgba.size(); i += 4) outRgba[i] = 255;
                        }
                        outW = w;
                        outH = h;
                        ok = true;
                    }
                }
            }
            if (base) GlobalUnlock(hDib);
        }
    }

    CloseClipboard();
    return ok;
}

struct Frame3D {
    lps::Vec3 c{0, 0, 0};
    lps::Vec3 T{1, 0, 0};
    lps::Vec3 B{0, -1, 0};
    lps::Vec3 N{0, 1, 0};
    float hw = 0.1f;
    float hh = 0.1f;
    float rotDeg = 0.0f;
    lps::MirrorTextMode mirror = lps::MirrorTextMode::None;
};

void buildTangentFrame3D(const lps::Vec3& c, const lps::Vec3& rawN, const lps::Vec3& rHint,
                         float rotDeg, float wW, float wH, lps::MirrorTextMode mirror, Frame3D& out) {
    out.c = c;
    out.N = lps::normalized(rawN);
    lps::Vec3 T = rHint - out.N * lps::dot(rHint, out.N);
    if (lps::lengthSq(T) < 1e-6f) {
        lps::Vec3 fb = (std::fabs(out.N.y) < 0.9f) ? lps::Vec3{0, 1, 0} : lps::Vec3{1, 0, 0};
        T = lps::cross(fb, out.N);
    }
    T = lps::normalized(T);
    lps::Vec3 B = lps::normalized(lps::cross(out.N, T));
    float rad = rotDeg * lps::kDeg2Rad;
    float cs = std::cos(rad), sn = std::sin(rad);
    out.T = T * cs + B * sn;
    out.B = -T * sn + B * cs;
    out.hw = wW * 0.5f;
    out.hh = wH * 0.5f;
    out.rotDeg = rotDeg;
    out.mirror = mirror;
}

bool computeVectorItemFrame3D(const lps::VectorItem& item, Frame3D& out) {
    if (!item.visible) return false;
    if (item.kind == lps::VectorItemKind::Text) {
        if (!item.text.anchoredIn3D || item.text.text.empty()) return false;
        float wH = std::max(0.03f, item.text.worldHeight);
        float nChars = static_cast<float>(std::max<size_t>(1, item.text.text.size()));
        float estAspect = std::max(0.55f, nChars * 0.56f + item.text.tracking * 0.008f * (nChars - 1.0f));
        buildTangentFrame3D(item.text.pos3D, item.text.nrm3D, item.text.right3D,
                            item.text.rotationDeg, wH * estAspect, wH, item.text.mirrorMode, out);
        return true;
    }
    if (item.kind == lps::VectorItemKind::Image) {
        if (!item.image.anchoredIn3D || item.image.imagePixels().empty()) return false;
        float wW = std::max(0.05f, item.image.worldWidth);
        float wH = wW * (static_cast<float>(item.image.imgH) / std::max(1, item.image.imgW));
        buildTangentFrame3D(item.image.pos3D, item.image.nrm3D, item.image.right3D,
                            item.image.rotationDeg, wW, wH, item.image.mirrorMode, out);
        return true;
    }
    if (item.kind == lps::VectorItemKind::RectShape) {
        if (!item.rect.anchoredIn3D || item.rect.worldWidth < 1e-4f || item.rect.worldHeight < 1e-4f) return false;
        lps::MirrorTextMode m = item.rect.mirrorX ? lps::MirrorTextMode::ReadableBothSides : lps::MirrorTextMode::None;
        buildTangentFrame3D(item.rect.pos3D, item.rect.nrm3D, item.rect.right3D,
                            item.rect.rotationDeg, item.rect.worldWidth, item.rect.worldHeight, m, out);
        return true;
    }
    return false;
}

bool computeLegacyLayerFrame3D(const lps::Layer& l, Frame3D& out) {
    if (!l.visible) return false;
    if (l.kind == lps::LayerKind::Text) {
        if (!l.text.anchoredIn3D || l.text.text.empty()) return false;
        float wH = std::max(0.03f, l.text.worldHeight);
        float nChars = static_cast<float>(std::max<size_t>(1, l.text.text.size()));
        float estAspect = std::max(0.55f, nChars * 0.56f + l.text.tracking * 0.008f * (nChars - 1.0f));
        buildTangentFrame3D(l.text.pos3D, l.text.nrm3D, l.text.right3D,
                            l.text.rotationDeg, wH * estAspect, wH, l.text.mirrorMode, out);
        return true;
    }
    if (l.kind == lps::LayerKind::Decal && !l.decal.imgRgba.empty()) {
        if (!l.decal.anchoredIn3D) return false;
        float wW = std::max(0.05f, l.decal.worldWidth);
        float wH = wW * (static_cast<float>(l.decal.imgH) / std::max(1, l.decal.imgW));
        buildTangentFrame3D(l.decal.pos3D, l.decal.nrm3D, l.decal.right3D,
                            l.decal.rotationDeg, wW, wH, l.decal.mirrorMode, out);
        return true;
    }
    return false;
}

int parseRichTooltipArg(const std::string& raw) {
    std::string s = raw;
    for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (s == "mirror" || s == "symmetry") return lps::kRichTooltipMirrorIdx;
    if (s == "select" || s == "selectmove" || s == "v") return static_cast<int>(lps::StudioTool::Select);
    if (s == "directselect" || s == "nodeedit" || s == "node" || s == "a") return static_cast<int>(lps::StudioTool::DirectSelect);
    if (s == "orbit") return static_cast<int>(lps::StudioTool::Orbit);
    if (s == "brush" || s == "b") return static_cast<int>(lps::StudioTool::Brush);
    if (s == "line" || s == "ruler" || s == "straightruler" || s == "l") return static_cast<int>(lps::StudioTool::Line);
    if (s == "pen" || s == "curve" || s == "curveblade" || s == "p") return static_cast<int>(lps::StudioTool::Pen);
    if (s == "rect" || s == "rectbox" || s == "rectangle" || s == "u") return static_cast<int>(lps::StudioTool::Rect);
    if (s == "eraser" || s == "e") return static_cast<int>(lps::StudioTool::Eraser);
    if (s == "fill" || s == "floodfill" || s == "bucket" || s == "k") return static_cast<int>(lps::StudioTool::Fill);
    if (s == "eyedropper" || s == "i") return static_cast<int>(lps::StudioTool::Eyedropper);
    if (s == "text" || s == "type" || s == "t") return static_cast<int>(lps::StudioTool::Text);
    if (s == "touchtype" || s == "touch" || s == "shiftt") return static_cast<int>(lps::StudioTool::TouchType);
    if (s == "image" || s == "decal" || s == "sponsordecal" || s == "d") return static_cast<int>(lps::StudioTool::Image);
    if (s == "vertex" || s == "vertexedit" || s == "edit") return static_cast<int>(lps::StudioTool::VertexEdit);
    if (s == "mask" || s == "facemask" || s == "facepaintmask" || s == "f") return static_cast<int>(lps::StudioTool::FacePaintMask);
    if (s == "hand" || s == "pan" || s == "handpan" || s == "h") return static_cast<int>(lps::StudioTool::Hand);
    if (s == "zoom" || s == "z") return static_cast<int>(lps::StudioTool::Zoom);
    if (s == "render" || s == "photo" || s == "beauty" || s == "camera" || s == "f12") return static_cast<int>(lps::StudioTool::BeautyRender);
    return static_cast<int>(lps::StudioTool::Pen);
}

void applyDefaultAoForMesh(AppState& app, bool makeVisible, bool allowPorschePanelsFallback = false) {
    if (!app.cliAoOverridePath.empty() && std::filesystem::exists(app.cliAoOverridePath)) {
        app.canvas.loadAoOverlay(app.cliAoOverridePath, true);
        return;
    }
    if (!app.mesh.resolvedAoTexPath.empty() && std::filesystem::exists(app.mesh.resolvedAoTexPath)) {
        app.canvas.loadAoOverlay(app.mesh.resolvedAoTexPath, makeVisible);
        return;
    }
    if (allowPorschePanelsFallback && std::filesystem::exists(kPorscheAoPanelsPath)) {
        app.canvas.loadAoOverlay(kPorscheAoPanelsPath, makeVisible);
        return;
    }
    if (app.mesh.loaded) {
        app.canvas.bakeMeshAoOverlay(app.mesh, 1024, makeVisible);
    }
}

bool loadMeshFile(AppState& app, const std::string& path, bool preserveCanvas = false) {
    if (path.empty() || !std::filesystem::exists(path)) {
        app.uiState.statusText = "Mesh not found: " + path;
        return false;
    }
    if (!app.mesh.loadFromFile(path, true)) {
        app.uiState.statusText = "Failed to load OBJ: " + path;
        return false;
    }
    app.uiState.activeMeshPath = path;
    app.uiState.meshGpuDirty = true;
    syncMeshGpu(app);
    applyCameraPreset(app, lps::CameraPreset::ThreeQuarterBeauty);

    std::string objName = std::filesystem::path(path).filename().string();
    if (!preserveCanvas) {
        int prevW = app.canvas.width;
        app.canvas.init(4096, lps::Color4u8{238, 240, 244, 255});
        app.uiState.textEditSyncedLayerId = 0;
        app.uiState.activeProjectPath.clear();
        app.rulerDrawing3D = false;
        app.rulerDragNodeIdx3D = -1;
        app.gizmoDrag3D = GizmoDrag3D::None;
        app.gizmoLayerIdx3D = -1;
        app.gizmoItemIdx3D = -1;
        app.rectDrawing3D = false;

        if (!app.mesh.resolvedDiffuseTexPath.empty() &&
            std::filesystem::exists(app.mesh.resolvedDiffuseTexPath) &&
            app.canvas.loadBaseTexture(app.mesh.resolvedDiffuseTexPath)) {
            applyDefaultAoForMesh(app, !app.cliAoOverridePath.empty(), false);
            app.uiState.statusText = "Loaded " + objName + " + texture " +
                                     std::filesystem::path(app.mesh.resolvedDiffuseTexPath).filename().string();
        } else {
            applyDefaultAoForMesh(app, true, false);
            app.uiState.statusText = "Loaded " + objName + " (4K White + AO Panel Shading)";
        }
        app.uiState.canvasResolution = app.canvas.width;
        if (app.canvas.width != prevW) {
            app.uvState.needsInitialFit = true;
        }
    } else {
        app.uiState.statusText = "Loaded mesh: " + objName;
    }

    app.canvas.rasterizeAllDirty(&app.mesh);
    syncCanvasGpu(app);
    app.uiState.hasActiveDocument = true;
    app.uiState.showHomeScreen = false;
    if (!preserveCanvas) {
        app.documentDirty = false;
        pushRecentProject(app, path, ".obj", std::to_string(app.canvas.width) + "px | " + std::to_string(app.mesh.tris.size()) + " tris");
    }
    return true;
}

void quickLoadPorsche(AppState& app, bool loadTemplateLivery) {
    app.canvas.init(4096, lps::Color4u8{238, 240, 244, 255});
    app.uiState.canvasResolution = 4096;
    app.uiState.activeProjectPath.clear();
    app.uiState.textEditSyncedLayerId = 0;
    app.uvState.needsInitialFit = true;
    app.gizmoDrag3D = GizmoDrag3D::None;
    app.gizmoLayerIdx3D = -1;
    app.gizmoItemIdx3D = -1;

    bool objOk = loadMeshFile(app, kPorscheObjPath, true);
    if (loadTemplateLivery && std::filesystem::exists(kPorscheLiveryPath)) {
        if (app.canvas.loadBaseTexture(kPorscheLiveryPath)) {
            app.uiState.canvasResolution = app.canvas.width;
            app.uvState.needsInitialFit = true;
            applyDefaultAoForMesh(app, !app.cliAoOverridePath.empty(), false);
            app.uiState.statusText = "Quick-Loaded Porsche 992 GT3 R + Stann #3 Livery (Shift+Click for AO blank)";
        }
    } else if (objOk) {
        applyDefaultAoForMesh(app, true, true);
        app.uiState.statusText = "Quick-Loaded Porsche 992 GT3 R (4K White + AO Shading)";
    }
    syncCanvasGpu(app);
    app.uiState.hasActiveDocument = true;
    app.uiState.showHomeScreen = false;
    app.documentDirty = false;
    if (objOk) {
        pushRecentProject(app, kPorscheObjPath, ".obj", "Porsche 992 GT3 R (4K)");
    }
}

bool openProjectFilePath(AppState& app, const std::string& p, bool isRecovery = false) {
    if (p.empty() || !std::filesystem::exists(p)) {
        app.uiState.statusText = "Project not found: " + p;
        return false;
    }
    std::string meshPath;
    if (!app.canvas.loadProject(p, meshPath)) {
        app.uiState.statusText = "Project load failed: " + std::filesystem::path(p).filename().string();
        return false;
    }
    if (!isRecovery) app.uiState.activeProjectPath = p;
    else app.uiState.activeProjectPath.clear();
    app.uiState.canvasResolution = app.canvas.width;
    app.uiState.textEditSyncedLayerId = 0;
    app.gizmoDrag3D = GizmoDrag3D::None;
    app.gizmoLayerIdx3D = -1;
    app.gizmoItemIdx3D = -1;
    app.uvState.needsInitialFit = true;
    if (!meshPath.empty() && std::filesystem::exists(meshPath)) {
        loadMeshFile(app, meshPath, true);
    }
    app.canvas.rasterizeAllDirty(&app.mesh);
    app.canvas.recomposite();
    syncCanvasGpu(app);
    app.uiState.hasActiveDocument = true;
    app.uiState.showHomeScreen = false;
    app.documentDirty = isRecovery;
    if (!isRecovery) {
        std::string ext = std::filesystem::path(p).extension().string();
        pushRecentProject(app, p, ext.empty() ? ".cr" : ext,
                          std::to_string(app.canvas.width) + "x" + std::to_string(app.canvas.height) + "px | " +
                          std::to_string(app.canvas.layers.size()) + " layers");
        app.uiState.statusText = "Loaded project: " + std::filesystem::path(p).filename().string();
    } else {
        app.uiState.statusText = "Recovered unsaved Coatroom session";
    }
    return true;
}

void createRectItem3D(AppState& app, const lps::Vec3& pA, const lps::Vec3& pB,
                      const lps::Vec3& nCenter, const lps::Vec3& rightAxis, const lps::Vec3& downAxis,
                      bool squareLock) {
    if (!app.mesh.loaded) return;
    lps::Vec3 rel = pB - pA;
    float uSpan = lps::dot(rel, rightAxis);
    float vSpan = lps::dot(rel, downAxis);
    if (squareLock) {
        float s = std::max(std::fabs(uSpan), std::fabs(vSpan));
        uSpan = (uSpan >= 0.0f) ? s : -s;
        vSpan = (vSpan >= 0.0f) ? s : -s;
    }
    float worldW = std::fabs(uSpan);
    float worldH = std::fabs(vSpan);
    if (worldW < 0.01f || worldH < 0.01f) return;

    lps::Vec3 centerPos = pA + rightAxis * (uSpan * 0.5f) + downAxis * (vSpan * 0.5f);
    lps::RayHit proj = app.mesh.projectPointToSurface(centerPos, nCenter, std::max(0.35f, std::max(worldW, worldH)));

    app.canvas.ensureEditableLayer();
    app.canvas.pushUndoSnapshot();

    lps::RectShapeProps rp{};
    rp.uvPos = proj.hit ? proj.uv : app.hoverHit3D.uv;
    rp.pos3D = proj.hit ? proj.pos : centerPos;
    rp.nrm3D = proj.hit ? proj.normal : nCenter;
    rp.right3D = rightAxis;
    rp.worldWidth = worldW;
    rp.worldHeight = worldH;
    rp.anchoredIn3D = true;
    rp.filled = true;
    rp.fillColor = app.uiState.brush.color;
    rp.strokeColor = app.uiState.secondaryColor;
    rp.strokeWidthPx = app.uiState.rectStrokeWidthPx;
    rp.hasStroke = (app.uiState.rectStrokeWidthPx > 0.25f);
    rp.mirrorX = app.uiState.brush.mirrorX;

    if (app.canvas.addRectItem("Rectangle", rp, &app.mesh) >= 0) {
        app.uiState.activePropsCat = lps::PropertiesCategory::Ruler;
        syncCanvasGpu(app, true);
        app.uiState.statusText = "Created 3D Vector Rectangle";
    }
}

lps::Vec3 computeSurfaceRight3D(const AppState& app, const lps::Vec3& normal) {
    lps::Vec3 eye, fwd, camRight, up;
    computeCameraBasis(app, eye, fwd, camRight, up);
    (void)eye; (void)fwd; (void)up;
    lps::Vec3 t = camRight - normal * lps::dot(camRight, normal);
    if (lps::lengthSq(t) < 1e-5f) {
        lps::Vec3 fb = (std::fabs(normal.x) < 0.85f) ? lps::Vec3{1.0f, 0.0f, 0.0f} : lps::Vec3{0.0f, 0.0f, 1.0f};
        t = lps::cross(fb, normal);
    }
    return lps::normalized(t);
}

int importImageFileAsItem(AppState& app, const std::string& path, const lps::RayHit* optHit = nullptr) {
    if (path.empty()) return -1;
    int w = 0, h = 0, ch = 0;
    uint8_t* px = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (!px || w <= 0 || h <= 0) {
        if (px) stbi_image_free(px);
        app.uiState.statusText = "Failed to import image: " + std::filesystem::path(path).filename().string();
        return -1;
    }
    std::vector<uint8_t> rgba(px, px + static_cast<size_t>(w) * h * 4u);
    stbi_image_free(px);

    lps::DecalLayerProps props{};
    if (optHit && optHit->hit) {
        props.mirrorMode = (!app.uiState.brush.mirrorX ||
                            std::fabs(optHit->pos.x - app.mesh.centerSymmetryX) < 0.08f)
                           ? lps::MirrorTextMode::None
                           : lps::MirrorTextMode::ReadableBothSides;
        props.uvPos = optHit->uv;
        props.pos3D = optHit->pos;
        props.nrm3D = optHit->normal;
        props.right3D = computeSurfaceRight3D(app, optHit->normal);
        props.anchoredIn3D = true;
    } else if (app.mesh.loaded) {
        lps::RayHit doorHit = app.mesh.raycast(
            {lps::Vec3{3.5f, 0.52f, 0.10f}, lps::Vec3{-1.0f, 0.0f, 0.0f}}, true);
        if (doorHit.hit) {
            props.mirrorMode = app.uiState.brush.mirrorX
                               ? lps::MirrorTextMode::ReadableBothSides
                               : lps::MirrorTextMode::None;
            props.uvPos = doorHit.uv;
            props.pos3D = doorHit.pos;
            props.nrm3D = doorHit.normal;
            props.right3D = computeSurfaceRight3D(app, doorHit.normal);
            props.anchoredIn3D = true;
        }
    }

    app.canvas.ensureEditableLayer();
    app.canvas.pushUndoSnapshot();
    std::string stem = std::filesystem::path(path).stem().string();
    int itemIdx = app.canvas.addImageItemFromRgba(stem.empty() ? "Image" : stem, rgba, w, h, props, &app.mesh);
    if (itemIdx >= 0) {
        app.uiState.activeMode = lps::StudioMode::TexturePaint;
        app.uiState.activeTool = lps::StudioTool::Image;
        app.uiState.activePropsCat = lps::PropertiesCategory::Decal;
        syncCanvasGpu(app, true);
        app.uiState.statusText = "Imported image: " + std::filesystem::path(path).filename().string();
    }
    return itemIdx;
}

bool performPasteClipboard(AppState& app, bool atHover3D) {
    lps::Vec2 optUv{0.5f, 0.5f};
    const lps::Vec2* uvPtr = nullptr;
    const lps::RayHit* hitPtr = nullptr;
    if (atHover3D && app.hoverHit3D.hit) {
        optUv = app.hoverHit3D.uv;
        uvPtr = &optUv;
        hitPtr = &app.hoverHit3D;
    }

    std::vector<uint8_t> clipRgba;
    int cw = 0, ch = 0;
    if (tryReadClipboardRgba(app.hwnd, clipRgba, cw, ch)) {
        app.canvas.ensureEditableLayer();
        app.canvas.pushUndoSnapshot();
        int idx = app.canvas.pasteExternalImageRgba("Clipboard Image", clipRgba, cw, ch, &app.mesh, uvPtr, hitPtr);
        if (idx >= 0) {
            app.uiState.activeMode = lps::StudioMode::TexturePaint;
            app.uiState.activeTool = lps::StudioTool::Select;
            syncCanvasGpu(app, true);
            app.uiState.statusText = "Pasted clipboard image (" + std::to_string(cw) + "x" + std::to_string(ch) + ")";
            return true;
        }
    }

    if (app.canvas.hasClipboardItem) {
        app.canvas.ensureEditableLayer();
        app.canvas.pushUndoSnapshot();
        int idx = app.canvas.pasteClipboardItem(&app.mesh, uvPtr, hitPtr);
        if (idx >= 0) {
            app.uiState.activeMode = lps::StudioMode::TexturePaint;
            app.uiState.activeTool = lps::StudioTool::Select;
            syncCanvasGpu(app, true);
            app.uiState.statusText = "Pasted item";
            return true;
        }
    }

    app.uiState.statusText = "Clipboard has no Coatroom item or bitmap";
    return false;
}

bool getActivePenProps(AppState& app, lps::RulerStripeProps*& outPen, int& outLayerIdx, int& outItemIdx) {
    outPen = nullptr;
    outLayerIdx = app.canvas.activeLayerIdx;
    outItemIdx = -1;
    if (auto* item = app.canvas.activeVectorItem()) {
        if (item->kind == lps::VectorItemKind::PenPath &&
            outLayerIdx >= 0 && outLayerIdx < static_cast<int>(app.canvas.layers.size()) &&
            !app.canvas.layers[outLayerIdx].locked) {
            outPen = &item->pen;
            outItemIdx = app.canvas.selectedItemIdx;
            return true;
        }
    }
    if (outLayerIdx >= 0 && outLayerIdx < static_cast<int>(app.canvas.layers.size())) {
        lps::Layer& l = app.canvas.layers[outLayerIdx];
        if (l.kind == lps::LayerKind::RulerStripe && !l.locked) {
            outPen = &l.ruler;
            return true;
        }
    }
    return false;
}

void rasterizeActiveLayerAndSync(AppState& app, int layerIdx, bool genMips = true) {
    if (layerIdx < 0 || layerIdx >= static_cast<int>(app.canvas.layers.size())) return;
    if (app.canvas.layers[layerIdx].kind == lps::LayerKind::RasterPaint) {
        app.canvas.rasterizeLayer(layerIdx, &app.mesh);
    } else {
        app.canvas.rasterizeVectorLayer(layerIdx, &app.mesh);
    }
    syncCanvasGpu(app, genMips);
}

lps::RayHit apply3DRulerSnap(const AppState& app, const lps::RayHit& rawHit,
                             const std::vector<lps::StripeNode>& nodes, bool shiftHeld,
                             float& outSegLenM, float& outSnapDeg) {
    outSegLenM = 0.0f;
    outSnapDeg = 0.0f;
    if (!rawHit.hit || nodes.empty() || !nodes.back().has3D) return rawHit;

    lps::Vec3 anchor = nodes.back().pos3D;
    lps::Vec3 d = rawHit.pos - anchor;
    float len = lps::length(d);
    outSegLenM = len;
    if (!shiftHeld || len < 1e-4f) {
        outSnapDeg = std::atan2(d.y, std::hypot(d.x, d.z)) * lps::kRad2Deg;
        return rawHit;
    }

    // Build candidate unit directions: world X/Y/Z axes + 45-degree diagonals in XY, XZ, YZ
    static const lps::Vec3 kDirs[] = {
        { 1, 0, 0}, {-1, 0, 0}, { 0, 1, 0}, { 0,-1, 0}, { 0, 0, 1}, { 0, 0,-1},
        { 0.70710678f,  0.70710678f, 0.0f}, { 0.70710678f, -0.70710678f, 0.0f},
        {-0.70710678f,  0.70710678f, 0.0f}, {-0.70710678f, -0.70710678f, 0.0f},
        { 0.70710678f, 0.0f,  0.70710678f}, { 0.70710678f, 0.0f, -0.70710678f},
        {-0.70710678f, 0.0f,  0.70710678f}, {-0.70710678f, 0.0f, -0.70710678f},
        { 0.0f,  0.70710678f,  0.70710678f}, { 0.0f,  0.70710678f, -0.70710678f},
        { 0.0f, -0.70710678f,  0.70710678f}, { 0.0f, -0.70710678f, -0.70710678f},
    };
    lps::Vec3 dNorm = d / len;
    lps::Vec3 bestDir = dNorm;
    float bestDot = -2.0f;
    for (const lps::Vec3& cand : kDirs) {
        // Prefer directions somewhat tangent to the surface
        float dt = lps::dot(cand, dNorm);
        if (dt > bestDot) {
            bestDot = dt;
            bestDir = cand;
        }
    }
    lps::Vec3 targetPos = anchor + bestDir * len;
    lps::RayHit proj = app.mesh.projectPointToSurface(targetPos, rawHit.normal, std::max(0.25f, len * 0.45f));
    if (proj.hit) {
        outSegLenM = lps::length(proj.pos - anchor);
        outSnapDeg = std::atan2(bestDir.y, std::hypot(bestDir.x, bestDir.z)) * lps::kRad2Deg;
        return proj;
    }
    return rawHit;
}

void appendSurfaceCircleLines(std::vector<lps::LineVertex3D>& outLines, const lps::Vec3& center,
                              const lps::Vec3& normal, float radius, const lps::Vec4& col, int segs = 28) {
    lps::Vec3 n = lps::normalized(normal);
    lps::Vec3 fb = (std::fabs(n.y) < 0.88f) ? lps::Vec3{0.0f, 1.0f, 0.0f} : lps::Vec3{1.0f, 0.0f, 0.0f};
    lps::Vec3 t = lps::normalized(lps::cross(fb, n));
    lps::Vec3 b = lps::normalized(lps::cross(n, t));
    lps::Vec3 lift = n * 0.005f;
    for (int i = 0; i < segs; ++i) {
        float a0 = (lps::kTwoPi * i) / segs;
        float a1 = (lps::kTwoPi * (i + 1)) / segs;
        lps::Vec3 p0 = center + lift + (t * std::cos(a0) + b * std::sin(a0)) * radius;
        lps::Vec3 p1 = center + lift + (t * std::cos(a1) + b * std::sin(a1)) * radius;
        outLines.push_back({p0, col});
        outLines.push_back({p1, col});
    }
}

const lps::RulerStripeProps* getActivePenPropsConst(const AppState& app) {
    int lIdx = app.canvas.activeLayerIdx;
    if (lIdx < 0 || lIdx >= static_cast<int>(app.canvas.layers.size())) return nullptr;
    const lps::Layer& l = app.canvas.layers[lIdx];
    if (app.canvas.selectedItemIdx >= 0 &&
        app.canvas.selectedItemIdx < static_cast<int>(l.items.size()) &&
        l.items[app.canvas.selectedItemIdx].kind == lps::VectorItemKind::PenPath) {
        return &l.items[app.canvas.selectedItemIdx].pen;
    }
    if (l.kind == lps::LayerKind::RulerStripe) return &l.ruler;
    return nullptr;
}

bool projectObbScreen(const AppState& app, const Frame3D& f,
                      ImVec2 outCorners[4], ImVec2& outRotKnob, ImVec2& outCenterScr) {
    lps::Vec3 lift = f.N * 0.012f;
    lps::Vec3 wPts[4] = {
        f.c + lift - f.T * f.hw - f.B * f.hh,
        f.c + lift + f.T * f.hw - f.B * f.hh,
        f.c + lift + f.T * f.hw + f.B * f.hh,
        f.c + lift - f.T * f.hw + f.B * f.hh,
    };
    for (int k = 0; k < 4; ++k) {
        if (!projectWorldToViewport3D(app, wPts[k], outCorners[k])) return false;
    }
    lps::Vec3 knobW = f.c + lift - f.B * (f.hh + std::max(0.045f, f.hh * 0.42f));
    if (!projectWorldToViewport3D(app, knobW, outRotKnob)) return false;
    if (!projectWorldToViewport3D(app, f.c + lift, outCenterScr)) return false;
    return true;
}

bool pointInFrame3DObb(const lps::Vec3& hitPos, const lps::Vec3& hitNrm, const Frame3D& f) {
    if (lps::dot(hitNrm, f.N) < 0.25f) return false;
    lps::Vec3 d = hitPos - f.c;
    if (std::fabs(lps::dot(d, f.N)) > 0.35f) return false;
    return std::fabs(lps::dot(d, f.T)) <= f.hw * 1.12f && std::fabs(lps::dot(d, f.B)) <= f.hh * 1.25f;
}

void buildViewport3DLines(const AppState& app, std::vector<lps::LineVertex3D>& depthLines,
                          std::vector<lps::LineVertex3D>& xrayLines) {
    depthLines.clear();
    xrayLines.clear();

    // 1. Optional Symmetry X=center plane indicator
    if (app.uiState.material.showSymmetryPlane && app.mesh.loaded) {
        float sx = app.mesh.centerSymmetryX;
        float y0 = app.mesh.bounds.min.y - 0.02f;
        float y1 = app.mesh.bounds.max.y + 0.18f;
        float z0 = app.mesh.bounds.min.z - 0.25f;
        float z1 = app.mesh.bounds.max.z + 0.25f;
        lps::Vec4 symCol{0.23f, 0.51f, 0.96f, 0.48f};
        depthLines.push_back({{sx, y0, z0}, symCol}); depthLines.push_back({{sx, y0, z1}, symCol});
        depthLines.push_back({{sx, y1, z0}, symCol}); depthLines.push_back({{sx, y1, z1}, symCol});
        depthLines.push_back({{sx, y0, z0}, symCol}); depthLines.push_back({{sx, y1, z0}, symCol});
        depthLines.push_back({{sx, y0, z1}, symCol}); depthLines.push_back({{sx, y1, z1}, symCol});
    }

    if (!app.mesh.loaded) return;

    // 2. 3D Brush / Eraser live tangent reticle (+ Mirror-X reticle)
    if ((app.uiState.activeTool == lps::StudioTool::Brush || app.uiState.activeTool == lps::StudioTool::Eraser) &&
        app.uiState.viewport3DHovered && app.hoverHit3D.hit) {
        float r = app.uiState.brush.project3DBrush
                  ? app.uiState.brush.worldRadius
                  : std::clamp(app.uiState.brush.radiusPx / 650.0f, 0.015f, 0.35f);
        appendSurfaceCircleLines(xrayLines, app.hoverHit3D.pos, app.hoverHit3D.normal, r,
                                 {0.94f, 0.96f, 0.99f, 0.85f});
        if (app.uiState.brush.mirrorX) {
            lps::Vec3 mp{2.0f * app.mesh.centerSymmetryX - app.hoverHit3D.pos.x,
                         app.hoverHit3D.pos.y, app.hoverHit3D.pos.z};
            lps::Vec3 mn{-app.hoverHit3D.normal.x, app.hoverHit3D.normal.y, app.hoverHit3D.normal.z};
            appendSurfaceCircleLines(xrayLines, mp, mn, r, {0.23f, 0.51f, 0.96f, 0.72f});
        }
    }

    // 3. 3D Ruler / Pen polyline & Shift-snap preview
    if (app.uiState.activeTool == lps::StudioTool::StraightRuler ||
        app.uiState.activeTool == lps::StudioTool::CurveBlade ||
        app.uiState.activeTool == lps::StudioTool::NodeEdit) {
        if (const lps::RulerStripeProps* penP = getActivePenPropsConst(app)) {
            const auto& nodes = penP->nodes;
            lps::Vec4 lineCol{0.28f, 0.45f, 0.70f, 0.92f};
            lps::Vec4 nodeCol{0.92f, 0.56f, 0.12f, 0.96f};
            for (size_t i = 0; i + 1 < nodes.size(); ++i) {
                if (nodes[i].has3D && nodes[i + 1].has3D) {
                    xrayLines.push_back({nodes[i].pos3D + nodes[i].nrm3D * 0.004f, lineCol});
                    xrayLines.push_back({nodes[i + 1].pos3D + nodes[i + 1].nrm3D * 0.004f, lineCol});
                }
            }
            for (const auto& n : nodes) {
                if (!n.has3D) continue;
                lps::Vec3 p = n.pos3D + n.nrm3D * 0.006f;
                float s = 0.022f;
                xrayLines.push_back({p - lps::Vec3{s, 0, 0}, nodeCol}); xrayLines.push_back({p + lps::Vec3{s, 0, 0}, nodeCol});
                xrayLines.push_back({p - lps::Vec3{0, s, 0}, nodeCol}); xrayLines.push_back({p + lps::Vec3{0, s, 0}, nodeCol});
                xrayLines.push_back({p - lps::Vec3{0, 0, s}, nodeCol}); xrayLines.push_back({p + lps::Vec3{0, 0, s}, nodeCol});
            }
            if (app.rulerDrawing3D && !nodes.empty() && nodes.back().has3D &&
                app.uiState.viewport3DHovered && app.hoverHit3D.hit && app.rulerDragNodeIdx3D < 0) {
                float lenM = 0.0f, deg = 0.0f;
                lps::RayHit snapped = apply3DRulerSnap(app, app.hoverHit3D, nodes, ImGui::GetIO().KeyShift, lenM, deg);
                lps::Vec3 pA = nodes.back().pos3D + nodes.back().nrm3D * 0.006f;
                lps::Vec3 pB = snapped.pos + snapped.normal * 0.006f;
                xrayLines.push_back({pA, {0.92f, 0.56f, 0.12f, 0.95f}});
                xrayLines.push_back({pB, {0.92f, 0.56f, 0.12f, 0.95f}});
            }
        }
    }

    // 3b. 3D RectBox preview
    if (app.uiState.activeTool == lps::StudioTool::RectBox && app.rectDrawing3D) {
        lps::Vec3 rel = app.rectCurrPos3D - app.rectStartPos3D;
        float uSpan = lps::dot(rel, app.rectRight3D);
        float vSpan = lps::dot(rel, app.rectDown3D);
        if (ImGui::GetIO().KeyShift) {
            float s = std::max(std::fabs(uSpan), std::fabs(vSpan));
            uSpan = (uSpan >= 0.0f) ? s : -s;
            vSpan = (vSpan >= 0.0f) ? s : -s;
        }
        lps::Vec3 lift = app.rectStartNrm3D * 0.006f;
        lps::Vec3 c0 = app.rectStartPos3D + lift;
        lps::Vec3 c1 = c0 + app.rectRight3D * uSpan;
        lps::Vec3 c2 = c1 + app.rectDown3D * vSpan;
        lps::Vec3 c3 = c0 + app.rectDown3D * vSpan;
        lps::Vec4 bCol{0.28f, 0.45f, 0.70f, 0.96f};
        xrayLines.push_back({c0, bCol}); xrayLines.push_back({c1, bCol});
        xrayLines.push_back({c1, bCol}); xrayLines.push_back({c2, bCol});
        xrayLines.push_back({c2, bCol}); xrayLines.push_back({c3, bCol});
        xrayLines.push_back({c3, bCol}); xrayLines.push_back({c0, bCol});
    }

    // 4. Edit Mesh selection centroid crosshair
    if (app.uiState.activeMode == lps::StudioMode::EditMesh) {
        lps::Vec3 sum{0, 0, 0};
        int cnt = 0;
        for (size_t i = 0; i < app.mesh.vertexSelected.size(); ++i) {
            if (app.mesh.vertexSelected[i]) {
                sum += app.mesh.uniquePositions[i];
                ++cnt;
            }
        }
        if (cnt > 0) {
            lps::Vec3 c = sum * (1.0f / static_cast<float>(cnt));
            float a = 0.14f;
            xrayLines.push_back({c - lps::Vec3{a, 0, 0}, {0.95f, 0.30f, 0.30f, 0.95f}});
            xrayLines.push_back({c + lps::Vec3{a, 0, 0}, {0.95f, 0.30f, 0.30f, 0.95f}});
            xrayLines.push_back({c - lps::Vec3{0, a, 0}, {0.30f, 0.92f, 0.45f, 0.95f}});
            xrayLines.push_back({c + lps::Vec3{0, a, 0}, {0.30f, 0.92f, 0.45f, 0.95f}});
            xrayLines.push_back({c - lps::Vec3{0, 0, a}, {0.25f, 0.56f, 0.98f, 0.95f}});
            xrayLines.push_back({c + lps::Vec3{0, 0, a}, {0.25f, 0.56f, 0.98f, 0.95f}});
        }
    }
}

void handle3DViewportInput(AppState& app) {
    ImGuiIO& io = ImGui::GetIO();
    bool hovered = app.uiState.viewport3DHovered;
    bool active = app.uiState.viewport3DActive;

    // 1. Zoom (Mouse Wheel)
    if (hovered && std::fabs(io.MouseWheel) > 0.0f) {
        float scale = std::pow(0.88f, io.MouseWheel);
        app.camDist = std::clamp(app.camDist * scale, 0.7f, 32.0f);
        app.sceneParams.orthoHalfHeight = std::clamp(app.sceneParams.orthoHalfHeight * scale, 0.25f, 15.0f);
    }

    // 2. Orbit, Pan & Scrubby Zoom (RMB / MMB / Orbit / HandPan / Zoom)
    bool rmbDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    bool mmbDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    bool lmbDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    bool orbitLmb = (app.uiState.activeTool == lps::StudioTool::Orbit ||
                     app.uiState.activeMode == lps::StudioMode::Object) && lmbDown;
    bool panLmb = (app.uiState.activeTool == lps::StudioTool::HandPan) && lmbDown;
    bool zoomLmb = (app.uiState.activeTool == lps::StudioTool::Zoom) && lmbDown;

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        app.rmbDragging = true;
        app.rmbAccumDrag = 0.0f;
    }

    if ((hovered || active) && (rmbDown || mmbDown || orbitLmb || panLmb || zoomLmb)) {
        float dx = io.MouseDelta.x;
        float dy = io.MouseDelta.y;
        if (rmbDown) app.rmbAccumDrag += std::hypot(dx, dy);

        if (std::fabs(dx) > 0.0f || std::fabs(dy) > 0.0f) {
            bool panMode = panLmb || ((io.KeyShift || io.KeyAlt) && (rmbDown || mmbDown));
            if (zoomLmb) {
                float scale = std::pow(0.992f, dx - dy);
                app.camDist = std::clamp(app.camDist * scale, 0.7f, 32.0f);
                app.sceneParams.orthoHalfHeight = std::clamp(app.sceneParams.orthoHalfHeight * scale, 0.25f, 15.0f);
            } else if (panMode) {
                lps::Vec3 eye, fwd, right, up;
                computeCameraBasis(app, eye, fwd, right, up);
                (void)eye; (void)fwd;
                float vpH = std::max(app.uiState.viewport3DPanelSize.y, 16.0f);
                float worldSpanH = app.uiState.isOrthoCamera
                                   ? (app.sceneParams.orthoHalfHeight * 2.0f)
                                   : (2.0f * app.camDist * std::tan(app.sceneParams.fovYDeg * 0.5f * lps::kDeg2Rad));
                float panScale = worldSpanH / vpH;
                app.camTarget -= right * (dx * panScale);
                app.camTarget += up * (dy * panScale);
            } else {
                app.camYaw += dx * 0.007f;
                app.camPitch = std::clamp(app.camPitch + dy * 0.007f, -1.50f, 1.50f);
            }
        }
    }

    if (app.rmbDragging && !rmbDown) {
        if (app.rmbAccumDrag < 4.5f && hovered) {
            bool handledRmb = false;
            if (app.uiState.activeTool == lps::StudioTool::StraightRuler ||
                app.uiState.activeTool == lps::StudioTool::CurveBlade ||
                app.uiState.activeTool == lps::StudioTool::NodeEdit) {
                lps::RulerStripeProps* pen = nullptr;
                int penLIdx = -1, penIIdx = -1;
                if (getActivePenProps(app, pen, penLIdx, penIIdx) && pen && !pen->nodes.empty()) {
                    for (int ni = static_cast<int>(pen->nodes.size()) - 1; ni >= 0; --ni) {
                        if (!pen->nodes[ni].has3D) continue;
                        ImVec2 sp;
                        if (projectWorldToViewport3D(app, pen->nodes[ni].pos3D + pen->nodes[ni].nrm3D * 0.007f, sp) &&
                            std::hypot(io.MousePos.x - sp.x, io.MousePos.y - sp.y) <= 10.5f) {
                            app.canvas.pushUndoSnapshot();
                            pen->nodes.erase(pen->nodes.begin() + ni);
                            if (pen->nodes.size() < 3) pen->closed = false;
                            app.rulerDragNodeIdx3D = -1;
                            app.penDragNodeIdx3D = -1;
                            app.penDragMode3D = 0;
                            rasterizeActiveLayerAndSync(app, penLIdx, true);
                            app.uiState.statusText = "Removed Pen anchor point";
                            handledRmb = true;
                            break;
                        }
                    }
                }
            }
            if (!handledRmb && app.rulerDrawing3D) {
                app.rulerDrawing3D = false;
                app.uiState.statusText = "Finished 3D Pen path";
                handledRmb = true;
            }
            if (!handledRmb) {
                app.uiState.request3DContextMenu = true;
            }
        }
        app.rmbDragging = false;
    }

    // 3. Hover 3D Raycast
    if (hovered && app.mesh.loaded) {
        lps::Ray ray = screenPosToRay3D(app, io.MousePos);
        app.hoverHit3D = app.mesh.raycast(ray, true);
        if (app.hoverHit3D.hit) {
            app.uiState.hoverUvValid = true;
            app.uiState.lastHoverUv = app.hoverHit3D.uv;
        }
    } else if (!hovered) {
        app.hoverHit3D.hit = false;
    }

    if (!app.mesh.loaded) return;

    bool allowLmbTool = !io.KeyAlt && !rmbDown && !mmbDown &&
                        app.uiState.activeTool != lps::StudioTool::Orbit &&
                        app.uiState.activeTool != lps::StudioTool::HandPan &&
                        app.uiState.activeTool != lps::StudioTool::Zoom &&
                        app.uiState.activeMode != lps::StudioMode::Object;

    int activeIdx = app.canvas.activeLayerIdx;
    lps::Layer* activeLayer = (activeIdx >= 0 && activeIdx < static_cast<int>(app.canvas.layers.size()))
                              ? &app.canvas.layers[activeIdx] : nullptr;

    // 4. Edit Mesh Mode (Vertex G/R/S + Face Paint Mask)
    if (app.uiState.activeMode == lps::StudioMode::EditMesh ||
        app.uiState.activeTool == lps::StudioTool::VertexEdit ||
        app.uiState.activeTool == lps::StudioTool::FacePaintMask) {
        if (app.uiState.activeTool == lps::StudioTool::FacePaintMask) {
            if (hovered && allowLmbTool && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                lps::Ray r = screenPosToRay3D(app, io.MousePos);
                int marked = app.mesh.selectFacePaintMaskByRay(
                    r, app.uiState.faceMaskConnectedAngle, app.uiState.faceMaskAngleDeg,
                    io.KeyShift, app.uiState.brush.mirrorX);
                app.uiState.meshGpuDirty = true;
                char msg[96];
                std::snprintf(msg, sizeof(msg), "Face Mask: %d triangles marked", marked);
                app.uiState.statusText = msg;
            }
            return;
        }

        // Vertex Edit + G/R/S interactive drag
        if (hovered && allowLmbTool && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            lps::Ray r = screenPosToRay3D(app, io.MousePos);
            int selCnt = 0;
            lps::Vec3 selCenter{0, 0, 0};
            for (size_t i = 0; i < app.mesh.vertexSelected.size(); ++i) {
                if (app.mesh.vertexSelected[i]) { selCenter += app.mesh.uniquePositions[i]; ++selCnt; }
            }
            if (selCnt > 0) selCenter = selCenter * (1.0f / static_cast<float>(selCnt));

            bool reselect = (selCnt == 0) || io.KeyShift || !app.hoverHit3D.hit ||
                            (lps::length(app.hoverHit3D.pos - selCenter) > app.uiState.vertexSelectRadius * 2.5f);
            if (reselect) {
                int picked = app.mesh.selectNearRay(r, app.uiState.vertexSelectRadius,
                                                    io.KeyShift, app.uiState.brush.mirrorX);
                app.uiState.meshGpuDirty = true;
                char msg[96];
                std::snprintf(msg, sizeof(msg), "Selected %d vertices", picked);
                app.uiState.statusText = msg;
            }

            selCnt = 0;
            selCenter = {0, 0, 0};
            for (size_t i = 0; i < app.mesh.vertexSelected.size(); ++i) {
                if (app.mesh.vertexSelected[i]) { selCenter += app.mesh.uniquePositions[i]; ++selCnt; }
            }
            if (selCnt > 0) {
                app.editSelCenter = selCenter * (1.0f / static_cast<float>(selCnt));
                app.editStartPositions = app.mesh.uniquePositions;
                app.editDragStartMouse = lps::Vec2{io.MousePos.x, io.MousePos.y};
                app.editDraggingVerts3D = true;
                app.editHudBadge.clear();
            }
        }

        if (app.editDraggingVerts3D) {
            lps::Vec2 dScr = lps::Vec2{io.MousePos.x, io.MousePos.y} - app.editDragStartMouse;
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                if (lps::length(dScr) > 4.0f && app.editStartPositions.size() == app.mesh.uniquePositions.size()) {
                    lps::Vec3 eye, fwd, right, up;
                    computeCameraBasis(app, eye, fwd, right, up);
                    float distToSel = std::max(0.4f, lps::length(app.editSelCenter - eye));
                    float vpH = std::max(app.uiState.viewport3DPanelSize.y, 16.0f);
                    float worldPerPx = (2.0f * distToSel * std::tan(app.sceneParams.fovYDeg * 0.5f * lps::kDeg2Rad)) / vpH;

                    if (app.uiState.editSubTool == lps::EditSubTool::Grab) {
                        lps::Vec3 delta = right * (dScr.x * worldPerPx) - up * (dScr.y * worldPerPx);
                        for (size_t i = 0; i < app.mesh.uniquePositions.size(); ++i) {
                            if (!app.mesh.vertexSelected[i]) continue;
                            lps::Vec3 di = delta;
                            if (app.uiState.brush.mirrorX && app.editStartPositions[i].x < app.mesh.centerSymmetryX) {
                                di.x = -di.x;
                            }
                            app.mesh.uniquePositions[i] = app.editStartPositions[i] + di;
                        }
                        char b[64];
                        std::snprintf(b, sizeof(b), "Grab: (%.3f, %.3f, %.3f) m", delta.x, delta.y, delta.z);
                        app.editHudBadge = b;
                    } else if (app.uiState.editSubTool == lps::EditSubTool::Scale) {
                        float sf = std::clamp(1.0f + (dScr.x - dScr.y) * 0.004f, 0.2f, 3.0f);
                        for (size_t i = 0; i < app.mesh.uniquePositions.size(); ++i) {
                            if (!app.mesh.vertexSelected[i]) continue;
                            app.mesh.uniquePositions[i] = app.editSelCenter + (app.editStartPositions[i] - app.editSelCenter) * sf;
                        }
                        char b[48];
                        std::snprintf(b, sizeof(b), "Scale: %.3fx", sf);
                        app.editHudBadge = b;
                    } else {
                        float ang = dScr.x * 0.008f;
                        float cs = std::cos(ang), sn = std::sin(ang);
                        for (size_t i = 0; i < app.mesh.uniquePositions.size(); ++i) {
                            if (!app.mesh.vertexSelected[i]) continue;
                            float aUse = (app.uiState.brush.mirrorX && app.editStartPositions[i].x < app.mesh.centerSymmetryX)
                                         ? -ang : ang;
                            float cUse = std::cos(aUse), sUse = std::sin(aUse);
                            lps::Vec3 p = app.editStartPositions[i] - app.editSelCenter;
                            // Rodrigues rotation around camera forward axis
                            lps::Vec3 rotP = p * cUse + lps::cross(fwd, p) * sUse + fwd * (lps::dot(fwd, p) * (1.0f - cUse));
                            (void)cs; (void)sn;
                            app.mesh.uniquePositions[i] = app.editSelCenter + rotP;
                        }
                        char b[48];
                        std::snprintf(b, sizeof(b), "Rotate: %.1f deg", ang * lps::kRad2Deg);
                        app.editHudBadge = b;
                    }
                    app.mesh.syncRenderVertices();
                    app.uiState.meshGpuDirty = true;
                }
            } else {
                if (lps::length(dScr) > 4.0f) {
                    app.mesh.recomputeSmoothNormals();
                    app.mesh.syncRenderVertices();
                    app.mesh.rebuildBoundsAndBVH();
                    app.uiState.meshGpuDirty = true;
                    app.uiState.statusText = app.editHudBadge.empty() ? "Edited vertices" : app.editHudBadge;
                }
                app.editDraggingVerts3D = false;
                app.editStartPositions.clear();
                app.editHudBadge.clear();
            }
        }
        return;
    }

    // 5. Texture Paint Mode Tools on 3D Mesh
    // -- BRUSH / ERASER --
    if (app.uiState.activeTool == lps::StudioTool::Brush ||
        app.uiState.activeTool == lps::StudioTool::Eraser) {
        if (hovered && allowLmbTool && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && app.hoverHit3D.hit) {
            if (activeLayer && activeLayer->locked) {
                app.uiState.statusText = "Active layer is locked";
            } else {
                if (!activeLayer || activeLayer->kind != lps::LayerKind::RasterPaint) {
                    activeIdx = app.canvas.ensureEditableLayer();
                    activeLayer = &app.canvas.layers[activeIdx];
                }
                if (activeLayer && activeLayer->kind == lps::LayerKind::RasterPaint && !activeLayer->locked) {
                    lps::BrushSettings bs = app.uiState.brush;
                    bs.eraser = (app.uiState.activeTool == lps::StudioTool::Eraser);
                    app.canvas.beginStroke(activeIdx);
                    app.canvas.paintDabAt3D(activeIdx, app.mesh, app.hoverHit3D, bs, nullptr);
                    app.painting3D = true;
                    app.prevHit3D = app.hoverHit3D;
                    syncCanvasGpu(app);
                }
            }
        }
        if (app.painting3D) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                lps::Ray r = screenPosToRay3D(app, io.MousePos);
                lps::RayHit hit = app.mesh.raycast(r, true);
                lps::BrushSettings bs = app.uiState.brush;
                bs.eraser = (app.uiState.activeTool == lps::StudioTool::Eraser);
                float minStep = std::max(0.006f, bs.worldRadius * std::max(0.12f, bs.spacing * 0.5f));
                if (hit.hit && (!app.prevHit3D.hit || lps::length(hit.pos - app.prevHit3D.pos) >= minStep)) {
                    app.canvas.paintDabAt3D(activeIdx, app.mesh, hit, bs, app.prevHit3D.hit ? &app.prevHit3D : nullptr);
                    app.prevHit3D = hit;
                    double now = ImGui::GetTime();
                    if (now - app.lastStrokeUploadTime > 0.014) {
                        syncCanvasGpu(app, true);
                        app.lastStrokeUploadTime = now;
                    }
                } else if (!hit.hit) {
                    app.prevHit3D.hit = false;
                }
            } else {
                app.canvas.endStroke(activeIdx);
                app.painting3D = false;
                syncCanvasGpu(app, true);
            }
        }
        return;
    }

    // -- RECTANGLE VECTOR TOOL IN 3D (U) --
    if (app.uiState.activeTool == lps::StudioTool::RectBox) {
        if (hovered && allowLmbTool && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && app.hoverHit3D.hit) {
            if (activeLayer && activeLayer->locked) {
                app.uiState.statusText = "Active layer is locked";
            } else {
                app.rectDrawing3D = true;
                app.rectStartPos3D = app.hoverHit3D.pos;
                app.rectStartNrm3D = app.hoverHit3D.normal;
                app.rectRight3D = computeSurfaceRight3D(app, app.hoverHit3D.normal);
                app.rectDown3D = lps::normalized(lps::cross(app.rectStartNrm3D, app.rectRight3D));
                app.rectCurrPos3D = app.hoverHit3D.pos;
            }
        }
        if (app.rectDrawing3D) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                if (app.hoverHit3D.hit) {
                    app.rectCurrPos3D = app.hoverHit3D.pos;
                }
            } else {
                app.rectDrawing3D = false;
                createRectItem3D(app, app.rectStartPos3D, app.rectCurrPos3D,
                                 app.rectStartNrm3D, app.rectRight3D, app.rectDown3D, io.KeyShift);
            }
        }
        return;
    }

    // -- STRAIGHT LINE (L) / CUBIC BEZIER PEN (P) / DIRECT SELECT (A) IN 3D --
    if (app.uiState.activeTool == lps::StudioTool::StraightRuler ||
        app.uiState.activeTool == lps::StudioTool::CurveBlade ||
        app.uiState.activeTool == lps::StudioTool::NodeEdit) {
        if (app.uiState.rulerFinishRequested) {
            app.rulerDrawing3D = false;
            app.rulerDragNodeIdx3D = -1;
            app.penDragMode3D = 0;
            app.penDragNodeIdx3D = -1;
            app.uiState.rulerFinishRequested = false;
        }
        auto syncNodeUvHandles3D = [&](lps::StripeNode& nd) {
            if (!nd.has3D) return;
            if (nd.hasHandles) {
                lps::RayHit mhIn = app.mesh.projectPointToSurface(nd.handleIn3D, nd.nrm3D, 0.30f);
                nd.handleInUv = mhIn.hit ? mhIn.uv : nd.uv;
                lps::RayHit mhOut = app.mesh.projectPointToSurface(nd.handleOut3D, nd.nrm3D, 0.30f);
                nd.handleOutUv = mhOut.hit ? mhOut.uv : nd.uv;
            } else {
                nd.handleInUv = nd.uv;
                nd.handleOutUv = nd.uv;
                nd.handleIn3D = nd.pos3D;
                nd.handleOut3D = nd.pos3D;
            }
        };

        lps::RulerStripeProps* penProps = nullptr;
        int penLayerIdx = -1, penItemIdx = -1;
        getActivePenProps(app, penProps, penLayerIdx, penItemIdx);

        if (hovered && penProps) {
            if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                app.rulerDrawing3D = false;
                app.penDragMode3D = 0;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && !penProps->nodes.empty()) {
                app.canvas.pushUndoSnapshot();
                penProps->nodes.pop_back();
                if (penProps->nodes.size() < 3) penProps->closed = false;
                rasterizeActiveLayerAndSync(app, penLayerIdx, true);
            }
        }

        const bool directEdit3D = (app.uiState.activeTool == lps::StudioTool::NodeEdit) || io.KeyCtrl;

        if (hovered && allowLmbTool && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            int hitNode = -1;
            int hitHandleType = 0; // 1=anchor, 2=handleIn, 3=handleOut
            if (penProps) {
                auto& nodes = penProps->nodes;
                if (directEdit3D) {
                    for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
                        if (!nodes[i].has3D || !nodes[i].hasHandles) continue;
                        ImVec2 spIn, spOut;
                        if (projectWorldToViewport3D(app, nodes[i].handleIn3D, spIn) &&
                            std::hypot(io.MousePos.x - spIn.x, io.MousePos.y - spIn.y) <= 9.0f) {
                            hitNode = i;
                            hitHandleType = 2;
                            break;
                        }
                        if (projectWorldToViewport3D(app, nodes[i].handleOut3D, spOut) &&
                            std::hypot(io.MousePos.x - spOut.x, io.MousePos.y - spOut.y) <= 9.0f) {
                            hitNode = i;
                            hitHandleType = 3;
                            break;
                        }
                    }
                }
                if (hitNode < 0) {
                    for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
                        if (!nodes[i].has3D) continue;
                        ImVec2 sp;
                        if (projectWorldToViewport3D(app, nodes[i].pos3D, sp) &&
                            std::hypot(io.MousePos.x - sp.x, io.MousePos.y - sp.y) <= 10.0f) {
                            hitNode = i;
                            hitHandleType = 1;
                            break;
                        }
                    }
                }
            }

            if (hitNode == 0 && hitHandleType == 1 && !directEdit3D &&
                app.uiState.activeTool == lps::StudioTool::CurveBlade &&
                app.rulerDrawing3D && penProps && penProps->nodes.size() >= 3) {
                app.canvas.pushUndoSnapshot();
                penProps->closed = true;
                app.rulerDrawing3D = false;
                rasterizeActiveLayerAndSync(app, penLayerIdx, true);
                app.uiState.statusText = "Closed 3D Bezier Pen path";
            } else if (hitNode >= 0 && penProps &&
                       (directEdit3D || app.uiState.activeTool == lps::StudioTool::StraightRuler)) {
                app.canvas.pushUndoSnapshot();
                auto& nd = penProps->nodes[hitNode];
                if (io.KeyAlt && hitHandleType == 1 && app.uiState.activeTool != lps::StudioTool::StraightRuler) {
                    nd.smooth = !nd.smooth;
                    if (!nd.smooth) {
                        nd.hasHandles = false;
                        nd.handleIn3D = nd.pos3D;
                        nd.handleOut3D = nd.pos3D;
                        syncNodeUvHandles3D(nd);
                        rasterizeActiveLayerAndSync(app, penLayerIdx, true);
                    }
                } else {
                    app.rulerDragNodeIdx3D = hitNode;
                    app.penDragNodeIdx3D = hitNode;
                    app.penDragMode3D = hitHandleType;
                    app.penDragStartPos3D = nd.pos3D;
                    app.penDragMouseDownPx = lps::Vec2{io.MousePos.x, io.MousePos.y};
                }
            } else if (!directEdit3D && app.hoverHit3D.hit) {
                if (activeLayer && activeLayer->locked) {
                    app.uiState.statusText = "Active layer is locked";
                } else {
                    const bool isPen = (app.uiState.activeTool == lps::StudioTool::CurveBlade);
                    app.canvas.pushUndoSnapshot();
                    if (!penProps) {
                        penLayerIdx = app.canvas.ensureEditableLayer();
                        lps::RulerStripeProps newPen{};
                        newPen.color = app.uiState.brush.color;
                        newPen.fillColor = app.uiState.brush.color;
                        newPen.outlineColor = app.uiState.secondaryColor;
                        newPen.mirrorX = app.uiState.brush.mirrorX;
                        newPen.smoothCurve = isPen;
                        newPen.use3DSpace = true;
                        penItemIdx = app.canvas.addPenItem(isPen ? "Pen Path" : "Line Path", newPen, &app.mesh);
                        getActivePenProps(app, penProps, penLayerIdx, penItemIdx);
                    }
                    if (penProps) {
                        penProps->smoothCurve = isPen;
                        penProps->use3DSpace = true;
                        float segLen = 0.0f, snapDeg = 0.0f;
                        lps::RayHit snapped = apply3DRulerSnap(app, app.hoverHit3D, penProps->nodes,
                                                               io.KeyShift, segLen, snapDeg);
                        lps::StripeNode sn{};
                        sn.uv = snapped.uv;
                        sn.handleInUv = snapped.uv;
                        sn.handleOutUv = snapped.uv;
                        sn.pos3D = snapped.pos;
                        sn.handleIn3D = snapped.pos;
                        sn.handleOut3D = snapped.pos;
                        sn.nrm3D = snapped.normal;
                        sn.has3D = true;
                        sn.hasHandles = false;
                        sn.smooth = isPen;
                        penProps->nodes.push_back(sn);
                        int newIdx = static_cast<int>(penProps->nodes.size()) - 1;
                        app.rulerDrawing3D = true;
                        if (isPen) {
                            app.penDragMode3D = 4;
                            app.penDragNodeIdx3D = newIdx;
                            app.penDragStartPos3D = snapped.pos;
                            app.penDragMouseDownPx = lps::Vec2{io.MousePos.x, io.MousePos.y};
                        }
                        if (penProps->nodes.size() >= 2) {
                            rasterizeActiveLayerAndSync(app, penLayerIdx, true);
                        }
                    }
                }
            }
        }

        if (app.penDragMode3D > 0 && app.penDragNodeIdx3D >= 0 && penProps &&
            app.penDragNodeIdx3D < static_cast<int>(penProps->nodes.size())) {
            auto& nd = penProps->nodes[app.penDragNodeIdx3D];
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                if (app.hoverHit3D.hit) {
                    float pxDrag = std::hypot(io.MousePos.x - app.penDragMouseDownPx.x,
                                              io.MousePos.y - app.penDragMouseDownPx.y);
                    bool changed = false;
                    if (app.penDragMode3D == 1) {
                        lps::Vec3 delta = app.hoverHit3D.pos - nd.pos3D;
                        nd.uv = app.hoverHit3D.uv;
                        nd.pos3D = app.hoverHit3D.pos;
                        nd.nrm3D = app.hoverHit3D.normal;
                        nd.has3D = true;
                        nd.handleIn3D += delta;
                        nd.handleOut3D += delta;
                        syncNodeUvHandles3D(nd);
                        changed = true;
                    } else if (app.penDragMode3D == 2) {
                        nd.handleIn3D = app.hoverHit3D.pos;
                        nd.hasHandles = true;
                        if (nd.smooth && !io.KeyAlt) nd.handleOut3D = nd.pos3D - (nd.handleIn3D - nd.pos3D);
                        syncNodeUvHandles3D(nd);
                        changed = true;
                    } else if (app.penDragMode3D == 3) {
                        nd.handleOut3D = app.hoverHit3D.pos;
                        nd.hasHandles = true;
                        if (nd.smooth && !io.KeyAlt) nd.handleIn3D = nd.pos3D - (nd.handleOut3D - nd.pos3D);
                        syncNodeUvHandles3D(nd);
                        changed = true;
                    } else if (app.penDragMode3D == 4 && pxDrag >= 4.0f) {
                        if (ImGui::IsKeyDown(ImGuiKey_Space)) {
                            lps::Vec3 offOut = nd.handleOut3D - nd.pos3D;
                            lps::Vec3 offIn = nd.handleIn3D - nd.pos3D;
                            nd.uv = app.hoverHit3D.uv;
                            nd.pos3D = app.hoverHit3D.pos;
                            nd.nrm3D = app.hoverHit3D.normal;
                            nd.handleOut3D = nd.pos3D + offOut;
                            nd.handleIn3D = nd.pos3D + offIn;
                        } else {
                            nd.handleOut3D = app.hoverHit3D.pos;
                            nd.hasHandles = true;
                            if (!io.KeyAlt) {
                                nd.smooth = true;
                                nd.handleIn3D = nd.pos3D - (nd.handleOut3D - nd.pos3D);
                            } else {
                                nd.smooth = false;
                            }
                        }
                        syncNodeUvHandles3D(nd);
                        changed = true;
                    }
                    if (changed) {
                        double now = ImGui::GetTime();
                        if (now - app.lastVectorRasterTime >= 0.014) {
                            rasterizeActiveLayerAndSync(app, penLayerIdx, true);
                            app.lastVectorRasterTime = now;
                        }
                    }
                }
            } else {
                rasterizeActiveLayerAndSync(app, penLayerIdx, true);
                app.penDragMode3D = 0;
                app.penDragNodeIdx3D = -1;
                app.rulerDragNodeIdx3D = -1;
            }
        }
        return;
    }

    // -- TEXT / IMAGE (DECALS) / VECTOR RECT / SELECT-MOVE OBB GIZMO PICKING & 60FPS DRAG IN 3D --
    auto getTargetFrame3D = [&](int lIdx, int iIdx, Frame3D& outF, float& outSize, float& outRot) -> bool {
        if (lIdx < 0 || lIdx >= static_cast<int>(app.canvas.layers.size())) return false;
        const lps::Layer& candL = app.canvas.layers[lIdx];
        if (!candL.visible) return false;
        if (iIdx >= 0 && iIdx < static_cast<int>(candL.items.size())) {
            const auto& it = candL.items[iIdx];
            if (!computeVectorItemFrame3D(it, outF)) return false;
            outRot = outF.rotDeg;
            if (it.kind == lps::VectorItemKind::Text) outSize = it.text.worldHeight;
            else if (it.kind == lps::VectorItemKind::Image) outSize = it.image.worldWidth;
            else if (it.kind == lps::VectorItemKind::RectShape) outSize = it.rect.worldWidth;
            else return false;
            return true;
        }
        if (!computeLegacyLayerFrame3D(candL, outF)) return false;
        outRot = outF.rotDeg;
        outSize = (candL.kind == lps::LayerKind::Text) ? candL.text.worldHeight : candL.decal.worldWidth;
        return true;
    };

    if (app.uiState.activeTool == lps::StudioTool::Text ||
        app.uiState.activeTool == lps::StudioTool::TouchType ||
        app.uiState.activeTool == lps::StudioTool::SponsorDecal ||
        app.uiState.activeTool == lps::StudioTool::SelectMove) {
        if (hovered && allowLmbTool && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            // In Touch Type (Shift+T), clicking a letter on the 3D surface selects that character directly
            if (app.uiState.activeTool == lps::StudioTool::TouchType && app.hoverHit3D.hit) {
                auto* actIt = app.canvas.activeVectorItem();
                if (actIt && actIt->kind == lps::VectorItemKind::Text && !actIt->text.lastCharBoxesUv.empty()) {
                    float bestD = 0.03f;
                    int bestCi = -1;
                    for (int ci = 0; ci < static_cast<int>(actIt->text.lastCharBoxesUv.size()); ++ci) {
                        const auto& cb = actIt->text.lastCharBoxesUv[ci];
                        if (app.hoverHit3D.uv.x >= cb.uvMin.x && app.hoverHit3D.uv.x <= cb.uvMax.x &&
                            app.hoverHit3D.uv.y >= cb.uvMin.y && app.hoverHit3D.uv.y <= cb.uvMax.y) {
                            bestCi = ci;
                            break;
                        }
                        float cx = (cb.uvMin.x + cb.uvMax.x) * 0.5f;
                        float cy = (cb.uvMin.y + cb.uvMax.y) * 0.5f;
                        float d = std::hypot(app.hoverHit3D.uv.x - cx, app.hoverHit3D.uv.y - cy);
                        if (d < bestD) { bestD = d; bestCi = ci; }
                    }
                    if (bestCi >= 0) {
                        actIt->text.selectedCharIdx = bestCi;
                        actIt->text.syncCharStyles();
                        app.uiState.activePropsCat = lps::PropertiesCategory::Text;
                        app.uiState.statusText = "Touch Type: selected letter #" + std::to_string(bestCi + 1);
                        return;
                    }
                }
            }
            GizmoDrag3D pickedMode = GizmoDrag3D::None;
            int pickedLayerIdx = -1;
            int pickedItemIdx = -1;

            // 1. Hit-test currently selected VectorItem or Legacy Layer 3D OBB handles
            if (activeLayer && !activeLayer->locked) {
                Frame3D af{};
                float origSz = 0.0f, origRot = 0.0f;
                int curItem = (activeLayer->kind == lps::LayerKind::RasterPaint) ? app.canvas.selectedItemIdx : -1;
                if (getTargetFrame3D(activeIdx, curItem, af, origSz, origRot)) {
                    ImVec2 corners[4], rotKnob, ctrScr;
                    if (projectObbScreen(app, af, corners, rotKnob, ctrScr)) {
                        if (std::hypot(io.MousePos.x - rotKnob.x, io.MousePos.y - rotKnob.y) <= 11.0f) {
                            pickedMode = GizmoDrag3D::Rotate;
                            pickedLayerIdx = activeIdx;
                            pickedItemIdx = curItem;
                            app.gizmoStartAngle3D = std::atan2(io.MousePos.y - ctrScr.y, io.MousePos.x - ctrScr.x);
                            app.gizmoOrigRot3D = origRot;
                        } else {
                            for (int k = 0; k < 4; ++k) {
                                if (std::hypot(io.MousePos.x - corners[k].x, io.MousePos.y - corners[k].y) <= 10.0f) {
                                    pickedMode = GizmoDrag3D::ScaleCorner;
                                    pickedLayerIdx = activeIdx;
                                    pickedItemIdx = curItem;
                                    app.gizmoStartScaleDist3D = std::max(12.0f,
                                        std::hypot(io.MousePos.x - ctrScr.x, io.MousePos.y - ctrScr.y));
                                    app.gizmoOrigSize3D = origSz;
                                    break;
                                }
                            }
                        }
                    }
                }
            }

            // 2. Hit-test top-most visible container VectorItem OR Legacy Layer OBB (primary + mirrored side)
            bool hitMirroredSide = false;
            auto tryHitFrame = [&](const Frame3D& f, int lIdx, int iIdx) -> bool {
                if (pointInFrame3DObb(app.hoverHit3D.pos, app.hoverHit3D.normal, f)) {
                    pickedLayerIdx = lIdx;
                    pickedItemIdx = iIdx;
                    pickedMode = GizmoDrag3D::Move;
                    return true;
                }
                if (f.mirror != lps::MirrorTextMode::None) {
                    Frame3D mf = f;
                    mf.c = {2.0f * app.mesh.centerSymmetryX - f.c.x, f.c.y, f.c.z};
                    mf.N = {-f.N.x, f.N.y, f.N.z};
                    mf.T = {-f.T.x, f.T.y, f.T.z};
                    if (f.mirror == lps::MirrorTextMode::ReadableBothSides) mf.T = -mf.T;
                    mf.B = lps::normalized(lps::cross(mf.N, mf.T));
                    if (pointInFrame3DObb(app.hoverHit3D.pos, app.hoverHit3D.normal, mf)) {
                        pickedLayerIdx = lIdx;
                        pickedItemIdx = iIdx;
                        pickedMode = GizmoDrag3D::Move;
                        hitMirroredSide = true;
                        return true;
                    }
                }
                return false;
            };

            if (pickedLayerIdx < 0 && app.hoverHit3D.hit) {
                for (int li = static_cast<int>(app.canvas.layers.size()) - 1; li >= 0 && pickedLayerIdx < 0; --li) {
                    const lps::Layer& cand = app.canvas.layers[li];
                    if (!cand.visible || cand.locked) continue;
                    // Container VectorItems (topmost down)
                    for (int ii = static_cast<int>(cand.items.size()) - 1; ii >= 0; --ii) {
                        const auto& it = cand.items[ii];
                        if (app.uiState.activeTool == lps::StudioTool::Text && it.kind != lps::VectorItemKind::Text) continue;
                        if (app.uiState.activeTool == lps::StudioTool::SponsorDecal && it.kind != lps::VectorItemKind::Image) continue;
                        Frame3D f{};
                        if (computeVectorItemFrame3D(it, f) && tryHitFrame(f, li, ii)) break;
                    }
                    if (pickedLayerIdx >= 0) break;
                    // Legacy standalone vector layer fallback
                    if (app.uiState.activeTool == lps::StudioTool::Text && cand.kind != lps::LayerKind::Text) continue;
                    if (app.uiState.activeTool == lps::StudioTool::SponsorDecal && cand.kind != lps::LayerKind::Decal) continue;
                    Frame3D lf{};
                    if (computeLegacyLayerFrame3D(cand, lf)) tryHitFrame(lf, li, -1);
                }
            }

            if (pickedLayerIdx >= 0) {
                app.canvas.pushUndoSnapshot();
                app.canvas.activeLayerIdx = pickedLayerIdx;
                app.canvas.selectedItemIdx = pickedItemIdx;
                activeIdx = pickedLayerIdx;
                activeLayer = &app.canvas.layers[activeIdx];
                if (auto* pickedItem = app.canvas.activeVectorItem()) {
                    if (pickedItem->kind == lps::VectorItemKind::Text) app.uiState.activePropsCat = lps::PropertiesCategory::Text;
                    else if (pickedItem->kind == lps::VectorItemKind::Image) app.uiState.activePropsCat = lps::PropertiesCategory::Decal;
                    else app.uiState.activePropsCat = lps::PropertiesCategory::Ruler;
                    if (hitMirroredSide && pickedMode == GizmoDrag3D::Move && app.hoverHit3D.hit) {
                        lps::Vec3 r3 = computeSurfaceRight3D(app, app.hoverHit3D.normal);
                        if (pickedItem->kind == lps::VectorItemKind::Text) {
                            pickedItem->text.pos3D = app.hoverHit3D.pos;
                            pickedItem->text.nrm3D = app.hoverHit3D.normal;
                            pickedItem->text.right3D = r3;
                            pickedItem->text.uvPos = app.hoverHit3D.uv;
                        } else if (pickedItem->kind == lps::VectorItemKind::Image) {
                            pickedItem->image.pos3D = app.hoverHit3D.pos;
                            pickedItem->image.nrm3D = app.hoverHit3D.normal;
                            pickedItem->image.right3D = r3;
                            pickedItem->image.uvPos = app.hoverHit3D.uv;
                        } else if (pickedItem->kind == lps::VectorItemKind::RectShape) {
                            pickedItem->rect.pos3D = app.hoverHit3D.pos;
                            pickedItem->rect.nrm3D = app.hoverHit3D.normal;
                            pickedItem->rect.right3D = r3;
                            pickedItem->rect.uvPos = app.hoverHit3D.uv;
                        }
                    }
                } else {
                    app.uiState.activePropsCat = (activeLayer->kind == lps::LayerKind::Text)
                                                 ? lps::PropertiesCategory::Text
                                                 : lps::PropertiesCategory::Decal;
                    if (hitMirroredSide && pickedMode == GizmoDrag3D::Move && app.hoverHit3D.hit) {
                        lps::Vec3 r3 = computeSurfaceRight3D(app, app.hoverHit3D.normal);
                        if (activeLayer->kind == lps::LayerKind::Text) {
                            activeLayer->text.pos3D = app.hoverHit3D.pos;
                            activeLayer->text.nrm3D = app.hoverHit3D.normal;
                            activeLayer->text.right3D = r3;
                            activeLayer->text.uvPos = app.hoverHit3D.uv;
                        } else {
                            activeLayer->decal.pos3D = app.hoverHit3D.pos;
                            activeLayer->decal.nrm3D = app.hoverHit3D.normal;
                            activeLayer->decal.right3D = r3;
                            activeLayer->decal.uvPos = app.hoverHit3D.uv;
                        }
                    }
                }
                app.uiState.textEditSyncedLayerId = 0;
                app.gizmoDrag3D = pickedMode;
                app.gizmoLayerIdx3D = pickedLayerIdx;
                app.gizmoItemIdx3D = pickedItemIdx;
                app.gizmoStartMouse3D = lps::Vec2{io.MousePos.x, io.MousePos.y};
                app.uiState.statusText = (pickedMode == GizmoDrag3D::Rotate)
                                         ? "Rotating 3D item..."
                                         : (pickedMode == GizmoDrag3D::ScaleCorner)
                                           ? "Scaling 3D item..." : "Moving 3D item...";
            } else if (app.hoverHit3D.hit && app.uiState.activeTool == lps::StudioTool::Text) {
                if (activeLayer && activeLayer->locked) {
                    app.uiState.statusText = "Active layer is locked";
                } else {
                    activeIdx = app.canvas.ensureEditableLayer();
                    app.canvas.pushUndoSnapshot();
                    lps::TextLayerProps tp{};
                    tp.text = "Lorem ipsum";
                    tp.fontFamily = "Arial";
                    tp.fontStyle = "Bold";
                    tp.fontPath = app.canvas.findFontPath("", tp.fontFamily, tp.fontStyle);
                    tp.fillColor = app.uiState.brush.color;
                    tp.outlineColor = app.uiState.secondaryColor;
                    tp.mirrorMode =
                        (!app.uiState.brush.mirrorX || std::fabs(app.hoverHit3D.pos.x - app.mesh.centerSymmetryX) < 0.08f)
                        ? lps::MirrorTextMode::None
                        : lps::MirrorTextMode::ReadableBothSides;
                    tp.uvPos = app.hoverHit3D.uv;
                    tp.pos3D = app.hoverHit3D.pos;
                    tp.nrm3D = app.hoverHit3D.normal;
                    tp.right3D = computeSurfaceRight3D(app, app.hoverHit3D.normal);
                    tp.anchoredIn3D = true;
                    int newItem = app.canvas.addTextItem("Text", tp, &app.mesh);
                    app.uiState.activePropsCat = lps::PropertiesCategory::Text;
                    app.uiState.textEditSyncedLayerId = 0;
                    app.gizmoDrag3D = GizmoDrag3D::Move;
                    app.gizmoLayerIdx3D = activeIdx;
                    app.gizmoItemIdx3D = newItem;
                    rasterizeActiveLayerAndSync(app, activeIdx, true);
                    app.uiState.statusText = "Placed Type: Lorem ipsum (type in bar above to edit)";
                }
            } else if (app.hoverHit3D.hit && app.uiState.activeTool == lps::StudioTool::SponsorDecal) {
                auto* curItem = app.canvas.activeVectorItem();
                if (curItem && curItem->kind == lps::VectorItemKind::Image &&
                    !curItem->image.imagePixels().empty() && !curItem->image.anchoredIn3D &&
                    activeLayer && !activeLayer->locked) {
                    app.canvas.pushUndoSnapshot();
                    curItem->image.mirrorMode =
                        (!app.uiState.brush.mirrorX || std::fabs(app.hoverHit3D.pos.x - app.mesh.centerSymmetryX) < 0.08f)
                        ? lps::MirrorTextMode::None
                        : lps::MirrorTextMode::ReadableBothSides;
                    curItem->image.uvPos = app.hoverHit3D.uv;
                    curItem->image.pos3D = app.hoverHit3D.pos;
                    curItem->image.nrm3D = app.hoverHit3D.normal;
                    curItem->image.right3D = computeSurfaceRight3D(app, app.hoverHit3D.normal);
                    curItem->image.anchoredIn3D = true;
                    app.uiState.activePropsCat = lps::PropertiesCategory::Decal;
                    app.gizmoDrag3D = GizmoDrag3D::Move;
                    app.gizmoLayerIdx3D = activeIdx;
                    app.gizmoItemIdx3D = app.canvas.selectedItemIdx;
                    rasterizeActiveLayerAndSync(app, activeIdx, true);
                } else {
                    std::string dp = lps::dialogs::openImageDialog(app.hwnd, L"Import Image (.png / .jpg)");
                    if (!dp.empty()) {
                        int nItem = importImageFileAsItem(app, dp, &app.hoverHit3D);
                        if (nItem >= 0) {
                            app.gizmoDrag3D = GizmoDrag3D::None;
                            app.gizmoLayerIdx3D = app.canvas.activeLayerIdx;
                            app.gizmoItemIdx3D = nItem;
                        }
                    }
                }
            } else if (app.hoverHit3D.hit && app.uiState.activeTool == lps::StudioTool::SelectMove &&
                       activeLayer && !activeLayer->locked) {
                auto* curItem = app.canvas.activeVectorItem();
                if (curItem && ((curItem->kind == lps::VectorItemKind::Text && !curItem->text.anchoredIn3D) ||
                                (curItem->kind == lps::VectorItemKind::Image && !curItem->image.anchoredIn3D && !curItem->image.imagePixels().empty()) ||
                                (curItem->kind == lps::VectorItemKind::RectShape && !curItem->rect.anchoredIn3D))) {
                    app.canvas.pushUndoSnapshot();
                    lps::Vec3 r3 = computeSurfaceRight3D(app, app.hoverHit3D.normal);
                    if (curItem->kind == lps::VectorItemKind::Text) {
                        curItem->text.uvPos = app.hoverHit3D.uv;
                        curItem->text.pos3D = app.hoverHit3D.pos;
                        curItem->text.nrm3D = app.hoverHit3D.normal;
                        curItem->text.right3D = r3;
                        curItem->text.anchoredIn3D = true;
                    } else if (curItem->kind == lps::VectorItemKind::Image) {
                        curItem->image.uvPos = app.hoverHit3D.uv;
                        curItem->image.pos3D = app.hoverHit3D.pos;
                        curItem->image.nrm3D = app.hoverHit3D.normal;
                        curItem->image.right3D = r3;
                        curItem->image.anchoredIn3D = true;
                    } else {
                        curItem->rect.uvPos = app.hoverHit3D.uv;
                        curItem->rect.pos3D = app.hoverHit3D.pos;
                        curItem->rect.nrm3D = app.hoverHit3D.normal;
                        curItem->rect.right3D = r3;
                        curItem->rect.anchoredIn3D = true;
                    }
                    app.gizmoDrag3D = GizmoDrag3D::Move;
                    app.gizmoLayerIdx3D = activeIdx;
                    app.gizmoItemIdx3D = app.canvas.selectedItemIdx;
                    rasterizeActiveLayerAndSync(app, activeIdx, true);
                }
            }
        }

        if (app.gizmoDrag3D != GizmoDrag3D::None &&
            app.gizmoLayerIdx3D >= 0 && app.gizmoLayerIdx3D < static_cast<int>(app.canvas.layers.size())) {
            int dIdx = app.gizmoLayerIdx3D;
            int iIdx = app.gizmoItemIdx3D;
            lps::Layer& dlLayer = app.canvas.layers[dIdx];
            lps::VectorItem* dlItem = (iIdx >= 0 && iIdx < static_cast<int>(dlLayer.items.size()))
                                      ? &dlLayer.items[iIdx] : nullptr;
            bool validTarget = !dlLayer.locked &&
                               (dlItem != nullptr || dlLayer.kind == lps::LayerKind::Text || dlLayer.kind == lps::LayerKind::Decal);
            if (validTarget) {
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    bool changed = false;
                    if (app.gizmoDrag3D == GizmoDrag3D::Move) {
                        if (app.hoverHit3D.hit && std::hypot(io.MouseDelta.x, io.MouseDelta.y) > 0.0f) {
                            lps::Vec3 right3D = computeSurfaceRight3D(app, app.hoverHit3D.normal);
                            if (dlItem) {
                                if (dlItem->kind == lps::VectorItemKind::Text) {
                                    dlItem->text.uvPos = app.hoverHit3D.uv;
                                    dlItem->text.pos3D = app.hoverHit3D.pos;
                                    dlItem->text.nrm3D = app.hoverHit3D.normal;
                                    if (!io.KeyShift) dlItem->text.right3D = right3D;
                                    dlItem->text.anchoredIn3D = true;
                                } else if (dlItem->kind == lps::VectorItemKind::Image) {
                                    dlItem->image.uvPos = app.hoverHit3D.uv;
                                    dlItem->image.pos3D = app.hoverHit3D.pos;
                                    dlItem->image.nrm3D = app.hoverHit3D.normal;
                                    if (!io.KeyShift) dlItem->image.right3D = right3D;
                                    dlItem->image.anchoredIn3D = true;
                                } else if (dlItem->kind == lps::VectorItemKind::RectShape) {
                                    dlItem->rect.uvPos = app.hoverHit3D.uv;
                                    dlItem->rect.pos3D = app.hoverHit3D.pos;
                                    dlItem->rect.nrm3D = app.hoverHit3D.normal;
                                    if (!io.KeyShift) dlItem->rect.right3D = right3D;
                                    dlItem->rect.anchoredIn3D = true;
                                }
                            } else if (dlLayer.kind == lps::LayerKind::Text) {
                                dlLayer.text.uvPos = app.hoverHit3D.uv;
                                dlLayer.text.pos3D = app.hoverHit3D.pos;
                                dlLayer.text.nrm3D = app.hoverHit3D.normal;
                                if (!io.KeyShift) dlLayer.text.right3D = right3D;
                                dlLayer.text.anchoredIn3D = true;
                            } else {
                                dlLayer.decal.uvPos = app.hoverHit3D.uv;
                                dlLayer.decal.pos3D = app.hoverHit3D.pos;
                                dlLayer.decal.nrm3D = app.hoverHit3D.normal;
                                if (!io.KeyShift) dlLayer.decal.right3D = right3D;
                                dlLayer.decal.anchoredIn3D = true;
                            }
                            changed = true;
                        }
                    } else {
                        Frame3D gf{};
                        float curSz = 0.0f, curRot = 0.0f;
                        ImVec2 ctrScr;
                        if (getTargetFrame3D(dIdx, iIdx, gf, curSz, curRot) &&
                            projectWorldToViewport3D(app, gf.c, ctrScr)) {
                            if (app.gizmoDrag3D == GizmoDrag3D::ScaleCorner) {
                                float curD = std::max(6.0f, std::hypot(io.MousePos.x - ctrScr.x, io.MousePos.y - ctrScr.y));
                                float ratio = std::clamp(curD / std::max(10.0f, app.gizmoStartScaleDist3D), 0.15f, 6.0f);
                                float newSz = app.gizmoOrigSize3D * ratio;
                                if (dlItem) {
                                    if (dlItem->kind == lps::VectorItemKind::Text) {
                                        dlItem->text.worldHeight = std::clamp(newSz, 0.04f, 1.50f);
                                    } else if (dlItem->kind == lps::VectorItemKind::Image) {
                                        dlItem->image.worldWidth = std::clamp(newSz, 0.06f, 2.40f);
                                    } else if (dlItem->kind == lps::VectorItemKind::RectShape) {
                                        float oldW = std::max(0.01f, dlItem->rect.worldWidth);
                                        float nw = std::clamp(newSz, 0.04f, 2.40f);
                                        float nh = std::clamp(dlItem->rect.worldHeight * (nw / oldW), 0.04f, 2.40f);
                                        dlItem->rect.worldWidth = nw;
                                        dlItem->rect.worldHeight = nh;
                                    }
                                } else if (dlLayer.kind == lps::LayerKind::Text) {
                                    dlLayer.text.worldHeight = std::clamp(newSz, 0.04f, 1.50f);
                                } else {
                                    dlLayer.decal.worldWidth = std::clamp(newSz, 0.06f, 2.40f);
                                }
                                changed = true;
                            } else if (app.gizmoDrag3D == GizmoDrag3D::Rotate) {
                                float ang = std::atan2(io.MousePos.y - ctrScr.y, io.MousePos.x - ctrScr.x);
                                float deltaDeg = (ang - app.gizmoStartAngle3D) * lps::kRad2Deg;
                                float newDeg = app.gizmoOrigRot3D + deltaDeg;
                                if (io.KeyShift) newDeg = std::round(newDeg / 15.0f) * 15.0f;
                                if (dlItem) {
                                    if (dlItem->kind == lps::VectorItemKind::Text) dlItem->text.rotationDeg = newDeg;
                                    else if (dlItem->kind == lps::VectorItemKind::Image) dlItem->image.rotationDeg = newDeg;
                                    else if (dlItem->kind == lps::VectorItemKind::RectShape) dlItem->rect.rotationDeg = newDeg;
                                } else if (dlLayer.kind == lps::LayerKind::Text) {
                                    dlLayer.text.rotationDeg = newDeg;
                                } else {
                                    dlLayer.decal.rotationDeg = newDeg;
                                }
                                changed = true;
                            }
                        }
                    }

                    if (changed) {
                        double now = ImGui::GetTime();
                        if (now - app.lastVectorRasterTime >= 0.014) {
                            rasterizeActiveLayerAndSync(app, dIdx, true);
                            app.lastVectorRasterTime = now;
                        }
                    }
                } else {
                    rasterizeActiveLayerAndSync(app, dIdx, true);
                    app.gizmoDrag3D = GizmoDrag3D::None;
                    app.gizmoLayerIdx3D = -1;
                    app.gizmoItemIdx3D = -1;
                }
            } else {
                app.gizmoDrag3D = GizmoDrag3D::None;
                app.gizmoLayerIdx3D = -1;
                app.gizmoItemIdx3D = -1;
            }
        }
        return;
    }

    // -- FLOOD FILL IN 3D --
    if (app.uiState.activeTool == lps::StudioTool::FloodFill && hovered && allowLmbTool &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) && app.hoverHit3D.hit) {
        if (activeLayer && activeLayer->locked) {
            app.uiState.statusText = "Active layer is locked";
        } else {
            if (!activeLayer || activeLayer->kind != lps::LayerKind::RasterPaint) {
                activeIdx = app.canvas.ensureEditableLayer();
                activeLayer = &app.canvas.layers[activeIdx];
            }
            if (activeLayer && activeLayer->kind == lps::LayerKind::RasterPaint && !activeLayer->locked) {
                app.canvas.floodFillAtUV(activeIdx, app.hoverHit3D.uv, app.uiState.brush.color,
                                         app.uiState.floodFillTolerance, app.uiState.brush.mirrorX, &app.mesh);
                syncCanvasGpu(app, true);
                app.uiState.statusText = "Flood filled region";
            }
        }
        return;
    }

    // -- EYEDROPPER IN 3D --
    if (app.uiState.activeTool == lps::StudioTool::Eyedropper && hovered && allowLmbTool &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left) && app.hoverHit3D.hit) {
        lps::Color4u8 col = app.canvas.sampleCompositeAtUV(app.hoverHit3D.uv);
        app.uiState.brush.color = col;
        char msg[64];
        std::snprintf(msg, sizeof(msg), "Picked RGB(%d, %d, %d)", col.r, col.g, col.b);
        app.uiState.statusText = msg;
    }
}

void draw3DViewportHudOverlay(const AppState& app) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 vpMin(app.uiState.viewport3DPanelMin.x, app.uiState.viewport3DPanelMin.y);
    ImVec2 vpMax(vpMin.x + app.uiState.viewport3DPanelSize.x, vpMin.y + app.uiState.viewport3DPanelSize.y);
    dl->PushClipRect(vpMin, vpMax, true);

    auto drawBadge = [&](const ImVec2& anchor, const char* txt, ImU32 borderCol) {
        ImVec2 ts = ImGui::CalcTextSize(txt);
        ImVec2 p0(std::round(anchor.x + 14.0f), std::round(anchor.y - ts.y - 14.0f));
        ImVec2 p1(p0.x + ts.x + 12.0f, p0.y + ts.y + 8.0f);
        dl->AddRectFilled(p0, p1, IM_COL32(22, 24, 29, 235), 2.0f);
        dl->AddRect(p0, p1, borderCol, 2.0f, 1.0f);
        dl->AddText(ImVec2(p0.x + 6.0f, p0.y + 4.0f), lps::StudioColors::TextPrimary, txt);
    };

    // Illustrator-style 3D surface OBB bounding gizmo (#00B8FF corners + rotate lollipop)
    if (app.mesh.loaded && app.uiState.show3DOverlays &&
        (app.uiState.activeTool == lps::StudioTool::SelectMove ||
         app.uiState.activeTool == lps::StudioTool::RectBox ||
         app.uiState.activeTool == lps::StudioTool::Text ||
         app.uiState.activeTool == lps::StudioTool::TouchType ||
         app.uiState.activeTool == lps::StudioTool::SponsorDecal)) {
        Frame3D f{};
        bool hasFrame = false;
        if (const auto* actIt = app.canvas.activeVectorItem()) {
            hasFrame = computeVectorItemFrame3D(*actIt, f);
        } else {
            int aIdx = app.canvas.activeLayerIdx;
            if (aIdx >= 0 && aIdx < static_cast<int>(app.canvas.layers.size())) {
                hasFrame = computeLegacyLayerFrame3D(app.canvas.layers[aIdx], f);
            }
        }
        if (hasFrame && lps::dot(app.sceneParams.camEye - f.c, f.N) > -0.05f) {
            ImVec2 sp[4], knob, ctrScr, topMid;
            if (projectObbScreen(app, f, sp, knob, ctrScr) &&
                projectWorldToViewport3D(app, f.c + f.N * 0.012f - f.B * f.hh, topMid)) {
                const ImU32 cyan = IM_COL32(0, 184, 255, 235);
                dl->AddPolyline(sp, 4, cyan, ImDrawFlags_Closed, 1.5f);
                dl->AddLine(topMid, knob, cyan, 1.3f);
                dl->AddCircleFilled(knob, 4.5f, IM_COL32(255, 255, 255, 255));
                dl->AddCircle(knob, 4.5f, cyan, 16, 1.4f);
                for (int k = 0; k < 4; ++k) {
                    dl->AddRectFilled(ImVec2(sp[k].x - 3.5f, sp[k].y - 3.5f),
                                      ImVec2(sp[k].x + 3.5f, sp[k].y + 3.5f),
                                      IM_COL32(255, 255, 255, 255));
                    dl->AddRect(ImVec2(sp[k].x - 3.5f, sp[k].y - 3.5f),
                                ImVec2(sp[k].x + 3.5f, sp[k].y + 3.5f), cyan, 0.0f, 0, 1.2f);
                }
                if (app.gizmoDrag3D != GizmoDrag3D::None) {
                    char gHud[64];
                    if (app.gizmoDrag3D == GizmoDrag3D::Rotate) {
                        std::snprintf(gHud, sizeof(gHud), "Rot: %.1f deg", f.rotDeg);
                    } else {
                        std::snprintf(gHud, sizeof(gHud), "W: %.2f m  H: %.2f m", f.hw * 2.0f, f.hh * 2.0f);
                    }
                    drawBadge(io.MousePos, gHud, cyan);
                }
            }
        }
    }

    if ((app.uiState.activeTool == lps::StudioTool::StraightRuler ||
         app.uiState.activeTool == lps::StudioTool::CurveBlade ||
         app.uiState.activeTool == lps::StudioTool::NodeEdit) &&
        app.uiState.show3DOverlays) {
        if (const lps::RulerStripeProps* rpPtr = getActivePenPropsConst(app);
            rpPtr && !rpPtr->nodes.empty()) {
            const auto& rp = *rpPtr;
            const auto& nodes = rp.nodes;
            const ImU32 cyan = IM_COL32(0, 184, 255, 235);
            const ImU32 handleCol = IM_COL32(56, 189, 248, 220);
            auto evalCub3 = [](const lps::Vec3& p0, const lps::Vec3& p1, const lps::Vec3& p2, const lps::Vec3& p3, float t) {
                float u = 1.0f - t;
                return p0 * (u * u * u) + p1 * (3.0f * u * u * t) + p2 * (3.0f * u * t * t) + p3 * (t * t * t);
            };
            size_t segCnt = (rp.closed && nodes.size() >= 3) ? nodes.size() : (nodes.size() > 1 ? nodes.size() - 1 : 0);
            for (size_t s = 0; s < segCnt; ++s) {
                const auto& nA = nodes[s];
                const auto& nB = nodes[(s + 1) % nodes.size()];
                if (!nA.has3D || !nB.has3D) continue;
                lps::Vec3 c1 = nA.hasHandles ? nA.handleOut3D : nA.pos3D;
                lps::Vec3 c2 = nB.hasHandles ? nB.handleIn3D : nB.pos3D;
                ImVec2 prevSp;
                bool prevOk = projectWorldToViewport3D(app, nA.pos3D + nA.nrm3D * 0.006f, prevSp);
                for (int k = 1; k <= 16; ++k) {
                    lps::Vec3 wp = evalCub3(nA.pos3D, c1, c2, nB.pos3D, k / 16.0f) +
                                   lps::normalized(nA.nrm3D + nB.nrm3D) * 0.006f;
                    ImVec2 curSp;
                    bool curOk = projectWorldToViewport3D(app, wp, curSp);
                    if (prevOk && curOk) dl->AddLine(prevSp, curSp, cyan, 1.5f);
                    prevOk = curOk;
                    prevSp = curSp;
                }
            }
            for (size_t ni = 0; ni < nodes.size(); ++ni) {
                const auto& n = nodes[ni];
                if (!n.has3D) continue;
                ImVec2 sp;
                if (!projectWorldToViewport3D(app, n.pos3D + n.nrm3D * 0.007f, sp)) continue;
                if (n.hasHandles) {
                    ImVec2 hi, ho;
                    if (projectWorldToViewport3D(app, n.handleIn3D + n.nrm3D * 0.007f, hi)) {
                        dl->AddLine(sp, hi, handleCol, 1.1f);
                        dl->AddCircleFilled(hi, 3.5f, handleCol);
                    }
                    if (projectWorldToViewport3D(app, n.handleOut3D + n.nrm3D * 0.007f, ho)) {
                        dl->AddLine(sp, ho, handleCol, 1.1f);
                        dl->AddCircleFilled(ho, 3.5f, handleCol);
                    }
                }
                dl->AddRectFilled(ImVec2(sp.x - 3.5f, sp.y - 3.5f), ImVec2(sp.x + 3.5f, sp.y + 3.5f),
                                  IM_COL32(255, 255, 255, 255));
                dl->AddRect(ImVec2(sp.x - 3.5f, sp.y - 3.5f), ImVec2(sp.x + 3.5f, sp.y + 3.5f), cyan, 0.0f, 0, 1.2f);
            }
            if (app.uiState.activeTool == lps::StudioTool::CurveBlade &&
                app.rulerDrawing3D && nodes.size() >= 3 && !rp.closed && nodes[0].has3D) {
                ImVec2 s0;
                if (projectWorldToViewport3D(app, nodes[0].pos3D + nodes[0].nrm3D * 0.007f, s0) &&
                    std::hypot(io.MousePos.x - s0.x, io.MousePos.y - s0.y) <= 10.0f) {
                    dl->AddCircle(s0, 7.0f, IM_COL32(251, 146, 60, 255), 16, 1.8f);
                    drawBadge(io.MousePos, "o Close Path", IM_COL32(251, 146, 60, 255));
                }
            }
        }
    }

    if ((app.uiState.activeTool == lps::StudioTool::StraightRuler ||
         app.uiState.activeTool == lps::StudioTool::CurveBlade) &&
        app.rulerDrawing3D && app.uiState.viewport3DHovered && app.hoverHit3D.hit) {
        if (const lps::RulerStripeProps* rpPtr = getActivePenPropsConst(app);
            rpPtr && !rpPtr->nodes.empty()) {
            float lenM = 0.0f, deg = 0.0f;
            apply3DRulerSnap(app, app.hoverHit3D, rpPtr->nodes, io.KeyShift, lenM, deg);
            char hud[80];
            std::snprintf(hud, sizeof(hud), "%.2f m | %.1f deg%s", lenM, deg, io.KeyShift ? " [SNAP]" : "");
            drawBadge(io.MousePos, hud, io.KeyShift ? lps::StudioColors::AmberAccent : lps::StudioColors::StudioBlue);
        }
    }

    if (app.uiState.activeTool == lps::StudioTool::Eyedropper &&
        app.uiState.viewport3DHovered && app.hoverHit3D.hit) {
        lps::Color4u8 peek = app.canvas.sampleCompositeAtUV(app.hoverHit3D.uv);
        ImVec2 swMin(io.MousePos.x + 14.0f, io.MousePos.y + 14.0f);
        ImVec2 swMax(swMin.x + 20.0f, swMin.y + 20.0f);
        dl->AddRectFilled(swMin, swMax, IM_COL32(peek.r, peek.g, peek.b, 255), 2.0f);
        dl->AddRect(swMin, swMax, lps::StudioColors::TextPrimary, 2.0f, 1.0f);
    }

    if (app.uiState.activeTool == lps::StudioTool::RectBox && app.rectDrawing3D) {
        lps::Vec3 rel = app.rectCurrPos3D - app.rectStartPos3D;
        float uSpan = std::fabs(lps::dot(rel, app.rectRight3D));
        float vSpan = std::fabs(lps::dot(rel, app.rectDown3D));
        if (io.KeyShift) {
            uSpan = vSpan = std::max(uSpan, vSpan);
        }
        char rHud[64];
        std::snprintf(rHud, sizeof(rHud), "%.2f x %.2f m%s", uSpan, vSpan, io.KeyShift ? " [SQ]" : "");
        drawBadge(io.MousePos, rHud, lps::StudioColors::StudioBlue);
    }

    if (app.editDraggingVerts3D && !app.editHudBadge.empty()) {
        drawBadge(io.MousePos, app.editHudBadge.c_str(), lps::StudioColors::AmberAccent);
    }
    dl->PopClipRect();
}

void handleShortcuts(AppState& app, const lps::StudioCallbacks& callbacks) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;

    // Ctrl shortcuts matching Illustrator & Blender menus
    if (io.KeyCtrl && !io.KeyAlt) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
            if (io.KeyShift) {
                if (callbacks.onRedo) callbacks.onRedo();
            } else {
                if (callbacks.onUndo) callbacks.onUndo();
            }
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
            if (callbacks.onRedo) callbacks.onRedo();
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_C, false) && !io.KeyShift) {
            if (callbacks.onCopy) callbacks.onCopy();
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_X, false) && !io.KeyShift) {
            if (callbacks.onCut) callbacks.onCut();
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_V, false) && !io.KeyShift) {
            if (callbacks.onPaste) callbacks.onPaste();
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_N, false)) {
            if (io.KeyShift) {
                if (callbacks.onNewLayer) callbacks.onNewLayer();
            } else if (callbacks.onNewProject) {
                callbacks.onNewProject();
            }
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_O, false)) {
            if (io.KeyShift) {
                if (callbacks.onOpenLiveryPng) callbacks.onOpenLiveryPng();
            } else if (callbacks.onOpenObj) {
                callbacks.onOpenObj();
            }
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_I, false) && !io.KeyShift) {
            if (callbacks.onImportImage) callbacks.onImportImage();
            else if (callbacks.onImportDecal) callbacks.onImportDecal();
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_S, false) && !io.KeyShift) {
            if (callbacks.onSaveProject) callbacks.onSaveProject();
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_L, false) && !io.KeyShift) {
            if (callbacks.onLoadProject) callbacks.onLoadProject();
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_E, false)) {
            if (io.KeyShift) {
                if (callbacks.onExportPng) callbacks.onExportPng(4096);
            } else {
                int idx = app.canvas.activeLayerIdx;
                app.canvas.pushUndoSnapshot();
                if (app.canvas.mergeDown(idx)) {
                    syncCanvasGpu(app);
                    app.uiState.statusText = "Merged Layer Down";
                }
            }
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_J, false) && !io.KeyShift) {
            if (callbacks.onDuplicateLayer) callbacks.onDuplicateLayer(app.canvas.activeLayerIdx);
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightBracket, false)) {
            app.canvas.pushUndoSnapshot();
            app.canvas.moveLayer(app.canvas.activeLayerIdx, +1);
            syncCanvasGpu(app);
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket, false)) {
            app.canvas.pushUndoSnapshot();
            app.canvas.moveLayer(app.canvas.activeLayerIdx, -1);
            syncCanvasGpu(app);
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_0, false) || ImGui::IsKeyPressed(ImGuiKey_Keypad0, false)) {
            app.uvState.needsInitialFit = true;
            app.uiState.statusText = "Fit 2D UV Canvas";
            return;
        }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) {
        if (callbacks.onQuickLoadPorsche) callbacks.onQuickLoadPorsche();
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F12, false)) {
        app.pendingBeautyDialog = true;
        return;
    }

    if (io.KeyAlt && !io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
        if (app.mesh.loaded) {
            app.mesh.clearSelection();
            app.uiState.meshGpuDirty = true;
            syncMeshGpu(app);
            app.uiState.statusText = "Cleared 3D selection / mask";
        }
        return;
    }

    if (!io.KeyCtrl && !io.KeyAlt && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_M, false)) {
        bool nextMir = !app.uiState.brush.mirrorX;
        if (callbacks.onToggleMirror) callbacks.onToggleMirror(nextMir);
        else app.uiState.brush.mirrorX = nextMir;
        return;
    }

    if (!io.KeyCtrl && !io.KeyAlt && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_T, false)) {
        app.uiState.activeMode = lps::StudioMode::TexturePaint;
        app.uiState.activeTool = lps::StudioTool::TouchType;
        app.uiState.activeTypeToolVariant = 1;
        app.uiState.activePropsCat = lps::PropertiesCategory::Text;
        app.uiState.statusText = "Touch Type Tool (Shift+T)";
        return;
    }

    if (!io.KeyCtrl && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        if (app.canvas.activeVectorItem()) {
            int lIdx = app.canvas.activeLayerIdx;
            if (lIdx >= 0 && lIdx < static_cast<int>(app.canvas.layers.size()) &&
                !app.canvas.layers[lIdx].locked) {
                app.canvas.pushUndoSnapshot();
                if (app.canvas.deleteSelectedItem(&app.mesh)) {
                    syncCanvasGpu(app, true);
                    app.uiState.statusText = "Deleted selected item";
                    return;
                }
            }
        }
        if (app.canvas.layers.size() > 1 && callbacks.onDeleteLayer) {
            callbacks.onDeleteLayer(app.canvas.activeLayerIdx);
        }
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
        if (app.uiState.activeMode == lps::StudioMode::EditMesh) {
            app.uiState.activeMode = lps::StudioMode::TexturePaint;
            app.uiState.activeTool = lps::StudioTool::Brush;
            app.uiState.brush.eraser = false;
            app.uiState.statusText = "Mode: Texture Paint";
        } else {
            app.uiState.activeMode = lps::StudioMode::EditMesh;
            app.uiState.activeTool = lps::StudioTool::VertexEdit;
            app.uiState.statusText = "Mode: Edit Mesh";
        }
        return;
    }

    if (!io.KeyCtrl && !io.KeyAlt && !io.KeyShift) {
        if (ImGui::IsKeyPressed(ImGuiKey_V, false)) {
            app.uiState.activeMode = lps::StudioMode::TexturePaint;
            app.uiState.activeTool = lps::StudioTool::SelectMove;
        } else if (ImGui::IsKeyPressed(ImGuiKey_A, false)) {
            app.uiState.activeMode = lps::StudioMode::TexturePaint;
            app.uiState.activeTool = lps::StudioTool::NodeEdit;
        } else if (ImGui::IsKeyPressed(ImGuiKey_O, false)) {
            app.uiState.activeTool = lps::StudioTool::Orbit;
        } else if (ImGui::IsKeyPressed(ImGuiKey_H, false)) {
            app.uiState.activeTool = lps::StudioTool::HandPan;
        } else if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
            app.uiState.activeTool = lps::StudioTool::Zoom;
        } else if (ImGui::IsKeyPressed(ImGuiKey_B, false)) {
            app.uiState.activeMode = lps::StudioMode::TexturePaint;
            app.uiState.activeTool = lps::StudioTool::Brush;
            app.uiState.brush.eraser = false;
        } else if (ImGui::IsKeyPressed(ImGuiKey_E, false)) {
            app.uiState.activeMode = lps::StudioMode::TexturePaint;
            app.uiState.activeTool = lps::StudioTool::Eraser;
            app.uiState.brush.eraser = true;
        } else if (ImGui::IsKeyPressed(ImGuiKey_L, false)) {
            app.uiState.activeMode = lps::StudioMode::TexturePaint;
            app.uiState.activeTool = lps::StudioTool::StraightRuler;
        } else if (ImGui::IsKeyPressed(ImGuiKey_P, false)) {
            app.uiState.activeMode = lps::StudioMode::TexturePaint;
            app.uiState.activeTool = lps::StudioTool::CurveBlade;
        } else if (ImGui::IsKeyPressed(ImGuiKey_U, false)) {
            app.uiState.activeMode = lps::StudioMode::TexturePaint;
            app.uiState.activeTool = lps::StudioTool::RectBox;
        } else if (ImGui::IsKeyPressed(ImGuiKey_T, false)) {
            app.uiState.activeMode = lps::StudioMode::TexturePaint;
            app.uiState.activeTool = lps::StudioTool::Text;
            app.uiState.activeTypeToolVariant = 0;
            app.uiState.activePropsCat = lps::PropertiesCategory::Text;
        } else if (ImGui::IsKeyPressed(ImGuiKey_D, false)) {
            app.uiState.activeMode = lps::StudioMode::TexturePaint;
            app.uiState.activeTool = lps::StudioTool::SponsorDecal;
        } else if (ImGui::IsKeyPressed(ImGuiKey_K, false)) {
            app.uiState.activeMode = lps::StudioMode::TexturePaint;
            app.uiState.activeTool = lps::StudioTool::FloodFill;
        } else if (ImGui::IsKeyPressed(ImGuiKey_I, false)) {
            app.uiState.activeMode = lps::StudioMode::TexturePaint;
            app.uiState.activeTool = lps::StudioTool::Eyedropper;
        } else if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
            app.uiState.activeMode = lps::StudioMode::EditMesh;
            app.uiState.activeTool = lps::StudioTool::FacePaintMask;
        } else if (ImGui::IsKeyPressed(ImGuiKey_X, false)) {
            std::swap(app.uiState.brush.color, app.uiState.secondaryColor);
            app.uiState.statusText = "Swapped Primary / Stroke Colors";
        } else if (ImGui::IsKeyPressed(ImGuiKey_G, false)) {
            app.uiState.activeMode = lps::StudioMode::EditMesh;
            app.uiState.activeTool = lps::StudioTool::VertexEdit;
            app.uiState.editSubTool = lps::EditSubTool::Grab;
        } else if (app.uiState.activeMode == lps::StudioMode::EditMesh) {
            if (ImGui::IsKeyPressed(ImGuiKey_R, false)) app.uiState.editSubTool = lps::EditSubTool::Rotate;
            else if (ImGui::IsKeyPressed(ImGuiKey_S, false)) app.uiState.editSubTool = lps::EditSubTool::Scale;
        }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_1, false) || ImGui::IsKeyPressed(ImGuiKey_Keypad1, false)) {
        applyCameraPreset(app, io.KeyCtrl ? lps::CameraPreset::Rear : lps::CameraPreset::Front);
    } else if (ImGui::IsKeyPressed(ImGuiKey_3, false) || ImGui::IsKeyPressed(ImGuiKey_Keypad3, false)) {
        applyCameraPreset(app, lps::CameraPreset::Side);
    } else if (ImGui::IsKeyPressed(ImGuiKey_7, false) || ImGui::IsKeyPressed(ImGuiKey_Keypad7, false)) {
        applyCameraPreset(app, lps::CameraPreset::Top);
    } else if (ImGui::IsKeyPressed(ImGuiKey_5, false) || ImGui::IsKeyPressed(ImGuiKey_Keypad5, false)) {
        app.uiState.isOrthoCamera = !app.uiState.isOrthoCamera;
        app.uiState.statusText = app.uiState.isOrthoCamera ? "Camera: Orthographic" : "Camera: Perspective";
    }
}

lps::StudioCallbacks makeCallbacks(AppState& app) {
    lps::StudioCallbacks cb{};
    cb.onNewProject = [&app]() {
        app.canvas.init(app.uiState.canvasResolution, lps::Color4u8{238, 240, 244, 255});
        app.uiState.activeProjectPath.clear();
        app.uiState.textEditSyncedLayerId = 0;
        app.gizmoDrag3D = GizmoDrag3D::None;
        app.gizmoLayerIdx3D = -1;
        app.gizmoItemIdx3D = -1;
        if (app.mesh.loaded) applyDefaultAoForMesh(app, true, false);
        syncCanvasGpu(app);
        app.uiState.hasActiveDocument = true;
        app.uiState.showHomeScreen = false;
        app.documentDirty = false;
        app.uiState.statusText = app.mesh.loaded
                                 ? ("Created fresh " + std::to_string(app.uiState.canvasResolution) + "px white canvas + AO shading")
                                 : ("Created fresh " + std::to_string(app.uiState.canvasResolution) + "px canvas");
    };
    cb.onOpenObj = [&app]() {
        std::string p = lps::dialogs::openObjDialog(app.hwnd);
        if (!p.empty()) loadMeshFile(app, p);
    };
    cb.onOpenLiveryPng = [&app]() {
        std::string p = lps::dialogs::openImageDialog(app.hwnd, L"Open Livery Base Texture (.png / .jpg)");
        if (!p.empty()) {
            int prevW = app.canvas.width;
            if (app.canvas.loadBaseTexture(p)) {
                app.uiState.canvasResolution = app.canvas.width;
                if (app.canvas.width != prevW) app.uvState.needsInitialFit = true;
                syncCanvasGpu(app);
                app.uiState.hasActiveDocument = true;
                app.uiState.showHomeScreen = false;
                app.documentDirty = false;
                pushRecentProject(app, p, ".png",
                                  std::to_string(app.canvas.width) + "x" + std::to_string(app.canvas.height) + "px");
                app.uiState.statusText = "Loaded livery base: " + std::filesystem::path(p).filename().string();
            } else {
                app.uiState.statusText = "Failed to load livery image";
            }
        }
    };
    cb.onLoadAoMap = [&app]() {
        std::string p = lps::dialogs::openImageDialog(app.hwnd, L"Load AO / Panel Shading Map (.png / .jpg)");
        if (!p.empty()) {
            if (app.canvas.loadAoOverlay(p, true)) {
                syncCanvasGpu(app);
                app.uiState.statusText = "Loaded AO shading map: " + std::filesystem::path(p).filename().string();
            } else {
                app.uiState.statusText = "Failed to load AO shading map";
            }
        }
    };
    cb.onBakeMeshAo = [&app]() {
        if (app.mesh.loaded && app.canvas.bakeMeshAoOverlay(app.mesh, 1024, true)) {
            syncCanvasGpu(app);
            app.uiState.statusText = "Baked 3D Mesh AO / Panel Crevice Shading";
        } else {
            app.uiState.statusText = "Load a 3D car (.obj) before baking AO";
        }
    };
    cb.onFillPlaceholderText = [&app]() {
        app.canvas.ensureEditableLayer();
        app.canvas.pushUndoSnapshot();
        auto* actIt = app.canvas.activeVectorItem();
        lps::TextLayerProps* tpPtr = (actIt && actIt->kind == lps::VectorItemKind::Text) ? &actIt->text : nullptr;
        if (!tpPtr) {
            lps::TextLayerProps tp{};
            tp.fillColor = app.uiState.brush.color;
            tp.outlineColor = app.uiState.secondaryColor;
            tp.mirrorMode = app.uiState.brush.mirrorX ? lps::MirrorTextMode::ReadableBothSides : lps::MirrorTextMode::None;
            if (app.mesh.loaded) {
                lps::RayHit doorHit = app.mesh.raycast(
                    {lps::Vec3{3.5f, 0.48f, 0.05f}, lps::Vec3{-1.0f, 0.0f, 0.0f}}, true);
                if (doorHit.hit) {
                    tp.uvPos = doorHit.uv;
                    tp.pos3D = doorHit.pos;
                    tp.nrm3D = doorHit.normal;
                    tp.right3D = computeSurfaceRight3D(app, doorHit.normal);
                    tp.anchoredIn3D = true;
                }
            }
            tp.text = "Lorem ipsum dolor sit amet";
            int newIt = app.canvas.addTextItem("Text", tp, &app.mesh);
            if (newIt >= 0) actIt = app.canvas.activeVectorItem();
        } else {
            tpPtr->text = "Lorem ipsum dolor sit amet";
        }
        if (actIt && actIt->kind == lps::VectorItemKind::Text) {
            std::snprintf(app.uiState.textEditBuf, sizeof(app.uiState.textEditBuf), "%s", actIt->text.text.c_str());
            app.uiState.textEditSyncedLayerId = actIt->id;
        }
        app.uiState.activeTool = lps::StudioTool::Text;
        app.uiState.activePropsCat = lps::PropertiesCategory::Text;
        rasterizeActiveLayerAndSync(app, app.canvas.activeLayerIdx, true);
        app.uiState.statusText = "Filled placeholder text: Lorem ipsum dolor sit amet";
    };
    cb.onWindowMinimize = [&app]() {
        if (app.hwnd) ShowWindow(app.hwnd, SW_MINIMIZE);
    };
    cb.onWindowToggleMaximize = [&app]() {
        if (app.hwnd) ShowWindow(app.hwnd, IsZoomed(app.hwnd) ? SW_RESTORE : SW_MAXIMIZE);
    };
    cb.onWindowClose = [&app]() {
        if (app.hwnd) PostMessageW(app.hwnd, WM_CLOSE, 0, 0);
    };
    cb.onImportImage = [&app]() {
        std::string p = lps::dialogs::openImageDialog(app.hwnd, L"Import Image (.png / .jpg)");
        if (!p.empty()) {
            importImageFileAsItem(app, p, nullptr);
        }
    };
    cb.onImportDecal = cb.onImportImage;
    cb.onExportPng = [&app](int res) {
        std::string defName = meshStemOrDefault(app, "livery") + "_livery_" + std::to_string(res) + ".png";
        std::string p = lps::dialogs::savePngDialog(app.hwnd, defName, L"Export Flattened Livery PNG");
        if (!p.empty()) {
            if (app.canvas.exportFlattenedPng(p, res, &app.mesh)) {
                app.uiState.statusText = "Exported PNG: " + std::filesystem::path(p).filename().string();
            } else {
                app.uiState.statusText = "PNG export failed";
            }
        }
    };
    cb.onSaveProject = [&app]() {
        std::string defName = meshStemOrDefault(app, "Untitled") + ".cr";
        std::string p = lps::dialogs::saveProjectDialog(app.hwnd, defName);
        if (!p.empty()) {
            if (app.canvas.saveProject(p, app.uiState.activeMeshPath)) {
                app.uiState.activeProjectPath = p;
                app.documentDirty = false;
                clearRecoverySnapshot(app);
                std::string ext = std::filesystem::path(p).extension().string();
                pushRecentProject(app, p, ext.empty() ? ".cr" : ext,
                                  std::to_string(app.canvas.width) + "x" + std::to_string(app.canvas.height) + "px | " +
                                  std::to_string(app.canvas.layers.size()) + " layers");
                app.uiState.statusText = "Saved project: " + std::filesystem::path(p).filename().string();
            } else {
                app.uiState.statusText = "Project save failed";
            }
        }
    };
    cb.onLoadProject = [&app]() {
        std::string p = lps::dialogs::openProjectDialog(app.hwnd);
        if (!p.empty()) {
            openProjectFilePath(app, p, false);
        }
    };
    cb.onStartPreset = [&app, &cb](int presetIdx) {
        if (presetIdx == 0) {
            app.returnToHomeIfNoDoc = true;
            if (cb.onLoadProject) cb.onLoadProject();
        } else if (presetIdx == 1) {
            app.returnToHomeIfNoDoc = true;
            if (cb.onOpenObj) cb.onOpenObj();
        } else if (presetIdx == 2) {
            app.returnToHomeIfNoDoc = true;
            if (cb.onOpenLiveryPng) cb.onOpenLiveryPng();
        } else {
            app.uiState.canvasResolution = 4096;
            if (cb.onNewProject) cb.onNewProject();
        }
    };
    cb.onOpenRecent = [&app](const std::string& path) {
        if (path.empty() || !std::filesystem::exists(path)) {
            app.uiState.statusText = "Recent file no longer exists: " + path;
            if (!app.uiState.hasActiveDocument) app.uiState.showHomeScreen = true;
            return;
        }
        std::string ext = std::filesystem::path(path).extension().string();
        for (char& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (ext == ".cr" || ext == ".lps") {
            openProjectFilePath(app, path, false);
        } else if (ext == ".obj") {
            loadMeshFile(app, path, false);
        } else {
            if (app.canvas.loadBaseTexture(path)) {
                app.uiState.canvasResolution = app.canvas.width;
                app.uvState.needsInitialFit = true;
                syncCanvasGpu(app);
                app.uiState.hasActiveDocument = true;
                app.uiState.showHomeScreen = false;
                pushRecentProject(app, path, ext, std::to_string(app.canvas.width) + "px texture");
            }
        }
    };
    cb.onReturnToEditor = [&app]() {
        if (app.uiState.hasActiveDocument) {
            app.uiState.showHomeScreen = false;
            app.uiState.statusText = "Returned to Studio";
        }
    };
    cb.onRestoreRecovery = [&app]() {
        if (std::filesystem::exists(app.recoveryCrPath)) {
            openProjectFilePath(app, app.recoveryCrPath.string(), true);
        }
        app.uiState.hasRecoveryBanner = false;
    };
    cb.onDismissRecovery = [&app]() {
        app.uiState.hasRecoveryBanner = false;
        clearRecoverySnapshot(app);
        app.uiState.statusText = "Dismissed recovery snapshot";
    };
    cb.onQuickLoadPorsche = [&app]() {
        bool blankRequested = ImGui::GetIO().KeyShift;
        quickLoadPorsche(app, !blankRequested);
    };
    cb.onRenderBeautyShot = [&app]() {
        app.pendingBeautyDialog = true;
    };
    cb.onRenderPhoto = cb.onRenderBeautyShot;
    cb.onToggleMirror = [&app](bool mir) {
        app.uiState.brush.mirrorX = mir;
        app.canvas.pushUndoSnapshot();
        app.canvas.setMirrorOnActiveItem(mir, &app.mesh);
        syncCanvasGpu(app, true);
        app.uiState.statusText = mir ? "Symmetry Mirror X: ON" : "Symmetry Mirror X: OFF";
    };
    cb.onCopy = [&app]() {
        if (app.canvas.copySelectedItem()) {
            app.uiState.statusText = "Copied selected item";
        } else {
            app.uiState.statusText = "Select a vector item to copy";
        }
    };
    cb.onCut = [&app]() {
        if (app.canvas.activeVectorItem()) {
            app.canvas.pushUndoSnapshot();
            if (app.canvas.cutSelectedItem(&app.mesh)) {
                syncCanvasGpu(app, true);
                app.uiState.statusText = "Cut selected item";
                return;
            }
        }
        app.uiState.statusText = "Select an unlocked vector item to cut";
    };
    cb.onPaste = [&app]() {
        performPasteClipboard(app, app.uiState.viewport3DHovered && app.hoverHit3D.hit);
    };
    cb.onNewLayer = [&app]() {
        app.canvas.pushUndoSnapshot();
        char nm[32];
        std::snprintf(nm, sizeof(nm), "Layer %zu", app.canvas.layers.size());
        app.canvas.addLayer(nm, false, {0, 0, 0, 0});
        syncCanvasGpu(app, true);
        app.uiState.statusText = std::string("Added ") + nm;
    };
    cb.onDuplicateLayer = [&app](int idx) {
        app.canvas.pushUndoSnapshot();
        if (app.canvas.duplicateLayer(idx) >= 0) {
            syncCanvasGpu(app, true);
            app.uiState.statusText = "Duplicated layer";
        }
    };
    cb.onDeleteLayer = [&app](int idx) {
        if (app.canvas.layers.size() <= 1) {
            app.uiState.statusText = "Cannot delete the last remaining layer";
            return;
        }
        app.canvas.pushUndoSnapshot();
        if (app.canvas.removeLayer(idx)) {
            syncCanvasGpu(app, true);
            app.uiState.statusText = "Deleted layer";
        } else {
            app.uiState.statusText = "Layer is locked or protected";
        }
    };
    cb.onRemovePenPoint = [&app](int itemIdx, int ptIdx) {
        int lIdx = app.canvas.activeLayerIdx;
        if (lIdx < 0 || lIdx >= static_cast<int>(app.canvas.layers.size())) return;
        lps::Layer& l = app.canvas.layers[lIdx];
        if (l.locked) return;
        lps::RulerStripeProps* penPtr = nullptr;
        if (itemIdx >= 0 && itemIdx < static_cast<int>(l.items.size()) &&
            l.items[itemIdx].kind == lps::VectorItemKind::PenPath) {
            penPtr = &l.items[itemIdx].pen;
        } else if (l.kind == lps::LayerKind::RulerStripe) {
            penPtr = &l.ruler;
        }
        if (!penPtr || ptIdx < 0 || ptIdx >= static_cast<int>(penPtr->nodes.size())) return;
        app.canvas.pushUndoSnapshot();
        penPtr->nodes.erase(penPtr->nodes.begin() + ptIdx);
        if (penPtr->nodes.size() < 3) penPtr->closed = false;
        rasterizeActiveLayerAndSync(app, lIdx, true);
        app.uiState.statusText = "Removed Pen anchor point";
    };
    cb.onOpenTerms = [&app]() {
        std::filesystem::path touPath = findBundledFileW(L"LICENSE_AND_TOU.txt");
        if (!touPath.empty()) {
            ShellExecuteW(app.hwnd, L"open", touPath.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            app.uiState.statusText = "Opened Terms of Use (LICENSE_AND_TOU.txt)";
        } else {
            app.uiState.requestOpenTermsModal = true;
            app.uiState.statusText = "Viewing built-in Terms of Use summary";
        }
    };
    cb.onCheckUpdates = [&app]() {
        app.uiState.statusText = "Checking for Coatroom updates...";
        lps::OtaCheckResult res = lps::checkForOtaUpdates(kCoatroomVersion, app.otaManifestUrl);
        if (!res.ok) {
            app.uiState.otaUpdateAvailable = false;
            app.uiState.statusText = "Update check: " + res.error;
            return;
        }
        if (!res.updateAvailable) {
            app.uiState.otaUpdateAvailable = false;
            app.uiState.otaLatestVersion = res.latest.version;
            app.uiState.statusText = "Up to date (Coatroom v" + std::string(kCoatroomVersion) + ")";
            return;
        }
        app.uiState.otaUpdateAvailable = true;
        app.uiState.otaLatestVersion = res.latest.version;
        app.uiState.statusText = "Update available: Coatroom v" + res.latest.version;

        if (!res.latest.downloadUrl.empty() || !res.latest.exeUrl.empty()) {
            int wlen = MultiByteToWideChar(CP_UTF8, 0, res.latest.version.c_str(), -1, nullptr, 0);
            std::wstring wVer(wlen > 0 ? wlen - 1 : 0, L'\0');
            if (wlen > 1) MultiByteToWideChar(CP_UTF8, 0, res.latest.version.c_str(), -1, wVer.data(), wlen);
            std::wstring msg = L"Coatroom v" + wVer +
                               L" is available.\n\nDownload, verify SHA-256 checksum, and install now?";
            if (MessageBoxW(app.hwnd, msg.c_str(), L"Coatroom Update", MB_YESNO | MB_ICONINFORMATION) == IDYES) {
                std::filesystem::path updaterPath = findBundledFileW(L"CoatroomUpdater.exe");
                wchar_t exeBuf[MAX_PATH] = {};
                GetModuleFileNameW(nullptr, exeBuf, MAX_PATH);
                std::wstring installDir = std::filesystem::path(exeBuf).parent_path().wstring();
                if (!updaterPath.empty()) {
                    std::wstring args = L"--pid " + std::to_wstring(GetCurrentProcessId()) +
                                        L" --dir \"" + installDir + L"\" --current-version " +
                                        L"1.2.0 --relaunch";
                    if (!app.otaManifestUrl.empty()) {
                        int mwlen = MultiByteToWideChar(CP_UTF8, 0, app.otaManifestUrl.c_str(), -1, nullptr, 0);
                        std::wstring wMan(mwlen > 0 ? mwlen - 1 : 0, L'\0');
                        if (mwlen > 1) MultiByteToWideChar(CP_UTF8, 0, app.otaManifestUrl.c_str(), -1, wMan.data(), mwlen);
                        args += L" --manifest \"" + wMan + L"\"";
                    }
                    ShellExecuteW(app.hwnd, L"open", updaterPath.wstring().c_str(), args.c_str(), nullptr, SW_SHOWNORMAL);
                    app.uiState.statusText = "Launching CoatroomUpdater v" + res.latest.version + "...";
                    if (app.hwnd) PostMessageW(app.hwnd, WM_CLOSE, 0, 0);
                } else {
                    wchar_t tmpDir[MAX_PATH] = {};
                    GetTempPathW(MAX_PATH, tmpDir);
                    std::filesystem::path stageExe = std::filesystem::path(tmpDir) / L"CoatroomSetup_update.exe";
                    std::string dlErr;
                    if (lps::downloadAndVerifyOtaInstaller(res.latest, stageExe.wstring(), dlErr, {})) {
                        std::wstring args = L"/UPDATE --dir \"" + installDir + L"\" --relaunch";
                        ShellExecuteW(app.hwnd, L"open", stageExe.wstring().c_str(), args.c_str(), nullptr, SW_SHOWNORMAL);
                        app.uiState.statusText = "Launching Coatroom v" + res.latest.version + " updater...";
                        if (app.hwnd) PostMessageW(app.hwnd, WM_CLOSE, 0, 0);
                    } else {
                        app.uiState.statusText = "Update download failed: " + dlErr;
                    }
                }
            }
        }
    };
    cb.onSetCameraPreset = [&app](lps::CameraPreset preset) {
        applyCameraPreset(app, preset);
    };
    cb.onToggleOrtho = [&app]() {
        app.uiState.isOrthoCamera = !app.uiState.isOrthoCamera;
        app.uiState.statusText = app.uiState.isOrthoCamera ? "Camera: Orthographic" : "Camera: Perspective";
    };
    cb.onUndo = [&app]() {
        if (app.canvas.canUndo()) {
            app.canvas.undo(&app.mesh);
            syncCanvasGpu(app, true);
            app.uiState.textEditSyncedLayerId = 0;
            app.uiState.statusText = "Undo";
        } else {
            app.uiState.statusText = "Nothing to undo";
        }
    };
    cb.onRedo = [&app]() {
        if (app.canvas.canRedo()) {
            app.canvas.redo(&app.mesh);
            syncCanvasGpu(app, true);
            app.uiState.textEditSyncedLayerId = 0;
            app.uiState.statusText = "Redo";
        } else {
            app.uiState.statusText = "Nothing to redo";
        }
    };
    cb.onResizeCanvas = [&app](int newRes) {
        app.canvas.resizeCanvas(newRes);
        app.uiState.canvasResolution = newRes;
        app.uvState.needsInitialFit = true;
        syncCanvasGpu(app);
        app.uiState.statusText = "Resized canvas to " + std::to_string(newRes) + "x" + std::to_string(newRes);
    };
    cb.onZoom3D = [&app](float zoomDelta) {
        float scale = std::pow(0.84f, zoomDelta);
        app.camDist = std::clamp(app.camDist * scale, 0.7f, 32.0f);
        app.sceneParams.orthoHalfHeight = std::clamp(app.sceneParams.orthoHalfHeight * scale, 0.25f, 15.0f);
    };
    cb.onPan3D = [&app](float dx, float dy) {
        lps::Vec3 eye, fwd, right, up;
        computeCameraBasis(app, eye, fwd, right, up);
        (void)eye; (void)fwd;
        float vpH = std::max(app.uiState.viewport3DPanelSize.y, 16.0f);
        float worldSpanH = app.uiState.isOrthoCamera
                           ? (app.sceneParams.orthoHalfHeight * 2.0f)
                           : (2.0f * app.camDist * std::tan(app.sceneParams.fovYDeg * 0.5f * lps::kDeg2Rad));
        float panScale = worldSpanH / vpH;
        app.camTarget -= right * (dx * panScale);
        app.camTarget += up * (dy * panScale);
    };
    cb.onClearSelection = [&app]() {
        if (app.uiState.activeMode != lps::StudioMode::EditMesh && app.canvas.activeVectorItem()) {
            int lIdx = app.canvas.activeLayerIdx;
            if (lIdx >= 0 && lIdx < static_cast<int>(app.canvas.layers.size()) &&
                !app.canvas.layers[lIdx].locked) {
                app.canvas.pushUndoSnapshot();
                if (app.canvas.deleteSelectedItem(&app.mesh)) {
                    syncCanvasGpu(app, true);
                    app.uiState.statusText = "Deleted selected item";
                    return;
                }
            }
        }
        if (app.mesh.loaded) {
            app.mesh.clearSelection();
            app.uiState.meshGpuDirty = true;
            syncMeshGpu(app);
            app.uiState.statusText = "Cleared 3D selection / mask";
        }
    };
    cb.onMeshModified = [&app]() { syncMeshGpu(app); };
    cb.onCanvasModified = [&app]() { syncCanvasGpu(app); };
    cb.onDraw3DViewportOverlay = [&app]() { draw3DViewportHudOverlay(app); };
    return cb;
}

bool readBoundBackbufferRgba(lps::D3D11Renderer& renderer, std::vector<uint8_t>& outRgba,
                             int& outW, int& outH) {
    ID3D11Device* dev = renderer.device();
    ID3D11DeviceContext* ctx = renderer.context();
    if (!dev || !ctx) return false;

    ID3D11RenderTargetView* rtv = nullptr;
    ctx->OMGetRenderTargets(1, &rtv, nullptr);
    if (!rtv) return false;

    ID3D11Resource* res = nullptr;
    rtv->GetResource(&res);
    rtv->Release();
    if (!res) return false;

    ID3D11Texture2D* tex = nullptr;
    HRESULT hr = res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex));
    res->Release();
    if (FAILED(hr) || !tex) return false;

    D3D11_TEXTURE2D_DESC desc{};
    tex->GetDesc(&desc);
    D3D11_TEXTURE2D_DESC sDesc = desc;
    sDesc.Usage = D3D11_USAGE_STAGING;
    sDesc.BindFlags = 0;
    sDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    sDesc.MiscFlags = 0;

    ID3D11Texture2D* staging = nullptr;
    hr = dev->CreateTexture2D(&sDesc, nullptr, &staging);
    if (FAILED(hr) || !staging) {
        tex->Release();
        return false;
    }

    ctx->CopyResource(staging, tex);
    tex->Release();

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
        staging->Release();
        return false;
    }

    outW = static_cast<int>(desc.Width);
    outH = static_cast<int>(desc.Height);
    outRgba.resize(static_cast<size_t>(outW) * outH * 4);
    bool isBgra = (desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM || desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);

    const uint8_t* srcBase = static_cast<const uint8_t*>(mapped.pData);
    for (int y = 0; y < outH; ++y) {
        const uint8_t* rowSrc = srcBase + static_cast<size_t>(y) * mapped.RowPitch;
        uint8_t* rowDst = outRgba.data() + static_cast<size_t>(y) * outW * 4;
        for (int x = 0; x < outW; ++x) {
            if (isBgra) {
                rowDst[x * 4 + 0] = rowSrc[x * 4 + 2];
                rowDst[x * 4 + 1] = rowSrc[x * 4 + 1];
                rowDst[x * 4 + 2] = rowSrc[x * 4 + 0];
            } else {
                rowDst[x * 4 + 0] = rowSrc[x * 4 + 0];
                rowDst[x * 4 + 1] = rowSrc[x * 4 + 1];
                rowDst[x * 4 + 2] = rowSrc[x * 4 + 2];
            }
            rowDst[x * 4 + 3] = 255;
        }
    }

    ctx->Unmap(staging, 0);
    staging->Release();
    return true;
}

bool captureWindowPng(HWND hwnd, const std::vector<uint8_t>& clientRgba, int clientW, int clientH,
                      const std::string& outPath) {
    auto parent = std::filesystem::path(outPath).parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
    }

    RECT rcWin{};
    if (!GetWindowRect(hwnd, &rcWin)) {
        return !clientRgba.empty() &&
               stbi_write_png(outPath.c_str(), clientW, clientH, 4, clientRgba.data(), clientW * 4) != 0;
    }
    int w = rcWin.right - rcWin.left;
    int h = rcWin.bottom - rcWin.top;
    if (w <= 0 || h <= 0) return false;

    HDC screenDC = GetDC(nullptr);
    HDC memDC = CreateCompatibleDC(screenDC);

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp || !bits) {
        if (bmp) DeleteObject(bmp);
        DeleteDC(memDC);
        ReleaseDC(nullptr, screenDC);
        return !clientRgba.empty() &&
               stbi_write_png(outPath.c_str(), clientW, clientH, 4, clientRgba.data(), clientW * 4) != 0;
    }

    HGDIOBJ oldObj = SelectObject(memDC, bmp);
    BOOL ok = PrintWindow(hwnd, memDC, PW_RENDERFULLCONTENT);
    if (!ok) {
        ok = BitBlt(memDC, 0, 0, w, h, screenDC, rcWin.left, rcWin.top, SRCCOPY);
    }

    std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4, 255);
    const uint8_t* bgra = static_cast<const uint8_t*>(bits);
    if (ok) {
        for (int i = 0; i < w * h; ++i) {
            rgba[i * 4 + 0] = bgra[i * 4 + 2];
            rgba[i * 4 + 1] = bgra[i * 4 + 1];
            rgba[i * 4 + 2] = bgra[i * 4 + 0];
            rgba[i * 4 + 3] = 255;
        }
    } else {
        std::fill(rgba.begin(), rgba.end(), 22);
        for (int i = 0; i < w * h; ++i) rgba[i * 4 + 3] = 255;
    }

    SelectObject(memDC, oldObj);
    DeleteObject(bmp);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);

    // Composite exact D3D11 backbuffer over client area so DXGI flip/blt frames are never blank/white
    if (!clientRgba.empty() && clientW > 0 && clientH > 0) {
        POINT clientOrigin{0, 0};
        ClientToScreen(hwnd, &clientOrigin);
        int offX = static_cast<int>(clientOrigin.x - rcWin.left);
        int offY = static_cast<int>(clientOrigin.y - rcWin.top);
        int copyW = std::min(clientW, w - offX);
        int copyH = std::min(clientH, h - offY);
        for (int y = 0; y < copyH; ++y) {
            if (offY + y < 0) continue;
            for (int x = 0; x < copyW; ++x) {
                if (offX + x < 0) continue;
                size_t srcIdx = (static_cast<size_t>(y) * clientW + x) * 4;
                size_t dstIdx = (static_cast<size_t>(offY + y) * w + (offX + x)) * 4;
                rgba[dstIdx + 0] = clientRgba[srcIdx + 0];
                rgba[dstIdx + 1] = clientRgba[srcIdx + 1];
                rgba[dstIdx + 2] = clientRgba[srcIdx + 2];
                rgba[dstIdx + 3] = 255;
            }
        }
    }

    return stbi_write_png(outPath.c_str(), w, h, 4, rgba.data(), w * 4) != 0;
}

void renderFrame(AppState& app, const lps::StudioCallbacks& callbacks,
                 std::vector<uint8_t>* outBackbufRgba = nullptr,
                 int* outClientW = nullptr, int* outClientH = nullptr) {
    syncCanvasGpu(app);
    syncMeshGpu(app);
    syncSceneParams(app);

    int vpW = std::max(64, static_cast<int>(std::round(app.uiState.viewport3DPanelSize.x)));
    int vpH = std::max(64, static_cast<int>(std::round(app.uiState.viewport3DPanelSize.y)));
    std::vector<lps::LineVertex3D> depthLines, xrayLines;
    buildViewport3DLines(app, depthLines, xrayLines);
    app.renderer.renderViewport(app.mesh, app.sceneParams, vpW, vpH, depthLines, xrayLines);
    app.uiState.viewport3DTex = reinterpret_cast<ImTextureID>(app.renderer.viewportSRV());

    const float clearCol[4] = {0.078f, 0.082f, 0.094f, 1.0f};
    app.renderer.beginFrame(clearCol);

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    handleShortcuts(app, callbacks);
    lps::drawStudioWorkspace(app.uiState, app.uvState, app.canvas, app.mesh, callbacks);
    handle3DViewportInput(app);

    if (app.returnToHomeIfNoDoc) {
        app.returnToHomeIfNoDoc = false;
        if (!app.uiState.hasActiveDocument) {
            app.uiState.showHomeScreen = true;
        }
    }

    if (app.uiState.requestCopySelection) {
        app.uiState.requestCopySelection = false;
        if (callbacks.onCopy) callbacks.onCopy();
    }
    if (app.uiState.requestCutSelection) {
        app.uiState.requestCutSelection = false;
        if (callbacks.onCut) callbacks.onCut();
    }
    if (app.uiState.requestPasteClipboard) {
        app.uiState.requestPasteClipboard = false;
        if (callbacks.onPaste) callbacks.onPaste();
    }

    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    if (outBackbufRgba && outClientW && outClientH) {
        readBoundBackbufferRgba(app.renderer, *outBackbufRgba, *outClientW, *outClientH);
    }

    app.renderer.endFrame(true);
}

int runSmokeTest(AppState& app, const lps::StudioCallbacks& callbacks, const std::string& outDir) {
    auto t0 = std::chrono::steady_clock::now();
    auto msSince = [&]() -> long long {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
    };

    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);

    // 1. Render initial frame so viewport sizes & ImGui layout settle
    for (int f = 0; f < 3; ++f) {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        renderFrame(app, callbacks);
    }
    std::printf("[SMOKE %lld ms] Initial UI frames rendered\n", msSince());
    std::fflush(stdout);

    // 2. Load Porsche 992 GT3 R OBJ on clean 4K (4096x4096) White + Auto AO Shading canvas
    app.canvas.init(4096, lps::Color4u8{238, 240, 244, 255});
    app.uiState.canvasResolution = 4096;
    if (!loadMeshFile(app, kPorscheObjPath)) {
        std::fprintf(stderr, "[SMOKE FAIL] Could not load Porsche OBJ: %s\n", kPorscheObjPath);
        return 1;
    }
    int aoIdx = app.canvas.findAoLayerIndex();
    if (aoIdx < 0 || !app.canvas.layers[aoIdx].visible ||
        app.canvas.layers[aoIdx].blend != lps::BlendMode::Multiply) {
        std::fprintf(stderr, "[SMOKE FAIL] Expected visible Multiply AO shading layer (idx=%d)\n", aoIdx);
        return 18;
    }
    int minAoLum = 255, maxAoLum = 0;
    for (int y = 0; y < app.canvas.height; y += 16) {
        for (int x = 0; x < app.canvas.width; x += 16) {
            size_t o = (static_cast<size_t>(y) * app.canvas.width + x) * 4;
            int lum = (app.canvas.compositeRgba[o + 0] + app.canvas.compositeRgba[o + 1] +
                       app.canvas.compositeRgba[o + 2]) / 3;
            minAoLum = std::min(minAoLum, lum);
            maxAoLum = std::max(maxAoLum, lum);
        }
    }
    if (maxAoLum < 200 || minAoLum > 160 || (maxAoLum - minAoLum) < 40) {
        std::fprintf(stderr, "[SMOKE FAIL] AO Multiply shading variance insufficient (min=%d, max=%d)\n",
                     minAoLum, maxAoLum);
        return 19;
    }
    std::printf("[SMOKE %lld ms] Loaded Porsche OBJ (%zu tris, %zu groups) + AO Multiply shading (lum=%d..%d)\n",
                msSince(), app.mesh.tris.size(), app.mesh.groups.size(), minAoLum, maxAoLum);
    std::fflush(stdout);

    // 3. Test bilateral L/R 3D Brush stroke on +X fender/side
    lps::BrushSettings bs = app.uiState.brush;
    bs.color = lps::Color4u8{205, 232, 32, 255}; // Acid Lime
    bs.worldRadius = 0.09f;
    bs.hardness = 0.90f;
    bs.opacity = 1.0f;
    bs.spacing = 0.35f;
    bs.mirrorX = true;
    bs.project3DBrush = true;

    lps::Ray rBrush1{lps::Vec3{3.5f, 0.65f, 0.85f}, lps::Vec3{-1.0f, 0.0f, 0.0f}};
    lps::Ray rBrush2{lps::Vec3{3.5f, 0.65f, 0.55f}, lps::Vec3{-1.0f, 0.0f, 0.0f}};
    lps::RayHit hB1 = app.mesh.raycast(rBrush1, true);
    lps::RayHit hB2 = app.mesh.raycast(rBrush2, true);
    if (!hB1.hit || !hB2.hit) {
        std::fprintf(stderr, "[SMOKE FAIL] Brush raycast missed Porsche side\n");
        return 2;
    }
    int paintLayerIdx = app.canvas.activeLayerIdx;
    app.canvas.beginStroke(paintLayerIdx);
    app.canvas.paintDabAt3D(paintLayerIdx, app.mesh, hB1, bs, nullptr);
    app.canvas.paintDabAt3D(paintLayerIdx, app.mesh, hB2, bs, &hB1);
    app.canvas.endStroke(paintLayerIdx);

    // Verify bilateral symmetry painted acid-lime (high G, low B) on opposite -X side too
    lps::Ray rLeftCheck{lps::Vec3{-3.5f, hB1.pos.y, hB1.pos.z}, lps::Vec3{1.0f, 0.0f, 0.0f}};
    lps::RayHit hLeft = app.mesh.raycast(rLeftCheck, true);
    if (!hLeft.hit) {
        std::fprintf(stderr, "[SMOKE FAIL] Left-side symmetry raycast missed\n");
        return 3;
    }
    lps::Color4u8 rightPx = app.canvas.sampleCompositeAtUV(hB1.uv);
    lps::Color4u8 leftPx = app.canvas.sampleCompositeAtUV(hLeft.uv);
    std::printf("[SMOKE %lld ms] Brush sample R=(%d,%d,%d) L=(%d,%d,%d)\n",
                msSince(), rightPx.r, rightPx.g, rightPx.b, leftPx.r, leftPx.g, leftPx.b);
    std::fflush(stdout);
    if (rightPx.g < 140 || leftPx.g < 140 || rightPx.b > 110 || leftPx.b > 110) {
        std::fprintf(stderr, "[SMOKE FAIL] Bilateral L/R brush paint verification failed\n");
        return 4;
    }

    // 4. Test Straight Ruler Stripe (symmetric 3D surface projection across solid fender/door/quarter)
    int rulerIdx = app.canvas.addRulerStripeLayer("Speed Stripe");
    auto& rl = app.canvas.layers[rulerIdx].ruler;
    rl.smoothCurve = false;
    rl.use3DSpace = true;
    rl.mirrorX = true;
    rl.worldWidth = 0.065f;
    rl.endTaper = 0.25f;
    rl.color = lps::Color4u8{225, 34, 52, 255}; // Signal Red

    lps::RayHit hS0 = app.mesh.raycast({lps::Vec3{3.5f, 0.64f, 1.15f}, lps::Vec3{-1.0f, 0.0f, 0.0f}}, true);
    lps::RayHit hS1 = app.mesh.raycast({lps::Vec3{3.5f, 0.65f, -0.95f}, lps::Vec3{-1.0f, 0.0f, 0.0f}}, true);
    if (!hS0.hit || !hS1.hit) {
        std::fprintf(stderr, "[SMOKE FAIL] Ruler raycast missed Porsche side (h0=%d, h1=%d)\n",
                     hS0.hit ? 1 : 0, hS1.hit ? 1 : 0);
        return 5;
    }
    auto makeSmokeNode = [](const lps::RayHit& h) {
        lps::StripeNode n{};
        n.uv = h.uv;
        n.handleInUv = h.uv;
        n.handleOutUv = h.uv;
        n.pos3D = h.pos;
        n.handleIn3D = h.pos;
        n.handleOut3D = h.pos;
        n.nrm3D = h.normal;
        n.has3D = true;
        n.hasHandles = false;
        n.smooth = false;
        return n;
    };
    rl.nodes.push_back(makeSmokeNode(hS0));
    rl.nodes.push_back(makeSmokeNode(hS1));
    app.canvas.rasterizeVectorLayer(rulerIdx, &app.mesh);
    std::printf("[SMOKE %lld ms] Ruler stripe rasterized\n", msSince());
    std::fflush(stdout);

    // 5. Test Adobe-style "Lorem ipsum" default + Live 3D edit -> "STANN #3" + non-shrinking 3D<->2D scale
    int textIdx = app.canvas.addTextLayer("Door Number");
    auto& tp = app.canvas.layers[textIdx].text;
    if (tp.text != "Lorem ipsum") {
        std::fprintf(stderr, "[SMOKE FAIL] Default TextLayerProps expected 'Lorem ipsum', got '%s'\n", tp.text.c_str());
        return 20;
    }
    tp.fontSizePx = 128.0f;
    tp.worldHeight = 0.22f;
    tp.outlineWidth = 4.0f;
    tp.fillColor = lps::Color4u8{24, 28, 36, 255};
    tp.outlineColor = lps::Color4u8{248, 250, 252, 255};
    tp.mirrorMode = lps::MirrorTextMode::ReadableBothSides;
    tp.anchoredIn3D = true;

    lps::RayHit hTxt = app.mesh.raycast({lps::Vec3{3.5f, 0.44f, 0.05f}, lps::Vec3{-1.0f, 0.0f, 0.0f}}, true);
    if (!hTxt.hit) {
        std::fprintf(stderr, "[SMOKE FAIL] Text placement raycast missed Porsche door\n");
        return 6;
    }
    tp.uvPos = hTxt.uv;
    tp.pos3D = hTxt.pos;
    tp.nrm3D = hTxt.normal;
    tp.right3D = lps::Vec3{0.0f, 0.0f, 1.0f}; // +Z nose is screen-right when facing +X door
    app.canvas.rasterizeVectorLayer(textIdx, &app.mesh);
    if (!app.canvas.layers[textIdx].hasPrevBounds) {
        std::fprintf(stderr, "[SMOKE FAIL] Lorem ipsum initial 3D rasterization produced empty bounds\n");
        return 21;
    }

    // Live edit text + translate position via DirtyRect partial upload
    tp.text = "STANN #3";
    tp.pos3D.y += 0.012f;
    app.canvas.rasterizeVectorLayer(textIdx, &app.mesh);
    if (!app.canvas.dirtyRect.valid || app.canvas.fullUpload) {
        std::fprintf(stderr, "[SMOKE FAIL] Live vector edit did not produce partial DirtyRect upload (valid=%d, full=%d)\n",
                     app.canvas.dirtyRect.valid ? 1 : 0, app.canvas.fullUpload ? 1 : 0);
        return 22;
    }

    // Verify 3D->2D Text scale calibration & roundtrip (<= 5% variance)
    const float expectedTextWH = tp.worldHeight;
    const float syncedFontPx = tp.fontSizePx;
    app.canvas.syncLayer2DTo3D(textIdx, &app.mesh);
    float textScaleDiff = std::fabs(tp.worldHeight - expectedTextWH) / std::max(0.01f, expectedTextWH);
    tp.worldHeight = expectedTextWH;
    if (textScaleDiff > 0.05f || syncedFontPx < 16.0f) {
        std::fprintf(stderr, "[SMOKE FAIL] Text 3D<->2D scale drift %.3f (fontPx=%.1f)\n",
                     textScaleDiff, syncedFontPx);
        return 23;
    }

    // Verify 3D<->2D Decal scale non-shrinking (<= 5% variance)
    const char* badgePath = "examples\\safety_car\\assets\\fia_safety_badge.png";
    if (std::filesystem::exists(badgePath)) {
        int decalIdx = app.canvas.addDecalLayer("Smoke FIA Badge", badgePath);
        if (decalIdx >= 0) {
            auto& dp = app.canvas.layers[decalIdx].decal;
            dp.worldWidth = 0.24f;
            dp.anchoredIn3D = true;
            dp.mirrorMode = lps::MirrorTextMode::ReadableBothSides;
            lps::RayHit hDec = app.mesh.raycast({lps::Vec3{3.5f, 0.56f, 0.35f}, lps::Vec3{-1.0f, 0.0f, 0.0f}}, true);
            if (hDec.hit) {
                dp.uvPos = hDec.uv;
                dp.pos3D = hDec.pos;
                dp.nrm3D = hDec.normal;
                dp.right3D = lps::Vec3{0.0f, 0.0f, 1.0f};
                app.canvas.rasterizeVectorLayer(decalIdx, &app.mesh);
                float estUvW = dp.worldWidth * app.mesh.estimateUvPerMeter(dp.pos3D, dp.nrm3D);
                float decalDiff = std::fabs(dp.uvScale.x - estUvW) / std::max(1e-4f, estUvW);
                if (decalDiff > 0.05f && app.canvas.layers[decalIdx].hasPrimaryUvBox) {
                    float boxW = app.canvas.layers[decalIdx].primaryUvHalf.x * 2.0f;
                    decalDiff = std::fabs(dp.uvScale.x - boxW) / std::max(1e-4f, boxW);
                }
                if (decalDiff > 0.05f || dp.uvScale.x < 0.02f) {
                    std::fprintf(stderr, "[SMOKE FAIL] Decal 3D<->2D scale variance %.3f (uvScale=%.4f)\n",
                                 decalDiff, dp.uvScale.x);
                    return 24;
                }
                app.canvas.layers[decalIdx].visible = false; // keep smoke beauty UI clean on STANN #3
            }
        }
    }

    // 5b. Verify closed filled Cubic Bezier Pen path + bilateral mirror symmetry
    int penIdx = app.canvas.addRulerStripeLayer("Smoke Aero Chevron");
    auto& pen = app.canvas.layers[penIdx].ruler;
    pen.closed = true;
    pen.filled = true;
    pen.hasStroke = true;
    pen.use3DSpace = true;
    pen.mirrorX = true;
    pen.fillColor = lps::Color4u8{232, 255, 24, 255};
    pen.color = lps::Color4u8{20, 24, 30, 255};
    pen.worldWidth = 0.015f;
    lps::RayHit hP0 = app.mesh.raycast({lps::Vec3{3.5f, 0.56f, 0.28f}, lps::Vec3{-1.0f, 0.0f, 0.0f}}, true);
    lps::RayHit hP1 = app.mesh.raycast({lps::Vec3{3.5f, 0.60f, -0.35f}, lps::Vec3{-1.0f, 0.0f, 0.0f}}, true);
    lps::RayHit hP2 = app.mesh.raycast({lps::Vec3{3.5f, 0.49f, -0.25f}, lps::Vec3{-1.0f, 0.0f, 0.0f}}, true);
    if (!hP0.hit || !hP1.hit || !hP2.hit) {
        std::fprintf(stderr, "[SMOKE FAIL] Cubic Bezier Pen raycasts missed Porsche side\n");
        return 25;
    }
    auto makeBezNode = [](const lps::RayHit& h, lps::Vec3 inOff, lps::Vec3 outOff) {
        lps::StripeNode n{};
        n.uv = h.uv; n.pos3D = h.pos; n.nrm3D = h.normal; n.has3D = true;
        n.handleIn3D = h.pos + inOff; n.handleOut3D = h.pos + outOff;
        n.handleInUv = h.uv; n.handleOutUv = h.uv;
        n.hasHandles = true; n.smooth = true;
        return n;
    };
    pen.nodes.push_back(makeBezNode(hP0, {0.0f, -0.02f, 0.05f}, {0.0f, 0.03f, -0.08f}));
    pen.nodes.push_back(makeBezNode(hP1, {0.0f, 0.03f, 0.08f}, {0.0f, -0.04f, 0.02f}));
    pen.nodes.push_back(makeBezNode(hP2, {0.0f, -0.03f, -0.05f}, {0.0f, 0.01f, 0.08f}));
    app.canvas.rasterizeVectorLayer(penIdx, &app.mesh);

    lps::Vec3 penCenter3 = (hP0.pos + hP1.pos + hP2.pos) * (1.0f / 3.0f);
    lps::RayHit hPenR = app.mesh.raycast({lps::Vec3{3.5f, penCenter3.y, penCenter3.z}, lps::Vec3{-1.0f, 0.0f, 0.0f}}, true);
    lps::RayHit hPenL = app.mesh.raycast({lps::Vec3{-3.5f, penCenter3.y, penCenter3.z}, lps::Vec3{1.0f, 0.0f, 0.0f}}, true);
    if (!hPenR.hit || !hPenL.hit) {
        std::fprintf(stderr, "[SMOKE FAIL] Closed Bezier Pen check raycast missed flank\n");
        return 26;
    }
    lps::Color4u8 penPxR = app.canvas.sampleCompositeAtUV(hPenR.uv);
    lps::Color4u8 penPxL = app.canvas.sampleCompositeAtUV(hPenL.uv);
    if (penPxR.g < 150 || penPxR.b > 110 || penPxL.g < 150 || penPxL.b > 110) {
        std::fprintf(stderr, "[SMOKE FAIL] Closed Bezier Pen mirror check failed R=(%d,%d,%d) L=(%d,%d,%d)\n",
                     penPxR.r, penPxR.g, penPxR.b, penPxL.r, penPxL.g, penPxL.b);
        return 27;
    }

    // 5c. Verify reorderLayer preserves active layer ID and places AO cleanly on top
    app.canvas.activeLayerIdx = textIdx;
    uint32_t trackedId = app.canvas.layers[textIdx].id;
    app.canvas.reorderLayer(penIdx, std::max(1, textIdx - 1));
    if (app.canvas.activeLayerIdx < 0 ||
        app.canvas.activeLayerIdx >= static_cast<int>(app.canvas.layers.size()) ||
        app.canvas.layers[app.canvas.activeLayerIdx].id != trackedId) {
        std::fprintf(stderr, "[SMOKE FAIL] reorderLayer did not preserve activeLayerIdx ID\n");
        return 28;
    }
    int aoCur = app.canvas.findAoLayerIndex();
    if (aoCur >= 0) app.canvas.reorderLayer(aoCur, static_cast<int>(app.canvas.layers.size()) - 1);
    if (app.canvas.layers.back().name != "AO / Panel Shading") {
        std::fprintf(stderr, "[SMOKE FAIL] reorderLayer failed to place AO on top\n");
        return 29;
    }

    syncCanvasGpu(app, true);
    std::printf("[SMOKE %lld ms] Lorem ipsum + 3D<->2D scale + closed Bezier Pen R=(%d,%d,%d)/L=(%d,%d,%d) + reorderLayer verified\n",
                msSince(), penPxR.r, penPxR.g, penPxR.b, penPxL.r, penPxL.g, penPxL.b);
    std::fflush(stdout);

    // Keep Text gizmo active in UI and render multiple live frames
    app.uiState.activeTool = lps::StudioTool::Text;
    app.uiState.shadingMode = lps::ShadingMode::Rendered;
    applyCameraPreset(app, lps::CameraPreset::ThreeQuarterBeauty);

    for (int f = 0; f < 5; ++f) {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        renderFrame(app, callbacks);
    }
    std::printf("[SMOKE %lld ms] Rendered live studio frames\n", msSince());
    std::fflush(stdout);

    // 6. Save & Reload .lps project roundtrip test
    std::string lpsPath = (std::filesystem::path(outDir) / "smoke_project.lps").string();
    if (!app.canvas.saveProject(lpsPath, app.uiState.activeMeshPath)) {
        std::fprintf(stderr, "[SMOKE FAIL] Failed to save .lps project: %s\n", lpsPath.c_str());
        return 7;
    }
    lps::CanvasEngine reloadCanvas;
    std::string reloadedMeshPath;
    if (!reloadCanvas.loadProject(lpsPath, reloadedMeshPath) ||
        reloadCanvas.layers.size() != app.canvas.layers.size() ||
        reloadCanvas.width != 4096) {
        std::fprintf(stderr, "[SMOKE FAIL] .lps project reload mismatch\n");
        return 8;
    }
    std::printf("[SMOKE %lld ms] .lps project save/reload verified (%zu layers, mesh=%s)\n",
                msSince(), reloadCanvas.layers.size(), reloadedMeshPath.c_str());
    std::fflush(stdout);

    // 7. Export 2K & 4K Flattened PNGs
    std::string png2k = (std::filesystem::path(outDir) / "smoke_livery_2k.png").string();
    std::string png4k = (std::filesystem::path(outDir) / "smoke_livery_4k.png").string();
    if (!app.canvas.exportFlattenedPng(png2k, 2048, &app.mesh) ||
        !app.canvas.exportFlattenedPng(png4k, 4096, &app.mesh)) {
        std::fprintf(stderr, "[SMOKE FAIL] Flattened 2K/4K PNG export failed\n");
        return 9;
    }
    std::printf("[SMOKE %lld ms] Exported 2K PNG: %s (%llu bytes)\n", msSince(), png2k.c_str(),
                static_cast<unsigned long long>(std::filesystem::file_size(png2k)));
    std::printf("[SMOKE %lld ms] Exported 4K PNG: %s (%llu bytes)\n", msSince(), png4k.c_str(),
                static_cast<unsigned long long>(std::filesystem::file_size(png4k)));
    std::fflush(stdout);

    // 8. F12 Studio Beauty Renders (Right 3/4 + Left 3/4 to inspect both sides!)
    syncSceneParams(app);
    std::string beautyR = (std::filesystem::path(outDir) / "smoke_beauty_f12.png").string();
    if (!app.renderer.renderBeautyShotToPng(app.mesh, app.sceneParams, beautyR, 1920, 1080)) {
        std::fprintf(stderr, "[SMOKE FAIL] F12 beauty render failed: %s\n", app.renderer.lastError().c_str());
        return 10;
    }
    std::printf("[SMOKE %lld ms] F12 beauty render (R): %s\n", msSince(), beautyR.c_str());

    lps::RenderSceneParams leftParams = app.sceneParams;
    leftParams.camEye = lps::Vec3{-app.sceneParams.camEye.x, app.sceneParams.camEye.y, app.sceneParams.camEye.z};
    leftParams.lights[0].position.x = -leftParams.lights[0].position.x;
    std::string beautyL = (std::filesystem::path(outDir) / "smoke_beauty_left_f12.png").string();
    if (!app.renderer.renderBeautyShotToPng(app.mesh, leftParams, beautyL, 1920, 1080)) {
        std::fprintf(stderr, "[SMOKE FAIL] F12 left-side beauty render failed\n");
        return 11;
    }
    std::printf("[SMOKE %lld ms] F12 beauty render (L): %s\n", msSince(), beautyL.c_str());
    std::fflush(stdout);

    // 9. Capture live Win32 + D3D11 + ImGui Window Screenshot
    std::vector<uint8_t> backbufRgba;
    int clientW = 0, clientH = 0;
    renderFrame(app, callbacks, &backbufRgba, &clientW, &clientH);
    DwmFlush();
    std::string winShot = (std::filesystem::path(outDir) / "smoke_window_ui.png").string();
    if (!captureWindowPng(app.hwnd, backbufRgba, clientW, clientH, winShot)) {
        std::fprintf(stderr, "[SMOKE FAIL] Window screenshot capture failed\n");
        return 12;
    }
    std::printf("[SMOKE %lld ms] Window UI screenshot: %s\n", msSince(), winShot.c_str());

    // 10. Multi-Car .obj + .mtl (map_Kd) Swap & Native Resolution Adaptation + UV Wrapping Test
    {
        std::filesystem::path synthDir = std::filesystem::path(outDir) / "synth_car";
        std::filesystem::create_directories(synthDir, ec);
        std::string synthTex = (synthDir / "synth_gt_car.png").string();
        std::string synthMtl = (synthDir / "synth_gt_car.mtl").string();
        std::string synthObj = (synthDir / "synth_gt_car.obj").string();

        const int tw = 1024, th = 1024;
        std::vector<uint8_t> texRgba(static_cast<size_t>(tw) * th * 4, 255);
        for (int y = 0; y < th; ++y) {
            for (int x = 0; x < tw; ++x) {
                float u = static_cast<float>(x) / tw;
                bool goldStripe = (u >= 0.42f && u <= 0.58f);
                size_t o = (static_cast<size_t>(y) * tw + x) * 4;
                texRgba[o + 0] = goldStripe ? 238 : 34;
                texRgba[o + 1] = goldStripe ? 186 : 82;
                texRgba[o + 2] = goldStripe ? 40  : 188;
                texRgba[o + 3] = 255;
            }
        }
        if (!stbi_write_png(synthTex.c_str(), tw, th, 4, texRgba.data(), tw * 4)) {
            std::fprintf(stderr, "[SMOKE FAIL] Failed writing synthetic car texture\n");
            return 13;
        }
        {
            std::ofstream mfs(synthMtl);
            mfs << "newmtl GTCarBody\nKd 1.0 1.0 1.0\nmap_Kd synth_gt_car.png\n";
        }
        {
            std::ofstream ofs(synthObj);
            ofs << "mtllib synth_gt_car.mtl\n"
                << "g SynthGT\nusemtl GTCarBody\n"
                // Low-poly sleek GT body wedge (front +Z, rear -Z)
                << "v -0.95 0.05  2.20\n" // 1: front-left bottom
                << "v  0.95 0.05  2.20\n" // 2: front-right bottom
                << "v  0.95 0.45  2.10\n" // 3: front-right hood
                << "v -0.95 0.45  2.10\n" // 4: front-left hood
                << "v -0.98 0.05 -2.20\n" // 5: rear-left bottom
                << "v  0.98 0.05 -2.20\n" // 6: rear-right bottom
                << "v  0.98 0.85 -2.10\n" // 7: rear-right deck
                << "v -0.98 0.85 -2.10\n" // 8: rear-left deck
                << "v -0.78 1.20 -0.25\n" // 9: roof-left
                << "v  0.78 1.20 -0.25\n" // 10: roof-right
                // Standard UVs + tiled out-of-range UVs [1.25, 1.75] to verify UV wrapping
                << "vt 0.10 0.10\n" // 1
                << "vt 0.90 0.10\n" // 2
                << "vt 0.90 0.90\n" // 3
                << "vt 0.10 0.90\n" // 4
                << "vt 1.25 1.25\n" // 5 (wrapped = 0.25, 0.25)
                << "vt 1.75 1.25\n" // 6 (wrapped = 0.75, 0.25)
                << "vt 1.75 1.75\n" // 7 (wrapped = 0.75, 0.75)
                << "vt 1.25 1.75\n" // 8 (wrapped = 0.25, 0.75)
                << "f 4/1 3/2 10/3 9/4\n" // hood/windshield
                << "f 9/5 10/6 7/7 8/8\n" // roof/rear deck with wrapped UVs
                << "f 1/1 2/2 3/3 4/4\n"  // nose
                << "f 2/1 6/2 7/3 3/4\n"  // right flank
                << "f 5/1 1/2 4/3 8/4\n"  // left flank
                << "f 6/1 5/2 8/3 7/4\n"; // rear
        }

        if (!loadMeshFile(app, synthObj)) {
            std::fprintf(stderr, "[SMOKE FAIL] Could not load synthetic multi-car OBJ\n");
            return 14;
        }
        int synthAoIdx = app.canvas.findAoLayerIndex();
        if (app.mesh.resolvedDiffuseTexPath.empty() ||
            app.canvas.width != 1024 ||
            app.canvas.layers.size() != 3 ||
            synthAoIdx < 0 || app.canvas.layers[synthAoIdx].visible) {
            std::fprintf(stderr, "[SMOKE FAIL] Multi-car MTL/canvas reset check failed (tex=%s, w=%d, layers=%zu, aoIdx=%d)\n",
                         app.mesh.resolvedDiffuseTexPath.c_str(), app.canvas.width, app.canvas.layers.size(), synthAoIdx);
            return 15;
        }
        lps::Color4u8 centerCol = app.canvas.sampleCompositeAtUV({0.5f, 0.5f});
        lps::Color4u8 sideCol   = app.canvas.sampleCompositeAtUV({0.15f, 0.5f});
        lps::Color4u8 wrapCol   = app.canvas.sampleCompositeAtUV({1.50f, 1.50f});
        std::printf("[SMOKE %lld ms] Multi-car swap verified: tex=%s (%dx%d), layers=%zu, center=(%d,%d,%d), side=(%d,%d,%d), wrap=(%d,%d,%d)\n",
                    msSince(),
                    std::filesystem::path(app.mesh.resolvedDiffuseTexPath).filename().string().c_str(),
                    app.canvas.width, app.canvas.height, app.canvas.layers.size(),
                    centerCol.r, centerCol.g, centerCol.b,
                    sideCol.r, sideCol.g, sideCol.b,
                    wrapCol.r, wrapCol.g, wrapCol.b);
        if (centerCol.r < 200 || sideCol.b < 150 || wrapCol.r < 200) {
            std::fprintf(stderr, "[SMOKE FAIL] Synthetic multi-car texture sampling/UV wrap mismatch\n");
            return 16;
        }
        for (int f = 0; f < 2; ++f) {
            MSG msg{};
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            renderFrame(app, callbacks);
        }
        syncSceneParams(app);
        std::string synthBeauty = (std::filesystem::path(outDir) / "smoke_synth_car_beauty.png").string();
        if (!app.renderer.renderBeautyShotToPng(app.mesh, app.sceneParams, synthBeauty, 1280, 720)) {
            std::fprintf(stderr, "[SMOKE FAIL] Synthetic car beauty render failed\n");
            return 17;
        }
        std::printf("[SMOKE %lld ms] Synthetic multi-car beauty render: %s\n", msSince(), synthBeauty.c_str());
    }

    // 11. Coatroom v1.2 Core Features Verification (Container Layers, CoW Undo/Delete, Copy/Paste, Mirror/PenRemove, .cr, OTA, BrandLogo)
    {
        // 11a. Render brand logo RGBA (64x64 squircle Cr badge)
        std::vector<uint8_t> logo = lps::renderBrandLogoRgba(64);
        if (logo.size() != static_cast<size_t>(64) * 64 * 4) {
            std::fprintf(stderr, "[SMOKE FAIL] renderBrandLogoRgba(64) invalid size: %zu\n", logo.size());
            return 30;
        }
        bool hasTeal = false;
        for (size_t k = 0; k < logo.size(); k += 4) {
            if (logo[k + 3] > 200 && logo[k + 1] > 180 && logo[k + 2] > 150) {
                hasTeal = true;
                break;
            }
        }
        if (!hasTeal || logo[(32u * 64u + 32u) * 4u + 3u] < 200) {
            std::fprintf(stderr, "[SMOKE FAIL] renderBrandLogoRgba(64) missing teal accent or center coverage\n");
            return 31;
        }

        // 11b. Container Layer non-spawning + RectShape + PenPath items + Mirror/RemovePoint + Copy/Paste + 200-step CoW Undo/Delete
        lps::CanvasEngine v12;
        v12.init(1024, lps::Color4u8{240, 240, 244, 255});
        size_t baseLayerCount = v12.layers.size();
        int ed1 = v12.ensureEditableLayer();
        int ed2 = v12.ensureEditableLayer();
        if (ed1 < 0 || ed1 != ed2 || v12.layers.size() != baseLayerCount) {
            std::fprintf(stderr, "[SMOKE FAIL] ensureEditableLayer spawned unexpected layer (%zu vs %zu)\n",
                         v12.layers.size(), baseLayerCount);
            return 32;
        }

        v12.pushUndoSnapshot();
        lps::RectShapeProps rProps{};
        rProps.uvPos = {0.35f, 0.35f};
        rProps.uvScale = {0.16f, 0.10f};
        rProps.anchoredIn3D = false;
        rProps.filled = true;
        rProps.fillColor = lps::Color4u8{47, 230, 206, 255};
        int rItem = v12.addRectItem("V12 Box", rProps, nullptr);
        if (rItem < 0 || v12.layers[ed1].items.size() != 1) {
            std::fprintf(stderr, "[SMOKE FAIL] addRectItem container storage failed\n");
            return 33;
        }

        v12.setMirrorOnActiveItem(true, nullptr);
        if (!v12.layers[ed1].items[rItem].rect.mirrorX) {
            std::fprintf(stderr, "[SMOKE FAIL] setMirrorOnActiveItem failed on RectShape\n");
            return 34;
        }

        if (!v12.copySelectedItem() || v12.pasteClipboardItem(nullptr) < 0 ||
            v12.layers[ed1].items.size() != 2) {
            std::fprintf(stderr, "[SMOKE FAIL] copySelectedItem / pasteClipboardItem failed\n");
            return 35;
        }

        // Pen point addition & removal + mirror
        lps::RulerStripeProps pProps{};
        pProps.use3DSpace = false;
        pProps.smoothCurve = true;
        pProps.color = lps::Color4u8{235, 108, 24, 255};
        for (int k = 0; k < 3; ++k) {
            lps::StripeNode sn{};
            sn.uv = {0.20f + 0.10f * k, 0.65f + (k == 1 ? 0.08f : 0.0f)};
            pProps.nodes.push_back(sn);
        }
        int pItem = v12.addPenItem("V12 Pen", pProps, nullptr);
        if (pItem < 0 || v12.layers[ed1].items[pItem].pen.nodes.size() != 3) {
            std::fprintf(stderr, "[SMOKE FAIL] addPenItem container setup failed\n");
            return 36;
        }
        v12.layers[ed1].items[pItem].pen.nodes.pop_back();
        v12.rasterizeLayer(ed1, nullptr);
        if (v12.layers[ed1].items[pItem].pen.nodes.size() != 2) {
            std::fprintf(stderr, "[SMOKE FAIL] Pen anchor removal failed\n");
            return 37;
        }

        // Delete layer + Undo/Redo restore test
        int tempLayer = v12.addLayer("V12 Deletable", false, {0, 0, 0, 0});
        v12.pushUndoSnapshot();
        size_t beforeDel = v12.layers.size();
        if (!v12.removeLayer(tempLayer) || v12.layers.size() != beforeDel - 1) {
            std::fprintf(stderr, "[SMOKE FAIL] removeLayer failed\n");
            return 38;
        }
        v12.undo(nullptr);
        if (v12.layers.size() != beforeDel) {
            std::fprintf(stderr, "[SMOKE FAIL] Undo after removeLayer failed\n");
            return 39;
        }
        v12.redo(nullptr);
        if (v12.layers.size() != beforeDel - 1) {
            std::fprintf(stderr, "[SMOKE FAIL] Redo after removeLayer failed\n");
            return 40;
        }

        // .cr CRM1 binary roundtrip test
        std::string crPath = (std::filesystem::path(outDir) / "smoke_project.cr").string();
        if (!v12.saveProject(crPath, app.uiState.activeMeshPath)) {
            std::fprintf(stderr, "[SMOKE FAIL] Failed saving .cr project\n");
            return 41;
        }
        {
            std::ifstream crIn(crPath, std::ios::binary);
            char magic[4] = {};
            crIn.read(magic, 4);
            if (std::memcmp(magic, "CRM1", 4) != 0) {
                std::fprintf(stderr, "[SMOKE FAIL] .cr magic header expected CRM1\n");
                return 42;
            }
        }
        lps::CanvasEngine crReload;
        std::string crMesh;
        if (!crReload.loadProject(crPath, crMesh) ||
            crReload.layers.size() != v12.layers.size() ||
            crReload.layers[ed1].items.size() != v12.layers[ed1].items.size()) {
            std::fprintf(stderr, "[SMOKE FAIL] .cr loadProject roundtrip item mismatch\n");
            return 43;
        }

        // OTA semver/SHA-256 manifest self-test
        std::string otaRep;
        if (!lps::runOtaSelfTest(otaRep)) {
            std::fprintf(stderr, "[SMOKE FAIL] runOtaSelfTest failed: %s\n", otaRep.c_str());
            return 44;
        }
        std::printf("[SMOKE %lld ms] Coatroom v1.2 checks OK (.cr CRM1, CoW undo/delete, container items, copy/paste, mirror, %s)\n",
                    msSince(), otaRep.c_str());
    }

    // 12. Crash Protection (.cr autosave/recovery) & Recent Projects JSON + 3/4 Car Thumbnail + Home Screen UI
    {
        auto savedStorageDir = app.coatroomDataDir;
        auto savedRecentPath = app.recentJsonPath;
        auto savedThumbsDir = app.thumbsDir;
        auto savedRecDir = app.recoveryDir;
        auto savedRecCr = app.recoveryCrPath;
        auto savedRecMeta = app.recoveryMetaPath;
        auto savedRecentList = app.uiState.recentProjects;
        bool savedSuppress = app.suppressRecentTracking;
        bool savedHasDoc = app.uiState.hasActiveDocument;
        bool savedHome = app.uiState.showHomeScreen;
        bool savedRecBanner = app.uiState.hasRecoveryBanner;

        // Re-load painted Porsche 992 GT3 R (from Check 2-5 smoke_project.cr) so we render a real Front 3/4th Porsche preview card!
        std::string smokeCr = (std::filesystem::path(outDir) / "smoke_project.cr").string();
        lps::CanvasEngine savedCanvasBackup = app.canvas;
        if (std::filesystem::exists(kPorscheObjPath)) {
            app.suppressRecentTracking = true;
            quickLoadPorsche(app, std::filesystem::exists(kPorscheLiveryPath));
        }

        std::filesystem::path smokeAppData = std::filesystem::path(outDir) / "smoke_appdata";
        std::filesystem::remove_all(smokeAppData, ec);
        initCoatroomStoragePaths(app, smokeAppData);
        app.uiState.recentProjects.clear();
        app.suppressRecentTracking = false;

        pushRecentProject(app, kPorscheObjPath, ".obj", "Porsche 992 GT3 R (4K)");
        pushRecentProject(app, smokeCr, ".cr", "4096x4096px | Painted Project");
        pushRecentProject(app, kPorscheObjPath, ".obj", "Porsche Re-opened"); // must deduplicate & promote to index 0

        auto reloadedRec = loadRecentProjectsFile(app.recentJsonPath);
        if (reloadedRec.size() != 2 || reloadedRec[0].kind != ".obj" || reloadedRec[1].kind != ".cr" ||
            reloadedRec[0].thumbPath.empty() || !std::filesystem::exists(reloadedRec[0].thumbPath) ||
            app.uiState.recentProjects[0].thumbTex == 0) {
            std::fprintf(stderr, "[SMOKE FAIL] Recent projects JSON / 320x180 Front 3/4 thumbnail check failed (count=%zu, thumb='%s')\n",
                         reloadedRec.size(), reloadedRec.empty() ? "" : reloadedRec[0].thumbPath.c_str());
            return 45;
        }

        // Write recovery snapshot & verify reload + banner + summary + Home Screen UI frame + screenshot
        app.uiState.hasActiveDocument = true;
        if (!writeRecoverySnapshot(app, "Smoke Autosave") ||
            !std::filesystem::exists(app.recoveryCrPath) ||
            !std::filesystem::exists(app.recoveryMetaPath)) {
            std::fprintf(stderr, "[SMOKE FAIL] writeRecoverySnapshot failed\n");
            return 46;
        }
        std::string recSum = readRecoverySummary(app);
        if (recSum.find("Smoke Autosave") == std::string::npos) {
            std::fprintf(stderr, "[SMOKE FAIL] readRecoverySummary missing metadata: %s\n", recSum.c_str());
            return 47;
        }

        app.uiState.showHomeScreen = true;
        app.uiState.hasRecoveryBanner = true;
        app.uiState.recoveryInfoText = recSum;
        std::vector<uint8_t> homeBackbuf;
        int homeW = 0, homeH = 0;
        for (int f = 0; f < 3; ++f) {
            MSG msg{};
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (f == 2) renderFrame(app, callbacks, &homeBackbuf, &homeW, &homeH);
            else renderFrame(app, callbacks);
        }
        DwmFlush();
        std::string homePreviewShot = (std::filesystem::path(outDir) / "smoke_home_ui.png").string();
        if (!captureWindowPng(app.hwnd, homeBackbuf, homeW, homeH, homePreviewShot)) {
            std::fprintf(stderr, "[SMOKE FAIL] Home Screen screenshot with Recent 3/4 car preview failed\n");
            return 50;
        }

        lps::CanvasEngine recCanvas;
        std::string recMesh;
        if (!recCanvas.loadProject(app.recoveryCrPath.string(), recMesh) ||
            recCanvas.layers.size() != app.canvas.layers.size()) {
            std::fprintf(stderr, "[SMOKE FAIL] Recovery .cr reload mismatch\n");
            return 48;
        }
        clearRecoverySnapshot(app);
        if (std::filesystem::exists(app.recoveryCrPath)) {
            std::fprintf(stderr, "[SMOKE FAIL] clearRecoverySnapshot left file on disk\n");
            return 49;
        }

        app.canvas = std::move(savedCanvasBackup);
        app.coatroomDataDir = savedStorageDir;
        app.recentJsonPath = savedRecentPath;
        app.thumbsDir = savedThumbsDir;
        app.recoveryDir = savedRecDir;
        app.recoveryCrPath = savedRecCr;
        app.recoveryMetaPath = savedRecMeta;
        app.uiState.recentProjects = std::move(savedRecentList);
        app.suppressRecentTracking = savedSuppress;
        app.uiState.hasActiveDocument = savedHasDoc;
        app.uiState.showHomeScreen = savedHome;
        app.uiState.hasRecoveryBanner = savedRecBanner;
        std::printf("[SMOKE %lld ms] Home Screen + Recent JSON (%zu entries, 320x180 3/4 thumb OK) + Crash Recovery (.cr) -> %s\n",
                    msSince(), reloadedRec.size(), homePreviewShot.c_str());
    }

    // 13. Grouped Human-Readable Fonts + Area Type Word Wrap + Per-Letter Color (Touch Type)
    {
        if (app.canvas.fontFamilies.empty() ||
            app.canvas.findFontPath("", "Arial", "Bold").empty()) {
            std::fprintf(stderr, "[SMOKE FAIL] Grouped fontFamilies / Arial Bold resolution failed (families=%zu)\n",
                         app.canvas.fontFamilies.size());
            return 51;
        }
        lps::CanvasEngine typeTest;
        typeTest.init(1024, lps::Color4u8{20, 20, 24, 255});
        typeTest.ensureEditableLayer();
        lps::TextLayerProps tp{};
        tp.text = "RED BLUE";
        tp.fontFamily = "Arial";
        tp.fontStyle = "Bold";
        tp.fontPath = app.canvas.findFontPath("", tp.fontFamily, tp.fontStyle);
        tp.fontSizePx = 120.0f;
        tp.isAreaBox = true;
        tp.boxWidthPx = 260.0f; // wraps "BLUE" onto line 2
        tp.boxHeightPx = 320.0f;
        tp.anchoredIn3D = false;
        tp.uvPos = {0.5f, 0.5f};
        tp.syncCharStyles();
        tp.charStyles[0].hasColor = true;
        tp.charStyles[0].color = lps::Color4u8{240, 25, 25, 255}; // R
        tp.charStyles[4].hasColor = true;
        tp.charStyles[4].color = lps::Color4u8{25, 90, 245, 255}; // B
        int ti = typeTest.addTextItem("Smoke Type", tp, nullptr);
        if (ti < 0) {
            std::fprintf(stderr, "[SMOKE FAIL] addTextItem failed in Check 13\n");
            return 52;
        }
        const auto& boxes = typeTest.layers[typeTest.activeLayerIdx].items[ti].text.lastCharBoxesUv;
        if (boxes.size() < 8 || boxes[4].uvMin.y <= boxes[0].uvMin.y + 0.02f) {
            std::fprintf(stderr, "[SMOKE FAIL] Area Box word wrap failed (y0=%.3f, y4=%.3f)\n",
                         boxes.empty() ? 0.0f : boxes[0].uvMin.y, boxes.size() > 4 ? boxes[4].uvMin.y : 0.0f);
            return 53;
        }
        auto maxRedInBox = [&](const lps::CharBoxUv& b, bool checkRed) {
            int x0 = std::clamp(static_cast<int>(b.uvMin.x * 1024.0f), 0, 1023);
            int x1 = std::clamp(static_cast<int>(b.uvMax.x * 1024.0f), 0, 1023);
            int y0 = std::clamp(static_cast<int>(b.uvMin.y * 1024.0f), 0, 1023);
            int y1 = std::clamp(static_cast<int>(b.uvMax.y * 1024.0f), 0, 1023);
            int best = 0;
            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    size_t o = (static_cast<size_t>(y) * 1024u + x) * 4u;
                    int val = checkRed ? (typeTest.compositeRgba[o + 0] - typeTest.compositeRgba[o + 2])
                                       : (typeTest.compositeRgba[o + 2] - typeTest.compositeRgba[o + 0]);
                    best = std::max(best, val);
                }
            }
            return best;
        };
        int redDiff = maxRedInBox(boxes[0], true);
        int blueDiff = maxRedInBox(boxes[4], false);
        if (redDiff < 120 || blueDiff < 120) {
            std::fprintf(stderr, "[SMOKE FAIL] Per-char color mismatch (redDiff=%d, blueDiff=%d)\n", redDiff, blueDiff);
            return 54;
        }
        std::printf("[SMOKE %lld ms] Grouped fonts (%zu families) + Area Box wrap + Per-letter color (R=%d, B=%d) verified!\n",
                    msSince(), app.canvas.fontFamilies.size(), redDiff, blueDiff);
    }

    std::printf("[SMOKE PASS] All automated checks passed!\n");
    std::fflush(stdout);
    return 0;
}

HICON createStudioAppIcon(int sz) {
    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof(BITMAPV5HEADER);
    bi.bV5Width = sz;
    bi.bV5Height = -sz; // top-down
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask   = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask  = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    void* bits = nullptr;
    HDC hdc = GetDC(nullptr);
    HBITMAP colorBmp = CreateDIBSection(hdc, reinterpret_cast<BITMAPINFO*>(&bi),
                                        DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, hdc);
    HBITMAP maskBmp = CreateBitmap(sz, sz, 1, 1, nullptr);
    if (!colorBmp || !bits || !maskBmp) {
        if (colorBmp) DeleteObject(colorBmp);
        if (maskBmp) DeleteObject(maskBmp);
        return nullptr;
    }

    auto* px = static_cast<uint32_t*>(bits);
    const float fSz = static_cast<float>(sz);

    // Precompute polyline approximation of 'C' and 'r' cubic Beziers matching drawAppMonogramLogo
    auto evalCubic = [](lps::Vec2 p0, lps::Vec2 p1, lps::Vec2 p2, lps::Vec2 p3, float t) -> lps::Vec2 {
        float u = 1.0f - t;
        float w0 = u * u * u;
        float w1 = 3.0f * u * u * t;
        float w2 = 3.0f * u * t * t;
        float w3 = t * t * t;
        return lps::Vec2{p0.x * w0 + p1.x * w1 + p2.x * w2 + p3.x * w3,
                         p0.y * w0 + p1.y * w1 + p2.y * w2 + p3.y * w3};
    };

    std::array<lps::Vec2, 25> cCurve;
    for (int i = 0; i <= 24; ++i) {
        cCurve[i] = evalCubic({0.50f, 0.30f}, {0.15f, 0.21f}, {0.15f, 0.79f}, {0.50f, 0.71f}, i / 24.0f);
    }
    std::array<lps::Vec2, 13> rCurve;
    for (int i = 0; i <= 12; ++i) {
        rCurve[i] = evalCubic({0.60f, 0.51f}, {0.65f, 0.41f}, {0.75f, 0.41f}, {0.81f, 0.47f}, i / 12.0f);
    }

    auto distSeg = [](float px0, float py0, float ax, float ay, float bx, float by) -> float {
        float vx = bx - ax, vy = by - ay;
        float wx = px0 - ax, wy = py0 - ay;
        float len2 = vx * vx + vy * vy;
        float t = (len2 > 1e-8f) ? std::clamp((wx * vx + wy * vy) / len2, 0.0f, 1.0f) : 0.0f;
        return std::hypot(px0 - (ax + vx * t), py0 - (ay + vy * t));
    };

    auto pointInChevron = [](float nx, float ny) -> bool {
        static const lps::Vec2 poly[6] = {
            {0.62f, 0.16f}, {0.76f, 0.16f}, {0.86f, 0.24f},
            {0.76f, 0.32f}, {0.62f, 0.32f}, {0.71f, 0.24f}
        };
        bool inside = false;
        for (int i = 0, j = 5; i < 6; j = i++) {
            float yi = poly[i].y, yj = poly[j].y;
            if (((yi > ny) != (yj > ny)) &&
                (nx < (poly[j].x - poly[i].x) * (ny - yi) / ((yj - yi) + 1e-9f) + poly[i].x)) {
                inside = !inside;
            }
        }
        return inside;
    };

    auto sdRoundedUnit = [](float nx, float ny) -> float {
        float ax = std::fabs(nx - 0.5f) - (0.46f - 0.22f);
        float ay = std::fabs(ny - 0.5f) - (0.46f - 0.22f);
        return std::hypot(std::max(ax, 0.0f), std::max(ay, 0.0f)) + std::min(std::max(ax, ay), 0.0f) - 0.22f;
    };

    // 4x4 supersampled anti-aliasing matching Coatroom v1.2 (#052228 deep emerald-teal + #2FE6CE + #F8FAFC)
    constexpr int kSS = 4;
    const float strokeHalf = 0.052f;
    for (int y = 0; y < sz; ++y) {
        for (int x = 0; x < sz; ++x) {
            float bgCov = 0.0f;
            float sumR = 0.0f, sumG = 0.0f, sumB = 0.0f;
            for (int sy = 0; sy < kSS; ++sy) {
                for (int sx = 0; sx < kSS; ++sx) {
                    float nx = (x + (sx + 0.5f) / kSS) / fSz;
                    float ny = (y + (sy + 0.5f) / kSS) / fSz;
                    float sdBox = sdRoundedUnit(nx, ny);
                    if (sdBox <= 0.0f) {
                        bgCov += 1.0f;
                        // Base deep emerald-teal #052228
                        float sr = 5.0f, sg = 34.0f, sb = 40.0f;
                        // Bright teal rim #2FE6CE
                        if (sdBox > -0.068f) {
                            sr = 47.0f; sg = 230.0f; sb = 206.0f;
                        } else {
                            float dC = 1e9f;
                            for (int k = 0; k < 24; ++k) {
                                dC = std::min(dC, distSeg(nx, ny, cCurve[k].x, cCurve[k].y,
                                                          cCurve[k + 1].x, cCurve[k + 1].y));
                            }
                            float dR = distSeg(nx, ny, 0.60f, 0.43f, 0.60f, 0.74f);
                            for (int k = 0; k < 12; ++k) {
                                dR = std::min(dR, distSeg(nx, ny, rCurve[k].x, rCurve[k].y,
                                                          rCurve[k + 1].x, rCurve[k + 1].y));
                            }
                            if (dC <= strokeHalf) {
                                sr = 248.0f; sg = 250.0f; sb = 252.0f; // #F8FAFC white 'C'
                            } else if (dR <= strokeHalf * 0.94f) {
                                sr = 47.0f; sg = 230.0f; sb = 206.0f;  // #2FE6CE teal 'r'
                            } else if (pointInChevron(nx, ny)) {
                                sr = 47.0f; sg = 230.0f; sb = 206.0f;  // #2FE6CE accent chevron
                            }
                        }
                        sumR += sr; sumG += sg; sumB += sb;
                    }
                }
            }
            const float invSamples = 1.0f / (kSS * kSS);
            bgCov *= invSamples;
            if (bgCov <= 0.001f) {
                px[y * sz + x] = 0;
                continue;
            }
            float sampleCount = bgCov / invSamples;
            float r = sumR / sampleCount;
            float g = sumG / sampleCount;
            float b = sumB / sampleCount;
            uint8_t a8 = static_cast<uint8_t>(std::clamp(static_cast<int>(bgCov * 255.0f + 0.5f), 0, 255));
            uint8_t r8 = static_cast<uint8_t>(std::clamp(static_cast<int>(r + 0.5f), 0, 255));
            uint8_t g8 = static_cast<uint8_t>(std::clamp(static_cast<int>(g + 0.5f), 0, 255));
            uint8_t b8 = static_cast<uint8_t>(std::clamp(static_cast<int>(b + 0.5f), 0, 255));
            px[y * sz + x] = (static_cast<uint32_t>(a8) << 24) |
                             (static_cast<uint32_t>(r8) << 16) |
                             (static_cast<uint32_t>(g8) << 8) |
                             static_cast<uint32_t>(b8);
        }
    }

    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmMask = maskBmp;
    ii.hbmColor = colorBmp;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(colorBmp);
    DeleteObject(maskBmp);
    return icon;
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) {
        return TRUE;
    }
    switch (msg) {
    case WM_NCCALCSIZE:
        if (wParam == TRUE && lParam != 0) {
            auto* ncp = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
            if (IsZoomed(hwnd)) {
                HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                MONITORINFO mi{};
                mi.cbSize = sizeof(mi);
                if (GetMonitorInfoW(mon, &mi)) {
                    ncp->rgrc[0] = mi.rcWork;
                }
            }
            return 0;
        }
        break;
    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        if (mmi) {
            mmi->ptMinTrackSize.x = 920;
            mmi->ptMinTrackSize.y = 580;
            HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi{};
            mi.cbSize = sizeof(mi);
            if (GetMonitorInfoW(mon, &mi)) {
                mmi->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
                mmi->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;
                mmi->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
                mmi->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;
            }
        }
        return 0;
    }
    case WM_NCACTIVATE:
        return TRUE;
    case WM_NCHITTEST:
        if (g_app) {
            int sx = static_cast<int>(static_cast<short>(LOWORD(lParam)));
            int sy = static_cast<int>(static_cast<short>(HIWORD(lParam)));
            RECT rcWin{};
            GetWindowRect(hwnd, &rcWin);
            if (!IsZoomed(hwnd)) {
                const int border = 6;
                const bool hitL = (sx < rcWin.left + border);
                const bool hitR = (sx >= rcWin.right - border);
                const bool hitT = (sy < rcWin.top + border);
                const bool hitB = (sy >= rcWin.bottom - border);
                if (hitT && hitL) return HTTOPLEFT;
                if (hitT && hitR) return HTTOPRIGHT;
                if (hitB && hitL) return HTBOTTOMLEFT;
                if (hitB && hitR) return HTBOTTOMRIGHT;
                if (hitL) return HTLEFT;
                if (hitR) return HTRIGHT;
                if (hitT) return HTTOP;
                if (hitB) return HTBOTTOM;
            }
            POINT cl{sx, sy};
            ScreenToClient(hwnd, &cl);
            const ImVec4& hdr = g_app->uiState.headerBarRect;
            float winW = static_cast<float>(rcWin.right - rcWin.left);
            bool overRightCap = (cl.x >= static_cast<LONG>(winW - 104.0f) && cl.y < 32);
            if (cl.x >= static_cast<LONG>(hdr.x) && cl.x < static_cast<LONG>(hdr.z) &&
                cl.y >= static_cast<LONG>(hdr.y) && cl.y < static_cast<LONG>(hdr.w) &&
                !overRightCap && !g_app->uiState.headerItemHovered) {
                return HTCAPTION;
            }
            return HTCLIENT;
        }
        break;
    case WM_SIZE:
        if (g_app) {
            g_app->uiState.windowMaximized = (wParam == SIZE_MAXIMIZED) || (IsZoomed(hwnd) != FALSE);
            if (wParam != SIZE_MINIMIZED) {
                UINT w = LOWORD(lParam);
                UINT h = HIWORD(lParam);
                if (w > 0 && h > 0) {
                    g_app->renderer.onResize(static_cast<int>(w), static_cast<int>(h));
                }
            }
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0; // disable ALT system menu trap
        break;
    case WM_DESTROY:
        if (g_app) g_app->running = false;
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int main(int argc, char** argv) {
    bool smokeTest = false;
    std::string smokeOutDir = "build\\smoke_test";
    std::string cliObjPath;
    std::string cliTexturePath;
    std::string cliAoPath;
    std::string cliScreenshotPath;
    std::string cliScriptPath;
    std::string cliTooltipTool;
    std::string cliOtaManifestUrl;
    bool cliForceHome = false;
    bool mcpStdio = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--smoke-test") == 0) {
            smokeTest = true;
        } else if (std::strcmp(argv[i], "--smoke-out") == 0 && i + 1 < argc) {
            smokeOutDir = argv[++i];
        } else if (std::strcmp(argv[i], "--obj") == 0 && i + 1 < argc) {
            cliObjPath = argv[++i];
        } else if (std::strcmp(argv[i], "--texture") == 0 && i + 1 < argc) {
            cliTexturePath = argv[++i];
        } else if (std::strcmp(argv[i], "--ao") == 0 && i + 1 < argc) {
            cliAoPath = argv[++i];
        } else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            cliScreenshotPath = argv[++i];
        } else if (std::strcmp(argv[i], "--screenshot-tooltip") == 0 && i + 1 < argc) {
            cliTooltipTool = argv[++i];
        } else if (std::strcmp(argv[i], "--ota-manifest") == 0 && i + 1 < argc) {
            cliOtaManifestUrl = argv[++i];
        } else if (std::strcmp(argv[i], "--home") == 0) {
            cliForceHome = true;
        } else if (std::strcmp(argv[i], "--script") == 0 && i + 1 < argc) {
            cliScriptPath = argv[++i];
        } else if (std::strcmp(argv[i], "--mcp-stdio") == 0) {
            mcpStdio = true;
        }
    }

    ImGui_ImplWin32_EnableDpiAwareness();

    AppState app;
    app.cliAoOverridePath = cliAoPath;
    app.otaManifestUrl = cliOtaManifestUrl;
    app.suppressRecentTracking = (smokeTest || !cliScriptPath.empty() || mcpStdio);
    initCoatroomStoragePaths(app, getCoatroomDataDir());
    if (!cliTooltipTool.empty()) {
        app.uiState.forcePreviewRichTooltip = parseRichTooltipArg(cliTooltipTool);
    }
    g_app = &app;

    HINSTANCE hInst = GetModuleHandleW(nullptr);
    HICON hIconBig = static_cast<HICON>(
        LoadImageW(hInst, MAKEINTRESOURCEW(kIdIconCoatroom), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
    HICON hIconSmall = static_cast<HICON>(
        LoadImageW(hInst, MAKEINTRESOURCEW(kIdIconCoatroom), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
    if (!hIconBig) hIconBig = createStudioAppIcon(32);
    if (!hIconSmall) hIconSmall = createStudioAppIcon(16);
    HBRUSH darkBgBrush = CreateSolidBrush(RGB(30, 30, 32));
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInst;
    wc.hIcon = hIconBig;
    wc.hIconSm = hIconSmall;
    wc.hbrBackground = darkBgBrush;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"CoatroomWndClass";
    if (!RegisterClassExW(&wc)) {
        std::fprintf(stderr, "RegisterClassExW failed\n");
        if (hIconBig) DestroyIcon(hIconBig);
        if (hIconSmall) DestroyIcon(hIconSmall);
        if (darkBgBrush) DeleteObject(darkBgBrush);
        return 1;
    }

    RECT workArea{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    int maxW = std::max(960, static_cast<int>(workArea.right - workArea.left - 24));
    int maxH = std::max(640, static_cast<int>(workArea.bottom - workArea.top - 24));
    int winW = std::min(1600, maxW);
    int winH = std::min(960, maxH);
    int winX = workArea.left + (static_cast<int>(workArea.right - workArea.left) - winW) / 2;
    int winY = workArea.top + (static_cast<int>(workArea.bottom - workArea.top) - winH) / 2;

    app.hwnd = CreateWindowExW(
        0, wc.lpszClassName, L"Coatroom",
        WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU | WS_CLIPCHILDREN,
        winX, winY, winW, winH,
        nullptr, nullptr, hInst, nullptr);
    if (!app.hwnd) {
        std::fprintf(stderr, "CreateWindowExW failed\n");
        UnregisterClassW(wc.lpszClassName, hInst);
        if (hIconBig) DestroyIcon(hIconBig);
        if (hIconSmall) DestroyIcon(hIconSmall);
        if (darkBgBrush) DeleteObject(darkBgBrush);
        return 1;
    }

    if (hIconBig) SendMessageW(app.hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hIconBig));
    if (hIconSmall) SendMessageW(app.hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIconSmall));

    // Apply borderless frame extents & dark Win11 studio frame border cleanly
    SetWindowPos(app.hwnd, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    BOOL darkMode = TRUE;
    if (FAILED(DwmSetWindowAttribute(app.hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode)))) {
        DwmSetWindowAttribute(app.hwnd, 19, &darkMode, sizeof(darkMode));
    }
    DWORD cornerPref = 1; // DWMWCP_DONOTROUND: sharp Illustrator edge corners
    DwmSetWindowAttribute(app.hwnd, 33, &cornerPref, sizeof(cornerPref));
    COLORREF borderCol = RGB(66, 66, 66);
    DwmSetWindowAttribute(app.hwnd, 34, &borderCol, sizeof(borderCol));

    RECT rc{};
    GetClientRect(app.hwnd, &rc);
    if (!app.renderer.init(app.hwnd, rc.right - rc.left, rc.bottom - rc.top)) {
        std::fprintf(stderr, "D3D11Renderer::init failed: %s\n", app.renderer.lastError().c_str());
        DestroyWindow(app.hwnd);
        UnregisterClassW(wc.lpszClassName, hInst);
        if (hIconBig) DestroyIcon(hIconBig);
        if (hIconSmall) DestroyIcon(hIconSmall);
        if (darkBgBrush) DeleteObject(darkBgBrush);
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = nullptr;

    lps::loadStudioFonts(io, 14.5f);
    lps::applyStudioTheme(1.0f);

    ImGui_ImplWin32_Init(app.hwnd);
    ImGui_ImplDX11_Init(app.renderer.device(), app.renderer.context());

    createAppLogoSrv(app);
    if (!cliOtaManifestUrl.empty()) {
        lps::OtaCheckResult initOta = lps::checkForOtaUpdates(kCoatroomVersion, cliOtaManifestUrl);
        if (initOta.ok && initOta.updateAvailable) {
            app.uiState.otaUpdateAvailable = true;
            app.uiState.otaLatestVersion = initOta.latest.version;
            app.uiState.statusText = "Update available: Coatroom v" + initOta.latest.version;
        }
    }

    app.canvas.init(4096, lps::Color4u8{238, 240, 244, 255});
    app.uiState.canvasResolution = 4096;

    const bool directOpenMode = smokeTest || !cliScriptPath.empty() || mcpStdio ||
                                !cliObjPath.empty() || !cliTexturePath.empty() ||
                                (!cliScreenshotPath.empty() && !cliForceHome);
    app.uiState.showHomeScreen = cliForceHome || !directOpenMode;
    app.uiState.hasActiveDocument = !app.uiState.showHomeScreen && !smokeTest && cliScriptPath.empty() && !mcpStdio;

    if (!smokeTest && cliScriptPath.empty() && !mcpStdio) {
        app.uiState.recentProjects = loadRecentProjectsFile(app.recentJsonPath);
        for (auto& rec : app.uiState.recentProjects) {
            if (!rec.thumbPath.empty()) {
                rec.thumbTex = loadThumbPngToSrv(app, rec.thumbPath);
            }
        }
        std::error_code recEc;
        if (std::filesystem::exists(app.recoveryCrPath, recEc) &&
            std::filesystem::file_size(app.recoveryCrPath, recEc) > 8) {
            app.uiState.hasRecoveryBanner = true;
            app.uiState.recoveryInfoText = readRecoverySummary(app);
        }
        SetUnhandledExceptionFilter(coatroomCrashFilter);
    }

    lps::StudioCallbacks callbacks = makeCallbacks(app);

    bool windowShown = false;
    auto ensureWindowShown = [&]() {
        if (!windowShown) {
            renderFrame(app, callbacks);
            ShowWindow(app.hwnd, SW_SHOW);
            UpdateWindow(app.hwnd);
            windowShown = true;
        }
    };

    lps::AutomationHost autoHost{};
    autoHost.renderer = &app.renderer;
    autoHost.mesh = &app.mesh;
    autoHost.canvas = &app.canvas;
    autoHost.uiState = &app.uiState;
    autoHost.uvState = &app.uvState;
    autoHost.sceneParams = &app.sceneParams;
    autoHost.camYaw = &app.camYaw;
    autoHost.camPitch = &app.camPitch;
    autoHost.camDist = &app.camDist;
    autoHost.camTarget = &app.camTarget;
    autoHost.loadObj = [&app](const std::string& path, bool preserveCanvas) {
        return loadMeshFile(app, path, preserveCanvas);
    };
    autoHost.setCameraPreset = [&app](lps::CameraPreset p) {
        applyCameraPreset(app, p);
    };
    autoHost.syncCanvasGpu = [&app]() { syncCanvasGpu(app, true); };
    autoHost.syncMeshGpu = [&app]() { syncMeshGpu(app); };
    autoHost.syncSceneParams = [&app]() { syncSceneParams(app); };
    autoHost.captureUiScreenshot = [&](const std::string& outPath) {
        ensureWindowShown();
        std::vector<uint8_t> backbufRgba;
        int clientW = 0, clientH = 0;
        for (int f = 0; f < 4; ++f) {
            MSG msg{};
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (f == 3) {
                renderFrame(app, callbacks, &backbufRgba, &clientW, &clientH);
            } else {
                renderFrame(app, callbacks);
            }
        }
        DwmFlush();
        return captureWindowPng(app.hwnd, backbufRgba, clientW, clientH, outPath);
    };

    if (!smokeTest && cliScriptPath.empty() && !mcpStdio &&
        (!app.uiState.showHomeScreen || !cliObjPath.empty() || !cliTexturePath.empty())) {
        if (!cliObjPath.empty()) {
            loadMeshFile(app, cliObjPath);
        } else if (std::filesystem::exists(kPorscheObjPath)) {
            quickLoadPorsche(app, false);
        }
        if (!cliTexturePath.empty() && std::filesystem::exists(cliTexturePath)) {
            if (app.canvas.loadBaseTexture(cliTexturePath)) {
                app.uiState.canvasResolution = app.canvas.width;
                app.uvState.needsInitialFit = true;
                applyDefaultAoForMesh(app, !cliAoPath.empty(), false);
                syncCanvasGpu(app);
                app.uiState.hasActiveDocument = true;
                app.uiState.showHomeScreen = false;
                app.uiState.statusText = "Loaded CLI texture: " +
                                         std::filesystem::path(cliTexturePath).filename().string();
            }
        } else if (!cliAoPath.empty() && std::filesystem::exists(cliAoPath)) {
            if (app.canvas.loadAoOverlay(cliAoPath, true)) {
                syncCanvasGpu(app);
                app.uiState.statusText = "Loaded CLI AO map: " +
                                         std::filesystem::path(cliAoPath).filename().string();
            }
        }
    }

    int exitCode = 0;
    if (smokeTest) {
        ensureWindowShown();
        exitCode = runSmokeTest(app, callbacks, smokeOutDir);
    } else if (!cliScriptPath.empty()) {
        std::string scriptErr;
        if (!lps::executeAutomationScriptFile(autoHost, cliScriptPath, scriptErr)) {
            std::fprintf(stderr, "[SCRIPT FAIL] %s\n", scriptErr.c_str());
            exitCode = 1;
        }
    } else if (mcpStdio) {
        exitCode = lps::runMcpStdioServer(autoHost);
    } else if (!cliScreenshotPath.empty()) {
        ensureWindowShown();
        std::vector<uint8_t> backbufRgba;
        int clientW = 0, clientH = 0;
        for (int f = 0; f < 4; ++f) {
            MSG msg{};
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (f == 3) {
                renderFrame(app, callbacks, &backbufRgba, &clientW, &clientH);
            } else {
                renderFrame(app, callbacks);
            }
        }
        DwmFlush();
        if (!captureWindowPng(app.hwnd, backbufRgba, clientW, clientH, cliScreenshotPath)) {
            std::fprintf(stderr, "[SCREENSHOT FAIL] Failed saving %s\n", cliScreenshotPath.c_str());
            exitCode = 1;
        } else {
            std::printf("[SCREENSHOT OK] %s\n", cliScreenshotPath.c_str());
        }
    } else {
        ensureWindowShown();
        app.lastAutosaveTime = ImGui::GetTime();
        while (app.running) {
            MSG msg{};
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
                if (msg.message == WM_QUIT) app.running = false;
            }
            if (!app.running) break;

            if (app.pendingBeautyDialog) {
                app.pendingBeautyDialog = false;
                std::string defBeauty = meshStemOrDefault(app, "car") + "_beauty.png";
                std::string p = lps::dialogs::savePngDialog(
                    app.hwnd, defBeauty, L"F12 Render Beauty Shot (1920x1080 PNG)");
                if (!p.empty()) {
                    syncCanvasGpu(app);
                    syncMeshGpu(app);
                    syncSceneParams(app);
                    if (app.renderer.renderBeautyShotToPng(app.mesh, app.sceneParams, p, 1920, 1080)) {
                        app.uiState.statusText = "Rendered Beauty Shot: " +
                                                 std::filesystem::path(p).filename().string();
                    } else {
                        app.uiState.statusText = "Beauty render failed: " + app.renderer.lastError();
                    }
                }
            }

            if (app.uiState.hasActiveDocument && app.documentDirty) {
                double nowT = ImGui::GetTime();
                if (nowT - app.lastAutosaveTime >= 45.0) {
                    writeRecoverySnapshot(app, "Autosave");
                }
            }

            renderFrame(app, callbacks);
        }
        if (!app.uiState.hasRecoveryBanner) {
            clearRecoverySnapshot(app);
        }
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    releaseAppLogoSrv(app);
    releaseAllThumbSrvs(app);
    app.renderer.shutdown();
    DestroyWindow(app.hwnd);
    UnregisterClassW(wc.lpszClassName, hInst);
    if (hIconBig) DestroyIcon(hIconBig);
    if (hIconSmall) DestroyIcon(hIconSmall);
    if (darkBgBrush) DeleteObject(darkBgBrush);
    g_app = nullptr;
    return exitCode;
}
