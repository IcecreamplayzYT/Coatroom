#include "render/d3d11_renderer.hpp"
#include "stb/stb_image_write.h"

#include <d3dcompiler.h>
#include <dxgi.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace lps {

namespace {

template <typename T>
inline void safeRelease(T*& ptr) {
    if (ptr) {
        ptr->Release();
        ptr = nullptr;
    }
}

struct alignas(16) SceneCBData {
    Mat4 gWorld;
    Mat4 gViewProj;
    Mat4 gInvViewProj;
    Mat4 gLightViewProj;
    float gCamPos[4];
    float gBoundsMin[4];
    float gBoundsMax[4];
    float gLightPos[3][4];
    float gLightCol[3][4];
    float gMatParams[4];
    float gSceneFlags[4];
    float gBgTop[4];
    float gBgBottom[4];
    float gWireColor[4];
};

struct alignas(16) LineCBData {
    Mat4 gViewProj;
};

struct FloorVertex {
    float x, y, z;
};

const char* kSharedShadersHLSL = R"HLSL(
cbuffer SceneCB : register(b0) {
    row_major float4x4 gWorld;
    row_major float4x4 gViewProj;
    row_major float4x4 gInvViewProj;
    row_major float4x4 gLightViewProj;
    float4 gCamPos;       // xyz = eye, w = ortho
    float4 gBoundsMin;    // xyz = min, w = floorY
    float4 gBoundsMax;    // xyz = max, w = symmetryX
    float4 gLightPos[3];  // xyz = pos, w = intensity
    float4 gLightCol[3];  // rgb = color, w = softboxSize
    float4 gMatParams;    // x = metallic, y = roughness, z = clearcoat, w = clearcoatRoughness
    float4 gSceneFlags;   // x = shadingMode, y = exposure, z = ambientStrength, w = showSelOverlay
    float4 gBgTop;        // rgb = top color, w = showFloor
    float4 gBgBottom;     // rgb = bottom color, w = isBeautyShot
    float4 gWireColor;    // rgba
};

Texture2D gLiveryTex : register(t0);
Texture2D<float> gShadowMap : register(t1);
SamplerState gAnisoSamp : register(s0);
SamplerComparisonState gShadowCmpSamp : register(s1);

static const float PI = 3.14159265359;

float3 srgbToLinear(float3 c) {
    float3 lo = c / 12.92;
    float3 hi = pow(max((c + 0.055) / 1.055, 0.0), 2.4);
    return lerp(lo, hi, step(0.04045, c));
}

float3 linearToSrgb(float3 c) {
    c = saturate(c);
    float3 lo = c * 12.92;
    float3 hi = 1.055 * pow(max(c, 0.0), 1.0 / 2.4) - 0.055;
    return lerp(lo, hi, step(0.0031308, c));
}

float3 toneMapPBRNeutral(float3 color) {
    const float startCompression = 0.80 - 0.04;
    const float desaturation = 0.15;
    float x = min(color.r, min(color.g, color.b));
    float offset = (x < 0.08) ? (x - 6.25 * x * x) : 0.04;
    color -= offset;
    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) return max(color, 0.0);
    const float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / max(peak, 1e-5);
    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return max(lerp(color, newPeak.xxx, g), 0.0);
}

float3 toneMapACES(float3 x) {
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}

float3 studioToneMap(float3 hdr) {
    float3 n = toneMapPBRNeutral(hdr);
    float3 a = toneMapACES(hdr * 0.95);
    return saturate(lerp(n, a, 0.30));
}

float D_GGX(float NoH, float a) {
    float a2 = a * a;
    float d = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / max(PI * d * d, 1e-6);
}

float G_Smith(float NoV, float NoL, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) * 0.125;
    float gv = NoV / (NoV * (1.0 - k) + k);
    float gl = NoL / (NoL * (1.0 - k) + k);
    return gv * gl;
}

float3 F_Schlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

float sampleShadowPCF(float3 worldPos, float3 N) {
    float4 lp = mul(float4(worldPos + N * 0.004, 1.0), gLightViewProj);
    if (lp.w <= 1e-5) return 1.0;
    float3 proj = lp.xyz / lp.w;
    float2 uv = float2(proj.x * 0.5 + 0.5, -proj.y * 0.5 + 0.5);
    if (uv.x < 0.002 || uv.x > 0.998 || uv.y < 0.002 || uv.y > 0.998 || proj.z < 0.0 || proj.z > 1.0) {
        return 1.0;
    }
    float texel = 1.0 / 2048.0;
    float sum = 0.0;
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            sum += gShadowMap.SampleCmpLevelZero(gShadowCmpSamp, uv + float2(x, y) * texel * 1.6, proj.z - 0.0005);
        }
    }
    return sum / 9.0;
}

float computeUnderbodyAO(float3 worldPos) {
    float2 centerXZ = 0.5 * (gBoundsMin.xz + gBoundsMax.xz);
    float2 halfExt  = max(0.5 * (gBoundsMax.xz - gBoundsMin.xz), float2(0.3, 0.3));
    float2 d = abs(worldPos.xz - centerXZ) - halfExt * 0.88;
    float boxDist = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
    float footprint = 1.0 - smoothstep(-0.18, 0.48, boxDist);
    float heightFade = 1.0 - smoothstep(gBoundsMin.w, gBoundsMin.w + 0.65, worldPos.y);
    return 1.0 - 0.58 * footprint * heightFade;
}

float3 sampleStudioEnv(float3 dir, float rough) {
    float y = dir.y;
    // Dark studio walls + cool floor bounce + crisp overhead softbox strips
    float3 floorCol = float3(0.055, 0.060, 0.072);
    float3 wallCol  = float3(0.115, 0.125, 0.145);
    float3 domeCol  = float3(0.22, 0.24, 0.28);

    float3 env = lerp(wallCol, domeCol, smoothstep(0.0, 0.75, y));
    env = lerp(floorCol, env, smoothstep(-0.22, 0.08, y));

    float sharp = lerp(24.0, 3.0, rough);
    // Overhead primary softbox strip along Z
    float strip1 = smoothstep(0.58 - rough * 0.35, 0.92, y) * smoothstep(0.55 + rough * 0.35, 0.08, abs(dir.x));
    // Two angled side softbox ribbons for dramatic Porsche fender highlights
    float sideRibbonL = pow(saturate(dot(dir, normalize(float3(-0.78, 0.52, -0.32)))), sharp);
    float sideRibbonR = pow(saturate(dot(dir, normalize(float3( 0.78, 0.48,  0.35)))), sharp);
    // Horizon subtle studio rim band
    float horizonBand = exp(-abs(y - 0.12) * lerp(14.0, 4.0, rough)) * 0.32;

    env += float3(1.18, 1.16, 1.12) * strip1 * lerp(1.35, 0.35, rough);
    env += float3(0.95, 0.98, 1.05) * sideRibbonL * 0.85;
    env += float3(1.00, 0.95, 0.90) * sideRibbonR * 0.75;
    env += float3(0.32, 0.35, 0.40) * horizonBand;
    return env;
}

// ---------- Background Shader ----------
struct BGVSOut {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

BGVSOut bgVS(uint id : SV_VertexID) {
    BGVSOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.uv = uv;
    o.pos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 1.0, 1.0);
    return o;
}

float4 bgPS(BGVSOut i) : SV_Target {
    float3 topSrgb = gBgTop.rgb;
    float3 botSrgb = gBgBottom.rgb;
    float mode = gSceneFlags.x;

    float v = saturate(i.uv.y);
    float3 col = lerp(topSrgb, botSrgb, smoothstep(0.05, 0.95, v));

    if (mode > 2.5) {
        // Subtle radial studio cyclorama dome vignette
        float2 centered = i.uv - float2(0.5, 0.42);
        float radial = saturate(1.0 - dot(centered, centered) * 1.45);
        col += float3(0.032, 0.036, 0.044) * radial;
    }
    return float4(col, 1.0);
}

// ---------- Studio Floor Shader ----------
struct FloorVSIn {
    float3 pos : POSITION;
};

struct FloorVSOut {
    float4 pos      : SV_POSITION;
    float3 worldPos : TEXCOORD0;
};

FloorVSOut floorVS(FloorVSIn i) {
    FloorVSOut o;
    float2 centerXZ = 0.5 * (gBoundsMin.xz + gBoundsMax.xz);
    float3 wp = float3(centerXZ.x + i.pos.x, gBoundsMin.w, centerXZ.y + i.pos.z);
    o.worldPos = wp;
    o.pos = mul(float4(wp, 1.0), gViewProj);
    return o;
}

float gridFactor(float2 coord, float spacing, float thickness) {
    float2 p = coord / spacing;
    float2 deriv = max(fwidth(p), 1e-5);
    float2 grid = abs(frac(p - 0.5) - 0.5) / deriv;
    float lineDist = min(grid.x, grid.y);
    return 1.0 - saturate(lineDist / thickness);
}

