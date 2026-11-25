#pragma once

#include "math/geom.hpp"
#include "mesh/obj_mesh.hpp"
#include "paint/canvas_engine.hpp"
#include "render/d3d11_renderer.hpp"
#include "ui/ui_types.hpp"

#include <functional>
#include <string>

namespace lps {

struct AutomationHost {
    D3D11Renderer* renderer = nullptr;
    ObjMesh* mesh = nullptr;
    CanvasEngine* canvas = nullptr;
    StudioUiState* uiState = nullptr;
    UvViewportState* uvState = nullptr;
    RenderSceneParams* sceneParams = nullptr;

    float* camYaw = nullptr;
    float* camPitch = nullptr;
    float* camDist = nullptr;
    Vec3* camTarget = nullptr;

    std::function<bool(const std::string& path, bool preserveCanvas)> loadObj;
    std::function<void(CameraPreset)> setCameraPreset;
    std::function<void()> syncCanvasGpu;
    std::function<void()> syncMeshGpu;
    std::function<void()> syncSceneParams;
    std::function<bool(const std::string& outPath)> captureUiScreenshot;
};

// Execute a Coatroom JSON batch script ({"commands": [...]}) and report any failure in outError.
bool executeAutomationScriptFile(AutomationHost& host, const std::string& jsonPath, std::string& outError);

// Run an MCP JSON-RPC 2.0 server over stdin/stdout (logs to stderr only). Exits 0 on EOF.
int runMcpStdioServer(AutomationHost& host);

} // namespace lps
