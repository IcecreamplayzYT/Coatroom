#pragma once

#include "math/geom.hpp"
#include "mesh/obj_mesh.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace lps {

enum class RenderShadingMode {
    Wireframe = 0,
    SolidClay = 1,
    MaterialPreview = 2,
    ShowroomRendered = 3,
};

struct StudioLight {
    Vec3 position{3.8f, 4.4f, -3.6f};
    Vec3 color{1.0f, 0.98f, 0.95f};
    float intensity = 2.8f;
    float softboxSize = 2.4f;
    bool enabled = true;
};

struct LineVertex3D {
    Vec3 pos;
    Vec4 color;
};

struct RenderSceneParams {
    // Camera
    Vec3 camEye{3.6f, 1.55f, -3.9f};
    Vec3 camTarget{0.0f, 0.55f, 0.0f};
    Vec3 camUp{0.0f, 1.0f, 0.0f};
    float fovYDeg = 38.0f;
    float nearZ = 0.05f;
    float farZ = 150.0f;
    bool orthographic = false;
    float orthoHalfHeight = 1.8f;

    // Shading & Material
    RenderShadingMode shadingMode = RenderShadingMode::ShowroomRendered;
    float metallic = 0.08f;
    float roughness = 0.24f;
    float clearcoat = 0.88f;
    float clearcoatRoughness = 0.06f;
    float exposure = 1.05f;
    float ambientStrength = 0.40f;

    // Studio & Overlays
    bool showStudioFloor = true;
    bool autoFloorFromBounds = true;
    float floorY = 0.0f;
    bool showSelectionOverlay = true;
    bool showWireframeOverlay = false;
    float symmetryPlaneX = 0.0f;

    Vec3 bgTopColor{0.12f, 0.13f, 0.16f};
    Vec3 bgBottomColor{0.055f, 0.06f, 0.075f};

    std::array<StudioLight, 3> lights = {{
        {Vec3{ 3.8f, 4.4f, -3.6f}, Vec3{1.00f, 0.98f, 0.95f}, 2.8f, 2.4f, true},
        {Vec3{-4.2f, 3.2f,  3.8f}, Vec3{0.78f, 0.88f, 1.00f}, 2.1f, 2.6f, true},
        {Vec3{-3.8f, 1.8f, -2.8f}, Vec3{1.00f, 0.92f, 0.84f}, 1.35f, 3.0f, true},
    }};

    Mat4 viewMatrix() const {
        return Mat4::lookAtLH(camEye, camTarget, camUp);
    }

    Mat4 projMatrix(float aspect) const {
        float safeAspect = (aspect > 1e-4f) ? aspect : 1.0f;
        if (orthographic) {
            return Mat4::orthoOffCenterLH(orthoHalfHeight * safeAspect, orthoHalfHeight, nearZ, farZ);
        }
        return Mat4::perspectiveFovLH(fovYDeg * kDeg2Rad, safeAspect, nearZ, farZ);
    }
};

class D3D11Renderer {
public:
    D3D11Renderer() = default;
    ~D3D11Renderer();

    D3D11Renderer(const D3D11Renderer&) = delete;
    D3D11Renderer& operator=(const D3D11Renderer&) = delete;

    // 1. Core Direct3D 11 lifecycle & ImGui accessors
    bool init(HWND hwnd, int initialW = 0, int initialH = 0);
    void shutdown();
    void onResize(int w, int h);
    void beginFrame(const float clearRgba[4]);
    void endFrame(bool vsync = true);

    ID3D11Device* device() const { return device_; }
    ID3D11DeviceContext* context() const { return context_; }
    ID3D11DeviceContext* deviceContext() const { return context_; }
    int backBufferWidth() const { return backBufferW_; }
    int backBufferHeight() const { return backBufferH_; }

    // 2. Dynamic offscreen 3D viewport & canvas texture upload/update
    void ensureViewportSize(int w, int h);
    ID3D11ShaderResourceView* viewportSRV() const { return vpResolvedSRV_; }
    int viewportWidth() const { return viewportW_; }
    int viewportHeight() const { return viewportH_; }

    ID3D11ShaderResourceView* createOrUpdateCanvasTexture(const uint8_t* rgba, int w, int h);
    ID3D11ShaderResourceView* updateCanvasTexture(const uint8_t* rgba, int w, int h) {
        return createOrUpdateCanvasTexture(rgba, w, h);
    }
    ID3D11ShaderResourceView* updateCanvasRegion(const uint8_t* fullRgba, int w, int h,
                                                   int x0, int y0, int x1, int y1,
                                                   bool genMips = true);
    ID3D11ShaderResourceView* canvasSRV() const { return canvasSRV_ ? canvasSRV_ : fallbackWhiteSRV_; }

    // 3. Mesh GPU upload, 4-mode PBR/Clay/Wireframe viewport render & 3D overlay lines
    void uploadMesh(const ObjMesh& mesh);
    void renderViewport(const ObjMesh& mesh, const RenderSceneParams& params, int vpW, int vpH);
    void renderViewport(const ObjMesh& mesh, const RenderSceneParams& params, int vpW, int vpH,
                        const std::vector<LineVertex3D>& depthLines,
                        const std::vector<LineVertex3D>& xrayLines);

    void drawLines3D(const std::vector<LineVertex3D>& lineListVertices, bool depthTest = true);
    void drawLines3D(const LineVertex3D* lineListVertices, size_t vertexCount,
                     const Mat4& viewProj, bool depthTest = true);