float4 floorPS(FloorVSOut i) : SV_Target {
    float mode = gSceneFlags.x;
    float3 N = float3(0.0, 1.0, 0.0);
    float3 V = normalize(gCamPos.xyz - i.worldPos);
    float NoV = max(dot(N, V), 0.01);

    float2 centerXZ = 0.5 * (gBoundsMin.xz + gBoundsMax.xz);
    float distSq = dot(i.worldPos.xz - centerXZ, i.worldPos.xz - centerXZ);
    float fade = exp(-distSq / 180.0);

    float minorG = gridFactor(i.worldPos.xz, 0.25, 1.0);
    float majorG = gridFactor(i.worldPos.xz, 1.00, 1.25);

    // Symmetry X axis highlight on floor
    float dxSym = abs(i.worldPos.x - gBoundsMax.w) / max(fwidth(i.worldPos.x), 1e-5);
    float symLine = 1.0 - saturate(dxSym / 1.4);
    float dzAxis = abs(i.worldPos.z) / max(fwidth(i.worldPos.z), 1e-5);
    float zAxisLine = 1.0 - saturate(dzAxis / 1.2);

    float shadow = 1.0;
    float ao = computeUnderbodyAO(i.worldPos);
    if (mode > 1.5) {
        shadow = sampleShadowPCF(i.worldPos, N);
    }
    float totalShadow = min(ao, lerp(0.32, 1.0, shadow));

    if (mode < 2.5) {
        // Blender-style flat technical grid + subtle ground shadow disc
        float3 baseCol = float3(0.095, 0.102, 0.118) * totalShadow;
        baseCol = lerp(baseCol, float3(0.145, 0.155, 0.180), minorG * 0.24);
        baseCol = lerp(baseCol, float3(0.215, 0.230, 0.265), majorG * 0.55);
        baseCol = lerp(baseCol, float3(0.23, 0.51, 0.96), zAxisLine * 0.35);
        baseCol = lerp(baseCol, float3(0.85, 0.47, 0.05), symLine * 0.45);
        float alpha = saturate(max(max(majorG * 0.65, minorG * 0.28), (1.0 - totalShadow) * 0.85) + max(symLine, zAxisLine) * 0.5) * fade;
        return float4(baseCol, alpha);
    }

    // ShowroomRendered: sleek dark studio floor with softbox specular reflections + PCF shadow
    float3 floorLin = srgbToLinear(float3(0.045, 0.048, 0.056));
    float3 R = reflect(-V, N);
    float3 refl = sampleStudioEnv(R, 0.26) * 0.32;

    [unroll]
    for (int l = 0; l < 3; ++l) {
        float inten = gLightPos[l].w;
        if (inten <= 0.001) continue;
        float3 toL = gLightPos[l].xyz - i.worldPos;
        float dist = max(length(toL), 0.1);
        float3 L = toL / dist;
        float NoL = saturate(dot(N, L));
        float3 H = normalize(L + V);
        float NoH = saturate(dot(N, H));
        float rough = 0.28;
        float D = D_GGX(NoH, rough * rough);
        float atten = inten / (1.0 + 0.035 * dist * dist);
        float sh = (l == 0) ? totalShadow : ao;
        refl += srgbToLinear(gLightCol[l].rgb) * D * NoL * atten * 0.08 * sh;
        floorLin += srgbToLinear(gLightCol[l].rgb) * floorLin * NoL * atten * 0.45 * sh;
    }

    float fresnel = 0.025 + 0.38 * pow(1.0 - NoV, 5.0);
    float3 hdr = floorLin * totalShadow + refl * fresnel * lerp(0.25, 1.0, totalShadow);

    // Subtle precision bay grid lines in showroom (toned down cleanly for F12 beauty shot)
    float isBeauty = gBgBottom.w;
    float gridFade = (isBeauty > 0.5) ? 0.25 : 1.0;
    float3 gridCol = srgbToLinear(float3(0.18, 0.20, 0.24));
    hdr = lerp(hdr, gridCol * 0.35, minorG * 0.14 * gridFade);
    hdr = lerp(hdr, gridCol * 0.65, majorG * 0.28 * gridFade);
    if (isBeauty < 0.5) {
        hdr = lerp(hdr, srgbToLinear(float3(0.85, 0.47, 0.05)) * 0.35, symLine * 0.25);
    }

    float3 srgb = linearToSrgb(studioToneMap(hdr * gSceneFlags.y));
    float alpha = saturate(0.88 - 0.12 * totalShadow) * fade;
    return float4(srgb, alpha);
}

// ---------- Mesh Shaders ----------
struct MeshVSIn {
    float3 pos     : POSITION;
    float3 nrm     : NORMAL;
    float2 uv      : TEXCOORD0;
    float2 selMask : TEXCOORD1;
};

struct MeshVSOut {
    float4 pos      : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 worldNrm : TEXCOORD1;
    float2 uv       : TEXCOORD2;
    float2 selMask  : TEXCOORD3;
};

float4 shadowVS(MeshVSIn i) : SV_POSITION {
    float4 wp = mul(float4(i.pos, 1.0), gWorld);
    return mul(wp, gLightViewProj);
}

MeshVSOut meshVS(MeshVSIn i) {
    MeshVSOut o;
    float4 wp = mul(float4(i.pos, 1.0), gWorld);
    o.worldPos = wp.xyz;
    o.pos = mul(wp, gViewProj);
    o.worldNrm = normalize(mul(float4(i.nrm, 0.0), gWorld).xyz);
    o.uv = i.uv;
    o.selMask = i.selMask;
    return o;
}

float4 meshPS(MeshVSOut i, bool isFrontFace : SV_IsFrontFace) : SV_Target {
    float mode = gSceneFlags.x;
    float exposure = gSceneFlags.y;
    float ambientStrength = gSceneFlags.z;
    bool showSel = (gSceneFlags.w > 0.5);

    float3 N = normalize(i.worldNrm);
    float3 V = normalize(gCamPos.xyz - i.worldPos);
    if (!isFrontFace && dot(N, V) < 0.0) N = -N;
    float NoV = max(dot(N, V), 0.01);

    // Mode 0: Wireframe
    if (mode < 0.5) {
        float3 frontWire = float3(0.76, 0.80, 0.86);
        float3 backWire  = float3(0.30, 0.34, 0.40);
        float3 col = lerp(backWire, frontWire, saturate(0.35 + 0.65 * NoV));
        if (showSel && i.selMask.x > 0.5) {
            col = float3(0.961, 0.620, 0.043); // #F59E0B
        }
        return float4(col, 1.0);
    }

    // Mode 1: SolidClay (Blender studio MatCap + subtle cavity & specular)
    if (mode < 1.5) {
        float3 clayBase = float3(0.46, 0.47, 0.50); // neutral ceramic studio clay (linear)
        float3 keyDir   = normalize(float3(0.55, 0.72, -0.42));
        float3 fillDir  = normalize(float3(-0.65, 0.35, -0.35));
        float3 rimDir   = normalize(float3(-0.20, 0.60, 0.77));

        float ndl1 = dot(N, keyDir) * 0.5 + 0.5;
        float ndl2 = saturate(dot(N, fillDir));
        float ndl3 = saturate(dot(N, rimDir));

        // Screen-space subtle cavity/crease emphasis
        float3 dndx = ddx(N);
        float3 dndy = ddy(N);
        float curv = saturate(length(dndx) + length(dndy));
        float cavity = 1.0 - 0.24 * smoothstep(0.04, 0.32, curv);

        float3 H = normalize(keyDir + V);
        float spec = pow(saturate(dot(N, H)), 42.0) * 0.26;
        float fres = pow(1.0 - NoV, 4.0) * 0.14;

        float ao = lerp(0.70, 1.0, saturate(N.y * 0.5 + 0.5)) * cavity;
        float3 hdr = clayBase * (0.22 + 0.85 * ndl1 + 0.28 * ndl2) * ao
                   + float3(0.95, 0.96, 1.00) * (spec + fres * (0.4 + 0.6 * ndl3));

        float3 srgb = linearToSrgb(toneMapPBRNeutral(hdr));
        if (showSel && i.selMask.x > 0.5) {
            srgb = lerp(srgb, float3(0.961, 0.620, 0.043), 0.38);
        }
        return float4(srgb, 1.0);
    }

    // Mode 2 (MaterialPreview) & Mode 3 (ShowroomRendered): PBR Metallic/Roughness + Clearcoat
    float4 texSample = gLiveryTex.Sample(gAnisoSamp, i.uv);
    float3 albedo = srgbToLinear(texSample.rgb);

    float metallic = saturate(gMatParams.x);
    float rough    = clamp(gMatParams.y, 0.045, 1.0);
    float coat     = saturate(gMatParams.z);
    float coatRough = clamp(gMatParams.w, 0.03, 0.6);

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float3 R = reflect(-V, N);
    float shadow = sampleShadowPCF(i.worldPos, N);
    float bounceAO = lerp(0.55, 1.0, smoothstep(-0.35, 0.45, N.y));

    float3 Lo = float3(0.0, 0.0, 0.0);

    if (mode < 2.5) {
        // MaterialPreview: clean neutral studio lighting + procedural light-tent IBL + clearcoat
        float3 keyDir = normalize(float3(0.55, 0.72, -0.42));
        float3 fillDir = normalize(float3(-0.62, 0.42, 0.52));

        float3 dirs[2] = { keyDir, fillDir };
        float3 cols[2] = { float3(2.85, 2.80, 2.72), float3(1.45, 1.56, 1.75) };
        [unroll]
        for (int k = 0; k < 2; ++k) {
            float3 L = dirs[k];
            float NoL = saturate(dot(N, L));
            if (NoL <= 0.0) continue;
            float3 H = normalize(L + V);
            float NoH = saturate(dot(N, H));
            float HoV = saturate(dot(H, V));

            float3 F = F_Schlick(HoV, F0);
            float D = D_GGX(NoH, rough * rough);
            float G = G_Smith(NoV, NoL, rough);
            float3 spec = (D * G * F) / max(4.0 * NoV * NoL, 1e-4);
            float3 kD = (1.0 - F) * (1.0 - metallic);
            float3 diff = kD * albedo / PI;

            float Fc = (0.04 + 0.96 * pow(1.0 - NoV, 5.0)) * coat;
            float Dc = D_GGX(NoH, coatRough * coatRough);
            float Gc = G_Smith(NoV, NoL, coatRough);
            float specCoat = (Dc * Gc * Fc) / max(4.0 * NoV * NoL, 1e-4);

            float sh = (k == 0) ? lerp(0.45, 1.0, shadow) : 1.0;
            Lo += ((diff + spec) * (1.0 - Fc * 0.5) + specCoat) * cols[k] * NoL * sh;
        }
    } else {
        // ShowroomRendered: 3 configurable Studio Softbox Lights with Karis area-light specular + Clearcoat Fresnel
        [unroll]
        for (int l = 0; l < 3; ++l) {
            float inten = gLightPos[l].w;
            if (inten <= 0.001) continue;
            float3 lPos = gLightPos[l].xyz;
            float3 lCol = srgbToLinear(gLightCol[l].rgb);
            float areaRad = max(gLightCol[l].w, 0.05) * 0.45;

            float3 toLight = lPos - i.worldPos;
            float dist = max(length(toLight), 0.15);
            float3 L = toLight / dist;
            float NoL = saturate(dot(N, L));

            // Representative point on softbox sphere/disc for smooth elongated automotive highlights
            float3 centerToRay = dot(toLight, R) * R - toLight;
            float3 closestPt = toLight + centerToRay * saturate(areaRad / max(length(centerToRay), 1e-4));
            float3 Lspec = normalize(closestPt);
            float NoLs = saturate(dot(N, Lspec));

            float3 H = normalize(Lspec + V);
            float NoH = saturate(dot(N, H));
            float HoV = saturate(dot(H, V));

            float alphaBase = rough * rough;
            float alphaPrime = saturate(alphaBase + areaRad / (3.5 * dist));
            float normRatioBase = alphaBase / max(alphaPrime, 1e-4);
            float normBase = normRatioBase * normRatioBase;

            float3 F = F_Schlick(HoV, F0);
            float D = D_GGX(NoH, alphaPrime) * normBase;
            float G = G_Smith(NoV, NoLs, rough);
            float3 spec = (D * G * F) / max(4.0 * NoV * max(NoLs, 0.05), 1e-4);

            float3 kD = (1.0 - F) * (1.0 - metallic);
            float3 diff = kD * albedo / PI;

            // Clearcoat dual-lobe with Fresnel (energy-conserving Karis normalization)
            float Fc = (0.04 + 0.96 * pow(1.0 - NoV, 5.0)) * coat;
            float alphaCoat = coatRough * coatRough;
            float alphaCoatPrime = saturate(alphaCoat + areaRad / (4.5 * dist));
            float normRatioCoat = alphaCoat / max(alphaCoatPrime, 1e-4);
            float normCoat = normRatioCoat * normRatioCoat;
            float Dc = D_GGX(NoH, alphaCoatPrime) * normCoat;
            float Gc = G_Smith(NoV, NoLs, coatRough);
            float specCoat = (Dc * Gc * Fc) / max(4.0 * NoV * max(NoLs, 0.05), 1e-4);

            float atten = inten * 26.0 / (dist * dist + 12.0);
            float sh = (l == 0) ? lerp(0.20, 1.0, shadow) : 1.0;

            Lo += (diff * NoL * (1.0 - Fc * 0.35) + spec * NoLs * (1.0 - Fc * 0.4) + specCoat * NoLs)
                * lCol * atten * sh;
        }
    }

    // Studio Environment IBL (Diffuse hemisphere + Clearcoat/Base Specular reflection)
    float3 envDiff = sampleStudioEnv(N, 0.95) * (1.0 - metallic) * albedo * 1.45;
    float3 F_env = F_Schlick(NoV, F0);
    float3 envSpec = sampleStudioEnv(R, rough) * F_env * 1.15;
    float Fc_env = (0.04 + 0.96 * pow(1.0 - NoV, 5.0)) * coat;
    float3 envCoat = sampleStudioEnv(R, coatRough) * Fc_env * 1.15;

    float envScale = max(ambientStrength, 0.05) * bounceAO * lerp(0.48, 1.0, shadow);
    float3 ambient = (envDiff * (1.0 - Fc_env * 0.35) + envSpec * (1.0 - Fc_env * 0.45) + envCoat) * envScale;

    float3 hdr = (Lo + ambient) * exposure;
    float3 srgb = linearToSrgb(studioToneMap(hdr));

    if (showSel && i.selMask.x > 0.5) {
        float rim = 0.25 + 0.45 * pow(1.0 - NoV, 2.0);
        srgb = lerp(srgb, float3(0.961, 0.620, 0.043), saturate(0.28 + rim * 0.35));
    }
    return float4(srgb, 1.0);
}

float4 meshSelWirePS(MeshVSOut i) : SV_Target {
    if (gWireColor.a < 0.01) discard;
    // If gWireColor.a > 0.5, selection-only wireframe; otherwise full subtle wireframe overlay
    if (gSceneFlags.w > 0.5 && gWireColor.a > 0.5) {
        if (i.selMask.x <= 0.5) discard;
    }
    return gWireColor;
}
)HLSL";

const char* kLineShadersHLSL = R"HLSL(
cbuffer LineCB : register(b0) {
    row_major float4x4 gViewProj;
};

struct LineVSIn {
    float3 pos : POSITION;
    float4 col : COLOR;
};

struct LineVSOut {
    float4 pos : SV_POSITION;
    float4 col : COLOR;
};

LineVSOut lineVS(LineVSIn i) {
    LineVSOut o;
    o.pos = mul(float4(i.pos, 1.0), gViewProj);
    o.col = i.col;
    return o;
}

float4 linePS(LineVSOut i) : SV_Target {
    return i.col;
}
)HLSL";

bool compileShader(const char* src, const char* entry, const char* target, ID3DBlob** outBlob, std::string& errOut) {
    ID3DBlob* errBlob = nullptr;
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
    HRESULT hr = D3DCompile(src, std::strlen(src), "embedded_hlsl", nullptr, nullptr,
                            entry, target, flags, 0, outBlob, &errBlob);
    if (FAILED(hr)) {
        errOut = std::string("HLSL compile failed (") + entry + "): ";
        if (errBlob) {
            errOut.append(static_cast<const char*>(errBlob->GetBufferPointer()), errBlob->GetBufferSize());
            errBlob->Release();
        }
        return false;
    }
    if (errBlob) errBlob->Release();
    return true;
}

} // namespace

D3D11Renderer::~D3D11Renderer() {
    shutdown();
}

bool D3D11Renderer::init(HWND hwnd, int initialW, int initialH) {
    if (initialized_) return true;
    hwnd_ = hwnd;

    int w = initialW;
    int h = initialH;
    if ((w <= 0 || h <= 0) && hwnd_) {
        RECT rc{};
        if (GetClientRect(hwnd_, &rc)) {
            w = std::max<int>(rc.right - rc.left, 1);
            h = std::max<int>(rc.bottom - rc.top, 1);
        }
    }
    w = std::max(w, 16);
    h = std::max(h, 16);
    backBufferW_ = w;
    backBufferH_ = h;

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = static_cast<UINT>(w);
    sd.BufferDesc.Height = static_cast<UINT>(h);
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd_;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1 };
    UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createFlags,
        levels, 2, D3D11_SDK_VERSION, &sd, &swapChain_, &device_, &featureLevel, &context_);
    if (FAILED(hr)) {
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createFlags,
            levels, 2, D3D11_SDK_VERSION, &sd, &swapChain_, &device_, &featureLevel, &context_);
    }
    if (FAILED(hr) || !device_ || !context_ || !swapChain_) {
        lastError_ = "Failed to initialize D3D11 device and swap chain.";
        shutdown();
        return false;
    }

    // Disable Alt+Enter automatic fullscreen toggle
    IDXGIDevice* dxgiDevice = nullptr;
    if (SUCCEEDED(device_->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice)))) {
        IDXGIAdapter* adapter = nullptr;
        if (SUCCEEDED(dxgiDevice->GetAdapter(&adapter))) {
            IDXGIFactory* factory = nullptr;
            if (SUCCEEDED(adapter->GetParent(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&factory)))) {
                factory->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);
                factory->Release();
            }
            adapter->Release();
        }
        dxgiDevice->Release();
    }

    if (!createMainRenderTarget() ||
        !createShadersAndStates() ||
        !createShadowMapResources() ||
        !createFallbackTexture() ||
        !createViewportTargets(640, 480)) {
        shutdown();
        return false;
    }

    initialized_ = true;
    return true;
}