    // 4. F12 Studio Beauty Render -> PNG
    bool renderBeautyShotToPng(const ObjMesh& mesh, const RenderSceneParams& params,
                               const std::string& path, int outW, int outH);

    const std::string& lastError() const { return lastError_; }

private:
    bool createMainRenderTarget();
    void releaseMainRenderTarget();
    void bindMainRenderTarget();

    bool createViewportTargets(int w, int h);
    void releaseViewportTargets();

    bool createShadowMapResources();
    bool createShadersAndStates();
    bool createFallbackTexture();

    void renderShadowPass(const ObjMesh& mesh, const RenderSceneParams& params, Mat4& outLightViewProj);
    void renderSceneIntoTarget(ID3D11RenderTargetView* targetRTV, ID3D11DepthStencilView* targetDSV,
                               int targetW, int targetH,
                               const ObjMesh& mesh, const RenderSceneParams& params,
                               bool isBeautyShot);
    void drawLinesInternal(const LineVertex3D* vertices, size_t vertexCount,
                           const Mat4& viewProj, bool depthTest);
    void drawVisibleMeshGroups(const ObjMesh& mesh);
    void updateSceneCB(const Mat4& world, const Mat4& viewProj, const Mat4& invViewProj,
                       const Mat4& lightViewProj, const ObjMesh& mesh,
                       const RenderSceneParams& params, float effectiveFloorY,
                       bool isBeautyShot, const float wireColor[4]);

    // Core D3D11 objects
    HWND hwnd_ = nullptr;
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    IDXGISwapChain* swapChain_ = nullptr;
    ID3D11RenderTargetView* mainRTV_ = nullptr;
    int backBufferW_ = 0;
    int backBufferH_ = 0;

    // Offscreen 3D Viewport targets (MSAA + resolved SRV)
    ID3D11Texture2D* vpMsaaTex_ = nullptr;
    ID3D11RenderTargetView* vpMsaaRTV_ = nullptr;
    ID3D11Texture2D* vpDepthTex_ = nullptr;
    ID3D11DepthStencilView* vpDSV_ = nullptr;
    ID3D11Texture2D* vpResolvedTex_ = nullptr;
    ID3D11ShaderResourceView* vpResolvedSRV_ = nullptr;
    int viewportW_ = 0;
    int viewportH_ = 0;
    UINT msaaSamples_ = 1;

    // Shadow Map (2048x2048 from Key Studio Light 0)
    static constexpr int kShadowMapSize = 2048;
    ID3D11Texture2D* shadowTex_ = nullptr;
    ID3D11DepthStencilView* shadowDSV_ = nullptr;
    ID3D11ShaderResourceView* shadowSRV_ = nullptr;

    // Canvas Livery Texture + fallback
    ID3D11Texture2D* canvasTex_ = nullptr;
    ID3D11ShaderResourceView* canvasSRV_ = nullptr;
    int canvasW_ = 0;
    int canvasH_ = 0;
    ID3D11Texture2D* fallbackWhiteTex_ = nullptr;
    ID3D11ShaderResourceView* fallbackWhiteSRV_ = nullptr;

    // Dynamic Mesh & Line Vertex Buffers
    ID3D11Buffer* meshVB_ = nullptr;
    size_t meshVBCapacityVerts_ = 0;
    size_t uploadedVertexCount_ = 0;

    ID3D11Buffer* lineVB_ = nullptr;
    size_t lineVBCapacityVerts_ = 0;

    ID3D11Buffer* floorVB_ = nullptr;

    // Constant buffers
    ID3D11Buffer* sceneCB_ = nullptr;
    ID3D11Buffer* lineCB_ = nullptr;

    // Shaders & Input Layouts
    ID3D11VertexShader* bgVS_ = nullptr;
    ID3D11PixelShader* bgPS_ = nullptr;

    ID3D11VertexShader* floorVS_ = nullptr;
    ID3D11PixelShader* floorPS_ = nullptr;
    ID3D11InputLayout* floorLayout_ = nullptr;

    ID3D11VertexShader* meshVS_ = nullptr;
    ID3D11PixelShader* meshPS_ = nullptr;
    ID3D11PixelShader* meshSelWirePS_ = nullptr;
    ID3D11VertexShader* shadowVS_ = nullptr;
    ID3D11InputLayout* meshLayout_ = nullptr;

    ID3D11VertexShader* lineVS_ = nullptr;
    ID3D11PixelShader* linePS_ = nullptr;
    ID3D11InputLayout* lineLayout_ = nullptr;

    // Pipeline States
    ID3D11RasterizerState* rsSolidTwoSided_ = nullptr;
    ID3D11RasterizerState* rsWireframe_ = nullptr;
    ID3D11RasterizerState* rsWireOverlay_ = nullptr;
    ID3D11RasterizerState* rsShadow_ = nullptr;

    ID3D11DepthStencilState* dsDisabled_ = nullptr;
    ID3D11DepthStencilState* dsReadWrite_ = nullptr;
    ID3D11DepthStencilState* dsReadOnly_ = nullptr;

    ID3D11BlendState* bsOpaque_ = nullptr;
    ID3D11BlendState* bsAlphaBlend_ = nullptr;

    ID3D11SamplerState* sampAnisoWrap_ = nullptr;
    ID3D11SamplerState* sampShadowCmp_ = nullptr;

    Mat4 lastViewProj_ = Mat4::identity();
    bool initialized_ = false;
    std::string lastError_;
};

} // namespace lps