void D3D11Renderer::shutdown() {
    initialized_ = false;
    if (context_) {
        context_->ClearState();
        context_->Flush();
    }

    safeRelease(sampAnisoWrap_);
    safeRelease(sampShadowCmp_);
    safeRelease(bsOpaque_);
    safeRelease(bsAlphaBlend_);
    safeRelease(dsDisabled_);
    safeRelease(dsReadWrite_);
    safeRelease(dsReadOnly_);
    safeRelease(rsSolidTwoSided_);
    safeRelease(rsWireframe_);
    safeRelease(rsWireOverlay_);
    safeRelease(rsShadow_);

    safeRelease(bgVS_);
    safeRelease(bgPS_);
    safeRelease(floorVS_);
    safeRelease(floorPS_);
    safeRelease(floorLayout_);
    safeRelease(meshVS_);
    safeRelease(meshPS_);
    safeRelease(meshSelWirePS_);
    safeRelease(shadowVS_);
    safeRelease(meshLayout_);
    safeRelease(lineVS_);
    safeRelease(linePS_);
    safeRelease(lineLayout_);

    safeRelease(sceneCB_);
    safeRelease(lineCB_);
    safeRelease(floorVB_);
    safeRelease(lineVB_);
    safeRelease(meshVB_);
    lineVBCapacityVerts_ = 0;
    meshVBCapacityVerts_ = 0;
    uploadedVertexCount_ = 0;

    safeRelease(fallbackWhiteSRV_);
    safeRelease(fallbackWhiteTex_);
    safeRelease(canvasSRV_);
    safeRelease(canvasTex_);
    canvasW_ = 0;
    canvasH_ = 0;

    safeRelease(shadowSRV_);
    safeRelease(shadowDSV_);
    safeRelease(shadowTex_);

    releaseViewportTargets();
    releaseMainRenderTarget();

    safeRelease(swapChain_);
    safeRelease(context_);
    safeRelease(device_);
    hwnd_ = nullptr;
}

bool D3D11Renderer::createMainRenderTarget() {
    ID3D11Texture2D* backBuffer = nullptr;
    HRESULT hr = swapChain_->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer));
    if (FAILED(hr) || !backBuffer) {
        lastError_ = "Failed to acquire swap chain back buffer.";
        return false;
    }
    hr = device_->CreateRenderTargetView(backBuffer, nullptr, &mainRTV_);
    backBuffer->Release();
    if (FAILED(hr) || !mainRTV_) {
        lastError_ = "Failed to create main RenderTargetView.";
        return false;
    }
    return true;
}

void D3D11Renderer::releaseMainRenderTarget() {
    safeRelease(mainRTV_);
}

void D3D11Renderer::bindMainRenderTarget() {
    if (!context_ || !mainRTV_) return;
    context_->OMSetRenderTargets(1, &mainRTV_, nullptr);
    D3D11_VIEWPORT vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = static_cast<float>(std::max(backBufferW_, 1));
    vp.Height = static_cast<float>(std::max(backBufferH_, 1));
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    context_->RSSetViewports(1, &vp);
}

void D3D11Renderer::onResize(int w, int h) {
    if (!initialized_ || !swapChain_ || w <= 0 || h <= 0) return;
    if (w == backBufferW_ && h == backBufferH_) return;

    context_->OMSetRenderTargets(0, nullptr, nullptr);
    releaseMainRenderTarget();
    HRESULT hr = swapChain_->ResizeBuffers(0, static_cast<UINT>(w), static_cast<UINT>(h), DXGI_FORMAT_UNKNOWN, 0);
    if (SUCCEEDED(hr)) {
        backBufferW_ = w;
        backBufferH_ = h;
        createMainRenderTarget();
    }
}

void D3D11Renderer::beginFrame(const float clearRgba[4]) {
    if (!initialized_ || !mainRTV_) return;
    bindMainRenderTarget();
    static const float defaultClear[4] = {0.078f, 0.082f, 0.094f, 1.0f};
    context_->ClearRenderTargetView(mainRTV_, clearRgba ? clearRgba : defaultClear);
}

void D3D11Renderer::endFrame(bool vsync) {
    if (!initialized_ || !swapChain_) return;
    swapChain_->Present(vsync ? 1u : 0u, 0u);
}

bool D3D11Renderer::createViewportTargets(int w, int h) {
    releaseViewportTargets();

    w = std::clamp(w, 16, 8192);
    h = std::clamp(h, 16, 8192);

    UINT qColor = 0, qDepth = 0;
    device_->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, 4, &qColor);
    device_->CheckMultisampleQualityLevels(DXGI_FORMAT_D24_UNORM_S8_UINT, 4, &qDepth);
    msaaSamples_ = (qColor > 0 && qDepth > 0) ? 4u : 1u;

    // Resolved non-MSAA target (also used as SRV for ImGui::Image)
    D3D11_TEXTURE2D_DESC rDesc{};
    rDesc.Width = static_cast<UINT>(w);
    rDesc.Height = static_cast<UINT>(h);
    rDesc.MipLevels = 1;
    rDesc.ArraySize = 1;
    rDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rDesc.SampleDesc.Count = 1;
    rDesc.SampleDesc.Quality = 0;
    rDesc.Usage = D3D11_USAGE_DEFAULT;
    rDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

    if (FAILED(device_->CreateTexture2D(&rDesc, nullptr, &vpResolvedTex_)) || !vpResolvedTex_) {
        lastError_ = "Failed to create offscreen viewport texture.";
        return false;
    }
    if (FAILED(device_->CreateShaderResourceView(vpResolvedTex_, nullptr, &vpResolvedSRV_)) || !vpResolvedSRV_) {
        lastError_ = "Failed to create offscreen viewport SRV.";
        return false;
    }

    if (msaaSamples_ > 1) {
        D3D11_TEXTURE2D_DESC mDesc = rDesc;
        mDesc.SampleDesc.Count = msaaSamples_;
        mDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
        if (FAILED(device_->CreateTexture2D(&mDesc, nullptr, &vpMsaaTex_)) || !vpMsaaTex_) {
            lastError_ = "Failed to create MSAA viewport texture.";
            return false;
        }
        if (FAILED(device_->CreateRenderTargetView(vpMsaaTex_, nullptr, &vpMsaaRTV_)) || !vpMsaaRTV_) {
            lastError_ = "Failed to create MSAA viewport RTV.";
            return false;
        }
    } else {
        if (FAILED(device_->CreateRenderTargetView(vpResolvedTex_, nullptr, &vpMsaaRTV_)) || !vpMsaaRTV_) {
            lastError_ = "Failed to create viewport RTV.";
            return false;
        }
    }

    D3D11_TEXTURE2D_DESC dDesc{};
    dDesc.Width = static_cast<UINT>(w);
    dDesc.Height = static_cast<UINT>(h);
    dDesc.MipLevels = 1;
    dDesc.ArraySize = 1;
    dDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dDesc.SampleDesc.Count = msaaSamples_;
    dDesc.SampleDesc.Quality = 0;
    dDesc.Usage = D3D11_USAGE_DEFAULT;
    dDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    if (FAILED(device_->CreateTexture2D(&dDesc, nullptr, &vpDepthTex_)) || !vpDepthTex_) {
        lastError_ = "Failed to create offscreen depth texture.";
        return false;
    }
    if (FAILED(device_->CreateDepthStencilView(vpDepthTex_, nullptr, &vpDSV_)) || !vpDSV_) {
        lastError_ = "Failed to create offscreen DSV.";
        return false;
    }

    viewportW_ = w;
    viewportH_ = h;
    return true;
}

void D3D11Renderer::releaseViewportTargets() {
    safeRelease(vpDSV_);
    safeRelease(vpDepthTex_);
    safeRelease(vpMsaaRTV_);
    safeRelease(vpMsaaTex_);
    safeRelease(vpResolvedSRV_);
    safeRelease(vpResolvedTex_);
    viewportW_ = 0;
    viewportH_ = 0;
}

void D3D11Renderer::ensureViewportSize(int w, int h) {
    if (!initialized_) return;
    w = std::clamp(w, 16, 8192);
    h = std::clamp(h, 16, 8192);
    if (w != viewportW_ || h != viewportH_ || !vpResolvedSRV_) {
        createViewportTargets(w, h);
    }
}

ID3D11ShaderResourceView* D3D11Renderer::createOrUpdateCanvasTexture(const uint8_t* rgba, int w, int h) {
    if (!initialized_ || !rgba || w <= 0 || h <= 0) {
        return canvasSRV();
    }

    if (!canvasTex_ || canvasW_ != w || canvasH_ != h) {
        safeRelease(canvasSRV_);
        safeRelease(canvasTex_);

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = static_cast<UINT>(w);
        desc.Height = static_cast<UINT>(h);
        desc.MipLevels = 0;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

        if (FAILED(device_->CreateTexture2D(&desc, nullptr, &canvasTex_)) || !canvasTex_) {
            return fallbackWhiteSRV_;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = static_cast<UINT>(-1);
        if (FAILED(device_->CreateShaderResourceView(canvasTex_, &srvDesc, &canvasSRV_)) || !canvasSRV_) {
            safeRelease(canvasTex_);
            return fallbackWhiteSRV_;
        }
        canvasW_ = w;
        canvasH_ = h;
    }

    context_->UpdateSubresource(canvasTex_, 0, nullptr, rgba, static_cast<UINT>(w * 4), 0);
    context_->GenerateMips(canvasSRV_);
    return canvasSRV_;
}

ID3D11ShaderResourceView* D3D11Renderer::updateCanvasRegion(const uint8_t* fullRgba, int w, int h,
                                                              int x0, int y0, int x1, int y1,
                                                              bool genMips) {
    if (!initialized_ || !fullRgba || w <= 0 || h <= 0) {
        return canvasSRV();
    }
    if (!canvasTex_ || !canvasSRV_ || canvasW_ != w || canvasH_ != h) {
        return createOrUpdateCanvasTexture(fullRgba, w, h);
    }

    x0 = std::clamp(x0, 0, w - 1);
    y0 = std::clamp(y0, 0, h - 1);
    x1 = std::clamp(x1, 0, w - 1);
    y1 = std::clamp(y1, 0, h - 1);
    if (x0 > x1 || y0 > y1) {
        return canvasSRV_;
    }

    D3D11_BOX box{};
    box.left   = static_cast<UINT>(x0);
    box.top    = static_cast<UINT>(y0);
    box.front  = 0;
    box.right  = static_cast<UINT>(x1 + 1);
    box.bottom = static_cast<UINT>(y1 + 1);
    box.back   = 1;

    const uint8_t* regionPtr = fullRgba + (static_cast<size_t>(y0) * static_cast<size_t>(w) + static_cast<size_t>(x0)) * 4u;
    context_->UpdateSubresource(canvasTex_, 0, &box, regionPtr, static_cast<UINT>(w * 4), 0);
    if (genMips) {
        context_->GenerateMips(canvasSRV_);
    }
    return canvasSRV_;
}

bool D3D11Renderer::createFallbackTexture() {
    const uint8_t whitePx[4] = {225, 228, 232, 255};
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 1;
    desc.Height = 1;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData{};
    initData.pSysMem = whitePx;
    initData.SysMemPitch = 4;

    if (FAILED(device_->CreateTexture2D(&desc, &initData, &fallbackWhiteTex_)) || !fallbackWhiteTex_) {
        lastError_ = "Failed to create fallback texture.";
        return false;
    }
    if (FAILED(device_->CreateShaderResourceView(fallbackWhiteTex_, nullptr, &fallbackWhiteSRV_)) || !fallbackWhiteSRV_) {
        lastError_ = "Failed to create fallback SRV.";
        return false;
    }
    return true;
}

bool D3D11Renderer::createShadowMapResources() {
    D3D11_TEXTURE2D_DESC td{};
    td.Width = kShadowMapSize;
    td.Height = kShadowMapSize;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R32_TYPELESS;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

    if (FAILED(device_->CreateTexture2D(&td, nullptr, &shadowTex_)) || !shadowTex_) {
        lastError_ = "Failed to create shadow map texture.";
        return false;
    }

    D3D11_DEPTH_STENCIL_VIEW_DESC dsvd{};
    dsvd.Format = DXGI_FORMAT_D32_FLOAT;
    dsvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    if (FAILED(device_->CreateDepthStencilView(shadowTex_, &dsvd, &shadowDSV_)) || !shadowDSV_) {
        lastError_ = "Failed to create shadow map DSV.";
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
    srvd.Format = DXGI_FORMAT_R32_FLOAT;
    srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvd.Texture2D.MipLevels = 1;
    if (FAILED(device_->CreateShaderResourceView(shadowTex_, &srvd, &shadowSRV_)) || !shadowSRV_) {
        lastError_ = "Failed to create shadow map SRV.";
        return false;
    }

    return true;
}

bool D3D11Renderer::createShadersAndStates() {
    ID3DBlob* blob = nullptr;

    // 1. Background VS/PS
    if (!compileShader(kSharedShadersHLSL, "bgVS", "vs_5_0", &blob, lastError_)) return false;
    if (FAILED(device_->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &bgVS_))) {
        blob->Release(); return false;
    }
    blob->Release();

    if (!compileShader(kSharedShadersHLSL, "bgPS", "ps_5_0", &blob, lastError_)) return false;
    if (FAILED(device_->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &bgPS_))) {
        blob->Release(); return false;
    }
    blob->Release();

    // 2. Floor VS/PS + InputLayout
    if (!compileShader(kSharedShadersHLSL, "floorVS", "vs_5_0", &blob, lastError_)) return false;
    if (FAILED(device_->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &floorVS_))) {
        blob->Release(); return false;
    }
    D3D11_INPUT_ELEMENT_DESC floorDesc[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };
    if (FAILED(device_->CreateInputLayout(floorDesc, 1, blob->GetBufferPointer(), blob->GetBufferSize(), &floorLayout_))) {
        blob->Release(); return false;
    }
    blob->Release();

    if (!compileShader(kSharedShadersHLSL, "floorPS", "ps_5_0", &blob, lastError_)) return false;
    if (FAILED(device_->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &floorPS_))) {
        blob->Release(); return false;
    }
    blob->Release();

    // 3. Mesh VS/PS/ShadowVS/SelWirePS + InputLayout
    if (!compileShader(kSharedShadersHLSL, "meshVS", "vs_5_0", &blob, lastError_)) return false;
    if (FAILED(device_->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &meshVS_))) {
        blob->Release(); return false;
    }
    D3D11_INPUT_ELEMENT_DESC meshDesc[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,    0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if (FAILED(device_->CreateInputLayout(meshDesc, 4, blob->GetBufferPointer(), blob->GetBufferSize(), &meshLayout_))) {
        blob->Release(); return false;
    }
    blob->Release();

    if (!compileShader(kSharedShadersHLSL, "shadowVS", "vs_5_0", &blob, lastError_)) return false;
    if (FAILED(device_->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &shadowVS_))) {
        blob->Release(); return false;
    }
    blob->Release();

    if (!compileShader(kSharedShadersHLSL, "meshPS", "ps_5_0", &blob, lastError_)) return false;
    if (FAILED(device_->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &meshPS_))) {
        blob->Release(); return false;
    }
    blob->Release();

    if (!compileShader(kSharedShadersHLSL, "meshSelWirePS", "ps_5_0", &blob, lastError_)) return false;
    if (FAILED(device_->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &meshSelWirePS_))) {
        blob->Release(); return false;
    }
    blob->Release();

    // 4. 3D Line VS/PS + InputLayout
    if (!compileShader(kLineShadersHLSL, "lineVS", "vs_5_0", &blob, lastError_)) return false;
    if (FAILED(device_->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &lineVS_))) {
        blob->Release(); return false;
    }
    D3D11_INPUT_ELEMENT_DESC lineDesc[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if (FAILED(device_->CreateInputLayout(lineDesc, 2, blob->GetBufferPointer(), blob->GetBufferSize(), &lineLayout_))) {
        blob->Release(); return false;
    }
    blob->Release();

    if (!compileShader(kLineShadersHLSL, "linePS", "ps_5_0", &blob, lastError_)) return false;
    if (FAILED(device_->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &linePS_))) {
        blob->Release(); return false;
    }
    blob->Release();

    // 5. Constant Buffers & Floor Quad VB
    {
        D3D11_BUFFER_DESC cbDesc{};
        cbDesc.ByteWidth = sizeof(SceneCBData);
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device_->CreateBuffer(&cbDesc, nullptr, &sceneCB_))) return false;

        cbDesc.ByteWidth = sizeof(LineCBData);
        if (FAILED(device_->CreateBuffer(&cbDesc, nullptr, &lineCB_))) return false;
    }

    {
        const float R = 32.0f;
        const FloorVertex fv[6] = {
            {-R, 0.0f, -R}, {-R, 0.0f,  R}, { R, 0.0f,  R},
            {-R, 0.0f, -R}, { R, 0.0f,  R}, { R, 0.0f, -R},
        };
        D3D11_BUFFER_DESC vbDesc{};
        vbDesc.ByteWidth = sizeof(fv);
        vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA sd{};
        sd.pSysMem = fv;
        if (FAILED(device_->CreateBuffer(&vbDesc, &sd, &floorVB_))) return false;
    }

    // 6. Rasterizer, DepthStencil, Blend, and Sampler States
    {
        D3D11_RASTERIZER_DESC rs{};
        rs.FillMode = D3D11_FILL_SOLID;
        rs.CullMode = D3D11_CULL_NONE;
        rs.DepthClipEnable = TRUE;
        rs.MultisampleEnable = TRUE;
        if (FAILED(device_->CreateRasterizerState(&rs, &rsSolidTwoSided_))) return false;

        rs.FillMode = D3D11_FILL_WIREFRAME;
        rs.AntialiasedLineEnable = TRUE;
        if (FAILED(device_->CreateRasterizerState(&rs, &rsWireframe_))) return false;

        rs.DepthBias = -60;
        rs.SlopeScaledDepthBias = -1.2f;
        if (FAILED(device_->CreateRasterizerState(&rs, &rsWireOverlay_))) return false;

        D3D11_RASTERIZER_DESC rsSh{};
        rsSh.FillMode = D3D11_FILL_SOLID;
        rsSh.CullMode = D3D11_CULL_NONE;
        rsSh.DepthBias = 1600;
        rsSh.SlopeScaledDepthBias = 1.6f;
        rsSh.DepthClipEnable = TRUE;
        if (FAILED(device_->CreateRasterizerState(&rsSh, &rsShadow_))) return false;
    }

    {
        D3D11_DEPTH_STENCIL_DESC ds{};
        ds.DepthEnable = FALSE;
        ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        ds.DepthFunc = D3D11_COMPARISON_ALWAYS;
        if (FAILED(device_->CreateDepthStencilState(&ds, &dsDisabled_))) return false;

        ds.DepthEnable = TRUE;
        ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        ds.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        if (FAILED(device_->CreateDepthStencilState(&ds, &dsReadWrite_))) return false;

        ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        if (FAILED(device_->CreateDepthStencilState(&ds, &dsReadOnly_))) return false;
    }

    {
        D3D11_BLEND_DESC bd{};
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(device_->CreateBlendState(&bd, &bsOpaque_))) return false;

        bd.RenderTarget[0].BlendEnable = TRUE;
        bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        if (FAILED(device_->CreateBlendState(&bd, &bsAlphaBlend_))) return false;
    }

    {
        D3D11_SAMPLER_DESC s{};
        s.Filter = D3D11_FILTER_ANISOTROPIC;
        s.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        s.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        s.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        s.MaxAnisotropy = 8;
        s.ComparisonFunc = D3D11_COMPARISON_NEVER;
        s.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(device_->CreateSamplerState(&s, &sampAnisoWrap_))) return false;

        D3D11_SAMPLER_DESC sc{};
        sc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
        sc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
        sc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
        sc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
        sc.BorderColor[0] = sc.BorderColor[1] = sc.BorderColor[2] = sc.BorderColor[3] = 1.0f;
        sc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
        sc.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(device_->CreateSamplerState(&sc, &sampShadowCmp_))) return false;
    }

    return true;
}

void D3D11Renderer::uploadMesh(const ObjMesh& mesh) {
    if (!initialized_) return;
    if (mesh.gpuVertices.empty()) {
        uploadedVertexCount_ = 0;
        return;
    }

    const size_t count = mesh.gpuVertices.size();
    if (!meshVB_ || count > meshVBCapacityVerts_) {
        safeRelease(meshVB_);
        meshVBCapacityVerts_ = count + (count / 8) + 256;
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = static_cast<UINT>(meshVBCapacityVerts_ * sizeof(VertexGPU));
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device_->CreateBuffer(&desc, nullptr, &meshVB_)) || !meshVB_) {
            meshVBCapacityVerts_ = 0;
            uploadedVertexCount_ = 0;
            return;
        }
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(context_->Map(meshVB_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        std::memcpy(mapped.pData, mesh.gpuVertices.data(), count * sizeof(VertexGPU));
        context_->Unmap(meshVB_, 0);
        uploadedVertexCount_ = count;
    }
}

void D3D11Renderer::drawVisibleMeshGroups(const ObjMesh& mesh) {
    if (!meshVB_ || uploadedVertexCount_ == 0) return;

    if (mesh.groups.empty()) {
        context_->Draw(static_cast<UINT>(uploadedVertexCount_), 0);
        return;
    }

    for (const SubGroup& grp : mesh.groups) {
        if (!grp.visible || grp.triCount <= 0) continue;
        UINT vStart = static_cast<UINT>(grp.firstTri) * 3u;
        UINT vCount = static_cast<UINT>(grp.triCount) * 3u;
        if (vStart + vCount <= uploadedVertexCount_) {
            context_->Draw(vCount, vStart);
        }
    }
}

void D3D11Renderer::updateSceneCB(const Mat4& world, const Mat4& viewProj, const Mat4& invViewProj,
                                  const Mat4& lightViewProj, const ObjMesh& mesh,
                                  const RenderSceneParams& params, float effectiveFloorY,
                                  bool isBeautyShot, const float wireColor[4]) {
    SceneCBData cb{};
    cb.gWorld = world;
    cb.gViewProj = viewProj;
    cb.gInvViewProj = invViewProj;
    cb.gLightViewProj = lightViewProj;

    cb.gCamPos[0] = params.camEye.x;
    cb.gCamPos[1] = params.camEye.y;
    cb.gCamPos[2] = params.camEye.z;
    cb.gCamPos[3] = params.orthographic ? 1.0f : 0.0f;

    Vec3 bmin = mesh.gpuVertices.empty() ? Vec3{-1.0f, 0.0f, -2.0f} : mesh.bounds.min;
    Vec3 bmax = mesh.gpuVertices.empty() ? Vec3{ 1.0f, 1.3f,  2.0f} : mesh.bounds.max;
    cb.gBoundsMin[0] = bmin.x; cb.gBoundsMin[1] = bmin.y; cb.gBoundsMin[2] = bmin.z; cb.gBoundsMin[3] = effectiveFloorY;
    cb.gBoundsMax[0] = bmax.x; cb.gBoundsMax[1] = bmax.y; cb.gBoundsMax[2] = bmax.z; cb.gBoundsMax[3] = params.symmetryPlaneX;

    for (int i = 0; i < 3; ++i) {
        const StudioLight& l = params.lights[i];
        cb.gLightPos[i][0] = l.position.x;
        cb.gLightPos[i][1] = l.position.y;
        cb.gLightPos[i][2] = l.position.z;
        cb.gLightPos[i][3] = l.enabled ? l.intensity : 0.0f;

        cb.gLightCol[i][0] = l.color.x;
        cb.gLightCol[i][1] = l.color.y;
        cb.gLightCol[i][2] = l.color.z;
        cb.gLightCol[i][3] = l.softboxSize;
    }

    cb.gMatParams[0] = params.metallic;
    cb.gMatParams[1] = params.roughness;
    cb.gMatParams[2] = params.clearcoat;
    cb.gMatParams[3] = params.clearcoatRoughness;

    cb.gSceneFlags[0] = static_cast<float>(static_cast<int>(params.shadingMode));
    cb.gSceneFlags[1] = params.exposure;
    cb.gSceneFlags[2] = params.ambientStrength;
    cb.gSceneFlags[3] = (params.showSelectionOverlay && !isBeautyShot) ? 1.0f : 0.0f;

    cb.gBgTop[0] = params.bgTopColor.x;
    cb.gBgTop[1] = params.bgTopColor.y;
    cb.gBgTop[2] = params.bgTopColor.z;
    cb.gBgTop[3] = params.showStudioFloor ? 1.0f : 0.0f;

    cb.gBgBottom[0] = params.bgBottomColor.x;
    cb.gBgBottom[1] = params.bgBottomColor.y;
    cb.gBgBottom[2] = params.bgBottomColor.z;
    cb.gBgBottom[3] = isBeautyShot ? 1.0f : 0.0f;

    cb.gWireColor[0] = wireColor[0];
    cb.gWireColor[1] = wireColor[1];
    cb.gWireColor[2] = wireColor[2];
    cb.gWireColor[3] = wireColor[3];

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(context_->Map(sceneCB_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        std::memcpy(mapped.pData, &cb, sizeof(cb));
        context_->Unmap(sceneCB_, 0);
    }
}

void D3D11Renderer::renderShadowPass(const ObjMesh& mesh, const RenderSceneParams& params, Mat4& outLightViewProj) {
    Vec3 center = mesh.gpuVertices.empty() ? Vec3{0.0f, 0.5f, 0.0f} : mesh.bounds.center();
    Vec3 ext    = mesh.gpuVertices.empty() ? Vec3{2.0f, 1.2f, 4.6f} : mesh.bounds.extent();
    float radius = std::max(0.5f * length(ext), 1.5f);

    Vec3 lPos = params.lights[0].enabled ? params.lights[0].position : Vec3{3.8f, 4.4f, -3.6f};
    Vec3 lDir = lPos - center;
    if (lengthSq(lDir) < 1e-4f) lDir = Vec3{0.5f, 0.8f, -0.5f};
    lDir = normalized(lDir);

    Vec3 eye = center + lDir * (radius * 3.0f);
    Vec3 up = (std::fabs(lDir.y) > 0.96f) ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{0.0f, 1.0f, 0.0f};
    Mat4 lightView = Mat4::lookAtLH(eye, center, up);
    float orthoHalf = radius * 1.35f;
    Mat4 lightProj = Mat4::orthoOffCenterLH(orthoHalf, orthoHalf, 0.1f, radius * 6.5f);
    outLightViewProj = lightView * lightProj;

    // Ensure shadow SRV is unbound from PS before binding DSV
    ID3D11ShaderResourceView* nullSRVs[2] = {nullptr, nullptr};
    context_->PSSetShaderResources(0, 2, nullSRVs);

    context_->ClearDepthStencilView(shadowDSV_, D3D11_CLEAR_DEPTH, 1.0f, 0);
    if (mesh.gpuVertices.empty() || uploadedVertexCount_ == 0) return;

    context_->OMSetRenderTargets(0, nullptr, shadowDSV_);
    D3D11_VIEWPORT vp{0.0f, 0.0f, static_cast<float>(kShadowMapSize), static_cast<float>(kShadowMapSize), 0.0f, 1.0f};
    context_->RSSetViewports(1, &vp);

    float dummyWire[4] = {0, 0, 0, 0};
    updateSceneCB(Mat4::identity(), outLightViewProj, Mat4::identity(), outLightViewProj,
                  mesh, params, params.floorY, false, dummyWire);

    UINT stride = sizeof(VertexGPU);
    UINT offset = 0;
    context_->IASetInputLayout(meshLayout_);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->IASetVertexBuffers(0, 1, &meshVB_, &stride, &offset);

    context_->RSSetState(rsShadow_);
    context_->OMSetDepthStencilState(dsReadWrite_, 0);
    context_->OMSetBlendState(bsOpaque_, nullptr, 0xFFFFFFFF);
    context_->VSSetShader(shadowVS_, nullptr, 0);
    context_->VSSetConstantBuffers(0, 1, &sceneCB_);
    context_->PSSetShader(nullptr, nullptr, 0);

    drawVisibleMeshGroups(mesh);

    context_->OMSetRenderTargets(0, nullptr, nullptr);
}

void D3D11Renderer::renderSceneIntoTarget(ID3D11RenderTargetView* targetRTV, ID3D11DepthStencilView* targetDSV,
                                          int targetW, int targetH,
                                          const ObjMesh& mesh, const RenderSceneParams& params,
                                          bool isBeautyShot) {
    if (!mesh.gpuVertices.empty() && (!meshVB_ || uploadedVertexCount_ != mesh.gpuVertices.size())) {
        uploadMesh(mesh);
    }

    float aspect = static_cast<float>(targetW) / static_cast<float>(std::max(targetH, 1));
    Mat4 world = Mat4::identity();
    Mat4 view = params.viewMatrix();
    Mat4 proj = params.projMatrix(aspect);
    Mat4 viewProj = view * proj;
    Mat4 invViewProj = inverseMat4(viewProj);
    lastViewProj_ = viewProj;

    float effectiveFloorY = params.floorY;
    if (params.autoFloorFromBounds && !mesh.gpuVertices.empty()) {
        effectiveFloorY = mesh.bounds.min.y - 0.002f;
    }

    Mat4 lightViewProj = Mat4::identity();
    renderShadowPass(mesh, params, lightViewProj);

    // Bind render target + depth buffer
    context_->OMSetRenderTargets(1, &targetRTV, targetDSV);
    D3D11_VIEWPORT vp{0.0f, 0.0f, static_cast<float>(targetW), static_cast<float>(targetH), 0.0f, 1.0f};
    context_->RSSetViewports(1, &vp);

    const float clearCol[4] = {params.bgBottomColor.x, params.bgBottomColor.y, params.bgBottomColor.z, 1.0f};
    context_->ClearRenderTargetView(targetRTV, clearCol);
    context_->ClearDepthStencilView(targetDSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    const float amberWire[4] = {0.961f, 0.620f, 0.043f, 0.90f};
    updateSceneCB(world, viewProj, invViewProj, lightViewProj, mesh, params, effectiveFloorY, isBeautyShot, amberWire);

    ID3D11ShaderResourceView* srvs[2] = {canvasSRV(), shadowSRV_};
    ID3D11SamplerState* samps[2] = {sampAnisoWrap_, sampShadowCmp_};

    context_->VSSetConstantBuffers(0, 1, &sceneCB_);
    context_->PSSetConstantBuffers(0, 1, &sceneCB_);
    context_->PSSetShaderResources(0, 2, srvs);
    context_->PSSetSamplers(0, 2, samps);

    // 1. Studio Backdrop Gradient
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->RSSetState(rsSolidTwoSided_);
    context_->OMSetDepthStencilState(dsDisabled_, 0);
    context_->OMSetBlendState(bsOpaque_, nullptr, 0xFFFFFFFF);
    context_->VSSetShader(bgVS_, nullptr, 0);
    context_->PSSetShader(bgPS_, nullptr, 0);
    context_->Draw(3, 0);

    // 2. Studio Floor Grid & Shadow/Reflection Plane
    if (params.showStudioFloor) {
        UINT fStride = sizeof(FloorVertex);
        UINT fOffset = 0;
        context_->IASetInputLayout(floorLayout_);
        context_->IASetVertexBuffers(0, 1, &floorVB_, &fStride, &fOffset);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->RSSetState(rsSolidTwoSided_);
        context_->OMSetDepthStencilState(dsReadOnly_, 0);
        context_->OMSetBlendState(bsAlphaBlend_, nullptr, 0xFFFFFFFF);
        context_->VSSetShader(floorVS_, nullptr, 0);
        context_->PSSetShader(floorPS_, nullptr, 0);
        context_->Draw(6, 0);
    }

    // 3. Draw Visible Mesh Sub-Objects
    if (!mesh.gpuVertices.empty() && uploadedVertexCount_ > 0 && meshVB_) {
        UINT mStride = sizeof(VertexGPU);
        UINT mOffset = 0;
        context_->IASetInputLayout(meshLayout_);
        context_->IASetVertexBuffers(0, 1, &meshVB_, &mStride, &mOffset);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->OMSetDepthStencilState(dsReadWrite_, 0);
        context_->OMSetBlendState(bsOpaque_, nullptr, 0xFFFFFFFF);

        context_->RSSetState(params.shadingMode == RenderShadingMode::Wireframe ? rsWireframe_ : rsSolidTwoSided_);
        context_->VSSetShader(meshVS_, nullptr, 0);
        context_->PSSetShader(meshPS_, nullptr, 0);
        drawVisibleMeshGroups(mesh);

        // Optional subtle mesh wireframe overlay
        if (params.showWireframeOverlay && !isBeautyShot && params.shadingMode != RenderShadingMode::Wireframe) {
            const float subtleWire[4] = {0.08f, 0.09f, 0.11f, 0.30f};
            updateSceneCB(world, viewProj, invViewProj, lightViewProj, mesh, params, effectiveFloorY, isBeautyShot, subtleWire);
            context_->RSSetState(rsWireOverlay_);
            context_->OMSetDepthStencilState(dsReadOnly_, 0);
            context_->OMSetBlendState(bsAlphaBlend_, nullptr, 0xFFFFFFFF);
            context_->PSSetShader(meshSelWirePS_, nullptr, 0);
            drawVisibleMeshGroups(mesh);
        }

        // Amber #F59E0B wireframe highlight on selected vertices/faces (gv.sel > 0.5)
        if (params.showSelectionOverlay && !isBeautyShot && params.shadingMode != RenderShadingMode::Wireframe) {
            updateSceneCB(world, viewProj, invViewProj, lightViewProj, mesh, params, effectiveFloorY, isBeautyShot, amberWire);
            context_->RSSetState(rsWireOverlay_);
            context_->OMSetDepthStencilState(dsReadOnly_, 0);
            context_->OMSetBlendState(bsAlphaBlend_, nullptr, 0xFFFFFFFF);
            context_->PSSetShader(meshSelWirePS_, nullptr, 0);
            drawVisibleMeshGroups(mesh);
        }
    }

    // Unbind SRVs so shadow map isn't held bound
    ID3D11ShaderResourceView* nullSRVs[2] = {nullptr, nullptr};
    context_->PSSetShaderResources(0, 2, nullSRVs);
}

void D3D11Renderer::drawLinesInternal(const LineVertex3D* vertices, size_t vertexCount,
                                      const Mat4& viewProj, bool depthTest) {
    if (!initialized_ || !vertices || vertexCount < 2) return;
    size_t drawCount = (vertexCount / 2) * 2;
    if (drawCount == 0) return;

    if (!lineVB_ || drawCount > lineVBCapacityVerts_) {
        safeRelease(lineVB_);
        lineVBCapacityVerts_ = drawCount + 1024;
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = static_cast<UINT>(lineVBCapacityVerts_ * sizeof(LineVertex3D));
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device_->CreateBuffer(&desc, nullptr, &lineVB_)) || !lineVB_) {
            lineVBCapacityVerts_ = 0;
            return;
        }
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(context_->Map(lineVB_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        std::memcpy(mapped.pData, vertices, drawCount * sizeof(LineVertex3D));
        context_->Unmap(lineVB_, 0);
    }

    LineCBData lcb{viewProj};
    if (SUCCEEDED(context_->Map(lineCB_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        std::memcpy(mapped.pData, &lcb, sizeof(lcb));
        context_->Unmap(lineCB_, 0);
    }

    UINT stride = sizeof(LineVertex3D);
    UINT offset = 0;
    context_->IASetInputLayout(lineLayout_);
    context_->IASetVertexBuffers(0, 1, &lineVB_, &stride, &offset);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

    context_->RSSetState(rsSolidTwoSided_);
    context_->OMSetDepthStencilState(depthTest ? dsReadOnly_ : dsDisabled_, 0);
    context_->OMSetBlendState(bsAlphaBlend_, nullptr, 0xFFFFFFFF);
    context_->VSSetShader(lineVS_, nullptr, 0);
    context_->VSSetConstantBuffers(0, 1, &lineCB_);
    context_->PSSetShader(linePS_, nullptr, 0);

    context_->Draw(static_cast<UINT>(drawCount), 0);
}

void D3D11Renderer::renderViewport(const ObjMesh& mesh, const RenderSceneParams& params, int vpW, int vpH) {
    if (!initialized_) return;
    ensureViewportSize(vpW, vpH);
    if (!vpMsaaRTV_ || !vpDSV_) return;

    renderSceneIntoTarget(vpMsaaRTV_, vpDSV_, viewportW_, viewportH_, mesh, params, false);

    if (msaaSamples_ > 1 && vpMsaaTex_ && vpResolvedTex_) {
        context_->ResolveSubresource(vpResolvedTex_, 0, vpMsaaTex_, 0, DXGI_FORMAT_R8G8B8A8_UNORM);
    }
    bindMainRenderTarget();
}

void D3D11Renderer::renderViewport(const ObjMesh& mesh, const RenderSceneParams& params, int vpW, int vpH,
                                   const std::vector<LineVertex3D>& depthLines,
                                   const std::vector<LineVertex3D>& xrayLines) {
    if (!initialized_) return;
    ensureViewportSize(vpW, vpH);
    if (!vpMsaaRTV_ || !vpDSV_) return;

    renderSceneIntoTarget(vpMsaaRTV_, vpDSV_, viewportW_, viewportH_, mesh, params, false);

    if (!depthLines.empty()) {
        drawLinesInternal(depthLines.data(), depthLines.size(), lastViewProj_, true);
    }
    if (!xrayLines.empty()) {
        drawLinesInternal(xrayLines.data(), xrayLines.size(), lastViewProj_, false);
    }

    if (msaaSamples_ > 1 && vpMsaaTex_ && vpResolvedTex_) {
        context_->ResolveSubresource(vpResolvedTex_, 0, vpMsaaTex_, 0, DXGI_FORMAT_R8G8B8A8_UNORM);
    }
    bindMainRenderTarget();
}

void D3D11Renderer::drawLines3D(const std::vector<LineVertex3D>& lineListVertices, bool depthTest) {
    if (lineListVertices.empty()) return;
    drawLines3D(lineListVertices.data(), lineListVertices.size(), lastViewProj_, depthTest);
}

void D3D11Renderer::drawLines3D(const LineVertex3D* lineListVertices, size_t vertexCount,
                                const Mat4& viewProj, bool depthTest) {
    if (!initialized_ || !lineListVertices || vertexCount < 2 || !vpMsaaRTV_ || !vpDSV_) return;

    context_->OMSetRenderTargets(1, &vpMsaaRTV_, vpDSV_);
    D3D11_VIEWPORT vp{0.0f, 0.0f, static_cast<float>(viewportW_), static_cast<float>(viewportH_), 0.0f, 1.0f};
    context_->RSSetViewports(1, &vp);

    drawLinesInternal(lineListVertices, vertexCount, viewProj, depthTest);

    if (msaaSamples_ > 1 && vpMsaaTex_ && vpResolvedTex_) {
        context_->ResolveSubresource(vpResolvedTex_, 0, vpMsaaTex_, 0, DXGI_FORMAT_R8G8B8A8_UNORM);
    }
    bindMainRenderTarget();
}

bool D3D11Renderer::renderBeautyShotToPng(const ObjMesh& mesh, const RenderSceneParams& params,
                                          const std::string& path, int outW, int outH) {
    if (!initialized_ || outW <= 0 || outH <= 0 || path.empty()) {
        lastError_ = "Invalid parameters for beauty shot render.";
        return false;
    }

    outW = std::clamp(outW, 16, 8192);
    outH = std::clamp(outH, 16, 8192);

    UINT qColor = 0, qDepth = 0;
    device_->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, 4, &qColor);
    device_->CheckMultisampleQualityLevels(DXGI_FORMAT_D24_UNORM_S8_UINT, 4, &qDepth);
    UINT samples = (qColor > 0 && qDepth > 0) ? 4u : 1u;

    ID3D11Texture2D* colorTex = nullptr;
    ID3D11Texture2D* msaaTex = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11Texture2D* depthTex = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    ID3D11Texture2D* stagingTex = nullptr;

    auto cleanup = [&]() {
        safeRelease(stagingTex);
        safeRelease(dsv);
        safeRelease(depthTex);
        safeRelease(rtv);
        safeRelease(msaaTex);
        safeRelease(colorTex);
        bindMainRenderTarget();
    };

    D3D11_TEXTURE2D_DESC cDesc{};
    cDesc.Width = static_cast<UINT>(outW);
    cDesc.Height = static_cast<UINT>(outH);
    cDesc.MipLevels = 1;
    cDesc.ArraySize = 1;
    cDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    cDesc.SampleDesc.Count = 1;
    cDesc.Usage = D3D11_USAGE_DEFAULT;
    cDesc.BindFlags = D3D11_BIND_RENDER_TARGET;

    if (FAILED(device_->CreateTexture2D(&cDesc, nullptr, &colorTex)) || !colorTex) {
        lastError_ = "Failed to create beauty shot resolve texture.";
        cleanup();
        return false;
    }

    if (samples > 1) {
        D3D11_TEXTURE2D_DESC mDesc = cDesc;
        mDesc.SampleDesc.Count = samples;
        if (FAILED(device_->CreateTexture2D(&mDesc, nullptr, &msaaTex)) || !msaaTex) {
            lastError_ = "Failed to create beauty shot MSAA texture.";
            cleanup();
            return false;
        }
        if (FAILED(device_->CreateRenderTargetView(msaaTex, nullptr, &rtv)) || !rtv) {
            cleanup();
            return false;
        }
    } else {
        if (FAILED(device_->CreateRenderTargetView(colorTex, nullptr, &rtv)) || !rtv) {
            cleanup();
            return false;
        }
    }

    D3D11_TEXTURE2D_DESC dDesc{};
    dDesc.Width = static_cast<UINT>(outW);
    dDesc.Height = static_cast<UINT>(outH);
    dDesc.MipLevels = 1;
    dDesc.ArraySize = 1;
    dDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dDesc.SampleDesc.Count = samples;
    dDesc.Usage = D3D11_USAGE_DEFAULT;
    dDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(device_->CreateTexture2D(&dDesc, nullptr, &depthTex)) || !depthTex ||
        FAILED(device_->CreateDepthStencilView(depthTex, nullptr, &dsv)) || !dsv) {
        lastError_ = "Failed to create beauty shot depth buffer.";
        cleanup();
        return false;
    }

    D3D11_TEXTURE2D_DESC sDesc = cDesc;
    sDesc.Usage = D3D11_USAGE_STAGING;
    sDesc.BindFlags = 0;
    sDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(device_->CreateTexture2D(&sDesc, nullptr, &stagingTex)) || !stagingTex) {
        lastError_ = "Failed to create beauty shot staging texture.";
        cleanup();
        return false;
    }

    // Force crisp studio Showroom mode without editor overlays for F12 Beauty Shot
    RenderSceneParams beautyParams = params;
    beautyParams.shadingMode = RenderShadingMode::ShowroomRendered;
    beautyParams.showSelectionOverlay = false;
    beautyParams.showWireframeOverlay = false;
    beautyParams.showStudioFloor = true;

    renderSceneIntoTarget(rtv, dsv, outW, outH, mesh, beautyParams, true);

    if (samples > 1 && msaaTex) {
        context_->ResolveSubresource(colorTex, 0, msaaTex, 0, DXGI_FORMAT_R8G8B8A8_UNORM);
    }
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    context_->CopyResource(stagingTex, colorTex);

    std::vector<uint8_t> pixels(static_cast<size_t>(outW) * outH * 4, 255);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context_->Map(stagingTex, 0, D3D11_MAP_READ, 0, &mapped))) {
        lastError_ = "Failed to map beauty shot staging texture.";
        cleanup();
        return false;
    }

    const uint8_t* src = static_cast<const uint8_t*>(mapped.pData);
    for (int y = 0; y < outH; ++y) {
        const uint8_t* rowSrc = src + static_cast<size_t>(y) * mapped.RowPitch;
        uint8_t* rowDst = pixels.data() + static_cast<size_t>(y) * outW * 4;
        std::memcpy(rowDst, rowSrc, static_cast<size_t>(outW) * 4);
        for (int x = 0; x < outW; ++x) {
            rowDst[x * 4 + 3] = 255;
        }
    }
    context_->Unmap(stagingTex, 0);
    cleanup();

    std::error_code ec;
    auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
    }

    int ok = stbi_write_png(path.c_str(), outW, outH, 4, pixels.data(), outW * 4);
    if (!ok) {
        lastError_ = "stbi_write_png failed for: " + path;
        return false;
    }
    return true;
}

} // namespace lps
