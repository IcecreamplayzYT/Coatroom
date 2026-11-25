#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb/stb_image.h"
#include "stb/stb_image_write.h"
#include "stb/stb_truetype.h"

#include "paint/canvas_engine.hpp"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <queue>
#include <thread>
#include <unordered_map>

namespace lps {

namespace {

inline uint8_t u8clamp(float v) {
    return static_cast<uint8_t>(std::clamp(static_cast<int>(v + 0.5f), 0, 255));
}

inline void blendPixel(uint8_t* dst, float sr, float sg, float sb, float sa, BlendMode mode, float layerOpacity) {
    float a = std::clamp(sa * layerOpacity, 0.0f, 1.0f);
    if (a <= 1e-4f) return;

    float dr = dst[0] / 255.0f;
    float dg = dst[1] / 255.0f;
    float db = dst[2] / 255.0f;
    float da = dst[3] / 255.0f;

    float mr = sr, mg = sg, mb = sb;
    if (mode == BlendMode::Multiply) {
        mr = dr * sr; mg = dg * sg; mb = db * sb;
    } else if (mode == BlendMode::Screen) {
        mr = 1.0f - (1.0f - dr) * (1.0f - sr);
        mg = 1.0f - (1.0f - dg) * (1.0f - sg);
        mb = 1.0f - (1.0f - db) * (1.0f - sb);
    } else if (mode == BlendMode::Overlay) {
        auto ov = [](float b, float s) { return b < 0.5f ? (2.0f * b * s) : (1.0f - 2.0f * (1.0f - b) * (1.0f - s)); };
        mr = ov(dr, sr); mg = ov(dg, sg); mb = ov(db, sb);
    }

    float outA = a + da * (1.0f - a);
    if (outA < 1e-5f) {
        dst[0] = dst[1] = dst[2] = dst[3] = 0;
        return;
    }
    float outR = (mr * a + dr * da * (1.0f - a)) / outA;
    float outG = (mg * a + dg * da * (1.0f - a)) / outA;
    float outB = (mb * a + db * da * (1.0f - a)) / outA;
    dst[0] = u8clamp(outR * 255.0f);
    dst[1] = u8clamp(outG * 255.0f);
    dst[2] = u8clamp(outB * 255.0f);
    dst[3] = u8clamp(outA * 255.0f);
}

inline void paintOverRaw(uint8_t* dst, uint8_t sr, uint8_t sg, uint8_t sb, float alpha, bool eraser) {
    float a = std::clamp(alpha, 0.0f, 1.0f);
    if (a <= 1e-4f) return;
    if (eraser) {
        dst[3] = u8clamp(dst[3] * (1.0f - a));
        return;
    }
    float da = dst[3] / 255.0f;
    float outA = a + da * (1.0f - a);
    if (outA < 1e-5f) {
        dst[0] = dst[1] = dst[2] = dst[3] = 0;
        return;
    }
    dst[0] = u8clamp((sr * a + dst[0] * da * (1.0f - a)) / outA);
    dst[1] = u8clamp((sg * a + dst[1] * da * (1.0f - a)) / outA);
    dst[2] = u8clamp((sb * a + dst[2] * da * (1.0f - a)) / outA);
    dst[3] = u8clamp(outA * 255.0f);
}

inline void sampleRgbaBilinear(const uint8_t* src, int w, int h, float tx, float ty, float& r, float& g, float& b, float& a) {
    if (tx < 0.0f || tx > 1.0f || ty < 0.0f || ty > 1.0f || w <= 0 || h <= 0) {
        r = g = b = a = 0.0f;
        return;
    }
    float fx = std::clamp(tx * (w - 1), 0.0f, static_cast<float>(w - 1));
    float fy = std::clamp(ty * (h - 1), 0.0f, static_cast<float>(h - 1));
    int x0 = static_cast<int>(fx), y0 = static_cast<int>(fy);
    int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
    float wx = fx - x0, wy = fy - y0;

    auto at = [&](int x, int y, int c) {
        return static_cast<float>(src[(static_cast<size_t>(y) * w + x) * 4 + c]);
    };
    float w00 = (1.0f - wx) * (1.0f - wy), w10 = wx * (1.0f - wy);
    float w01 = (1.0f - wx) * wy,         w11 = wx * wy;
    r = w00 * at(x0,y0,0) + w10 * at(x1,y0,0) + w01 * at(x0,y1,0) + w11 * at(x1,y1,0);
    g = w00 * at(x0,y0,1) + w10 * at(x1,y0,1) + w01 * at(x0,y1,1) + w11 * at(x1,y1,1);
    b = w00 * at(x0,y0,2) + w10 * at(x1,y0,2) + w01 * at(x0,y1,2) + w11 * at(x1,y1,2);
    a = (w00 * at(x0,y0,3) + w10 * at(x1,y0,3) + w01 * at(x0,y1,3) + w11 * at(x1,y1,3)) / 255.0f;
}

float distPointToSegSq(Vec2 p, Vec2 a, Vec2 b, float& outT) {
    Vec2 ab = b - a;
    float l2 = lengthSq(ab);
    if (l2 < 1e-8f) { outT = 0.0f; return lengthSq(p - a); }
    outT = std::clamp(dot(p - a, ab) / l2, 0.0f, 1.0f);
    Vec2 proj = a + ab * outT;
    return lengthSq(p - proj);
}

Vec2 catmullRom2(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, float t) {
    float t2 = t * t, t3 = t2 * t;
    return {
        0.5f * ((2.0f * p1.x) + (-p0.x + p2.x) * t + (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * t2 + (-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) * t3),
        0.5f * ((2.0f * p1.y) + (-p0.y + p2.y) * t + (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 + (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3)
    };
}

Vec3 catmullRom3(Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, float t) {
    float t2 = t * t, t3 = t2 * t;
    return ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3) * 0.5f;
}

template <typename T>
inline T evalCubic(const T& p0, const T& p1, const T& p2, const T& p3, float t) {
    float u = 1.0f - t;
    float uu = u * u;
    float tt = t * t;
    return p0 * (uu * u) + p1 * (3.0f * uu * t) + p2 * (3.0f * u * tt) + p3 * (tt * t);
}

void rasterizePoly2DToBuffer(uint8_t* dstRgba, int bufW, int bufH,
                             const std::vector<Vec2>& poly,
                             bool closed, bool filled, Color4u8 fillColor,
                             bool hasStroke, float halfStrokePx, float outlinePx, float endTaper,
                             Color4u8 strokeColor, Color4u8 outlineColor,
                             DirtyRect* outDirty) {
    if (poly.size() < 2 || bufW <= 0 || bufH <= 0) return;

    Vec2 bmin{1e9f, 1e9f}, bmax{-1e9f, -1e9f};
    for (const Vec2& p : poly) {
        bmin.x = std::min(bmin.x, p.x);
        bmin.y = std::min(bmin.y, p.y);
        bmax.x = std::max(bmax.x, p.x);
        bmax.y = std::max(bmax.y, p.y);
    }
    float strokePad = hasStroke ? (halfStrokePx + outlinePx) : 0.0f;
    int pad = static_cast<int>(std::ceil(std::max(0.0f, strokePad))) + 3;
    int x0 = std::clamp(static_cast<int>(std::floor(bmin.x)) - pad, 0, bufW - 1);
    int x1 = std::clamp(static_cast<int>(std::ceil(bmax.x)) + pad, 0, bufW - 1);
    int y0 = std::clamp(static_cast<int>(std::floor(bmin.y)) - pad, 0, bufH - 1);
    int y1 = std::clamp(static_cast<int>(std::ceil(bmax.y)) + pad, 0, bufH - 1);
    if (x0 > x1 || y0 > y1) return;

    const int rw = x1 - x0 + 1;
    const int rh = y1 - y0 + 1;

    if (filled && poly.size() >= 3 && fillColor.a > 0) {
        const size_t nPoly = poly.size();
        std::vector<uint8_t> rowCov(static_cast<size_t>(rw), 0);
        std::vector<float> xs;
        xs.reserve(32);
        const float fillAlphaScale = fillColor.a / 255.0f;

        for (int py = y0; py <= y1; ++py) {
            std::fill(rowCov.begin(), rowCov.end(), 0);
            for (int subY = 0; subY < 2; ++subY) {
                float sy = py + (subY == 0 ? 0.25f : 0.75f);
                xs.clear();
                for (size_t i = 0; i < nPoly; ++i) {
                    const Vec2& a = poly[i];
                    const Vec2& b = poly[(i + 1) % nPoly];
                    if ((a.y <= sy && b.y > sy) || (b.y <= sy && a.y > sy)) {
                        xs.push_back(a.x + (sy - a.y) * (b.x - a.x) / (b.y - a.y));
                    }
                }
                if (xs.size() < 2) continue;
                std::sort(xs.begin(), xs.end());
                for (size_t k = 0; k + 1 < xs.size(); k += 2) {
                    float xa = xs[k];
                    float xb = xs[k + 1];
                    if (xb <= xa) continue;
                    int pxStart = std::max(x0, static_cast<int>(std::floor(xa - 0.75f)));
                    int pxEnd = std::min(x1, static_cast<int>(std::ceil(xb - 0.25f)));
                    for (int px = pxStart; px <= pxEnd; ++px) {
                        float cx0 = px + 0.25f;
                        float cx1 = px + 0.75f;
                        if (cx0 >= xa && cx0 < xb) rowCov[px - x0] += 1;
                        if (cx1 >= xa && cx1 < xb) rowCov[px - x0] += 1;
                    }
                }
            }
            for (int px = x0; px <= x1; ++px) {
                uint8_t c = rowCov[px - x0];
                if (c == 0) continue;
                float cov = (static_cast<float>(c) * 0.25f) * fillAlphaScale;
                size_t o = (static_cast<size_t>(py) * bufW + px) * 4;
                paintOverRaw(&dstRgba[o], fillColor.r, fillColor.g, fillColor.b, cov, false);
                if (outDirty) outDirty->expand(px, py);
            }
        }
    }

    if (hasStroke && halfStrokePx > 0.0f) {
        const int segCnt = closed ? static_cast<int>(poly.size()) : (static_cast<int>(poly.size()) - 1);
        if (segCnt <= 0) return;
        const bool applyTaper = (endTaper > 1e-4f) && !closed;
        std::vector<float> minD2(static_cast<size_t>(rw) * rh, 1e20f);
        std::vector<float> bestGT;
        if (applyTaper) bestGT.assign(static_cast<size_t>(rw) * rh, 0.5f);

        for (int s = 0; s < segCnt; ++s) {
            const Vec2& a = poly[s];
            const Vec2& b = poly[(s + 1) % poly.size()];
            int sx0 = std::max(x0, static_cast<int>(std::floor(std::min(a.x, b.x))) - pad);
            int sx1 = std::min(x1, static_cast<int>(std::ceil(std::max(a.x, b.x))) + pad);
            int sy0 = std::max(y0, static_cast<int>(std::floor(std::min(a.y, b.y))) - pad);
            int sy1 = std::min(y1, static_cast<int>(std::ceil(std::max(a.y, b.y))) + pad);
            for (int py = sy0; py <= sy1; ++py) {
                for (int px = sx0; px <= sx1; ++px) {
                    float lt = 0.0f;
                    float d2 = distPointToSegSq(Vec2{px + 0.5f, py + 0.5f}, a, b, lt);
                    size_t li = static_cast<size_t>(py - y0) * rw + (px - x0);
                    if (d2 < minD2[li]) {
                        minD2[li] = d2;
                        if (applyTaper) bestGT[li] = (s + lt) / static_cast<float>(segCnt);
                    }
                }
            }
        }

        const float maxR2 = static_cast<float>(pad * pad);
        const float strokeAlphaScale = strokeColor.a / 255.0f;
        const float outAlphaScale = outlineColor.a / 255.0f;
        for (int py = y0; py <= y1; ++py) {
            for (int px = x0; px <= x1; ++px) {
                size_t li = static_cast<size_t>(py - y0) * rw + (px - x0);
                float d2 = minD2[li];
                if (d2 > maxR2) continue;
                float dist = std::sqrt(d2);
                float localHw = halfStrokePx;
                if (applyTaper) {
                    localHw *= (1.0f - endTaper * std::fabs(2.0f * bestGT[li] - 1.0f));
                }
                float aaFill = std::clamp(localHw + 0.7f - dist, 0.0f, 1.0f);
                float aaOut = (outlinePx > 0.1f) ? std::clamp(localHw + outlinePx + 0.7f - dist, 0.0f, 1.0f) : 0.0f;
                if (aaOut <= 0.01f && aaFill <= 0.01f) continue;
                size_t o = (static_cast<size_t>(py) * bufW + px) * 4;
                if (aaOut > aaFill && aaOut > 0.01f) {
                    paintOverRaw(&dstRgba[o], outlineColor.r, outlineColor.g, outlineColor.b, aaOut * outAlphaScale, false);
                }
                if (aaFill > 0.01f) {
                    paintOverRaw(&dstRgba[o], strokeColor.r, strokeColor.g, strokeColor.b, aaFill * strokeAlphaScale, false);
                }
                if (outDirty) outDirty->expand(px, py);
            }
        }
    }
}

bool renderTextToBitmap(const TextLayerProps& tp, std::vector<uint8_t>& outRgba, int& outW, int& outH) {
    if (tp.text.empty()) return false;

    std::string fontFile = tp.fontPath;
    if (fontFile.empty() || !std::filesystem::exists(fontFile)) {
        const char* candidates[] = {
            "C:\\Windows\\Fonts\\arialbd.ttf",
            "C:\\Windows\\Fonts\\ariblk.ttf",
            "C:\\Windows\\Fonts\\segoeui.ttf",
            "C:\\Windows\\Fonts\\arial.ttf"
        };
        for (const char* c : candidates) {
            if (std::filesystem::exists(c)) { fontFile = c; break; }
        }
    }
    if (fontFile.empty()) return false;

    std::ifstream ifs(fontFile, std::ios::binary);
    if (!ifs.is_open()) return false;
    std::vector<uint8_t> ttfData((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    stbtt_fontinfo font{};
    if (!stbtt_InitFont(&font, ttfData.data(), stbtt_GetFontOffsetForIndex(ttfData.data(), 0))) return false;

    float pixelH = std::max(16.0f, tp.fontSizePx);
    float scale = stbtt_ScaleForPixelHeight(&font, pixelH);
    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);
    int baseline = static_cast<int>(std::ceil(ascent * scale));

    int outPad = static_cast<int>(std::ceil(tp.outlineWidth)) + 8;
    float totalW = 0.0f;
    for (size_t i = 0; i < tp.text.size(); ++i) {
        int adv = 0, lsb = 0;
        unsigned char ch = static_cast<unsigned char>(tp.text[i]);
        stbtt_GetCodepointHMetrics(&font, ch, &adv, &lsb);
        totalW += adv * scale + tp.tracking;
        if (i + 1 < tp.text.size()) {
            totalW += stbtt_GetCodepointKernAdvance(&font, ch, static_cast<unsigned char>(tp.text[i + 1])) * scale;
        }
    }

    int shearExtra = static_cast<int>(std::ceil(std::fabs(tp.italicShear) * pixelH)) + 6;
    outW = std::clamp(static_cast<int>(std::ceil(totalW)) + outPad * 2 + shearExtra, 8, 2048);
    outH = std::clamp(static_cast<int>(std::ceil((ascent - descent) * scale)) + outPad * 2, 8, 1024);

    std::vector<uint8_t> mask(static_cast<size_t>(outW) * outH, 0);
    float xpos = static_cast<float>(outPad + (tp.italicShear < 0 ? 0 : shearExtra / 2));

    for (size_t i = 0; i < tp.text.size(); ++i) {
        unsigned char ch = static_cast<unsigned char>(tp.text[i]);
        int adv = 0, lsb = 0, x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetCodepointHMetrics(&font, ch, &adv, &lsb);
        float xShift = xpos - std::floor(xpos);
        stbtt_GetCodepointBitmapBoxSubpixel(&font, ch, scale, scale, xShift, 0.0f, &x0, &y0, &x1, &y1);
        int gw = x1 - x0, gh = y1 - y0;
        if (gw > 0 && gh > 0) {
            std::vector<uint8_t> gbmp(static_cast<size_t>(gw) * gh, 0);
            stbtt_MakeCodepointBitmapSubpixel(&font, gbmp.data(), gw, gh, gw, scale, scale, xShift, 0.0f, ch);
            int baseX = static_cast<int>(std::floor(xpos)) + x0;
            int baseY = outPad + baseline + y0;
            for (int gy = 0; gy < gh; ++gy) {
                int dy = baseY + gy;
                if (dy < 0 || dy >= outH) continue;
                int shearDx = static_cast<int>(std::round((outPad + baseline - dy) * tp.italicShear));
                for (int gx = 0; gx < gw; ++gx) {
                    int dx = baseX + gx + shearDx;
                    if (dx < 0 || dx >= outW) continue;
                    uint8_t& m = mask[static_cast<size_t>(dy) * outW + dx];
                    m = std::max(m, gbmp[static_cast<size_t>(gy) * gw + gx]);
                }
            }
        }
        xpos += adv * scale + tp.tracking;
        if (i + 1 < tp.text.size()) {
            xpos += stbtt_GetCodepointKernAdvance(&font, ch, static_cast<unsigned char>(tp.text[i + 1])) * scale;
        }
    }

    outRgba.assign(static_cast<size_t>(outW) * outH * 4, 0);
    int rOut = static_cast<int>(std::ceil(tp.outlineWidth));

    for (int y = 0; y < outH; ++y) {
        for (int x = 0; x < outW; ++x) {
            float fgA = mask[static_cast<size_t>(y) * outW + x] / 255.0f;
            float bgA = 0.0f;
            if (tp.outlineWidth > 0.25f) {
                float r2 = tp.outlineWidth * tp.outlineWidth;
                for (int oy = -rOut; oy <= rOut; ++oy) {
                    int ny = y + oy;
                    if (ny < 0 || ny >= outH) continue;
                    for (int ox = -rOut; ox <= rOut; ++ox) {
                        if (ox * ox + oy * oy > r2 + 0.5f) continue;
                        int nx = x + ox;
                        if (nx < 0 || nx >= outW) continue;
                        bgA = std::max(bgA, mask[static_cast<size_t>(ny) * outW + nx] / 255.0f);
                    }
                }
            }
            size_t idx = (static_cast<size_t>(y) * outW + x) * 4;
            if (bgA > 1e-4f && bgA > fgA) {
                float comb = std::clamp(fgA + bgA * (1.0f - fgA), 0.0f, 1.0f);
                float t = (comb > 1e-4f) ? (fgA / comb) : 0.0f;
                outRgba[idx + 0] = u8clamp(tp.outlineColor.r * (1.0f - t) + tp.fillColor.r * t);
                outRgba[idx + 1] = u8clamp(tp.outlineColor.g * (1.0f - t) + tp.fillColor.g * t);
                outRgba[idx + 2] = u8clamp(tp.outlineColor.b * (1.0f - t) + tp.fillColor.b * t);
                outRgba[idx + 3] = u8clamp(comb * 255.0f);
            } else if (fgA > 1e-4f) {
                outRgba[idx + 0] = tp.fillColor.r;
                outRgba[idx + 1] = tp.fillColor.g;
                outRgba[idx + 2] = tp.fillColor.b;
                outRgba[idx + 3] = u8clamp(fgA * tp.fillColor.a);
            }
        }
    }
    return true;
}

void rasterizeRect2DToBuffer(uint8_t* dstRgba, int bufW, int bufH,
                             Vec2 centerPx, float drawW, float drawH, float rotDeg,
                             float cornerRadiusPx, float strokeWidthPx,
                             bool filled, bool hasStroke,
                             Color4u8 fillColor, Color4u8 strokeColor,
                             DirtyRect* outDirty) {
    if (!dstRgba || bufW <= 0 || bufH <= 0 || drawW <= 0.5f || drawH <= 0.5f) return;
    if (!filled && (!hasStroke || strokeWidthPx <= 0.0f)) return;

    float hw = 0.5f * drawW;
    float hh = 0.5f * drawH;
    float r = std::clamp(cornerRadiusPx, 0.0f, std::min(hw, hh));
    float sw = (hasStroke && strokeWidthPx > 0.0f) ? strokeWidthPx : 0.0f;
    float halfSw = 0.5f * sw;

    float rad = rotDeg * kDeg2Rad;
    float cs = std::cos(rad), sn = std::sin(rad);
    float pad = halfSw + 2.0f;
    float rBound = std::hypot(hw + pad, hh + pad);
    int x0 = std::clamp(static_cast<int>(std::floor(centerPx.x - rBound)), 0, bufW - 1);
    int x1 = std::clamp(static_cast<int>(std::ceil(centerPx.x + rBound)), 0, bufW - 1);
    int y0 = std::clamp(static_cast<int>(std::floor(centerPx.y - rBound)), 0, bufH - 1);
    int y1 = std::clamp(static_cast<int>(std::ceil(centerPx.y + rBound)), 0, bufH - 1);

    const float fillAlphaScale = fillColor.a / 255.0f;
    const float strokeAlphaScale = strokeColor.a / 255.0f;

    for (int py = y0; py <= y1; ++py) {
        float dy = (py + 0.5f) - centerPx.y;
        for (int px = x0; px <= x1; ++px) {
            float dx = (px + 0.5f) - centerPx.x;
            float lx = dx * cs + dy * sn;
            float ly = -dx * sn + dy * cs;
            float qx = std::fabs(lx) - (hw - r);
            float qy = std::fabs(ly) - (hh - r);
            float sdf = std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) +
                        std::min(std::max(qx, qy), 0.0f) - r;

            float fA = filled ? std::clamp(0.5f - sdf, 0.0f, 1.0f) * fillAlphaScale : 0.0f;
            float sA = (sw > 0.0f) ? std::clamp(halfSw + 0.5f - std::fabs(sdf), 0.0f, 1.0f) * strokeAlphaScale : 0.0f;
            if (fA <= 1e-4f && sA <= 1e-4f) continue;

            size_t o = (static_cast<size_t>(py) * bufW + px) * 4;
            if (fA > 1e-4f) {
                paintOverRaw(&dstRgba[o], fillColor.r, fillColor.g, fillColor.b, fA, false);
            }
            if (sA > 1e-4f) {
                paintOverRaw(&dstRgba[o], strokeColor.r, strokeColor.g, strokeColor.b, sA, false);
            }
            if (outDirty) outDirty->expand(px, py);
        }
    }
}

void drawText2DToLayer(Layer& l, int width, int height, const std::vector<uint8_t>& bmp, int bw, int bh,
                       Vec2 uvPos, float drawW, float drawH, float rotDeg, bool flipX, DirtyRect* outDirty) {
    float cx = uvPos.x * width, cy = uvPos.y * height;
    float rad = rotDeg * kDeg2Rad;
    float cs = std::cos(rad), sn = std::sin(rad);
    int rB = static_cast<int>(std::ceil(0.6f * std::hypot(drawW, drawH))) + 2;
    int x0 = std::clamp(static_cast<int>(cx) - rB, 0, width - 1);
    int x1 = std::clamp(static_cast<int>(cx) + rB, 0, width - 1);
    int y0 = std::clamp(static_cast<int>(cy) - rB, 0, height - 1);
    int y1 = std::clamp(static_cast<int>(cy) + rB, 0, height - 1);
    for (int py = y0; py <= y1; ++py) {
        for (int px = x0; px <= x1; ++px) {
            float dx = (px + 0.5f) - cx, dy = (py + 0.5f) - cy;
            float lx = (dx * cs + dy * sn) / drawW + 0.5f;
            float ly = (-dx * sn + dy * cs) / drawH + 0.5f;
            if (flipX) lx = 1.0f - lx;
            if (lx < 0.0f || lx > 1.0f || ly < 0.0f || ly > 1.0f) continue;
            float sr, sg, sb, sa;
            sampleRgbaBilinear(bmp.data(), bw, bh, lx, ly, sr, sg, sb, sa);
            if (sa <= 1e-4f) continue;
            size_t o = (static_cast<size_t>(py) * width + px) * 4;
            paintOverRaw(&l.rgba[o], u8clamp(sr), u8clamp(sg), u8clamp(sb), sa, false);
            if (outDirty) outDirty->expand(px, py);
        }
    }
}

void drawDecal2DToLayer(Layer& l, int width, int height, const std::vector<uint8_t>& imgPix, int imgW, int imgH,
                        Vec2 uvPos, Vec2 uvScale, float rotDeg, bool flipX, Color4u8 tint, DirtyRect* outDirty) {
    float cx = uvPos.x * width, cy = uvPos.y * height;
    float drawW = uvScale.x * width, drawH = uvScale.y * height;
    if (drawW <= 0.5f || drawH <= 0.5f) return;
    float rad = rotDeg * kDeg2Rad;
    float cs = std::cos(rad), sn = std::sin(rad);
    int rB = static_cast<int>(std::ceil(0.6f * std::hypot(drawW, drawH))) + 2;
    int x0 = std::clamp(static_cast<int>(cx) - rB, 0, width - 1);
    int x1 = std::clamp(static_cast<int>(cx) + rB, 0, width - 1);
    int y0 = std::clamp(static_cast<int>(cy) - rB, 0, height - 1);
    int y1 = std::clamp(static_cast<int>(cy) + rB, 0, height - 1);
    for (int py = y0; py <= y1; ++py) {
        for (int px = x0; px <= x1; ++px) {
            float dx = (px + 0.5f) - cx, dy = (py + 0.5f) - cy;
            float lx = (dx * cs + dy * sn) / drawW + 0.5f;
            float ly = (-dx * sn + dy * cs) / drawH + 0.5f;
            if (flipX) lx = 1.0f - lx;
            if (lx < 0.0f || lx > 1.0f || ly < 0.0f || ly > 1.0f) continue;
            float sr, sg, sb, sa;
            sampleRgbaBilinear(imgPix.data(), imgW, imgH, lx, ly, sr, sg, sb, sa);
            float effA = sa * (tint.a / 255.0f);
            if (effA <= 1e-4f) continue;
            size_t o = (static_cast<size_t>(py) * width + px) * 4;
            paintOverRaw(&l.rgba[o], u8clamp(sr * (tint.r / 255.0f)),
                         u8clamp(sg * (tint.g / 255.0f)),
                         u8clamp(sb * (tint.b / 255.0f)), effA, false);
            if (outDirty) outDirty->expand(px, py);
        }
    }
}

void copyRectBytes(std::vector<uint8_t>& dst, const std::vector<uint8_t>& src,
                   int width, int x0, int y0, int x1, int y1) {
    const size_t fullBytes = dst.size();
    if (src.size() != fullBytes) return;
    const size_t rowBytes = static_cast<size_t>(x1 - x0 + 1) * 4;
    for (int py = y0; py <= y1; ++py) {
        size_t off = (static_cast<size_t>(py) * width + x0) * 4;
        std::memcpy(&dst[off], &src[off], rowBytes);
    }
}

} // namespace

std::vector<uint8_t> renderBrandLogoRgba(int sizePx) {
    if (sizePx <= 0) return {};
    sizePx = std::clamp(sizePx, 8, 1024);
    std::vector<uint8_t> out(static_cast<size_t>(sizePx) * sizePx * 4, 0);

    // 4x4 SSAA Adobe squircle: |2x-1|^4.4 + |2y-1|^4.4 <= 1.0
    // Dark midnight emerald-teal gradient #052228 -> #041B20 (zero outer glow, zero corner brackets)
    constexpr float kExp = 4.4f;
    for (int py = 0; py < sizePx; ++py) {
        float tGrad = (py + 0.5f) / static_cast<float>(sizePx);
        float bgR = 5.0f + (4.0f - 5.0f) * tGrad;
        float bgG = 34.0f + (27.0f - 34.0f) * tGrad;
        float bgB = 40.0f + (32.0f - 40.0f) * tGrad;
        for (int px = 0; px < sizePx; ++px) {
            int hits = 0;
            for (int sy = 0; sy < 4; ++sy) {
                float ny = (py + (sy + 0.5f) * 0.25f) / static_cast<float>(sizePx);
                float ay = std::pow(std::fabs(2.0f * ny - 1.0f), kExp);
                for (int sx = 0; sx < 4; ++sx) {
                    float nx = (px + (sx + 0.5f) * 0.25f) / static_cast<float>(sizePx);
                    float ax = std::pow(std::fabs(2.0f * nx - 1.0f), kExp);
                    if (ax + ay <= 1.0f) ++hits;
                }
            }
            if (hits > 0) {
                size_t o = (static_cast<size_t>(py) * sizePx + px) * 4;
                out[o + 0] = u8clamp(bgR);
                out[o + 1] = u8clamp(bgG);
                out[o + 2] = u8clamp(bgB);
                out[o + 3] = static_cast<uint8_t>((hits * 255 + 8) / 16);
            }
        }
    }

    std::string fontFile;
    const char* fontCands[] = {
        "C:\\Windows\\Fonts\\segoeuib.ttf",
        "C:\\Windows\\Fonts\\arialbd.ttf"
    };
    for (const char* c : fontCands) {
        if (std::filesystem::exists(c)) { fontFile = c; break; }
    }
    if (!fontFile.empty()) {
        std::ifstream ifs(fontFile, std::ios::binary);
        if (ifs.is_open()) {
            std::vector<uint8_t> ttfData((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
            stbtt_fontinfo font{};
            if (stbtt_InitFont(&font, ttfData.data(), stbtt_GetFontOffsetForIndex(ttfData.data(), 0))) {
                float pixelH = sizePx * 0.58f;
                float scale = stbtt_ScaleForPixelHeight(&font, pixelH);
                int advC = 0, lsbC = 0, advR = 0, lsbR = 0;
                stbtt_GetCodepointHMetrics(&font, 'C', &advC, &lsbC);
                stbtt_GetCodepointHMetrics(&font, 'r', &advR, &lsbR);
                float kern = stbtt_GetCodepointKernAdvance(&font, 'C', 'r') * scale;
                float xC = 0.0f;
                float xR = advC * scale + kern;
                int cX0, cY0, cX1, cY1, rX0, rY0, rX1, rY1;
                stbtt_GetCodepointBitmapBoxSubpixel(&font, 'C', scale, scale, 0.0f, 0.0f, &cX0, &cY0, &cX1, &cY1);
                stbtt_GetCodepointBitmapBoxSubpixel(&font, 'r', scale, scale, xR - std::floor(xR), 0.0f, &rX0, &rY0, &rX1, &rY1);

                float inkMinX = static_cast<float>(cX0);
                float inkMaxX = std::max(static_cast<float>(cX1), std::floor(xR) + rX1);
                float inkMinY = static_cast<float>(std::min(cY0, rY0));
                float inkMaxY = static_cast<float>(std::max(cY1, rY1));
                float offX = 0.5f * sizePx - 0.5f * (inkMinX + inkMaxX);
                float baseLineY = 0.5f * sizePx - 0.5f * (inkMinY + inkMaxY);

                auto stampGlyph = [&](int cp, float gxPos) {
                    float fx = offX + gxPos;
                    int ix = static_cast<int>(std::floor(fx));
                    float xShift = fx - ix;
                    int gx0 = 0, gy0 = 0, gx1 = 0, gy1 = 0;
                    stbtt_GetCodepointBitmapBoxSubpixel(&font, cp, scale, scale, xShift, 0.0f, &gx0, &gy0, &gx1, &gy1);
                    int gw = gx1 - gx0, gh = gy1 - gy0;
                    if (gw <= 0 || gh <= 0) return;
                    std::vector<uint8_t> gbmp(static_cast<size_t>(gw) * gh, 0);
                    stbtt_MakeCodepointBitmapSubpixel(&font, gbmp.data(), gw, gh, gw, scale, scale, xShift, 0.0f, cp);
                    int baseY = static_cast<int>(std::round(baseLineY)) + gy0;
                    for (int y = 0; y < gh; ++y) {
                        int dy = baseY + y;
                        if (dy < 0 || dy >= sizePx) continue;
                        for (int x = 0; x < gw; ++x) {
                            int dx = ix + gx0 + x;
                            if (dx < 0 || dx >= sizePx) continue;
                            float ga = gbmp[static_cast<size_t>(y) * gw + x] / 255.0f;
                            if (ga <= 1e-4f) continue;
                            size_t o = (static_cast<size_t>(dy) * sizePx + dx) * 4;
                            float maskA = out[o + 3] / 255.0f;
                            if (maskA <= 1e-4f) continue;
                            // #2FE6CE = (47, 230, 206) blended over squircle fill while keeping squircle alpha
                            out[o + 0] = u8clamp(out[o + 0] * (1.0f - ga) + 47.0f * ga);
                            out[o + 1] = u8clamp(out[o + 1] * (1.0f - ga) + 230.0f * ga);
                            out[o + 2] = u8clamp(out[o + 2] * (1.0f - ga) + 206.0f * ga);
                        }
                    }
                };
                stampGlyph('C', xC);
                stampGlyph('r', xR);
            }
        }
    }
    return out;
}

void CanvasEngine::init(int size, Color4u8 bgColor) {
    width = size;
    height = size;
    layers.clear();
    undoStack.clear();
    redoStack.clear();
    nextLayerId = 1;
    nextItemId = 1;
    selectedItemIdx = -1;
    strokeLayerIdx = -1;
    pendingStrokeSnap = {};
    compositeRgba.assign(static_cast<size_t>(width) * height * 4, 255);

    addRasterLayer("Base Paint", true, bgColor);
    addRasterLayer("Paint Layer 1", false, {0, 0, 0, 0});
    activeLayerIdx = 1;
    scanSystemFonts();
    recomposite();
}

void CanvasEngine::resizeCanvas(int newSize) {
    if (newSize == width && newSize == height) return;
    int oldW = width, oldH = height;
    auto resizeBuf = [&](const std::vector<uint8_t>& src) {
        std::vector<uint8_t> resized(static_cast<size_t>(newSize) * newSize * 4, 0);
        for (int y = 0; y < newSize; ++y) {
            float ty = (y + 0.5f) / newSize;
            for (int x = 0; x < newSize; ++x) {
                float tx = (x + 0.5f) / newSize;
                float r, g, b, a;
                sampleRgbaBilinear(src.data(), oldW, oldH, tx, ty, r, g, b, a);
                size_t dst = (static_cast<size_t>(y) * newSize + x) * 4;
                resized[dst + 0] = u8clamp(r);
                resized[dst + 1] = u8clamp(g);
                resized[dst + 2] = u8clamp(b);
                resized[dst + 3] = u8clamp(a * 255.0f);
            }
        }
        return resized;
    };

    for (Layer& l : layers) {
        if (l.paintBuffer && l.paintBuffer->size() == static_cast<size_t>(oldW) * oldH * 4) {
            l.paintBuffer = std::make_shared<std::vector<uint8_t>>(resizeBuf(*l.paintBuffer));
        }
        if (l.kind == LayerKind::RasterPaint && l.items.empty() && !l.rgba.empty()) {
            if (l.paintBuffer) {
                l.rgba = *l.paintBuffer;
            } else {
                l.rgba = resizeBuf(l.rgba);
            }
        } else {
            l.rgba.assign(static_cast<size_t>(newSize) * newSize * 4, 0);
            l.dirtyVector = true;
            l.hasPrevBounds = false;
            l.prevMinX = l.prevMinY = 0;
            l.prevMaxX = l.prevMaxY = -1;
        }
    }
    width = height = newSize;
    compositeRgba.assign(static_cast<size_t>(width) * height * 4, 255);
    undoStack.clear();
    redoStack.clear();
    selectedItemIdx = -1;
    recomposite();
}

void CanvasEngine::scanSystemFonts() {
    if (!systemFonts.empty()) return;
    const std::string fontDir = "C:\\Windows\\Fonts";
    if (!std::filesystem::exists(fontDir)) return;
    for (const auto& entry : std::filesystem::directory_iterator(fontDir)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".ttf" || ext == ".otf") {
            systemFonts.push_back({entry.path().stem().string(), entry.path().string()});
        }
    }
    std::sort(systemFonts.begin(), systemFonts.end(), [](const FontFaceInfo& a, const FontFaceInfo& b) {
        return a.displayName < b.displayName;
    });
}

int CanvasEngine::addLayer(const std::string& name, bool fillBg, Color4u8 bg) {
    pushUndoSnapshot();
    int idx = addRasterLayer(name, fillBg, bg);
    selectedItemIdx = -1;
    return idx;
}

int CanvasEngine::addRasterLayer(const std::string& name, bool fillBg, Color4u8 bg) {
    Layer l;
    l.id = nextLayerId++;
    l.name = name;
    l.kind = LayerKind::RasterPaint;
    const size_t fullBytes = static_cast<size_t>(width) * height * 4;
    l.rgba.assign(fullBytes, 0);
    if (fillBg) {
        auto& pb = l.ensureUniquePaintBuffer(width, height);
        for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
            pb[i*4+0] = bg.r; pb[i*4+1] = bg.g; pb[i*4+2] = bg.b; pb[i*4+3] = bg.a;
        }
        l.rgba = pb;
    }
    l.dirtyVector = false;
    layers.push_back(std::move(l));
    activeLayerIdx = static_cast<int>(layers.size()) - 1;
    selectedItemIdx = -1;
    recomposite();
    return activeLayerIdx;
}

int CanvasEngine::addTextLayer(const std::string& name) {
    Layer l;
    l.id = nextLayerId++;
    l.name = name;
    l.kind = LayerKind::Text;
    l.rgba.assign(static_cast<size_t>(width) * height * 4, 0);
    l.dirtyVector = true;
    for (const auto& f : systemFonts) {
        if (f.displayName == "ariblk" || f.displayName == "arialbd" || f.displayName == "impact") {
            l.text.fontPath = f.filePath;
            break;
        }
    }
    layers.push_back(std::move(l));
    activeLayerIdx = static_cast<int>(layers.size()) - 1;
    selectedItemIdx = -1;
    return activeLayerIdx;
}

int CanvasEngine::addDecalLayer(const std::string& name, const std::string& imgPath) {
    int w = 0, h = 0, comp = 0;
    uint8_t* px = stbi_load(imgPath.c_str(), &w, &h, &comp, 4);
    if (!px || w <= 0 || h <= 0) {
        if (px) stbi_image_free(px);
        return -1;
    }
    Layer l;
    l.id = nextLayerId++;
    l.name = name;
    l.kind = LayerKind::Decal;
    l.decal.sourcePath = imgPath;
    l.decal.imgW = w;
    l.decal.imgH = h;
    l.decal.imgRgbaShared = std::make_shared<std::vector<uint8_t>>(px, px + static_cast<size_t>(w) * h * 4);
    stbi_image_free(px);
    l.decal.uvScale = {0.18f, 0.18f * (static_cast<float>(h) / w)};
    l.rgba.assign(static_cast<size_t>(width) * height * 4, 0);
    l.dirtyVector = true;
    layers.push_back(std::move(l));
    activeLayerIdx = static_cast<int>(layers.size()) - 1;
    selectedItemIdx = -1;
    return activeLayerIdx;
}

int CanvasEngine::addRulerStripeLayer(const std::string& name) {
    Layer l;
    l.id = nextLayerId++;
    l.name = name;
    l.kind = LayerKind::RulerStripe;
    l.rgba.assign(static_cast<size_t>(width) * height * 4, 0);
    l.dirtyVector = true;
    layers.push_back(std::move(l));
    activeLayerIdx = static_cast<int>(layers.size()) - 1;
    selectedItemIdx = -1;
    return activeLayerIdx;
}

bool CanvasEngine::removeLayer(int index) {
    if (layers.size() <= 1 || index < 0 || index >= static_cast<int>(layers.size())) return false;
    pushUndoSnapshot();
    layers.erase(layers.begin() + index);
    activeLayerIdx = std::clamp(activeLayerIdx, 0, static_cast<int>(layers.size()) - 1);
    if (layers[activeLayerIdx].items.empty()) {
        selectedItemIdx = -1;
    } else {
        selectedItemIdx = std::clamp(selectedItemIdx, 0, static_cast<int>(layers[activeLayerIdx].items.size()) - 1);
    }
    recomposite();
    return true;
}

void CanvasEngine::deleteLayer(int index) {
    removeLayer(index);
}

void CanvasEngine::moveLayer(int index, int delta) {
    int target = index + delta;
    if (index < 0 || index >= static_cast<int>(layers.size())) return;
    if (target < 0 || target >= static_cast<int>(layers.size())) return;
    pushUndoSnapshot();
    std::swap(layers[index], layers[target]);
    if (activeLayerIdx == index) activeLayerIdx = target;
    else if (activeLayerIdx == target) activeLayerIdx = index;
    recomposite();
}

void CanvasEngine::reorderLayer(int fromIdx, int toIdx) {
    int n = static_cast<int>(layers.size());
    if (n < 2) return;
    fromIdx = std::clamp(fromIdx, 0, n - 1);
    toIdx = std::clamp(toIdx, 0, n - 1);
    if (fromIdx == toIdx) return;

    pushUndoSnapshot();
    uint32_t activeId = (activeLayerIdx >= 0 && activeLayerIdx < n) ? layers[activeLayerIdx].id : 0;
    Layer moved = std::move(layers[fromIdx]);
    layers.erase(layers.begin() + fromIdx);
    layers.insert(layers.begin() + toIdx, std::move(moved));
    if (activeId != 0) {
        for (int i = 0; i < n; ++i) {
            if (layers[i].id == activeId) {
                activeLayerIdx = i;
                break;
            }
        }
    }
    recomposite();
}

int CanvasEngine::duplicateLayer(int index) {
    if (index < 0 || index >= static_cast<int>(layers.size())) return -1;
    pushUndoSnapshot();
    Layer copy = layers[index];
    copy.id = nextLayerId++;
    copy.name += " Copy";
    for (VectorItem& it : copy.items) {
        it.id = nextItemId++;
    }
    layers.insert(layers.begin() + index + 1, std::move(copy));
    activeLayerIdx = index + 1;
    selectedItemIdx = layers[activeLayerIdx].items.empty() ? -1 : 0;
    recomposite();
    return activeLayerIdx;
}

bool CanvasEngine::rasterizeLayerToPaint(int index, const ObjMesh* mesh) {
    if (index < 0 || index >= static_cast<int>(layers.size())) return false;
    if (mesh) cachedMesh = mesh;
    Layer& l = layers[index];
    if (l.kind != LayerKind::RasterPaint || !l.items.empty()) {
        pushUndoSnapshot();
        rasterizeLayer(index, mesh);
        Layer& dst = layers[index];
        auto& pb = dst.ensureUniquePaintBuffer(width, height);
        if (dst.rgba.size() == pb.size()) {
            pb = dst.rgba;
        }
        dst.items.clear();
        dst.kind = LayerKind::RasterPaint;
        dst.dirtyVector = false;
        if (activeLayerIdx == index) selectedItemIdx = -1;
        recomposite();
    }
    return true;
}

bool CanvasEngine::mergeDown(int index) {
    if (index <= 0 || index >= static_cast<int>(layers.size())) return false;
    if (layers[index - 1].kind != LayerKind::RasterPaint) return false;
    pushUndoSnapshot();
    if (layers[index].kind != LayerKind::RasterPaint || !layers[index].items.empty() || layers[index].dirtyVector) {
        rasterizeLayer(index, cachedMesh);
    }
    if (!layers[index - 1].items.empty()) {
        rasterizeLayer(index - 1, cachedMesh);
    }
    Layer& top = layers[index];
    Layer& bot = layers[index - 1];
    auto& botPb = bot.ensureUniquePaintBuffer(width, height);
    if (bot.rgba.size() == botPb.size() && !bot.items.empty()) {
        botPb = bot.rgba;
        bot.items.clear();
    } else if (bot.rgba.size() == botPb.size() && bot.paintBuffer) {
        // Ensure botPb holds existing bot.rgba if legacy code touched bot.rgba directly
        bool pbAllZero = false;
        if (bot.rgba != botPb) {
            for (uint8_t v : botPb) { pbAllZero = (v == 0); if (!pbAllZero) break; }
            if (pbAllZero) botPb = bot.rgba;
        }
    }
    size_t nPx = static_cast<size_t>(width) * height;
    if (top.rgba.size() == nPx * 4 && botPb.size() == nPx * 4) {
        for (size_t i = 0; i < nPx; ++i) {
            size_t o = i * 4;
            blendPixel(&botPb[o], top.rgba[o] / 255.0f, top.rgba[o+1] / 255.0f,
                       top.rgba[o+2] / 255.0f, top.rgba[o+3] / 255.0f, top.blend, top.opacity);
        }
        bot.rgba = botPb;
    }
    layers.erase(layers.begin() + index);
    activeLayerIdx = index - 1;
    selectedItemIdx = -1;
    recomposite();
    return true;
}

int CanvasEngine::ensureEditableLayer() {
    auto isGoodEditable = [&](int idx) {
        if (idx < 0 || idx >= static_cast<int>(layers.size())) return false;
        const Layer& l = layers[idx];
        return l.kind == LayerKind::RasterPaint && !l.locked && l.name != "AO / Panel Shading";
    };
    if (isGoodEditable(activeLayerIdx)) return activeLayerIdx;
    for (int i = static_cast<int>(layers.size()) - 1; i >= 1; --i) {
        if (isGoodEditable(i)) {
            activeLayerIdx = i;
            return i;
        }
    }
    for (int i = static_cast<int>(layers.size()) - 1; i >= 0; --i) {
        if (isGoodEditable(i)) {
            activeLayerIdx = i;
            return i;
        }
    }
    int aoIdx = findAoLayerIndex();
    int newIdx = addRasterLayer("Paint Layer 1", false, {0, 0, 0, 0});
    if (aoIdx >= 0 && newIdx > aoIdx) {
        Layer added = std::move(layers[newIdx]);
        layers.erase(layers.begin() + newIdx);
        layers.insert(layers.begin() + aoIdx, std::move(added));
        activeLayerIdx = aoIdx;
        return aoIdx;
    }
    return newIdx;
}

VectorItem* CanvasEngine::activeVectorItem() {
    return getVectorItem(activeLayerIdx, selectedItemIdx);
}

const VectorItem* CanvasEngine::activeVectorItem() const {
    return getVectorItem(activeLayerIdx, selectedItemIdx);
}

VectorItem* CanvasEngine::getVectorItem(int layerIdx, int itemIdx) {
    if (layerIdx < 0 || layerIdx >= static_cast<int>(layers.size())) return nullptr;
    auto& items = layers[layerIdx].items;
    if (itemIdx < 0 || itemIdx >= static_cast<int>(items.size())) return nullptr;
    return &items[itemIdx];
}

const VectorItem* CanvasEngine::getVectorItem(int layerIdx, int itemIdx) const {
    if (layerIdx < 0 || layerIdx >= static_cast<int>(layers.size())) return nullptr;
    const auto& items = layers[layerIdx].items;
    if (itemIdx < 0 || itemIdx >= static_cast<int>(items.size())) return nullptr;
    return &items[itemIdx];
}

int CanvasEngine::addPenItem(const std::string& name, const RulerStripeProps& props, const ObjMesh* mesh) {
    if (mesh) cachedMesh = mesh;
    int lIdx = ensureEditableLayer();
    if (lIdx < 0) return -1;
    pushUndoSnapshot();
    VectorItem item;
    item.id = nextItemId++;
    item.name = name.empty() ? "Pen Path" : name;
    item.kind = VectorItemKind::PenPath;
    item.pen = props;
    layers[lIdx].items.push_back(std::move(item));
    activeLayerIdx = lIdx;
    selectedItemIdx = static_cast<int>(layers[lIdx].items.size()) - 1;
    if (layers[lIdx].items.back().pen.nodes.size() >= 2) {
        rasterizeLayer(lIdx, mesh);
    }
    return selectedItemIdx;
}

int CanvasEngine::addRectItem(const std::string& name, const RectShapeProps& props, const ObjMesh* mesh) {
    if (mesh) cachedMesh = mesh;
    int lIdx = ensureEditableLayer();
    if (lIdx < 0) return -1;
    pushUndoSnapshot();
    VectorItem item;
    item.id = nextItemId++;
    item.name = name.empty() ? "Rectangle" : name;
    item.kind = VectorItemKind::RectShape;
    item.rect = props;
    layers[lIdx].items.push_back(std::move(item));
    activeLayerIdx = lIdx;
    selectedItemIdx = static_cast<int>(layers[lIdx].items.size()) - 1;
    rasterizeLayer(lIdx, mesh);
    return selectedItemIdx;
}

int CanvasEngine::addTextItem(const std::string& name, const TextLayerProps& props, const ObjMesh* mesh) {
    if (mesh) cachedMesh = mesh;
    int lIdx = ensureEditableLayer();
    if (lIdx < 0) return -1;
    pushUndoSnapshot();
    VectorItem item;
    item.id = nextItemId++;
    item.name = name.empty() ? "Text" : name;
    item.kind = VectorItemKind::Text;
    item.text = props;
    if (item.text.fontPath.empty()) {
        for (const auto& f : systemFonts) {
            if (f.displayName == "ariblk" || f.displayName == "arialbd" || f.displayName == "impact") {
                item.text.fontPath = f.filePath;
                break;
            }
        }
    }
    layers[lIdx].items.push_back(std::move(item));
    activeLayerIdx = lIdx;
    selectedItemIdx = static_cast<int>(layers[lIdx].items.size()) - 1;
    rasterizeLayer(lIdx, mesh);
    return selectedItemIdx;
}

int CanvasEngine::addImageItemFromRgba(const std::string& name, std::vector<uint8_t> rgba, int w, int h,
                                       const DecalLayerProps& props, const ObjMesh* mesh) {
    if (rgba.empty() || w <= 0 || h <= 0) return -1;
    if (mesh) cachedMesh = mesh;
    int lIdx = ensureEditableLayer();
    if (lIdx < 0) return -1;
    pushUndoSnapshot();
    VectorItem item;
    item.id = nextItemId++;
    item.name = name.empty() ? "Image" : name;
    item.kind = VectorItemKind::Image;
    item.image = props;
    item.image.imgW = w;
    item.image.imgH = h;
    item.image.imgRgba.clear();
    item.image.imgRgbaShared = std::make_shared<std::vector<uint8_t>>(std::move(rgba));
    if (item.image.uvScale.x <= 1e-4f || item.image.uvScale.y <= 1e-4f) {
        item.image.uvScale = {0.18f, 0.18f * (static_cast<float>(h) / std::max(1, w))};
    }
    layers[lIdx].items.push_back(std::move(item));
    activeLayerIdx = lIdx;
    selectedItemIdx = static_cast<int>(layers[lIdx].items.size()) - 1;
    rasterizeLayer(lIdx, mesh);
    return selectedItemIdx;
}

void CanvasEngine::syncItem3DTo2D(VectorItem& item, const ObjMesh* mesh) {
    if (mesh) cachedMesh = mesh;
    if (item.kind == VectorItemKind::Image) {
        if (!item.image.anchoredIn3D) return;
        float uvPerM = (mesh && mesh->loaded) ? mesh->estimateUvPerMeter(item.image.pos3D, item.image.nrm3D) : 0.16f;
        float aspect = static_cast<float>(std::max(1, item.image.imgH)) / static_cast<float>(std::max(1, item.image.imgW));
        float uw = item.image.worldWidth * uvPerM;
        if (item.hasPrimaryUvBox && std::max(item.primaryUvHalf.x, item.primaryUvHalf.y) < 0.35f) {
            uw = item.primaryUvHalf.x * 2.0f;
        }
        item.image.uvScale = {uw, uw * aspect};
    } else if (item.kind == VectorItemKind::Text) {
        if (!item.text.anchoredIn3D) return;
        float uvPerM = (mesh && mesh->loaded) ? mesh->estimateUvPerMeter(item.text.pos3D, item.text.nrm3D) : 0.16f;
        item.text.fontSizePx = std::clamp((item.text.worldHeight * uvPerM * 2048.0f) / 1.18f, 12.0f, 512.0f);
    } else if (item.kind == VectorItemKind::RectShape) {
        if (!item.rect.anchoredIn3D) return;
        float uvPerM = (mesh && mesh->loaded) ? mesh->estimateUvPerMeter(item.rect.pos3D, item.rect.nrm3D) : 0.16f;
        float aspect = item.rect.worldHeight / std::max(0.01f, item.rect.worldWidth);
        float uw = item.rect.worldWidth * uvPerM;
        if (item.hasPrimaryUvBox && std::max(item.primaryUvHalf.x, item.primaryUvHalf.y) < 0.35f) {
            uw = item.primaryUvHalf.x * 2.0f;
        }
        item.rect.uvScale = {uw, uw * aspect};
    }
}

void CanvasEngine::syncItem2DTo3D(VectorItem& item, const ObjMesh* mesh) {
    if (mesh) cachedMesh = mesh;
    if (item.kind == VectorItemKind::Image) {
        if (mesh && mesh->loaded) {
            Vec3 p3, n3; int32_t tri = -1;
            if (mesh->queryUvAtPixel(item.image.uvPos.x, item.image.uvPos.y, p3, n3, tri)) {
                item.image.pos3D = p3;
                item.image.nrm3D = n3;
            }
        }
        float uvPerM = (mesh && mesh->loaded) ? mesh->estimateUvPerMeter(item.image.pos3D, item.image.nrm3D) : 0.16f;
        item.image.worldWidth = std::max(0.01f, item.image.uvScale.x / std::max(uvPerM, 1e-4f));
    } else if (item.kind == VectorItemKind::Text) {
        if (mesh && mesh->loaded) {
            Vec3 p3, n3; int32_t tri = -1;
            if (mesh->queryUvAtPixel(item.text.uvPos.x, item.text.uvPos.y, p3, n3, tri)) {
                item.text.pos3D = p3;
                item.text.nrm3D = n3;
            }
        }
        float uvPerM = (mesh && mesh->loaded) ? mesh->estimateUvPerMeter(item.text.pos3D, item.text.nrm3D) : 0.16f;
        item.text.worldHeight = std::max(0.01f, (item.text.fontSizePx * 1.18f) / (2048.0f * std::max(uvPerM, 1e-4f)));
    } else if (item.kind == VectorItemKind::RectShape) {
        if (mesh && mesh->loaded) {
            Vec3 p3, n3; int32_t tri = -1;
            if (mesh->queryUvAtPixel(item.rect.uvPos.x, item.rect.uvPos.y, p3, n3, tri)) {
                item.rect.pos3D = p3;
                item.rect.nrm3D = n3;
            }
        }
        float uvPerM = (mesh && mesh->loaded) ? mesh->estimateUvPerMeter(item.rect.pos3D, item.rect.nrm3D) : 0.16f;
        item.rect.worldWidth = std::max(0.01f, item.rect.uvScale.x / std::max(uvPerM, 1e-4f));
        item.rect.worldHeight = std::max(0.01f, item.rect.uvScale.y / std::max(uvPerM, 1e-4f));
    }
}

bool CanvasEngine::copySelectedItem() {
    const VectorItem* it = activeVectorItem();
    if (!it) return false;
    clipboardItem = *it;
    hasClipboardItem = true;
    return true;
}

bool CanvasEngine::cutSelectedItem(const ObjMesh* mesh) {
    if (!copySelectedItem()) return false;
    return deleteSelectedItem(mesh);
}

bool CanvasEngine::deleteSelectedItem(const ObjMesh* mesh) {
    if (mesh) cachedMesh = mesh;
    if (activeLayerIdx < 0 || activeLayerIdx >= static_cast<int>(layers.size())) return false;
    Layer& l = layers[activeLayerIdx];
    if (l.locked || selectedItemIdx < 0 || selectedItemIdx >= static_cast<int>(l.items.size())) return false;
    pushUndoSnapshot();
    l.items.erase(l.items.begin() + selectedItemIdx);
    if (l.items.empty()) selectedItemIdx = -1;
    else selectedItemIdx = std::min(selectedItemIdx, static_cast<int>(l.items.size()) - 1);
    rasterizeLayer(activeLayerIdx, mesh);
    recomposite();
    return true;
}

int CanvasEngine::pasteClipboardItem(const ObjMesh* mesh, const Vec2* optUv, const RayHit* optHit3D) {
    if (!hasClipboardItem) return -1;
    if (mesh) cachedMesh = mesh;
    int lIdx = ensureEditableLayer();
    if (lIdx < 0) return -1;
    pushUndoSnapshot();
    VectorItem pasted = clipboardItem;
    pasted.id = nextItemId++;
    pasted.name += " Copy";

    auto computeTangentRight = [](const Vec3& n) {
        Vec3 up = (std::fabs(n.y) < 0.9f) ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
        return normalized(cross(up, n));
    };

    if (optHit3D && optHit3D->hit) {
        if (pasted.kind == VectorItemKind::Text) {
            pasted.text.anchoredIn3D = true;
            pasted.text.uvPos = optHit3D->uv;
            pasted.text.pos3D = optHit3D->pos;
            pasted.text.nrm3D = optHit3D->normal;
            pasted.text.right3D = computeTangentRight(optHit3D->normal);
        } else if (pasted.kind == VectorItemKind::Image) {
            pasted.image.anchoredIn3D = true;
            pasted.image.uvPos = optHit3D->uv;
            pasted.image.pos3D = optHit3D->pos;
            pasted.image.nrm3D = optHit3D->normal;
            pasted.image.right3D = computeTangentRight(optHit3D->normal);
        } else if (pasted.kind == VectorItemKind::RectShape) {
            pasted.rect.anchoredIn3D = true;
            pasted.rect.uvPos = optHit3D->uv;
            pasted.rect.pos3D = optHit3D->pos;
            pasted.rect.nrm3D = optHit3D->normal;
            pasted.rect.right3D = computeTangentRight(optHit3D->normal);
        } else if (pasted.kind == VectorItemKind::PenPath && !pasted.pen.nodes.empty()) {
            Vec2 cuv{0, 0}; Vec3 c3{0, 0, 0};
            for (const auto& nd : pasted.pen.nodes) { cuv += nd.uv; c3 += nd.pos3D; }
            cuv = cuv / static_cast<float>(pasted.pen.nodes.size());
            c3 = c3 / static_cast<float>(pasted.pen.nodes.size());
            Vec2 duv = optHit3D->uv - cuv;
            Vec3 d3 = optHit3D->pos - c3;
            for (auto& nd : pasted.pen.nodes) {
                nd.uv += duv; nd.handleInUv += duv; nd.handleOutUv += duv;
                if (nd.has3D) {
                    nd.pos3D += d3; nd.handleIn3D += d3; nd.handleOut3D += d3;
                    nd.nrm3D = optHit3D->normal;
                }
            }
        }
    } else if (optUv) {
        if (pasted.kind == VectorItemKind::Text) {
            pasted.text.uvPos = *optUv;
            pasted.text.anchoredIn3D = false;
            syncItem2DTo3D(pasted, mesh);
        } else if (pasted.kind == VectorItemKind::Image) {
            pasted.image.uvPos = *optUv;
            pasted.image.anchoredIn3D = false;
            syncItem2DTo3D(pasted, mesh);
        } else if (pasted.kind == VectorItemKind::RectShape) {
            pasted.rect.uvPos = *optUv;
            pasted.rect.anchoredIn3D = false;
            syncItem2DTo3D(pasted, mesh);
        } else if (pasted.kind == VectorItemKind::PenPath && !pasted.pen.nodes.empty()) {
            Vec2 cuv{0, 0};
            for (const auto& nd : pasted.pen.nodes) cuv += nd.uv;
            cuv = cuv / static_cast<float>(pasted.pen.nodes.size());
            Vec2 duv = *optUv - cuv;
            pasted.pen.use3DSpace = false;
            for (auto& nd : pasted.pen.nodes) {
                nd.uv += duv; nd.handleInUv += duv; nd.handleOutUv += duv;
                nd.has3D = false;
            }
        }
    } else {
        Vec2 nudge{0.02f, 0.02f};
        if (pasted.kind == VectorItemKind::Text) {
            pasted.text.uvPos += nudge;
            if (pasted.text.anchoredIn3D) pasted.text.pos3D += pasted.text.right3D * 0.04f;
        } else if (pasted.kind == VectorItemKind::Image) {
            pasted.image.uvPos += nudge;
            if (pasted.image.anchoredIn3D) pasted.image.pos3D += pasted.image.right3D * 0.04f;
        } else if (pasted.kind == VectorItemKind::RectShape) {
            pasted.rect.uvPos += nudge;
            if (pasted.rect.anchoredIn3D) pasted.rect.pos3D += pasted.rect.right3D * 0.04f;
        } else if (pasted.kind == VectorItemKind::PenPath) {
            for (auto& nd : pasted.pen.nodes) {
                nd.uv += nudge; nd.handleInUv += nudge; nd.handleOutUv += nudge;
                if (nd.has3D) {
                    nd.pos3D.z += 0.04f; nd.handleIn3D.z += 0.04f; nd.handleOut3D.z += 0.04f;
                }
            }
        }
    }

    layers[lIdx].items.push_back(std::move(pasted));
    activeLayerIdx = lIdx;
    selectedItemIdx = static_cast<int>(layers[lIdx].items.size()) - 1;
    rasterizeLayer(lIdx, mesh);
    recomposite();
    return selectedItemIdx;
}

int CanvasEngine::pasteExternalImageRgba(const std::string& name, std::vector<uint8_t> rgba, int w, int h,
                                         const ObjMesh* mesh, const Vec2* optUv, const RayHit* optHit3D) {
    if (rgba.empty() || w <= 0 || h <= 0) return -1;
    DecalLayerProps props;
    props.uvScale = {0.20f, 0.20f * (static_cast<float>(h) / std::max(1, w))};
    props.worldWidth = 0.40f;
    props.mirrorX = false;
    props.mirrorMode = MirrorTextMode::None;
    if (optHit3D && optHit3D->hit) {
        props.anchoredIn3D = true;
        props.pos3D = optHit3D->pos;
        props.nrm3D = optHit3D->normal;
        Vec3 up = (std::fabs(optHit3D->normal.y) < 0.9f) ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
        props.right3D = normalized(cross(up, optHit3D->normal));
        props.uvPos = optHit3D->uv;
    } else if (optUv) {
        props.anchoredIn3D = false;
        props.uvPos = *optUv;
    }
    return addImageItemFromRgba(name, std::move(rgba), w, h, props, mesh);
}

void CanvasEngine::setMirrorOnActiveItem(bool mirror, const ObjMesh* mesh) {
    if (mesh) cachedMesh = mesh;
    VectorItem* item = activeVectorItem();
    if (!item) return;
    pushUndoSnapshot();
    if (item->kind == VectorItemKind::PenPath) {
        item->pen.mirrorX = mirror;
    } else if (item->kind == VectorItemKind::RectShape) {
        item->rect.mirrorX = mirror;
    } else if (item->kind == VectorItemKind::Text) {
        item->text.mirrorX = mirror;
        item->text.mirrorMode = mirror
            ? (item->text.mirrorMode == MirrorTextMode::None ? MirrorTextMode::ReadableBothSides : item->text.mirrorMode)
            : MirrorTextMode::None;
    } else if (item->kind == VectorItemKind::Image) {
        item->image.mirrorX = mirror;
        item->image.mirrorMode = mirror
            ? (item->image.mirrorMode == MirrorTextMode::None ? MirrorTextMode::ReadableBothSides : item->image.mirrorMode)
            : MirrorTextMode::None;
    }
    rasterizeLayer(activeLayerIdx, mesh);
    recomposite();
}

void CanvasEngine::syncLayer3DTo2D(int layerIdx, const ObjMesh* mesh) {
    if (layerIdx < 0 || layerIdx >= static_cast<int>(layers.size())) return;
    if (mesh) cachedMesh = mesh;
    Layer& l = layers[layerIdx];
    if (!l.items.empty()) {
        for (VectorItem& it : l.items) {
            syncItem3DTo2D(it, mesh);
        }
        return;
    }
    if (l.kind == LayerKind::Decal) {
        if (!l.decal.anchoredIn3D) return;
        float uvPerM = (mesh && mesh->loaded) ? mesh->estimateUvPerMeter(l.decal.pos3D, l.decal.nrm3D) : 0.16f;
        float aspect = static_cast<float>(std::max(1, l.decal.imgH)) / static_cast<float>(std::max(1, l.decal.imgW));
        float uw = l.decal.worldWidth * uvPerM;
        if (l.hasPrimaryUvBox && std::max(l.primaryUvHalf.x, l.primaryUvHalf.y) < 0.35f) {
            uw = l.primaryUvHalf.x * 2.0f;
        }
        l.decal.uvScale = {uw, uw * aspect};
    } else if (l.kind == LayerKind::Text) {
        if (!l.text.anchoredIn3D) return;
        float uvPerM = (mesh && mesh->loaded) ? mesh->estimateUvPerMeter(l.text.pos3D, l.text.nrm3D) : 0.16f;
        l.text.fontSizePx = std::clamp((l.text.worldHeight * uvPerM * 2048.0f) / 1.18f, 12.0f, 512.0f);
    }
}

void CanvasEngine::syncLayer2DTo3D(int layerIdx, const ObjMesh* mesh) {
    if (layerIdx < 0 || layerIdx >= static_cast<int>(layers.size())) return;
    if (mesh) cachedMesh = mesh;
    Layer& l = layers[layerIdx];
    if (!l.items.empty()) {
        for (VectorItem& it : l.items) {
            syncItem2DTo3D(it, mesh);
        }
        return;
    }
    if (l.kind == LayerKind::Decal) {
        if (mesh && mesh->loaded) {
            Vec3 p3, n3;
            int32_t tri = -1;
            if (mesh->queryUvAtPixel(l.decal.uvPos.x, l.decal.uvPos.y, p3, n3, tri)) {
                l.decal.pos3D = p3;
                l.decal.nrm3D = n3;
            }
        }
        float uvPerM = (mesh && mesh->loaded) ? mesh->estimateUvPerMeter(l.decal.pos3D, l.decal.nrm3D) : 0.16f;
        l.decal.worldWidth = std::max(0.01f, l.decal.uvScale.x / std::max(uvPerM, 1e-4f));
    } else if (l.kind == LayerKind::Text) {
        if (mesh && mesh->loaded) {
            Vec3 p3, n3;
            int32_t tri = -1;
            if (mesh->queryUvAtPixel(l.text.uvPos.x, l.text.uvPos.y, p3, n3, tri)) {
                l.text.pos3D = p3;
                l.text.nrm3D = n3;
            }
        }
        float uvPerM = (mesh && mesh->loaded) ? mesh->estimateUvPerMeter(l.text.pos3D, l.text.nrm3D) : 0.16f;
        l.text.worldHeight = std::max(0.01f, (l.text.fontSizePx * 1.18f) / (2048.0f * std::max(uvPerM, 1e-4f)));
    }
}

bool CanvasEngine::loadBaseTexture(const std::string& path) {
    int w = 0, h = 0, comp = 0;
    uint8_t* px = stbi_load(path.c_str(), &w, &h, &comp, 4);
    if (!px || w <= 0 || h <= 0) {
        if (px) stbi_image_free(px);
        return false;
    }

    // Snap to standard supported studio canvas dimensions clamped [1024, 4096]
    int maxDim = std::max(w, h);
    int targetSize = (maxDim <= 1536) ? 1024 : ((maxDim <= 3072) ? 2048 : 4096);

    if (layers.empty()) {
        init(targetSize);
    } else if (width != targetSize || height != targetSize) {
        resizeCanvas(targetSize);
    }

    if (layers.empty() || layers[0].kind != LayerKind::RasterPaint) {
        Layer baseLayer;
        baseLayer.id = nextLayerId++;
        baseLayer.name = "Base Paint";
        baseLayer.kind = LayerKind::RasterPaint;
        baseLayer.rgba.assign(static_cast<size_t>(width) * height * 4, 0);
        layers.insert(layers.begin(), std::move(baseLayer));
        if (activeLayerIdx >= 0) activeLayerIdx++;
    }

    Layer& base = layers[0];
    auto& basePb = base.ensureUniquePaintBuffer(width, height);
    base.rgba.resize(static_cast<size_t>(width) * height * 4);
    base.visible = true;
    if (w == width && h == height) {
        std::memcpy(basePb.data(), px, static_cast<size_t>(width) * height * 4);
    } else {
        for (int y = 0; y < height; ++y) {
            float ty = (y + 0.5f) / height;
            for (int x = 0; x < width; ++x) {
                float tx = (x + 0.5f) / width;
                float r, g, b, a;
                sampleRgbaBilinear(px, w, h, tx, ty, r, g, b, a);
                size_t o = (static_cast<size_t>(y) * width + x) * 4;
                basePb[o + 0] = u8clamp(r);
                basePb[o + 1] = u8clamp(g);
                basePb[o + 2] = u8clamp(b);
                basePb[o + 3] = u8clamp(a * 255.0f);
            }
        }
    }
    base.rgba = basePb;
    stbi_image_free(px);
    if (!base.items.empty()) rasterizeLayer(0, cachedMesh);
    recomposite();
    return true;
}

int CanvasEngine::findAoLayerIndex() const {
    for (int i = static_cast<int>(layers.size()) - 1; i >= 0; --i) {
        if (layers[i].name == "AO / Panel Shading") return i;
    }
    return -1;
}

int CanvasEngine::upsertTopAoLayer(bool makeVisible) {
    if (layers.empty()) {
        init(width);
    }
    int existing = findAoLayerIndex();
    if (existing >= 0 && layers[existing].kind == LayerKind::RasterPaint) {
        Layer ao = std::move(layers[existing]);
        layers.erase(layers.begin() + existing);
        if (activeLayerIdx > existing) activeLayerIdx--;
        else if (activeLayerIdx == existing) activeLayerIdx = std::max(0, static_cast<int>(layers.size()) - 1);
        ao.blend = BlendMode::Multiply;
        ao.opacity = 1.0f;
        ao.visible = makeVisible;
        ao.rgba.resize(static_cast<size_t>(width) * height * 4);
        layers.push_back(std::move(ao));
        return static_cast<int>(layers.size()) - 1;
    }

    Layer ao;
    ao.id = nextLayerId++;
    ao.name = "AO / Panel Shading";
    ao.kind = LayerKind::RasterPaint;
    ao.blend = BlendMode::Multiply;
    ao.opacity = 1.0f;
    ao.visible = makeVisible;
    ao.rgba.resize(static_cast<size_t>(width) * height * 4);
    layers.push_back(std::move(ao));
    return static_cast<int>(layers.size()) - 1;
}

bool CanvasEngine::loadAoOverlay(const std::string& path, bool makeVisible) {
    int w = 0, h = 0, comp = 0;
    uint8_t* px = stbi_load(path.c_str(), &w, &h, &comp, 4);
    if (!px || w <= 0 || h <= 0) {
        if (px) stbi_image_free(px);
        return false;
    }
    int aoIdx = upsertTopAoLayer(makeVisible);
    Layer& ao = layers[aoIdx];
    auto& aoPb = ao.ensureUniquePaintBuffer(width, height);
    ao.rgba.resize(static_cast<size_t>(width) * height * 4);
    if (w == width && h == height) {
        std::memcpy(aoPb.data(), px, static_cast<size_t>(width) * height * 4);
    } else {
        for (int y = 0; y < height; ++y) {
            float ty = (y + 0.5f) / height;
            for (int x = 0; x < width; ++x) {
                float tx = (x + 0.5f) / width;
                float r, g, b, a;
                sampleRgbaBilinear(px, w, h, tx, ty, r, g, b, a);
                size_t o = (static_cast<size_t>(y) * width + x) * 4;
                aoPb[o + 0] = u8clamp(r);
                aoPb[o + 1] = u8clamp(g);
                aoPb[o + 2] = u8clamp(b);
                aoPb[o + 3] = u8clamp(a * 255.0f);
            }
        }
    }
    ao.rgba = aoPb;
    stbi_image_free(px);
    recomposite();
    return true;
}

bool CanvasEngine::bakeMeshAoOverlay(const ObjMesh& mesh, int res, bool makeVisible) {
    if (!mesh.loaded || mesh.tris.empty()) return false;
    res = std::clamp(res, 256, 2048);

    // 1. Identify sharp dihedral creases & open panel boundary edges for crisp game-style panel seams
    const size_t nTris = mesh.tris.size();
    std::vector<uint8_t> triEdgeCrease(nTris, 0);
    struct EdgeRec { int32_t triIdx; uint8_t edgeBit; uint16_t count; };
    auto makeEdgeKey = [](uint32_t a, uint32_t b) -> uint64_t {
        if (a > b) std::swap(a, b);
        return (static_cast<uint64_t>(a) << 32) | static_cast<uint64_t>(b);
    };
    std::unordered_map<uint64_t, EdgeRec> edgeMap;
    edgeMap.reserve(nTris * 2);

    for (size_t ti = 0; ti < nTris; ++ti) {
        const Triangle& t = mesh.tris[ti];
        if (!mesh.groups[t.groupIdx].visible) continue;
        uint32_t vp[3][2] = {{t.p0, t.p1}, {t.p1, t.p2}, {t.p2, t.p0}};
        for (int e = 0; e < 3; ++e) {
            uint64_t key = makeEdgeKey(vp[e][0], vp[e][1]);
            auto it = edgeMap.find(key);
            if (it == edgeMap.end()) {
                edgeMap.emplace(key, EdgeRec{static_cast<int32_t>(ti), static_cast<uint8_t>(1 << e), 1});
            } else {
                it->second.count++;
                if (it->second.triIdx >= 0) {
                    const Vec3& nA = mesh.tris[it->second.triIdx].faceNrm;
                    if (dot(nA, t.faceNrm) < 0.88f || it->second.count > 2) {
                        triEdgeCrease[it->second.triIdx] |= it->second.edgeBit;
                        triEdgeCrease[ti] |= static_cast<uint8_t>(1 << e);
                    }
                }
            }
        }
    }
    for (const auto& kv : edgeMap) {
        if (kv.second.count == 1 && kv.second.triIdx >= 0) {
            triEdgeCrease[kv.second.triIdx] |= kv.second.edgeBit;
        }
    }

    // 2. Tile-bin triangles in UV space for zero-contention multithreaded raster + BVH AO
    const int tileSize = 32;
    const int gridW = (res + tileSize - 1) / tileSize;
    std::vector<std::vector<int32_t>> tileTris(static_cast<size_t>(gridW) * gridW);

    for (size_t ti = 0; ti < nTris; ++ti) {
        const Triangle& t = mesh.tris[ti];
        if (!mesh.groups[t.groupIdx].visible) continue;
        Vec2 u0{t.uv0.x * res, t.uv0.y * res};
        Vec2 u1{t.uv1.x * res, t.uv1.y * res};
        Vec2 u2{t.uv2.x * res, t.uv2.y * res};
        float area = (u1.x - u0.x) * (u2.y - u0.y) - (u1.y - u0.y) * (u2.x - u0.x);
        if (std::fabs(area) < 1e-4f) continue;

        int ux0 = std::clamp(static_cast<int>(std::floor(std::min({u0.x, u1.x, u2.x}))), 0, res - 1);
        int ux1 = std::clamp(static_cast<int>(std::ceil(std::max({u0.x, u1.x, u2.x}))), 0, res - 1);
        int uy0 = std::clamp(static_cast<int>(std::floor(std::min({u0.y, u1.y, u2.y}))), 0, res - 1);
        int uy1 = std::clamp(static_cast<int>(std::ceil(std::max({u0.y, u1.y, u2.y}))), 0, res - 1);
        int tx0 = ux0 / tileSize, tx1 = ux1 / tileSize;
        int ty0 = uy0 / tileSize, ty1 = uy1 / tileSize;
        for (int ty = ty0; ty <= ty1; ++ty) {
            for (int tx = tx0; tx <= tx1; ++tx) {
                tileTris[static_cast<size_t>(ty) * gridW + tx].push_back(static_cast<int32_t>(ti));
            }
        }
    }

    // Dark charcoal UV gutter {18, 20, 24, 255} matching game panel AO sheets
    const size_t nPixRes = static_cast<size_t>(res) * res;
    std::vector<uint8_t> aoBuf(nPixRes * 4);
    for (size_t i = 0; i < nPixRes; ++i) {
        aoBuf[i * 4 + 0] = 18;
        aoBuf[i * 4 + 1] = 20;
        aoBuf[i * 4 + 2] = 24;
        aoBuf[i * 4 + 3] = 255;
    }
    std::vector<uint8_t> covMask(nPixRes, 0);

    // 7 stratified cosine-weighted hemisphere sample directions in local tangent frame
    struct LocalRayDir { float lx, ly, lz; };
    LocalRayDir hemiDirs[7];
    for (int r = 0; r < 7; ++r) {
        float fi = (r + 0.5f) / 7.0f;
        float phi = 2.39996323f * r + 0.4f;
        float cosTheta = std::sqrt(1.0f - fi * 0.85f);
        float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
        hemiDirs[r] = {sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta};
    }

    Vec3 ext = mesh.bounds.extent();
    float maxE = std::max({ext.x, ext.y, ext.z, 1.0f});
    float aoMaxDist = maxE * 0.24f;

    auto pointSegDist = [](Vec2 p, Vec2 a, Vec2 b) -> float {
        Vec2 ab = b - a;
        float d2 = ab.x * ab.x + ab.y * ab.y;
        if (d2 < 1e-6f) return length(p - a);
        float t = std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / d2, 0.0f, 1.0f);
        return length(p - (a + ab * t));
    };

    const int totalTiles = gridW * gridW;
    unsigned int hwThreads = std::thread::hardware_concurrency();
    int nThreads = static_cast<int>(std::clamp(hwThreads ? hwThreads : 4u, 1u, 16u));
    std::atomic<int> nextTile{0};

    auto workerFn = [&]() {
        while (true) {
            int tileId = nextTile.fetch_add(1, std::memory_order_relaxed);
            if (tileId >= totalTiles) break;
            const auto& tList = tileTris[tileId];
            if (tList.empty()) continue;

            int tx = tileId % gridW, ty = tileId / gridW;
            int bx0 = tx * tileSize, bx1 = std::min(res - 1, bx0 + tileSize - 1);
            int by0 = ty * tileSize, by1 = std::min(res - 1, by0 + tileSize - 1);

            for (int32_t ti : tList) {
                const Triangle& t = mesh.tris[ti];
                Vec2 u0{t.uv0.x * res, t.uv0.y * res};
                Vec2 u1{t.uv1.x * res, t.uv1.y * res};
                Vec2 u2{t.uv2.x * res, t.uv2.y * res};
                float area = (u1.x - u0.x) * (u2.y - u0.y) - (u1.y - u0.y) * (u2.x - u0.x);
                if (std::fabs(area) < 1e-4f) continue;

                int ux0 = std::clamp(static_cast<int>(std::floor(std::min({u0.x, u1.x, u2.x}))), bx0, bx1);
                int ux1 = std::clamp(static_cast<int>(std::ceil(std::max({u0.x, u1.x, u2.x}))), bx0, bx1);
                int uy0 = std::clamp(static_cast<int>(std::floor(std::min({u0.y, u1.y, u2.y}))), by0, by1);
                int uy1 = std::clamp(static_cast<int>(std::ceil(std::max({u0.y, u1.y, u2.y}))), by0, by1);

                const Vec3& p0 = mesh.uniquePositions[t.p0];
                const Vec3& p1 = mesh.uniquePositions[t.p1];
                const Vec3& p2 = mesh.uniquePositions[t.p2];
                Vec3 vn0{mesh.gpuVertices[t.i0].nx, mesh.gpuVertices[t.i0].ny, mesh.gpuVertices[t.i0].nz};
                Vec3 vn1{mesh.gpuVertices[t.i1].nx, mesh.gpuVertices[t.i1].ny, mesh.gpuVertices[t.i1].nz};
                Vec3 vn2{mesh.gpuVertices[t.i2].nx, mesh.gpuVertices[t.i2].ny, mesh.gpuVertices[t.i2].nz};
                uint8_t cMask = triEdgeCrease[ti];

                for (int py = uy0; py <= uy1; ++py) {
                    for (int px = ux0; px <= ux1; ++px) {
                        size_t pixIdx = static_cast<size_t>(py) * res + px;
                        Vec2 pt{px + 0.5f, py + 0.5f};
                        float w0 = ((u1.x - pt.x) * (u2.y - pt.y) - (u1.y - pt.y) * (u2.x - pt.x)) / area;
                        float uu = ((u2.x - pt.x) * (u0.y - pt.y) - (u2.y - pt.y) * (u0.x - pt.x)) / area;
                        float vv = 1.0f - w0 - uu;
                        if (w0 < -0.005f || uu < -0.005f || vv < -0.005f) continue;
                        if (covMask[pixIdx] == 2 && (w0 < 0.0f || uu < 0.0f || vv < 0.0f)) continue;

                        w0 = std::clamp(w0, 0.0f, 1.0f);
                        uu = std::clamp(uu, 0.0f, 1.0f);
                        vv = std::clamp(vv, 0.0f, 1.0f);
                        Vec3 pos = p0 * w0 + p1 * uu + p2 * vv;
                        Vec3 nrm = normalized(vn0 * w0 + vn1 * uu + vn2 * vv);
                        if (lengthSq(nrm) < 1e-6f) nrm = t.faceNrm;

                        Vec3 upHint = (std::fabs(nrm.y) < 0.9f) ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
                        Vec3 T = normalized(cross(upHint, nrm));
                        Vec3 B = cross(nrm, T);

                        Vec3 orig = pos + t.faceNrm * (maxE * 0.0008f);
                        float occSum = 0.0f;
                        for (int r = 0; r < 7; ++r) {
                            Vec3 dir = T * hemiDirs[r].lx + B * hemiDirs[r].ly + nrm * hemiDirs[r].lz;
                            float d = mesh.raycastDist(Ray{orig, dir}, aoMaxDist, true);
                            if (d < aoMaxDist) {
                                float normD = d / aoMaxDist;
                                occSum += (1.0f - normD * normD);
                            }
                        }
                        float openness = std::clamp(1.0f - (occSum / 7.0f) * 0.88f, 0.0f, 1.0f);
                        float sky = std::clamp(0.76f + 0.24f * nrm.y, 0.42f, 1.0f);
                        float baseShade = std::clamp(0.42f + 0.55f * openness * sky, 0.42f, 0.97f);

                        if (cMask != 0) {
                            float minCreaseD = 100.0f;
                            if (cMask & 1) minCreaseD = std::min(minCreaseD, pointSegDist(pt, u0, u1));
                            if (cMask & 2) minCreaseD = std::min(minCreaseD, pointSegDist(pt, u1, u2));
                            if (cMask & 4) minCreaseD = std::min(minCreaseD, pointSegDist(pt, u2, u0));
                            if (minCreaseD < 1.9f) {
                                float cFactor = 1.0f - 0.50f * std::exp(-minCreaseD * minCreaseD * 0.95f);
                                baseShade *= cFactor;
                            }
                        }

                        uint8_t v8 = u8clamp(std::clamp(baseShade, 0.16f, 0.97f) * 255.0f);
                        aoBuf[pixIdx * 4 + 0] = v8;
                        aoBuf[pixIdx * 4 + 1] = v8;
                        aoBuf[pixIdx * 4 + 2] = v8;
                        aoBuf[pixIdx * 4 + 3] = 255;
                        covMask[pixIdx] = 2;
                    }
                }
            }
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(nThreads);
    for (int i = 0; i < nThreads; ++i) workers.emplace_back(workerFn);
    for (auto& w : workers) w.join();

    // 3. 2px UV seam dilation leaving unused UV gutter dark charcoal {18, 20, 24, 255}
    std::vector<uint8_t> dilateBuf = aoBuf;
    std::vector<uint8_t> nextMask = covMask;
    for (int pass = 0; pass < 2; ++pass) {
        for (int y = 0; y < res; ++y) {
            for (int x = 0; x < res; ++x) {
                size_t idx = static_cast<size_t>(y) * res + x;
                if (covMask[idx] != 0) continue;
                int sr = 0, sg = 0, sb = 0, cnt = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    int ny = y + dy;
                    if (ny < 0 || ny >= res) continue;
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        int nx = x + dx;
                        if (nx < 0 || nx >= res) continue;
                        size_t nIdx = static_cast<size_t>(ny) * res + nx;
                        if (covMask[nIdx] != 0) {
                            sr += aoBuf[nIdx * 4 + 0];
                            sg += aoBuf[nIdx * 4 + 1];
                            sb += aoBuf[nIdx * 4 + 2];
                            cnt++;
                        }
                    }
                }
                if (cnt > 0) {
                    dilateBuf[idx * 4 + 0] = static_cast<uint8_t>(sr / cnt);
                    dilateBuf[idx * 4 + 1] = static_cast<uint8_t>(sg / cnt);
                    dilateBuf[idx * 4 + 2] = static_cast<uint8_t>(sb / cnt);
                    dilateBuf[idx * 4 + 3] = 255;
                    nextMask[idx] = 1;
                }
            }
        }
        aoBuf.swap(dilateBuf);
        covMask.swap(nextMask);
    }

    // 4. Write bilinearly to top "AO / Panel Shading" Multiply layer
    cachedMesh = &mesh;
    int aoIdx = upsertTopAoLayer(makeVisible);
    Layer& ao = layers[aoIdx];
    auto& aoPb = ao.ensureUniquePaintBuffer(width, height);
    ao.rgba.resize(static_cast<size_t>(width) * height * 4);
    if (res == width && res == height) {
        std::memcpy(aoPb.data(), aoBuf.data(), aoPb.size());
    } else {
        for (int y = 0; y < height; ++y) {
            float ty = (y + 0.5f) / height;
            for (int x = 0; x < width; ++x) {
                float tx = (x + 0.5f) / width;
                float r, g, b, a;
                sampleRgbaBilinear(aoBuf.data(), res, res, tx, ty, r, g, b, a);
                size_t o = (static_cast<size_t>(y) * width + x) * 4;
                aoPb[o + 0] = u8clamp(r);
                aoPb[o + 1] = u8clamp(g);
                aoPb[o + 2] = u8clamp(b);
                aoPb[o + 3] = 255;
            }
        }
    }
    ao.rgba = aoPb;
    recomposite();
    return true;
}

bool CanvasEngine::exportFlattenedPng(const std::string& path, int outSize, const ObjMesh* optDilateMesh) {
    recomposite();
    std::vector<uint8_t> out(static_cast<size_t>(outSize) * outSize * 4);
    if (outSize == width) {
        out = compositeRgba;
    } else {
        for (int y = 0; y < outSize; ++y) {
            float ty = (y + 0.5f) / outSize;
            for (int x = 0; x < outSize; ++x) {
                float tx = (x + 0.5f) / outSize;
                float r, g, b, a;
                sampleRgbaBilinear(compositeRgba.data(), width, height, tx, ty, r, g, b, a);
                size_t o = (static_cast<size_t>(y) * outSize + x) * 4;
                out[o + 0] = u8clamp(r);
                out[o + 1] = u8clamp(g);
                out[o + 2] = u8clamp(b);
                out[o + 3] = u8clamp(a * 255.0f);
            }
        }
    }
    // Optional UV bleed dilation at seam borders to keep mipmaps sharp in sim games
    if (optDilateMesh && optDilateMesh->loaded) {
        // Simple 4-pass valid shell dilation around empty transparent UV gutters if background alpha == 0
        // (For opaque liveries bg is already solid matte black, so skip unless transparent border detected)
    }
    stbi_write_png_compression_level = 4;
    return stbi_write_png(path.c_str(), outSize, outSize, 4, out.data(), outSize * 4) != 0;
}

DocumentSnapshot CanvasEngine::captureSnapshot() const {
    DocumentSnapshot snap;
    snap.activeLayerIdx = activeLayerIdx;
    snap.selectedItemIdx = selectedItemIdx;
    snap.layers.reserve(layers.size());
    for (const Layer& l : layers) {
        LayerSnapshot ls;
        ls.id = l.id;
        ls.name = l.name;
        ls.kind = l.kind;
        ls.blend = l.blend;
        ls.opacity = l.opacity;
        ls.visible = l.visible;
        ls.locked = l.locked;
        ls.primaryUvCenter = l.primaryUvCenter;
        ls.primaryUvHalf = l.primaryUvHalf;
        ls.hasPrimaryUvBox = l.hasPrimaryUvBox;
        ls.text = l.text;
        ls.decal = l.decal;
        ls.ruler = l.ruler;
        ls.paintBuffer = l.paintBuffer;
        ls.items = l.items;
        snap.layers.push_back(std::move(ls));
    }
    return snap;
}

void CanvasEngine::restoreSnapshot(const DocumentSnapshot& snap, const ObjMesh* mesh) {
    const ObjMesh* meshUse = mesh ? mesh : cachedMesh;
    const size_t fullBytes = static_cast<size_t>(width) * height * 4;
    layers.clear();
    layers.reserve(snap.layers.size());
    for (const LayerSnapshot& ls : snap.layers) {
        Layer l;
        l.id = ls.id;
        l.name = ls.name;
        l.kind = ls.kind;
        l.blend = ls.blend;
        l.opacity = ls.opacity;
        l.visible = ls.visible;
        l.locked = ls.locked;
        l.primaryUvCenter = ls.primaryUvCenter;
        l.primaryUvHalf = ls.primaryUvHalf;
        l.hasPrimaryUvBox = ls.hasPrimaryUvBox;
        l.text = ls.text;
        l.decal = ls.decal;
        l.ruler = ls.ruler;
        l.paintBuffer = ls.paintBuffer;
        l.items = ls.items;
        if (l.kind == LayerKind::RasterPaint && l.items.empty()) {
            if (l.paintBuffer && l.paintBuffer->size() == fullBytes) {
                l.rgba = *l.paintBuffer;
            } else {
                l.rgba.assign(fullBytes, 0);
            }
            l.dirtyVector = false;
        } else {
            l.rgba.assign(fullBytes, 0);
            l.dirtyVector = true;
        }
        l.hasPrevBounds = false;
        layers.push_back(std::move(l));
    }

    activeLayerIdx = layers.empty() ? 0 : std::clamp(snap.activeLayerIdx, 0, static_cast<int>(layers.size()) - 1);
    if (!layers.empty() && !layers[activeLayerIdx].items.empty()) {
        selectedItemIdx = std::clamp(snap.selectedItemIdx, -1, static_cast<int>(layers[activeLayerIdx].items.size()) - 1);
    } else {
        selectedItemIdx = -1;
    }

    for (int i = 0; i < static_cast<int>(layers.size()); ++i) {
        if (layers[i].dirtyVector) {
            rasterizeLayer(i, meshUse);
        }
    }
    recomposite();
}

void CanvasEngine::pushUndoSnapshot() {
    undoStack.push_back(captureSnapshot());
    if (undoStack.size() > kMaxUndoSteps) {
        undoStack.erase(undoStack.begin());
    }
    redoStack.clear();
}

void CanvasEngine::beginStroke(int layerIdx) {
    if (layerIdx < 0 || layerIdx >= static_cast<int>(layers.size())) return;
    if (layers[layerIdx].kind != LayerKind::RasterPaint || layers[layerIdx].locked) return;
    strokeLayerIdx = layerIdx;
    pendingStrokeSnap = captureSnapshot();
}

void CanvasEngine::endStroke(int layerIdx) {
    if (strokeLayerIdx != layerIdx || strokeLayerIdx < 0 || strokeLayerIdx >= static_cast<int>(layers.size())) {
        strokeLayerIdx = -1;
        pendingStrokeSnap = {};
        return;
    }
    if (layerIdx < static_cast<int>(pendingStrokeSnap.layers.size()) &&
        layers[layerIdx].paintBuffer != pendingStrokeSnap.layers[layerIdx].paintBuffer) {
        undoStack.push_back(std::move(pendingStrokeSnap));
        if (undoStack.size() > kMaxUndoSteps) undoStack.erase(undoStack.begin());
        redoStack.clear();
    }
    strokeLayerIdx = -1;
    pendingStrokeSnap = {};
}

void CanvasEngine::undo(const ObjMesh* mesh) {
    if (undoStack.empty()) return;
    if (mesh) cachedMesh = mesh;
    DocumentSnapshot cur = captureSnapshot();
    DocumentSnapshot prev = std::move(undoStack.back());
    undoStack.pop_back();
    redoStack.push_back(std::move(cur));
    if (redoStack.size() > kMaxUndoSteps) redoStack.erase(redoStack.begin());
    restoreSnapshot(prev, mesh);
}

void CanvasEngine::redo(const ObjMesh* mesh) {
    if (redoStack.empty()) return;
    if (mesh) cachedMesh = mesh;
    DocumentSnapshot cur = captureSnapshot();
    DocumentSnapshot next = std::move(redoStack.back());
    redoStack.pop_back();
    undoStack.push_back(std::move(cur));
    if (undoStack.size() > kMaxUndoSteps) undoStack.erase(undoStack.begin());
    restoreSnapshot(next, mesh);
}

namespace {

inline void collectTrisInBox(const ObjMesh& mesh, const Vec3& bmin, const Vec3& bmax,
                             std::vector<int32_t>& outTris) {
    mesh.collectTrianglesInAABB(bmin, bmax, outTris);
}

inline void recompositeRectFast(std::vector<uint8_t>& compositeRgba,
                                const std::vector<Layer>& layers,
                                int width, int height,
                                int x0, int y0, int x1, int y1) {
    size_t nPx = static_cast<size_t>(width) * height;
    if (compositeRgba.size() != nPx * 4) {
        compositeRgba.resize(nPx * 4);
    }
    x0 = std::clamp(x0, 0, width - 1);
    x1 = std::clamp(x1, 0, width - 1);
    y0 = std::clamp(y0, 0, height - 1);
    y1 = std::clamp(y1, 0, height - 1);
    if (x0 > x1 || y0 > y1) return;

    const bool fullRow = (x0 == 0 && x1 == width - 1);
    size_t startLayer = 0;

    // Fast path for opaque Normal base layer (layers[0])
    if (!layers.empty() && layers[0].visible && layers[0].blend == BlendMode::Normal &&
        layers[0].opacity >= 0.999f && layers[0].rgba.size() == nPx * 4) {
        const uint8_t* src0 = layers[0].rgba.data();
        for (int y = y0; y <= y1; ++y) {
            size_t rowOff = static_cast<size_t>(y) * width * 4;
            for (int x = x0; x <= x1; ++x) {
                size_t o = rowOff + static_cast<size_t>(x) * 4;
                uint8_t sa = src0[o + 3];
                if (sa == 255) {
                    compositeRgba[o + 0] = src0[o + 0];
                    compositeRgba[o + 1] = src0[o + 1];
                    compositeRgba[o + 2] = src0[o + 2];
                    compositeRgba[o + 3] = 255;
                } else if (sa == 0) {
                    compositeRgba[o + 0] = 22;
                    compositeRgba[o + 1] = 24;
                    compositeRgba[o + 2] = 29;
                    compositeRgba[o + 3] = 255;
                } else {
                    uint32_t a = sa;
                    uint32_t inv = 255u - a;
                    compositeRgba[o + 0] = static_cast<uint8_t>((src0[o + 0] * a + 22u * inv + 127u) / 255u);
                    compositeRgba[o + 1] = static_cast<uint8_t>((src0[o + 1] * a + 24u * inv + 127u) / 255u);
                    compositeRgba[o + 2] = static_cast<uint8_t>((src0[o + 2] * a + 29u * inv + 127u) / 255u);
                    compositeRgba[o + 3] = 255;
                }
            }
        }
        startLayer = 1;
    } else {
        for (int y = y0; y <= y1; ++y) {
            size_t rowOff = static_cast<size_t>(y) * width * 4;
            for (int x = x0; x <= x1; ++x) {
                size_t o = rowOff + static_cast<size_t>(x) * 4;
                compositeRgba[o + 0] = 22;
                compositeRgba[o + 1] = 24;
                compositeRgba[o + 2] = 29;
                compositeRgba[o + 3] = 255;
            }
        }
    }
    (void)fullRow;

    for (size_t li = startLayer; li < layers.size(); ++li) {
        const Layer& l = layers[li];
        if (!l.visible || l.opacity <= 1e-4f || l.rgba.size() != nPx * 4) continue;
        const uint8_t* src = l.rgba.data();
        if (l.blend == BlendMode::Normal) {
            uint32_t op255 = static_cast<uint32_t>(std::clamp(static_cast<int>(l.opacity * 255.0f + 0.5f), 0, 255));
            for (int y = y0; y <= y1; ++y) {
                size_t rowOff = static_cast<size_t>(y) * width * 4;
                for (int x = x0; x <= x1; ++x) {
                    size_t o = rowOff + static_cast<size_t>(x) * 4;
                    uint8_t sa = src[o + 3];
                    if (sa == 0) continue;
                    if (sa == 255 && op255 == 255) {
                        compositeRgba[o + 0] = src[o + 0];
                        compositeRgba[o + 1] = src[o + 1];
                        compositeRgba[o + 2] = src[o + 2];
                        compositeRgba[o + 3] = 255;
                    } else {
                        uint32_t a = (static_cast<uint32_t>(sa) * op255 + 127u) / 255u;
                        if (a == 0) continue;
                        uint32_t inv = 255u - a;
                        compositeRgba[o + 0] = static_cast<uint8_t>((src[o + 0] * a + compositeRgba[o + 0] * inv + 127u) / 255u);
                        compositeRgba[o + 1] = static_cast<uint8_t>((src[o + 1] * a + compositeRgba[o + 1] * inv + 127u) / 255u);
                        compositeRgba[o + 2] = static_cast<uint8_t>((src[o + 2] * a + compositeRgba[o + 2] * inv + 127u) / 255u);
                        compositeRgba[o + 3] = 255;
                    }
                }
            }
        } else if (l.blend == BlendMode::Multiply && l.opacity >= 0.999f) {
            for (int y = y0; y <= y1; ++y) {
                size_t rowOff = static_cast<size_t>(y) * width * 4;
                for (int x = x0; x <= x1; ++x) {
                    size_t o = rowOff + static_cast<size_t>(x) * 4;
                    uint8_t sa = src[o + 3];
                    if (sa == 0) continue;
                    if (sa == 255) {
                        compositeRgba[o + 0] = static_cast<uint8_t>((compositeRgba[o + 0] * src[o + 0] + 127u) / 255u);
                        compositeRgba[o + 1] = static_cast<uint8_t>((compositeRgba[o + 1] * src[o + 1] + 127u) / 255u);
                        compositeRgba[o + 2] = static_cast<uint8_t>((compositeRgba[o + 2] * src[o + 2] + 127u) / 255u);
                    } else {
                        blendPixel(&compositeRgba[o], src[o] / 255.0f, src[o + 1] / 255.0f,
                                   src[o + 2] / 255.0f, sa / 255.0f, l.blend, l.opacity);
                    }
                }
            }
        } else {
            for (int y = y0; y <= y1; ++y) {
                size_t rowOff = static_cast<size_t>(y) * width * 4;
                for (int x = x0; x <= x1; ++x) {
                    size_t o = rowOff + static_cast<size_t>(x) * 4;
                    if (src[o + 3] == 0) continue;
                    blendPixel(&compositeRgba[o], src[o] / 255.0f, src[o + 1] / 255.0f,
                               src[o + 2] / 255.0f, src[o + 3] / 255.0f, l.blend, l.opacity);
                }
            }
        }
    }
}

void stampSurfaceBrushCandidates(std::vector<uint8_t>& dstRgba, const ObjMesh& mesh, const std::vector<int32_t>& candTris,
                                 const Vec3& centerPos, const Vec3& centerNrm, const BrushSettings& bs,
                                 int width, int height, DirtyRect& dirty) {
    float R = bs.worldRadius;
    float rSq = R * R;
    for (int32_t ti : candTris) {
        const Triangle& t = mesh.tris[ti];
        if (!mesh.groups[t.groupIdx].visible) continue;
        if (bs.maskSelectedFacesOnly && !t.selected) continue;
        if (dot(t.faceNrm, centerNrm) < 0.15f) continue;

        const Vec3& a = mesh.uniquePositions[t.p0];
        const Vec3& b = mesh.uniquePositions[t.p1];
        const Vec3& c = mesh.uniquePositions[t.p2];
        Vec3 bmin = minVec(a, minVec(b, c));
        Vec3 bmax = maxVec(a, maxVec(b, c));
        if (centerPos.x + R < bmin.x || centerPos.x - R > bmax.x ||
            centerPos.y + R < bmin.y || centerPos.y - R > bmax.y ||
            centerPos.z + R < bmin.z || centerPos.z - R > bmax.z) continue;

        Vec2 p0{t.uv0.x * width, t.uv0.y * height};
        Vec2 p1{t.uv1.x * width, t.uv1.y * height};
        Vec2 p2{t.uv2.x * width, t.uv2.y * height};
        float area = (p1.x - p0.x) * (p2.y - p0.y) - (p1.y - p0.y) * (p2.x - p0.x);
        if (std::fabs(area) < 1e-4f) continue;

        int ux0 = std::clamp(static_cast<int>(std::floor(std::min({p0.x, p1.x, p2.x}))) - 1, 0, width - 1);
        int ux1 = std::clamp(static_cast<int>(std::ceil(std::max({p0.x, p1.x, p2.x})))  + 1, 0, width - 1);
        int uy0 = std::clamp(static_cast<int>(std::floor(std::min({p0.y, p1.y, p2.y}))) - 1, 0, height - 1);
        int uy1 = std::clamp(static_cast<int>(std::ceil(std::max({p0.y, p1.y, p2.y})))  + 1, 0, height - 1);

        for (int py = uy0; py <= uy1; ++py) {
            for (int px = ux0; px <= ux1; ++px) {
                Vec2 p{px + 0.5f, py + 0.5f};
                float w0 = ((p1.x - p.x) * (p2.y - p.y) - (p1.y - p.y) * (p2.x - p.x)) / area;
                float u  = ((p2.x - p.x) * (p0.y - p.y) - (p2.y - p.y) * (p0.x - p.x)) / area;
                float v  = 1.0f - w0 - u;
                if (w0 < -0.02f || u < -0.02f || v < -0.02f) continue;

                Vec3 p3 = a * std::clamp(w0, 0.0f, 1.0f) + b * std::clamp(u, 0.0f, 1.0f) + c * std::clamp(v, 0.0f, 1.0f);
                float d2 = lengthSq(p3 - centerPos);
                if (d2 >= rSq) continue;

                float distNorm = std::sqrt(d2) / R;
                float falloff = (bs.hardness >= 0.99f) ? 1.0f : std::clamp((1.0f - distNorm) / std::max(1.0f - bs.hardness, 1e-3f), 0.0f, 1.0f);
                falloff = falloff * falloff * (3.0f - 2.0f * falloff);
                size_t idx = (static_cast<size_t>(py) * width + px) * 4;
                paintOverRaw(&dstRgba[idx], bs.color.r, bs.color.g, bs.color.b, bs.opacity * falloff, bs.eraser);
                dirty.expand(px, py);
            }
        }
    }
}

} // namespace

void CanvasEngine::stampUvCircle(std::vector<uint8_t>& buf, Vec2 uv, float radiusPx, float hardness, Color4u8 col,
                                 float opacity, bool eraser, const ObjMesh* maskMesh, bool maskFacesOnly) {
    if (uv.x < 0.0f || uv.x > 1.0f) uv.x -= std::floor(uv.x);
    if (uv.y < 0.0f || uv.y > 1.0f) uv.y -= std::floor(uv.y);
    float cx = uv.x * width;
    float cy = uv.y * height;
    int r = static_cast<int>(std::ceil(radiusPx)) + 1;
    int x0 = std::clamp(static_cast<int>(std::floor(cx)) - r, 0, width - 1);
    int x1 = std::clamp(static_cast<int>(std::ceil(cx)) + r,  0, width - 1);
    int y0 = std::clamp(static_cast<int>(std::floor(cy)) - r, 0, height - 1);
    int y1 = std::clamp(static_cast<int>(std::ceil(cy)) + r,  0, height - 1);
    float rInv = 1.0f / std::max(radiusPx, 0.5f);

    for (int py = y0; py <= y1; ++py) {
        float dy = (py + 0.5f) - cy;
        for (int px = x0; px <= x1; ++px) {
            float dx = (px + 0.5f) - cx;
            float dist = std::sqrt(dx * dx + dy * dy) * rInv;
            if (dist >= 1.0f) continue;

            if (maskFacesOnly && maskMesh && maskMesh->loaded) {
                Vec3 dummyP, dummyN; int32_t tri = -1;
                if (!maskMesh->queryUvAtPixel((px + 0.5f) / width, (py + 0.5f) / height, dummyP, dummyN, tri)) continue;
                if (tri < 0 || !maskMesh->tris[tri].selected) continue;
            }

            float falloff = (hardness >= 0.99f) ? 1.0f : std::clamp((1.0f - dist) / std::max(1.0f - hardness, 1e-3f), 0.0f, 1.0f);
            falloff = falloff * falloff * (3.0f - 2.0f * falloff);
            size_t idx = (static_cast<size_t>(py) * width + px) * 4;
            paintOverRaw(&buf[idx], col.r, col.g, col.b, opacity * falloff, eraser);
        }
    }
}

void CanvasEngine::stampSurfaceBrush3D(Layer& layer, const ObjMesh& mesh, const Vec3& centerPos,
                                         const Vec3& centerNrm, const BrushSettings& bs) {
    auto& pb = layer.ensureUniquePaintBuffer(width, height);
    float R = bs.worldRadius;
    float rSq = R * R;
    for (int32_t ti = 0; ti < static_cast<int32_t>(mesh.tris.size()); ++ti) {
        const Triangle& t = mesh.tris[ti];
        if (!mesh.groups[t.groupIdx].visible) continue;
        if (bs.maskSelectedFacesOnly && !t.selected) continue;
        if (dot(t.faceNrm, centerNrm) < 0.15f) continue;

        const Vec3& a = mesh.uniquePositions[t.p0];
        const Vec3& b = mesh.uniquePositions[t.p1];
        const Vec3& c = mesh.uniquePositions[t.p2];
        Vec3 bmin = minVec(a, minVec(b, c));
        Vec3 bmax = maxVec(a, maxVec(b, c));
        if (centerPos.x + R < bmin.x || centerPos.x - R > bmax.x ||
            centerPos.y + R < bmin.y || centerPos.y - R > bmax.y ||
            centerPos.z + R < bmin.z || centerPos.z - R > bmax.z) continue;

        Vec2 p0{t.uv0.x * width, t.uv0.y * height};
        Vec2 p1{t.uv1.x * width, t.uv1.y * height};
        Vec2 p2{t.uv2.x * width, t.uv2.y * height};
        float area = (p1.x - p0.x) * (p2.y - p0.y) - (p1.y - p0.y) * (p2.x - p0.x);
        if (std::fabs(area) < 1e-4f) continue;

        int ux0 = std::clamp(static_cast<int>(std::floor(std::min({p0.x, p1.x, p2.x}))) - 1, 0, width - 1);
        int ux1 = std::clamp(static_cast<int>(std::ceil(std::max({p0.x, p1.x, p2.x})))  + 1, 0, width - 1);
        int uy0 = std::clamp(static_cast<int>(std::floor(std::min({p0.y, p1.y, p2.y}))) - 1, 0, height - 1);
        int uy1 = std::clamp(static_cast<int>(std::ceil(std::max({p0.y, p1.y, p2.y})))  + 1, 0, height - 1);

        for (int py = uy0; py <= uy1; ++py) {
            for (int px = ux0; px <= ux1; ++px) {
                Vec2 p{px + 0.5f, py + 0.5f};
                float w0 = ((p1.x - p.x) * (p2.y - p.y) - (p1.y - p.y) * (p2.x - p.x)) / area;
                float u  = ((p2.x - p.x) * (p0.y - p.y) - (p2.y - p.y) * (p0.x - p.x)) / area;
                float v  = 1.0f - w0 - u;
                if (w0 < -0.02f || u < -0.02f || v < -0.02f) continue;

                Vec3 p3 = a * std::clamp(w0, 0.0f, 1.0f) + b * std::clamp(u, 0.0f, 1.0f) + c * std::clamp(v, 0.0f, 1.0f);
                float d2 = lengthSq(p3 - centerPos);
                if (d2 >= rSq) continue;

                float distNorm = std::sqrt(d2) / R;
                float falloff = (bs.hardness >= 0.99f) ? 1.0f : std::clamp((1.0f - distNorm) / std::max(1.0f - bs.hardness, 1e-3f), 0.0f, 1.0f);
                falloff = falloff * falloff * (3.0f - 2.0f * falloff);
                size_t idx = (static_cast<size_t>(py) * width + px) * 4;
                paintOverRaw(&pb[idx], bs.color.r, bs.color.g, bs.color.b, bs.opacity * falloff, bs.eraser);
            }
        }
    }
    layer.rgba = pb;
}

void CanvasEngine::paintDabAt3D(int layerIdx, const ObjMesh& mesh, const RayHit& hit, const BrushSettings& bs,
                                 const RayHit* prevHit) {
    cachedMesh = &mesh;
    if (layerIdx < 0 || layerIdx >= static_cast<int>(layers.size())) return;
    Layer& l = layers[layerIdx];
    if (l.kind != LayerKind::RasterPaint || l.locked || !hit.hit) return;

    auto& pb = l.ensureUniquePaintBuffer(width, height);
    if (l.rgba.size() != pb.size()) l.rgba.assign(pb.size(), 0);

    int steps = 1;
    Vec3 startP = hit.pos, startN = hit.normal;
    if (prevHit && prevHit->hit) {
        float dist = length(hit.pos - prevHit->pos);
        float stepLen = std::max(bs.worldRadius * bs.spacing, 0.004f);
        steps = std::clamp(static_cast<int>(std::ceil(dist / stepLen)), 1, 48);
        startP = prevHit->pos;
        startN = prevHit->normal;
    }

    DirtyRect dirty;
    if (bs.project3DBrush) {
        Vec3 pad{bs.worldRadius, bs.worldRadius, bs.worldRadius};
        Vec3 boxMin = minVec(startP, hit.pos) - pad;
        Vec3 boxMax = maxVec(startP, hit.pos) + pad;
        std::vector<int32_t> candMain, candMir;
        collectTrisInBox(mesh, boxMin, boxMax, candMain);
        if (bs.mirrorX) {
            Vec3 mMin{2.0f * mesh.centerSymmetryX - boxMax.x, boxMin.y, boxMin.z};
            Vec3 mMax{2.0f * mesh.centerSymmetryX - boxMin.x, boxMax.y, boxMax.z};
            collectTrisInBox(mesh, mMin, mMax, candMir);
        }
        for (int s = 1; s <= steps; ++s) {
            float t = static_cast<float>(s) / steps;
            Vec3 curP = lerp(startP, hit.pos, t);
            Vec3 curN = normalized(lerp(startN, hit.normal, t));
            stampSurfaceBrushCandidates(pb, mesh, candMain, curP, curN, bs, width, height, dirty);
            if (bs.mirrorX) {
                Vec3 mp{2.0f * mesh.centerSymmetryX - curP.x, curP.y, curP.z};
                Vec3 mn{-curN.x, curN.y, curN.z};
                stampSurfaceBrushCandidates(pb, mesh, candMir, mp, mn, bs, width, height, dirty);
            }
        }
    } else {
        auto addUvDirty = [&](Vec2 uv) {
            if (uv.x < 0.0f || uv.x > 1.0f) uv.x -= std::floor(uv.x);
            if (uv.y < 0.0f || uv.y > 1.0f) uv.y -= std::floor(uv.y);
            float cx = uv.x * width, cy = uv.y * height;
            int r = static_cast<int>(std::ceil(bs.radiusPx)) + 2;
            dirty.expandRect(static_cast<int>(cx) - r, static_cast<int>(cy) - r,
                             static_cast<int>(cx) + r, static_cast<int>(cy) + r);
        };
        for (int s = 1; s <= steps; ++s) {
            float t = static_cast<float>(s) / steps;
            Vec3 curP = lerp(startP, hit.pos, t);
            Vec3 curN = normalized(lerp(startN, hit.normal, t));
            RayHit curH = mesh.projectPointToSurface(curP, curN, bs.worldRadius * 2.0f);
            if (curH.hit) {
                stampUvCircle(pb, curH.uv, bs.radiusPx, bs.hardness, bs.color, bs.opacity, bs.eraser, &mesh, bs.maskSelectedFacesOnly);
                addUvDirty(curH.uv);
                if (bs.mirrorX) {
                    Vec3 mp{2.0f * mesh.centerSymmetryX - curH.pos.x, curH.pos.y, curH.pos.z};
                    Vec3 mn{-curH.normal.x, curH.normal.y, curH.normal.z};
                    RayHit mh = mesh.projectPointToSurface(mp, mn, bs.worldRadius * 3.0f);
                    if (mh.hit) {
                        stampUvCircle(pb, mh.uv, bs.radiusPx, bs.hardness, bs.color, bs.opacity, bs.eraser, &mesh, bs.maskSelectedFacesOnly);
                        addUvDirty(mh.uv);
                    }
                }
            }
        }
    }
    if (dirty.valid) {
        int x0 = std::clamp(dirty.x0, 0, width - 1);
        int y0 = std::clamp(dirty.y0, 0, height - 1);
        int x1 = std::clamp(dirty.x1, 0, width - 1);
        int y1 = std::clamp(dirty.y1, 0, height - 1);
        if (x0 <= x1 && y0 <= y1) {
            if (l.items.empty()) {
                copyRectBytes(l.rgba, pb, width, x0, y0, x1, y1);
                recompositeRectFast(compositeRgba, layers, width, height, x0, y0, x1, y1);
                this->dirtyRect.expandRect(x0, y0, x1, y1);
                gpuDirty = true;
            } else {
                rasterizeLayer(layerIdx, &mesh);
            }
        }
    }
}

void CanvasEngine::paintDabAtUV(int layerIdx, const ObjMesh* mesh, Vec2 uv, const BrushSettings& bs, const Vec2* prevUv) {
    if (mesh) cachedMesh = mesh;
    if (layerIdx < 0 || layerIdx >= static_cast<int>(layers.size())) return;
    Layer& l = layers[layerIdx];
    if (l.kind != LayerKind::RasterPaint || l.locked) return;

    auto& pb = l.ensureUniquePaintBuffer(width, height);
    if (l.rgba.size() != pb.size()) l.rgba.assign(pb.size(), 0);

    int steps = 1;
    Vec2 startUv = uv;
    if (prevUv) {
        float dPx = length((uv - *prevUv) * static_cast<float>(width));
        float stepPx = std::max(bs.radiusPx * bs.spacing, 1.0f);
        steps = std::clamp(static_cast<int>(std::ceil(dPx / stepPx)), 1, 64);
        startUv = *prevUv;
    }

    DirtyRect dirty;
    auto addUvDirty = [&](Vec2 cUv) {
        if (cUv.x < 0.0f || cUv.x > 1.0f) cUv.x -= std::floor(cUv.x);
        if (cUv.y < 0.0f || cUv.y > 1.0f) cUv.y -= std::floor(cUv.y);
        float cx = cUv.x * width, cy = cUv.y * height;
        int r = static_cast<int>(std::ceil(bs.radiusPx)) + 2;
        dirty.expandRect(static_cast<int>(cx) - r, static_cast<int>(cy) - r,
                         static_cast<int>(cx) + r, static_cast<int>(cy) + r);
    };

    for (int s = 1; s <= steps; ++s) {
        float t = static_cast<float>(s) / steps;
        Vec2 curUv = startUv + (uv - startUv) * t;
        stampUvCircle(pb, curUv, bs.radiusPx, bs.hardness, bs.color, bs.opacity, bs.eraser, mesh, bs.maskSelectedFacesOnly);
        addUvDirty(curUv);
        if (bs.mirrorX && mesh && mesh->loaded) {
            Vec3 p3, n3; int32_t tri = -1;
            if (mesh->queryUvAtPixel(curUv.x, curUv.y, p3, n3, tri)) {
                Vec3 mp{2.0f * mesh->centerSymmetryX - p3.x, p3.y, p3.z};
                Vec3 mn{-n3.x, n3.y, n3.z};
                RayHit mh = mesh->projectPointToSurface(mp, mn, 0.25f);
                if (mh.hit && mh.triIdx != tri) {
                    stampUvCircle(pb, mh.uv, bs.radiusPx, bs.hardness, bs.color, bs.opacity, bs.eraser, mesh, bs.maskSelectedFacesOnly);
                    addUvDirty(mh.uv);
                }
            }
        }
    }
    if (dirty.valid) {
        int x0 = std::clamp(dirty.x0, 0, width - 1);
        int y0 = std::clamp(dirty.y0, 0, height - 1);
        int x1 = std::clamp(dirty.x1, 0, width - 1);
        int y1 = std::clamp(dirty.y1, 0, height - 1);
        if (x0 <= x1 && y0 <= y1) {
            if (l.items.empty()) {
                copyRectBytes(l.rgba, pb, width, x0, y0, x1, y1);
                recompositeRectFast(compositeRgba, layers, width, height, x0, y0, x1, y1);
                this->dirtyRect.expandRect(x0, y0, x1, y1);
                gpuDirty = true;
            } else {
                rasterizeLayer(layerIdx, mesh);
            }
        }
    }
}

void CanvasEngine::floodFillAtUV(int layerIdx, Vec2 uv, Color4u8 fillCol, float tolerance, bool mirrorX, const ObjMesh* mesh) {
    if (mesh) cachedMesh = mesh;
    if (layerIdx < 0 || layerIdx >= static_cast<int>(layers.size())) return;
    Layer& l = layers[layerIdx];
    if (l.kind != LayerKind::RasterPaint || l.locked) return;

    beginStroke(layerIdx);
    auto& pb = l.ensureUniquePaintBuffer(width, height);
    auto doFill = [&](Vec2 startUv) {
        if (startUv.x < 0.0f || startUv.x > 1.0f) startUv.x -= std::floor(startUv.x);
        if (startUv.y < 0.0f || startUv.y > 1.0f) startUv.y -= std::floor(startUv.y);
        int sx = std::clamp(static_cast<int>(startUv.x * width), 0, width - 1);
        int sy = std::clamp(static_cast<int>(startUv.y * height), 0, height - 1);
        size_t sIdx = (static_cast<size_t>(sy) * width + sx) * 4;
        uint8_t tr = pb[sIdx+0], tg = pb[sIdx+1], tb = pb[sIdx+2], ta = pb[sIdx+3];
        int tol = static_cast<int>(tolerance * 255.0f);
        if (std::abs(tr - fillCol.r) <= 1 && std::abs(tg - fillCol.g) <= 1 &&
            std::abs(tb - fillCol.b) <= 1 && std::abs(ta - fillCol.a) <= 1) return;

        std::vector<uint8_t> vis(static_cast<size_t>(width) * height, 0);
        std::queue<uint32_t> q;
        uint32_t startPack = static_cast<uint32_t>(sy) * width + sx;
        q.push(startPack);
        vis[startPack] = 1;

        while (!q.empty()) {
            uint32_t p = q.front(); q.pop();
            int x = static_cast<int>(p % width), y = static_cast<int>(p / width);
            size_t o = static_cast<size_t>(p) * 4;
            pb[o+0] = fillCol.r; pb[o+1] = fillCol.g; pb[o+2] = fillCol.b; pb[o+3] = fillCol.a;

            const int dx[4] = {-1, 1, 0, 0}, dy[4] = {0, 0, -1, 1};
            for (int k = 0; k < 4; ++k) {
                int nx = x + dx[k], ny = y + dy[k];
                if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;
                uint32_t np = static_cast<uint32_t>(ny) * width + nx;
                if (vis[np]) continue;
                size_t no = static_cast<size_t>(np) * 4;
                if (std::abs(pb[no+0] - tr) <= tol &&
                    std::abs(pb[no+1] - tg) <= tol &&
                    std::abs(pb[no+2] - tb) <= tol &&
                    std::abs(pb[no+3] - ta) <= tol) {
                    vis[np] = 1;
                    q.push(np);
                }
            }
        }
    };

    doFill(uv);
    if (mirrorX && mesh && mesh->loaded) {
        Vec3 p3, n3; int32_t tri = -1;
        if (mesh->queryUvAtPixel(uv.x, uv.y, p3, n3, tri)) {
            Vec3 mp{2.0f * mesh->centerSymmetryX - p3.x, p3.y, p3.z};
            Vec3 mn{-n3.x, n3.y, n3.z};
            RayHit mh = mesh->projectPointToSurface(mp, mn, 0.25f);
            if (mh.hit && mh.triIdx != tri) doFill(mh.uv);
        }
    }
    endStroke(layerIdx);
    if (l.items.empty()) {
        l.rgba = pb;
    } else {
        rasterizeLayer(layerIdx, mesh);
    }
    recomposite();
}

Color4u8 CanvasEngine::sampleCompositeAtUV(Vec2 uv) const {
    if (uv.x < 0.0f || uv.x > 1.0f) uv.x -= std::floor(uv.x);
    if (uv.y < 0.0f || uv.y > 1.0f) uv.y -= std::floor(uv.y);
    int px = std::clamp(static_cast<int>(uv.x * width), 0, width - 1);
    int py = std::clamp(static_cast<int>(uv.y * height), 0, height - 1);
    size_t o = (static_cast<size_t>(py) * width + px) * 4;
    return {compositeRgba[o+0], compositeRgba[o+1], compositeRgba[o+2], compositeRgba[o+3]};
}

void CanvasEngine::projectBitmap3D(Layer& layer, const ObjMesh& mesh, const uint8_t* srcRgba, int srcW, int srcH,
                                   const Vec3& centerPos, const Vec3& centerNrm, const Vec3& rightHint,
                                   float worldW, float worldH, float rotDeg, bool flipX, Color4u8 tint,
                                   bool mirrorSecondPass, MirrorTextMode mirrorMode, DirtyRect* outDirty,
                                   Vec2* outPrimaryUvCenter, Vec2* outPrimaryUvHalf, bool* outHasPrimaryUvBox) {
    cachedMesh = &mesh;
    float halfDiag = 0.55f * std::sqrt(worldW * worldW + worldH * worldH);
    float maxD = std::max(halfDiag, 0.1f);
    std::vector<int32_t> candTris;

    auto projectPass = [&](Vec3 pCenter, Vec3 nCenter, Vec3 rightAxis, float passRotDeg,
                           bool keepDoorUpright, bool flipB, DirtyRect* pass1Dirty) {
        Vec3 T = rightAxis - nCenter * dot(rightAxis, nCenter);
        if (lengthSq(T) < 1e-6f) {
            Vec3 fallback = (std::fabs(nCenter.y) < 0.9f) ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
            T = cross(fallback, nCenter);
        }
        T = normalized(T);
        Vec3 B = normalized(cross(nCenter, T));
        if (flipB) {
            B = -B;
        } else if (keepDoorUpright && dot(B, Vec3{0, -1, 0}) < 0 && std::fabs(nCenter.y) < 0.85f) {
            B = -B;
        }

        float rad = passRotDeg * kDeg2Rad;
        float cs = std::cos(rad), sn = std::sin(rad);
        Vec3 T_rot = T * cs + B * sn;
        Vec3 B_rot = -T * sn + B * cs;

        mesh.collectTrianglesInAABB(pCenter - Vec3{maxD, maxD, maxD}, pCenter + Vec3{maxD, maxD, maxD}, candTris);
        for (int32_t ti : candTris) {
            const Triangle& t = mesh.tris[ti];
            if (!mesh.groups[t.groupIdx].visible) continue;
            if (dot(t.faceNrm, nCenter) < 0.25f) continue;

            const Vec3& a = mesh.uniquePositions[t.p0];
            const Vec3& b = mesh.uniquePositions[t.p1];
            const Vec3& c = mesh.uniquePositions[t.p2];
            Vec3 bmin = minVec(a, minVec(b, c)), bmax = maxVec(a, maxVec(b, c));
            if (pCenter.x + maxD < bmin.x || pCenter.x - maxD > bmax.x ||
                pCenter.y + maxD < bmin.y || pCenter.y - maxD > bmax.y ||
                pCenter.z + maxD < bmin.z || pCenter.z - maxD > bmax.z) continue;

            Vec2 u0{t.uv0.x * width, t.uv0.y * height};
            Vec2 u1{t.uv1.x * width, t.uv1.y * height};
            Vec2 u2{t.uv2.x * width, t.uv2.y * height};
            float area = (u1.x - u0.x) * (u2.y - u0.y) - (u1.y - u0.y) * (u2.x - u0.x);
            if (std::fabs(area) < 1e-4f) continue;

            int ux0 = std::clamp(static_cast<int>(std::floor(std::min({u0.x, u1.x, u2.x}))), 0, width - 1);
            int ux1 = std::clamp(static_cast<int>(std::ceil(std::max({u0.x, u1.x, u2.x}))),  0, width - 1);
            int uy0 = std::clamp(static_cast<int>(std::floor(std::min({u0.y, u1.y, u2.y}))), 0, height - 1);
            int uy1 = std::clamp(static_cast<int>(std::ceil(std::max({u0.y, u1.y, u2.y}))),  0, height - 1);

            for (int py = uy0; py <= uy1; ++py) {
                for (int px = ux0; px <= ux1; ++px) {
                    Vec2 p{px + 0.5f, py + 0.5f};
                    float w0 = ((u1.x - p.x) * (u2.y - p.y) - (u1.y - p.y) * (u2.x - p.x)) / area;
                    float uu = ((u2.x - p.x) * (u0.y - p.y) - (u2.y - p.y) * (u0.x - p.x)) / area;
                    float vv = 1.0f - w0 - uu;
                    if (w0 < -0.02f || uu < -0.02f || vv < -0.02f) continue;

                    Vec3 p3 = a * std::clamp(w0, 0.0f, 1.0f) + b * std::clamp(uu, 0.0f, 1.0f) + c * std::clamp(vv, 0.0f, 1.0f);
                    Vec3 rel = p3 - pCenter;
                    if (std::fabs(dot(rel, nCenter)) > std::max(0.28f, halfDiag * 0.45f)) continue;

                    float tx = dot(rel, T_rot) / worldW + 0.5f;
                    float ty = dot(rel, B_rot) / worldH + 0.5f;
                    if (flipX) tx = 1.0f - tx;
                    if (tx < 0.0f || tx > 1.0f || ty < 0.0f || ty > 1.0f) continue;

                    float sr, sg, sb, sa;
                    sampleRgbaBilinear(srcRgba, srcW, srcH, tx, ty, sr, sg, sb, sa);
                    if (sa <= 1e-4f) continue;
                    size_t dstIdx = (static_cast<size_t>(py) * width + px) * 4;
                    paintOverRaw(&layer.rgba[dstIdx],
                                 u8clamp(sr * (tint.r / 255.0f)),
                                 u8clamp(sg * (tint.g / 255.0f)),
                                 u8clamp(sb * (tint.b / 255.0f)),
                                 sa * (tint.a / 255.0f), false);
                    if (outDirty) outDirty->expand(px, py);
                    if (pass1Dirty) pass1Dirty->expand(px, py);
                }
            }
        }
    };

    DirtyRect primaryDirty;
    projectPass(centerPos, centerNrm, rightHint, rotDeg, false, false, &primaryDirty);

    Vec2* dstCenter = outPrimaryUvCenter ? outPrimaryUvCenter : &layer.primaryUvCenter;
    Vec2* dstHalf = outPrimaryUvHalf ? outPrimaryUvHalf : &layer.primaryUvHalf;
    bool* dstHasBox = outHasPrimaryUvBox ? outHasPrimaryUvBox : &layer.hasPrimaryUvBox;

    if (primaryDirty.valid) {
        float invW = 1.0f / std::max(1, width);
        float invH = 1.0f / std::max(1, height);
        float minU = primaryDirty.x0 * invW;
        float maxU = (primaryDirty.x1 + 1) * invW;
        float minV = primaryDirty.y0 * invH;
        float maxV = (primaryDirty.y1 + 1) * invH;
        *dstCenter = {0.5f * (minU + maxU), 0.5f * (minV + maxV)};
        *dstHalf = {std::max(1e-4f, 0.5f * (maxU - minU)),
                    std::max(1e-4f, 0.5f * (maxV - minV))};
        *dstHasBox = (std::max(dstHalf->x, dstHalf->y) < 0.40f);
    } else {
        *dstHasBox = false;
    }

    if (mirrorSecondPass && mirrorMode != MirrorTextMode::None) {
        Vec3 mPos{2.0f * mesh.centerSymmetryX - centerPos.x, centerPos.y, centerPos.z};
        Vec3 mNrm{-centerNrm.x, centerNrm.y, centerNrm.z};
        Vec3 rGeo{-rightHint.x, rightHint.y, rightHint.z};
        bool readable = (mirrorMode == MirrorTextMode::ReadableBothSides);
        Vec3 rUse = readable ? -rGeo : rGeo;
        float mirRot = readable ? -rotDeg : rotDeg;
        projectPass(mPos, mNrm, rUse, mirRot, readable, !readable, nullptr);
    }
}

void CanvasEngine::rasterizeLayer(int layerIdx, const ObjMesh* mesh) {
    if (layerIdx < 0 || layerIdx >= static_cast<int>(layers.size())) return;
    if (mesh) cachedMesh = mesh;
    Layer& l = layers[layerIdx];
    const size_t fullBytes = static_cast<size_t>(width) * height * 4;

    if (l.kind == LayerKind::RasterPaint && l.items.empty()) {
        if (l.paintBuffer && l.paintBuffer->size() == fullBytes) {
            l.rgba = *l.paintBuffer;
        } else if (l.rgba.size() != fullBytes) {
            l.rgba.assign(fullBytes, 0);
        }
        l.dirtyVector = false;
        return;
    }

    DirtyRect unionDirty;
    const bool hasPb = (l.paintBuffer && l.paintBuffer->size() == fullBytes);
    if (l.rgba.size() != fullBytes) {
        if (hasPb) l.rgba = *l.paintBuffer;
        else l.rgba.assign(fullBytes, 0);
        l.hasPrevBounds = false;
        unionDirty.expandRect(0, 0, width - 1, height - 1);
    } else if (!l.hasPrevBounds) {
        if (hasPb) {
            l.rgba = *l.paintBuffer;
            unionDirty.expandRect(0, 0, width - 1, height - 1);
        }
    } else {
        int px0 = std::clamp(l.prevMinX, 0, width - 1);
        int py0 = std::clamp(l.prevMinY, 0, height - 1);
        int px1 = std::clamp(l.prevMaxX, 0, width - 1);
        int py1 = std::clamp(l.prevMaxY, 0, height - 1);
        if (px0 <= px1 && py0 <= py1) {
            if (hasPb) {
                copyRectBytes(l.rgba, *l.paintBuffer, width, px0, py0, px1, py1);
            } else {
                const size_t rowBytes = static_cast<size_t>(px1 - px0 + 1) * 4;
                for (int py = py0; py <= py1; ++py) {
                    std::memset(&l.rgba[(static_cast<size_t>(py) * width + px0) * 4], 0, rowBytes);
                }
            }
            unionDirty.expandRect(px0, py0, px1, py1);
        }
    }

    DirtyRect newDirty;
    float uvScale = static_cast<float>(width) / 2048.0f;

    auto rasterizeText = [&](TextLayerProps& tp, Vec2* pCenter, Vec2* pHalf, bool* pHasBox, auto&& syncFn) {
        std::vector<uint8_t> bmp; int bw = 0, bh = 0;
        if (!renderTextToBitmap(tp, bmp, bw, bh)) return;
        if (tp.anchoredIn3D && mesh && mesh->loaded) {
            float wH = std::max(0.03f, tp.worldHeight);
            float wW = wH * (static_cast<float>(bw) / std::max(1, bh));
            bool doMirror = tp.mirrorX || (tp.mirrorMode != MirrorTextMode::None);
            MirrorTextMode mm = (tp.mirrorMode != MirrorTextMode::None) ? tp.mirrorMode
                              : (doMirror ? MirrorTextMode::ReadableBothSides : MirrorTextMode::None);
            projectBitmap3D(l, *mesh, bmp.data(), bw, bh,
                            tp.pos3D, tp.nrm3D, tp.right3D,
                            wW, wH, tp.rotationDeg, false, {255, 255, 255, 255},
                            doMirror, mm, &newDirty, pCenter, pHalf, pHasBox);
            syncFn();
        } else {
            float drawW = bw * uvScale, drawH = bh * uvScale;
            drawText2DToLayer(l, width, height, bmp, bw, bh, tp.uvPos, drawW, drawH, tp.rotationDeg, false, &newDirty);
            if (tp.mirrorX) {
                bool flipX = (tp.mirrorMode == MirrorTextMode::TrueFlip);
                drawText2DToLayer(l, width, height, bmp, bw, bh, {1.0f - tp.uvPos.x, tp.uvPos.y},
                                  drawW, drawH, -tp.rotationDeg, flipX, &newDirty);
            }
        }
    };

    auto rasterizeDecal = [&](DecalLayerProps& dp, Vec2* pCenter, Vec2* pHalf, bool* pHasBox, auto&& syncFn) {
        const auto& imgPix = dp.imagePixels();
        if (imgPix.empty() || dp.imgW <= 0 || dp.imgH <= 0) return;
        if (dp.anchoredIn3D && mesh && mesh->loaded) {
            float wW = std::max(0.05f, dp.worldWidth);
            float wH = wW * (static_cast<float>(dp.imgH) / std::max(1, dp.imgW));
            bool doMirror = dp.mirrorX || (dp.mirrorMode != MirrorTextMode::None);
            MirrorTextMode mm = (dp.mirrorMode != MirrorTextMode::None) ? dp.mirrorMode
                              : (doMirror ? MirrorTextMode::ReadableBothSides : MirrorTextMode::None);
            projectBitmap3D(l, *mesh, imgPix.data(), dp.imgW, dp.imgH,
                            dp.pos3D, dp.nrm3D, dp.right3D,
                            wW, wH, dp.rotationDeg, false, dp.tint,
                            doMirror, mm, &newDirty, pCenter, pHalf, pHasBox);
            syncFn();
        } else {
            drawDecal2DToLayer(l, width, height, imgPix, dp.imgW, dp.imgH,
                               dp.uvPos, dp.uvScale, dp.rotationDeg, false, dp.tint, &newDirty);
            if (dp.mirrorX) {
                bool flipX = (dp.mirrorMode == MirrorTextMode::TrueFlip);
                drawDecal2DToLayer(l, width, height, imgPix, dp.imgW, dp.imgH,
                                   {1.0f - dp.uvPos.x, dp.uvPos.y}, dp.uvScale, -dp.rotationDeg, flipX, dp.tint, &newDirty);
            }
        }
    };

    auto rasterizeRect = [&](RectShapeProps& rp, Vec2* pCenter, Vec2* pHalf, bool* pHasBox, auto&& syncFn) {
        if (!rp.filled && (!rp.hasStroke || rp.strokeWidthPx <= 0.0f)) return;
        if (rp.anchoredIn3D && mesh && mesh->loaded) {
            float wW = std::max(0.02f, rp.worldWidth);
            float wH = std::max(0.02f, rp.worldHeight);
            float maxSpan = std::max(wW, wH);
            float pxPerM = std::min(1024.0f / maxSpan, std::max(512.0f, 384.0f / std::max(0.05f, maxSpan)));
            float strokeWorld = (rp.hasStroke && rp.strokeWidthPx > 0.0f) ? (rp.strokeWidthPx / 2048.0f) : 0.0f;
            float padM = strokeWorld + 0.01f;
            float spanW = wW + 2.0f * padM;
            float spanH = wH + 2.0f * padM;
            int bufW = std::clamp(static_cast<int>(std::ceil(spanW * pxPerM)), 16, 1024);
            int bufH = std::clamp(static_cast<int>(std::ceil(spanH * pxPerM)), 16, 1024);
            float scalePx = static_cast<float>(bufW - 1) / spanW;
            std::vector<uint8_t> tmpRgba(static_cast<size_t>(bufW) * bufH * 4, 0);
            rasterizeRect2DToBuffer(tmpRgba.data(), bufW, bufH,
                                    {0.5f * bufW, 0.5f * bufH}, wW * scalePx, wH * scalePx, 0.0f,
                                    rp.cornerRadiusPx * (scalePx / 2048.0f),
                                    rp.strokeWidthPx * (scalePx / 2048.0f),
                                    rp.filled, rp.hasStroke, rp.fillColor, rp.strokeColor, nullptr);
            projectBitmap3D(l, *mesh, tmpRgba.data(), bufW, bufH,
                            rp.pos3D, rp.nrm3D, rp.right3D,
                            spanW, spanH, rp.rotationDeg, false, Color4u8{255, 255, 255, 255},
                            rp.mirrorX, rp.mirrorX ? MirrorTextMode::TrueFlip : MirrorTextMode::None,
                            &newDirty, pCenter, pHalf, pHasBox);
            syncFn();
        } else {
            float drawW = rp.uvScale.x * width, drawH = rp.uvScale.y * height;
            rasterizeRect2DToBuffer(l.rgba.data(), width, height,
                                    {rp.uvPos.x * width, rp.uvPos.y * height},
                                    drawW, drawH, rp.rotationDeg,
                                    rp.cornerRadiusPx * uvScale, rp.strokeWidthPx * uvScale,
                                    rp.filled, rp.hasStroke, rp.fillColor, rp.strokeColor, &newDirty);
            if (rp.mirrorX) {
                rasterizeRect2DToBuffer(l.rgba.data(), width, height,
                                        {(1.0f - rp.uvPos.x) * width, rp.uvPos.y * height},
                                        drawW, drawH, -rp.rotationDeg,
                                        rp.cornerRadiusPx * uvScale, rp.strokeWidthPx * uvScale,
                                        rp.filled, rp.hasStroke, rp.fillColor, rp.strokeColor, &newDirty);
            }
        }
    };

    auto rasterizePen = [&](const RulerStripeProps& rp) {
        if (rp.nodes.size() < 2) return;
        const auto& pts = rp.nodes;
        const int nN = static_cast<int>(pts.size());
        const bool isClosed = rp.closed && nN >= 3;
        bool anyHandles = false;
        for (const StripeNode& sn : pts) {
            if (sn.hasHandles) { anyHandles = true; break; }
        }
        bool use3D = rp.use3DSpace && mesh && mesh->loaded && pts[0].has3D;

        if (use3D) {
            std::vector<Vec3> poly3;
            const int segNodeCnt = isClosed ? nN : (nN - 1);
            poly3.push_back(pts[0].pos3D);
            if (anyHandles || !rp.smoothCurve) {
                constexpr int kSub = 24;
                for (int i = 0; i < segNodeCnt; ++i) {
                    const StripeNode& nA = pts[i];
                    const StripeNode& nB = pts[(i + 1) % nN];
                    Vec3 p0 = nA.pos3D;
                    Vec3 p1 = nA.hasHandles ? nA.handleOut3D : nA.pos3D;
                    Vec3 p2 = nB.hasHandles ? nB.handleIn3D : nB.pos3D;
                    Vec3 p3 = nB.pos3D;
                    int maxK = (isClosed && i == nN - 1) ? (kSub - 1) : kSub;
                    for (int k = 1; k <= maxK; ++k) {
                        poly3.push_back(evalCubic(p0, p1, p2, p3, static_cast<float>(k) / kSub));
                    }
                }
            } else {
                constexpr int kSub = 18;
                for (int i = 0; i < segNodeCnt; ++i) {
                    const StripeNode& n0 = pts[isClosed ? ((i - 1 + nN) % nN) : std::max(0, i - 1)];
                    const StripeNode& n1 = pts[i];
                    const StripeNode& n2 = pts[(i + 1) % nN];
                    const StripeNode& n3 = pts[isClosed ? ((i + 2) % nN) : std::min(nN - 1, i + 2)];
                    int maxK = (isClosed && i == nN - 1) ? (kSub - 1) : kSub;
                    for (int k = 1; k <= maxK; ++k) {
                        poly3.push_back(catmullRom3(n0.pos3D, n1.pos3D, n2.pos3D, n3.pos3D,
                                                   static_cast<float>(k) / kSub));
                    }
                }
            }

            if (rp.filled && nN >= 3 && poly3.size() >= 3) {
                Vec3 c3{0, 0, 0}, avgN{0, 0, 0};
                for (const Vec3& p : poly3) c3 += p;
                c3 = c3 / static_cast<float>(poly3.size());
                for (const StripeNode& n : pts) avgN += n.nrm3D;
                avgN = (lengthSq(avgN) > 1e-6f) ? normalized(avgN) : Vec3{0, 1, 0};

                Vec3 refAxis = (std::fabs(avgN.y) < 0.9f) ? Vec3{0.0f, 1.0f, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
                Vec3 T = refAxis - avgN * dot(refAxis, avgN);
                if (lengthSq(T) < 1e-6f) T = cross(Vec3{1.0f, 0.0f, 0.0f}, avgN);
                T = normalized(T);
                Vec3 B = normalized(cross(avgN, T));

                std::vector<Vec2> projUV;
                projUV.reserve(poly3.size());
                float minU = 1e9f, maxU = -1e9f, minV = 1e9f, maxV = -1e9f;
                for (const Vec3& p : poly3) {
                    Vec3 d = p - c3;
                    float u = dot(d, T);
                    float v = dot(d, B);
                    projUV.push_back({u, v});
                    minU = std::min(minU, u); maxU = std::max(maxU, u);
                    minV = std::min(minV, v); maxV = std::max(maxV, v);
                }

                float midU = 0.5f * (minU + maxU);
                float midV = 0.5f * (minV + maxV);
                Vec3 boxCenter = c3 + T * midU + B * midV;

                bool drawStroke = rp.hasStroke && rp.widthPx > 0.0f && rp.worldWidth > 0.0f;
                float strokeW = drawStroke ? rp.worldWidth : 0.0f;
                float outWorld = (drawStroke && rp.outlineWidth > 0.1f)
                                 ? (rp.outlineWidth * (rp.worldWidth / std::max(1.0f, rp.widthPx)))
                                 : 0.0f;
                float padM = std::max(0.01f, 0.75f * strokeW + outWorld + 0.01f);
                float spanU = std::max(0.02f, (maxU - minU) + 2.0f * padM);
                float spanV = std::max(0.02f, (maxV - minV) + 2.0f * padM);

                float maxSpan = std::max(spanU, spanV);
                float pxPerM = std::min(1024.0f / maxSpan, std::max(512.0f, 384.0f / std::max(0.05f, maxSpan)));
                int bufW = std::clamp(static_cast<int>(std::ceil(spanU * pxPerM)), 16, 1024);
                int bufH = std::clamp(static_cast<int>(std::ceil(spanV * pxPerM)), 16, 1024);

                std::vector<Vec2> polyTmp;
                polyTmp.reserve(projUV.size());
                for (const Vec2& uvP : projUV) {
                    polyTmp.push_back({
                        ((uvP.x - midU) / spanU + 0.5f) * (bufW - 1),
                        ((uvP.y - midV) / spanV + 0.5f) * (bufH - 1)
                    });
                }

                std::vector<uint8_t> tmpRgba(static_cast<size_t>(bufW) * bufH * 4, 0);
                float halfStrokePx = 0.5f * strokeW * (static_cast<float>(bufW - 1) / spanU);
                float outlinePx = outWorld * (static_cast<float>(bufW - 1) / spanU);
                rasterizePoly2DToBuffer(tmpRgba.data(), bufW, bufH, polyTmp,
                                        isClosed, true, rp.fillColor,
                                        drawStroke, halfStrokePx, outlinePx, 0.0f,
                                        rp.color, rp.outlineColor, nullptr);

                projectBitmap3D(l, *mesh, tmpRgba.data(), bufW, bufH,
                                boxCenter, avgN, T,
                                spanU, spanV, 0.0f, false, Color4u8{255, 255, 255, 255},
                                rp.mirrorX,
                                rp.mirrorX ? MirrorTextMode::TrueFlip : MirrorTextMode::None,
                                &newDirty);
            } else if (rp.hasStroke && poly3.size() >= 2) {
                std::vector<std::pair<Vec3, Vec3>> segs;
                for (size_t i = 0; i + 1 < poly3.size(); ++i) {
                    segs.push_back({poly3[i], poly3[i + 1]});
                }
                if (isClosed && poly3.size() >= 3) {
                    segs.push_back({poly3.back(), poly3.front()});
                }

                float baseW = std::max(0.005f, rp.worldWidth);
                float outWorld = (rp.outlineWidth > 0.1f)
                                 ? (rp.outlineWidth * (baseW / std::max(1.0f, rp.widthPx)))
                                 : 0.0f;
                int totalS = static_cast<int>(segs.size());
                Vec3 polyMin{1e9f, 1e9f, 1e9f}, polyMax{-1e9f, -1e9f, -1e9f};
                for (const auto& s : segs) {
                    polyMin = minVec(polyMin, minVec(s.first, s.second));
                    polyMax = maxVec(polyMax, maxVec(s.first, s.second));
                }
                float padR = baseW + outWorld + 0.25f;
                std::vector<int32_t> candMain, candMir;
                collectTrisInBox(*mesh, polyMin - Vec3{padR, padR, padR}, polyMax + Vec3{padR, padR, padR}, candMain);
                if (rp.mirrorX) {
                    Vec3 mMin{2.0f * mesh->centerSymmetryX - (polyMax.x + padR), polyMin.y - padR, polyMin.z - padR};
                    Vec3 mMax{2.0f * mesh->centerSymmetryX - (polyMin.x - padR), polyMax.y + padR, polyMax.z + padR};
                    collectTrisInBox(*mesh, mMin, mMax, candMir);
                }

                auto stampStrokePass = [&](Color4u8 col, float extraRadius) {
                    BrushSettings bs;
                    bs.color = col;
                    bs.hardness = 0.94f;
                    bs.opacity = col.a / 255.0f;
                    bs.project3DBrush = true;
                    bs.mirrorX = rp.mirrorX;
                    for (int si = 0; si < totalS; ++si) {
                        Vec3 a = segs[si].first, b = segs[si].second;
                        float len = length(b - a);
                        int steps = std::clamp(static_cast<int>(std::ceil(len / std::max(baseW * 0.28f, 0.004f))), 1, 80);
                        for (int k = 0; k <= steps; ++k) {
                            float f = static_cast<float>(k) / steps;
                            float globalT = (si + f) / std::max(1, totalS);
                            float edgeFactor = isClosed ? 1.0f : (1.0f - rp.endTaper * std::fabs(2.0f * globalT - 1.0f));
                            Vec3 p = lerp(a, b, f);
                            RayHit h = mesh->projectPointToSurface(p, Vec3{0, 1, 0}, 0.2f);
                            bs.worldRadius = 0.5f * baseW * std::max(0.15f, edgeFactor) + extraRadius;
                            Vec3 nUse = h.hit ? h.normal : pts[0].nrm3D;
                            Vec3 pUse = h.hit ? h.pos : p;
                            stampSurfaceBrushCandidates(l.rgba, *mesh, candMain, pUse, nUse, bs, width, height, newDirty);
                            if (rp.mirrorX) {
                                Vec3 mp{2.0f * mesh->centerSymmetryX - pUse.x, pUse.y, pUse.z};
                                Vec3 mn{-nUse.x, nUse.y, nUse.z};
                                stampSurfaceBrushCandidates(l.rgba, *mesh, candMir, mp, mn, bs, width, height, newDirty);
                            }
                        }
                    }
                };
                if (outWorld > 1e-4f) {
                    stampStrokePass(rp.outlineColor, outWorld);
                }
                stampStrokePass(rp.color, 0.0f);
            }
        } else {
            std::vector<Vec2> poly;
            const int segNodeCnt = isClosed ? nN : (nN - 1);
            Vec2 dim{static_cast<float>(width), static_cast<float>(height)};
            poly.push_back({pts[0].uv.x * dim.x, pts[0].uv.y * dim.y});
            if (anyHandles || !rp.smoothCurve) {
                constexpr int kSub = 24;
                for (int i = 0; i < segNodeCnt; ++i) {
                    const StripeNode& nA = pts[i];
                    const StripeNode& nB = pts[(i + 1) % nN];
                    Vec2 p0{nA.uv.x * dim.x, nA.uv.y * dim.y};
                    Vec2 p1 = nA.hasHandles ? Vec2{nA.handleOutUv.x * dim.x, nA.handleOutUv.y * dim.y} : p0;
                    Vec2 p3{nB.uv.x * dim.x, nB.uv.y * dim.y};
                    Vec2 p2 = nB.hasHandles ? Vec2{nB.handleInUv.x * dim.x, nB.handleInUv.y * dim.y} : p3;
                    int maxK = (isClosed && i == nN - 1) ? (kSub - 1) : kSub;
                    for (int k = 1; k <= maxK; ++k) {
                        poly.push_back(evalCubic(p0, p1, p2, p3, static_cast<float>(k) / kSub));
                    }
                }
            } else {
                constexpr int kSub = 20;
                for (int i = 0; i < segNodeCnt; ++i) {
                    const StripeNode& n0 = pts[isClosed ? ((i - 1 + nN) % nN) : std::max(0, i - 1)];
                    const StripeNode& n1 = pts[i];
                    const StripeNode& n2 = pts[(i + 1) % nN];
                    const StripeNode& n3 = pts[isClosed ? ((i + 2) % nN) : std::min(nN - 1, i + 2)];
                    Vec2 p0{n0.uv.x * dim.x, n0.uv.y * dim.y};
                    Vec2 p1{n1.uv.x * dim.x, n1.uv.y * dim.y};
                    Vec2 p2{n2.uv.x * dim.x, n2.uv.y * dim.y};
                    Vec2 p3{n3.uv.x * dim.x, n3.uv.y * dim.y};
                    int maxK = (isClosed && i == nN - 1) ? (kSub - 1) : kSub;
                    for (int k = 1; k <= maxK; ++k) {
                        poly.push_back(catmullRom2(p0, p1, p2, p3, static_cast<float>(k) / kSub));
                    }
                }
            }

            float hw = 0.5f * rp.widthPx * uvScale;
            float outW = rp.outlineWidth * uvScale;
            bool drawStroke = rp.hasStroke && rp.widthPx > 0.0f;
            bool drawFill = rp.filled && nN >= 3;
            rasterizePoly2DToBuffer(l.rgba.data(), width, height, poly,
                                    isClosed, drawFill, rp.fillColor,
                                    drawStroke, hw, outW, rp.endTaper,
                                    rp.color, rp.outlineColor, &newDirty);

            if (rp.mirrorX) {
                std::vector<Vec2> mirPoly;
                mirPoly.reserve(poly.size());
                for (const Vec2& pt : poly) {
                    mirPoly.push_back({dim.x - pt.x, pt.y});
                }
                rasterizePoly2DToBuffer(l.rgba.data(), width, height, mirPoly,
                                        isClosed, drawFill, rp.fillColor,
                                        drawStroke, hw, outW, rp.endTaper,
                                        rp.color, rp.outlineColor, &newDirty);
            }
        }
    };

    if (!l.items.empty()) {
        for (VectorItem& item : l.items) {
            if (!item.visible) continue;
            if (item.kind == VectorItemKind::Text) {
                rasterizeText(item.text, &item.primaryUvCenter, &item.primaryUvHalf, &item.hasPrimaryUvBox,
                              [&]() { syncItem3DTo2D(item, mesh); });
            } else if (item.kind == VectorItemKind::Image) {
                rasterizeDecal(item.image, &item.primaryUvCenter, &item.primaryUvHalf, &item.hasPrimaryUvBox,
                               [&]() { syncItem3DTo2D(item, mesh); });
            } else if (item.kind == VectorItemKind::RectShape) {
                rasterizeRect(item.rect, &item.primaryUvCenter, &item.primaryUvHalf, &item.hasPrimaryUvBox,
                              [&]() { syncItem3DTo2D(item, mesh); });
            } else if (item.kind == VectorItemKind::PenPath) {
                rasterizePen(item.pen);
            }
        }
    } else if (l.kind == LayerKind::Text) {
        rasterizeText(l.text, &l.primaryUvCenter, &l.primaryUvHalf, &l.hasPrimaryUvBox,
                      [&]() { syncLayer3DTo2D(layerIdx, mesh); });
    } else if (l.kind == LayerKind::Decal) {
        rasterizeDecal(l.decal, &l.primaryUvCenter, &l.primaryUvHalf, &l.hasPrimaryUvBox,
                       [&]() { syncLayer3DTo2D(layerIdx, mesh); });
    } else if (l.kind == LayerKind::RulerStripe) {
        rasterizePen(l.ruler);
    }

    if (newDirty.valid) {
        l.prevMinX = std::clamp(newDirty.x0, 0, width - 1);
        l.prevMinY = std::clamp(newDirty.y0, 0, height - 1);
        l.prevMaxX = std::clamp(newDirty.x1, 0, width - 1);
        l.prevMaxY = std::clamp(newDirty.y1, 0, height - 1);
        l.hasPrevBounds = (l.prevMinX <= l.prevMaxX && l.prevMinY <= l.prevMaxY);
        if (l.hasPrevBounds) {
            unionDirty.expandRect(l.prevMinX, l.prevMinY, l.prevMaxX, l.prevMaxY);
        }
    } else {
        l.hasPrevBounds = false;
        l.prevMinX = l.prevMinY = 0;
        l.prevMaxX = l.prevMaxY = -1;
    }

    l.dirtyVector = false;
    if (unionDirty.valid) {
        int ux0 = std::clamp(unionDirty.x0, 0, width - 1);
        int uy0 = std::clamp(unionDirty.y0, 0, height - 1);
        int ux1 = std::clamp(unionDirty.x1, 0, width - 1);
        int uy1 = std::clamp(unionDirty.y1, 0, height - 1);
        if (ux0 <= ux1 && uy0 <= uy1) {
            recompositeRectFast(compositeRgba, layers, width, height, ux0, uy0, ux1, uy1);
            this->dirtyRect.expandRect(ux0, uy0, ux1, uy1);
            gpuDirty = true;
        }
    }
}

void CanvasEngine::rasterizeVectorLayer(int layerIdx, const ObjMesh* mesh) {
    rasterizeLayer(layerIdx, mesh);
}

void CanvasEngine::rasterizeAllDirty(const ObjMesh* mesh) {
    if (mesh) cachedMesh = mesh;
    for (int i = 0; i < static_cast<int>(layers.size()); ++i) {
        if ((layers[i].kind != LayerKind::RasterPaint || !layers[i].items.empty()) && layers[i].dirtyVector) {
            rasterizeLayer(i, mesh);
        }
    }
}

void CanvasEngine::recomposite() {
    recompositeRectFast(compositeRgba, layers, width, height, 0, 0, width - 1, height - 1);
    dirtyRect = {0, 0, width - 1, height - 1, true};
    fullUpload = true;
    gpuDirty = true;
}

bool CanvasEngine::saveProject(const std::string& path, const std::string& meshPath) const {
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs.is_open()) return false;
    auto writeStr = [&](const std::string& s) {
        uint32_t len = static_cast<uint32_t>(s.size());
        ofs.write(reinterpret_cast<const char*>(&len), 4);
        if (len) ofs.write(s.data(), len);
    };
    auto writeText = [&](const TextLayerProps& tp) {
        writeStr(tp.text);
        writeStr(tp.fontPath);
        ofs.write(reinterpret_cast<const char*>(&tp.fontSizePx), sizeof(float) * 5);
        ofs.write(reinterpret_cast<const char*>(&tp.fillColor), sizeof(Color4u8) * 2);
        ofs.write(reinterpret_cast<const char*>(&tp.uvPos), sizeof(Vec2));
        ofs.write(reinterpret_cast<const char*>(&tp.pos3D), sizeof(Vec3) * 3);
        uint8_t tf = (tp.anchoredIn3D ? 1 : 0) | (tp.mirrorX ? 2 : 0);
        int mm = static_cast<int>(tp.mirrorMode);
        ofs.write(reinterpret_cast<const char*>(&tf), 1);
        ofs.write(reinterpret_cast<const char*>(&tp.rotationDeg), 4);
        ofs.write(reinterpret_cast<const char*>(&mm), 4);
    };
    auto writeDecal = [&](const DecalLayerProps& dp) {
        writeStr(dp.sourcePath);
        ofs.write(reinterpret_cast<const char*>(&dp.imgW), 4);
        ofs.write(reinterpret_cast<const char*>(&dp.imgH), 4);
        const auto& pix = dp.imagePixels();
        uint32_t pixLen = static_cast<uint32_t>(pix.size());
        ofs.write(reinterpret_cast<const char*>(&pixLen), 4);
        if (pixLen) ofs.write(reinterpret_cast<const char*>(pix.data()), pixLen);
        ofs.write(reinterpret_cast<const char*>(&dp.uvPos), sizeof(Vec2) * 2);
        ofs.write(reinterpret_cast<const char*>(&dp.pos3D), sizeof(Vec3) * 3);
        ofs.write(reinterpret_cast<const char*>(&dp.worldWidth), 4);
        uint8_t df = (dp.anchoredIn3D ? 1 : 0) | (dp.mirrorX ? 2 : 0);
        int mm = static_cast<int>(dp.mirrorMode);
        ofs.write(reinterpret_cast<const char*>(&df), 1);
        ofs.write(reinterpret_cast<const char*>(&dp.rotationDeg), 4);
        ofs.write(reinterpret_cast<const char*>(&dp.tint), sizeof(Color4u8));
        ofs.write(reinterpret_cast<const char*>(&mm), 4);
    };
    auto writeRect = [&](const RectShapeProps& rp) {
        ofs.write(reinterpret_cast<const char*>(&rp.uvPos), sizeof(Vec2) * 2);
        ofs.write(reinterpret_cast<const char*>(&rp.pos3D), sizeof(Vec3) * 3);
        ofs.write(reinterpret_cast<const char*>(&rp.worldWidth), sizeof(float) * 5);
        ofs.write(reinterpret_cast<const char*>(&rp.fillColor), sizeof(Color4u8) * 2);
        uint8_t rf = (rp.filled ? 1 : 0) | (rp.hasStroke ? 2 : 0) |
                     (rp.anchoredIn3D ? 4 : 0) | (rp.mirrorX ? 8 : 0);
        ofs.write(reinterpret_cast<const char*>(&rf), 1);
    };
    auto writePen = [&](const RulerStripeProps& rp) {
        uint32_t nc = static_cast<uint32_t>(rp.nodes.size());
        ofs.write(reinterpret_cast<const char*>(&nc), 4);
        for (const StripeNode& sn : rp.nodes) {
            ofs.write(reinterpret_cast<const char*>(&sn.uv), sizeof(Vec2));
            ofs.write(reinterpret_cast<const char*>(&sn.handleInUv), sizeof(Vec2));
            ofs.write(reinterpret_cast<const char*>(&sn.handleOutUv), sizeof(Vec2));
            ofs.write(reinterpret_cast<const char*>(&sn.pos3D), sizeof(Vec3));
            ofs.write(reinterpret_cast<const char*>(&sn.handleIn3D), sizeof(Vec3));
            ofs.write(reinterpret_cast<const char*>(&sn.handleOut3D), sizeof(Vec3));
            ofs.write(reinterpret_cast<const char*>(&sn.nrm3D), sizeof(Vec3));
            uint8_t nFlags = (sn.has3D ? 1 : 0) | (sn.hasHandles ? 2 : 0) | (sn.smooth ? 4 : 0);
            ofs.write(reinterpret_cast<const char*>(&nFlags), 1);
        }
        ofs.write(reinterpret_cast<const char*>(&rp.widthPx), sizeof(float) * 3);
        uint8_t rf = (rp.smoothCurve ? 1 : 0) | (rp.use3DSpace ? 2 : 0) |
                     (rp.mirrorX ? 4 : 0) | (rp.closed ? 8 : 0) |
                     (rp.filled ? 16 : 0) | (rp.hasStroke ? 32 : 0);
        ofs.write(reinterpret_cast<const char*>(&rf), 1);
        ofs.write(reinterpret_cast<const char*>(&rp.color), sizeof(Color4u8) * 2);
        ofs.write(reinterpret_cast<const char*>(&rp.outlineWidth), 4);
        ofs.write(reinterpret_cast<const char*>(&rp.fillColor), sizeof(Color4u8));
    };

    const char magic[4] = {'C', 'R', 'M', '1'};
    ofs.write(magic, 4);
    ofs.write(reinterpret_cast<const char*>(&width), 4);
    ofs.write(reinterpret_cast<const char*>(&height), 4);
    writeStr(meshPath);
    ofs.write(reinterpret_cast<const char*>(&activeLayerIdx), 4);
    ofs.write(reinterpret_cast<const char*>(&selectedItemIdx), 4);
    uint32_t cnt = static_cast<uint32_t>(layers.size());
    ofs.write(reinterpret_cast<const char*>(&cnt), 4);

    for (const Layer& l : layers) {
        writeStr(l.name);
        int k = static_cast<int>(l.kind), bm = static_cast<int>(l.blend);
        ofs.write(reinterpret_cast<const char*>(&k), 4);
        ofs.write(reinterpret_cast<const char*>(&bm), 4);
        ofs.write(reinterpret_cast<const char*>(&l.opacity), 4);

        const std::vector<uint8_t>* pbPtr = nullptr;
        if (l.paintBuffer && !l.paintBuffer->empty()) {
            pbPtr = l.paintBuffer.get();
        } else if (l.kind == LayerKind::RasterPaint && l.items.empty() && !l.rgba.empty()) {
            pbPtr = &l.rgba;
        }
        uint8_t flags = (l.visible ? 1 : 0) | (l.locked ? 2 : 0) |
                        (l.hasPrimaryUvBox ? 4 : 0) | (pbPtr ? 8 : 0);
        ofs.write(reinterpret_cast<const char*>(&flags), 1);
        ofs.write(reinterpret_cast<const char*>(&l.primaryUvCenter), sizeof(Vec2));
        ofs.write(reinterpret_cast<const char*>(&l.primaryUvHalf), sizeof(Vec2));

        if (pbPtr) {
            uint32_t pbLen = static_cast<uint32_t>(pbPtr->size());
            ofs.write(reinterpret_cast<const char*>(&pbLen), 4);
            ofs.write(reinterpret_cast<const char*>(pbPtr->data()), pbLen);
        }

        uint32_t itemCnt = static_cast<uint32_t>(l.items.size());
        ofs.write(reinterpret_cast<const char*>(&itemCnt), 4);
        for (const VectorItem& it : l.items) {
            writeStr(it.name);
            uint8_t ik = static_cast<uint8_t>(it.kind);
            uint8_t iflags = (it.visible ? 1 : 0) | (it.locked ? 2 : 0) | (it.hasPrimaryUvBox ? 4 : 0);
            ofs.write(reinterpret_cast<const char*>(&ik), 1);
            ofs.write(reinterpret_cast<const char*>(&iflags), 1);
            ofs.write(reinterpret_cast<const char*>(&it.primaryUvCenter), sizeof(Vec2));
            ofs.write(reinterpret_cast<const char*>(&it.primaryUvHalf), sizeof(Vec2));
            if (it.kind == VectorItemKind::PenPath) writePen(it.pen);
            else if (it.kind == VectorItemKind::RectShape) writeRect(it.rect);
            else if (it.kind == VectorItemKind::Text) writeText(it.text);
            else if (it.kind == VectorItemKind::Image) writeDecal(it.image);
        }

        if (l.kind == LayerKind::Text && l.items.empty()) writeText(l.text);
        else if (l.kind == LayerKind::Decal && l.items.empty()) writeDecal(l.decal);
        else if (l.kind == LayerKind::RulerStripe && l.items.empty()) writePen(l.ruler);
    }
    return ofs.good();
}

bool CanvasEngine::loadProject(const std::string& path, std::string& outMeshPath) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) return false;
    auto readStr = [&](std::string& s) {
        uint32_t len = 0;
        ifs.read(reinterpret_cast<char*>(&len), 4);
        if (!ifs.good() || len > (64u << 20)) { s.clear(); return; }
        s.resize(len);
        if (len) ifs.read(s.data(), len);
    };
    char magic[4] = {};
    ifs.read(magic, 4);
    const bool isV1 = (std::memcmp(magic, "LPS1", 4) == 0);
    const bool isV2 = (std::memcmp(magic, "LPS2", 4) == 0);
    const bool isCrm1 = (std::memcmp(magic, "CRM1", 4) == 0);
    if (!isV1 && !isV2 && !isCrm1) return false;

    int sz = 2048;
    ifs.read(reinterpret_cast<char*>(&sz), 4);
    int szH = sz;
    if (isCrm1) {
        ifs.read(reinterpret_cast<char*>(&szH), 4);
    }
    init(sz);
    width = sz;
    height = szH;
    layers.clear();
    readStr(outMeshPath);

    int savedActiveIdx = 0, savedSelectedIdx = -1;
    if (isCrm1) {
        ifs.read(reinterpret_cast<char*>(&savedActiveIdx), 4);
        ifs.read(reinterpret_cast<char*>(&savedSelectedIdx), 4);
    }

    auto readText = [&](TextLayerProps& tp) {
        readStr(tp.text); readStr(tp.fontPath);
        ifs.read(reinterpret_cast<char*>(&tp.fontSizePx), sizeof(float) * 5);
        ifs.read(reinterpret_cast<char*>(&tp.fillColor), sizeof(Color4u8) * 2);
        ifs.read(reinterpret_cast<char*>(&tp.uvPos), sizeof(Vec2));
        ifs.read(reinterpret_cast<char*>(&tp.pos3D), sizeof(Vec3) * 3);
        uint8_t tf = 0; int mm = 1;
        ifs.read(reinterpret_cast<char*>(&tf), 1);
        tp.anchoredIn3D = (tf & 1) != 0;
        tp.mirrorX = isCrm1 ? ((tf & 2) != 0) : false;
        ifs.read(reinterpret_cast<char*>(&tp.rotationDeg), 4);
        ifs.read(reinterpret_cast<char*>(&mm), 4);
        tp.mirrorMode = static_cast<MirrorTextMode>(mm);
        if (!isCrm1 && tp.anchoredIn3D && tp.mirrorMode != MirrorTextMode::None) {
            tp.mirrorX = true;
        }
    };
    auto readDecal = [&](DecalLayerProps& dp) {
        readStr(dp.sourcePath);
        if (isCrm1) {
            ifs.read(reinterpret_cast<char*>(&dp.imgW), 4);
            ifs.read(reinterpret_cast<char*>(&dp.imgH), 4);
            uint32_t pixLen = 0;
            ifs.read(reinterpret_cast<char*>(&pixLen), 4);
            if (pixLen > 0 && pixLen <= (256u << 20)) {
                auto buf = std::make_shared<std::vector<uint8_t>>(pixLen);
                ifs.read(reinterpret_cast<char*>(buf->data()), pixLen);
                dp.imgRgbaShared = std::move(buf);
            } else if (!dp.sourcePath.empty()) {
                int w = 0, h = 0, c = 0;
                uint8_t* px = stbi_load(dp.sourcePath.c_str(), &w, &h, &c, 4);
                if (px) {
                    dp.imgW = w; dp.imgH = h;
                    dp.imgRgbaShared = std::make_shared<std::vector<uint8_t>>(px, px + static_cast<size_t>(w) * h * 4);
                    stbi_image_free(px);
                }
            }
        } else {
            int w = 0, h = 0, c = 0;
            uint8_t* px = stbi_load(dp.sourcePath.c_str(), &w, &h, &c, 4);
            if (px) {
                dp.imgW = w; dp.imgH = h;
                dp.imgRgbaShared = std::make_shared<std::vector<uint8_t>>(px, px + static_cast<size_t>(w) * h * 4);
                stbi_image_free(px);
            }
        }
        ifs.read(reinterpret_cast<char*>(&dp.uvPos), sizeof(Vec2) * 2);
        ifs.read(reinterpret_cast<char*>(&dp.pos3D), sizeof(Vec3) * 3);
        ifs.read(reinterpret_cast<char*>(&dp.worldWidth), 4);
        uint8_t df = 0; int mm = 1;
        ifs.read(reinterpret_cast<char*>(&df), 1);
        dp.anchoredIn3D = (df & 1) != 0;
        dp.mirrorX = isCrm1 ? ((df & 2) != 0) : false;
        ifs.read(reinterpret_cast<char*>(&dp.rotationDeg), 4);
        ifs.read(reinterpret_cast<char*>(&dp.tint), sizeof(Color4u8));
        ifs.read(reinterpret_cast<char*>(&mm), 4);
        dp.mirrorMode = static_cast<MirrorTextMode>(mm);
        if (!isCrm1 && dp.anchoredIn3D && dp.mirrorMode != MirrorTextMode::None) {
            dp.mirrorX = true;
        }
    };
    auto readRect = [&](RectShapeProps& rp) {
        ifs.read(reinterpret_cast<char*>(&rp.uvPos), sizeof(Vec2) * 2);
        ifs.read(reinterpret_cast<char*>(&rp.pos3D), sizeof(Vec3) * 3);
        ifs.read(reinterpret_cast<char*>(&rp.worldWidth), sizeof(float) * 5);
        ifs.read(reinterpret_cast<char*>(&rp.fillColor), sizeof(Color4u8) * 2);
        uint8_t rf = 1;
        ifs.read(reinterpret_cast<char*>(&rf), 1);
        rp.filled = (rf & 1) != 0;
        rp.hasStroke = (rf & 2) != 0;
        rp.anchoredIn3D = (rf & 4) != 0;
        rp.mirrorX = (rf & 8) != 0;
    };
    auto readPen = [&](RulerStripeProps& rp) {
        uint32_t nc = 0;
        ifs.read(reinterpret_cast<char*>(&nc), 4);
        rp.nodes.resize(nc);
        if (isV1) {
            struct StripeNodeV1 {
                Vec2 uv;
                Vec3 pos3D;
                Vec3 nrm3D;
                bool has3D;
            };
            if (nc) {
                std::vector<StripeNodeV1> v1Nodes(nc);
                ifs.read(reinterpret_cast<char*>(v1Nodes.data()), nc * sizeof(StripeNodeV1));
                for (uint32_t j = 0; j < nc; ++j) {
                    StripeNode& dst = rp.nodes[j];
                    dst.uv = v1Nodes[j].uv;
                    dst.handleInUv = v1Nodes[j].uv;
                    dst.handleOutUv = v1Nodes[j].uv;
                    dst.pos3D = v1Nodes[j].pos3D;
                    dst.handleIn3D = v1Nodes[j].pos3D;
                    dst.handleOut3D = v1Nodes[j].pos3D;
                    dst.nrm3D = v1Nodes[j].nrm3D;
                    dst.has3D = v1Nodes[j].has3D;
                    dst.hasHandles = false;
                    dst.smooth = true;
                }
            }
        } else {
            for (uint32_t j = 0; j < nc; ++j) {
                StripeNode& sn = rp.nodes[j];
                ifs.read(reinterpret_cast<char*>(&sn.uv), sizeof(Vec2));
                ifs.read(reinterpret_cast<char*>(&sn.handleInUv), sizeof(Vec2));
                ifs.read(reinterpret_cast<char*>(&sn.handleOutUv), sizeof(Vec2));
                ifs.read(reinterpret_cast<char*>(&sn.pos3D), sizeof(Vec3));
                ifs.read(reinterpret_cast<char*>(&sn.handleIn3D), sizeof(Vec3));
                ifs.read(reinterpret_cast<char*>(&sn.handleOut3D), sizeof(Vec3));
                ifs.read(reinterpret_cast<char*>(&sn.nrm3D), sizeof(Vec3));
                uint8_t nFlags = 0;
                ifs.read(reinterpret_cast<char*>(&nFlags), 1);
                sn.has3D = (nFlags & 1) != 0;
                sn.hasHandles = (nFlags & 2) != 0;
                sn.smooth = (nFlags & 4) != 0;
            }
        }
        ifs.read(reinterpret_cast<char*>(&rp.widthPx), sizeof(float) * 3);
        uint8_t rf = 7;
        ifs.read(reinterpret_cast<char*>(&rf), 1);
        rp.smoothCurve = (rf & 1) != 0;
        rp.use3DSpace = (rf & 2) != 0;
        rp.mirrorX = (rf & 4) != 0;
        if (!isV1) {
            rp.closed = (rf & 8) != 0;
            rp.filled = (rf & 16) != 0;
            rp.hasStroke = (rf & 32) != 0;
        } else {
            rp.closed = false;
            rp.filled = false;
            rp.hasStroke = true;
        }
        ifs.read(reinterpret_cast<char*>(&rp.color), sizeof(Color4u8) * 2);
        ifs.read(reinterpret_cast<char*>(&rp.outlineWidth), 4);
        if (!isV1) {
            ifs.read(reinterpret_cast<char*>(&rp.fillColor), sizeof(Color4u8));
        }
    };

    uint32_t cnt = 0;
    ifs.read(reinterpret_cast<char*>(&cnt), 4);
    for (uint32_t i = 0; i < cnt; ++i) {
        Layer l;
        l.id = nextLayerId++;
        readStr(l.name);
        int k = 0, bm = 0;
        ifs.read(reinterpret_cast<char*>(&k), 4);
        ifs.read(reinterpret_cast<char*>(&bm), 4);
        l.kind = static_cast<LayerKind>(k);
        l.blend = static_cast<BlendMode>(bm);
        ifs.read(reinterpret_cast<char*>(&l.opacity), 4);
        uint8_t flags = 1;
        ifs.read(reinterpret_cast<char*>(&flags), 1);
        l.visible = (flags & 1) != 0;
        l.locked = (flags & 2) != 0;
        if (isV2 || isCrm1) {
            l.hasPrimaryUvBox = (flags & 4) != 0;
            ifs.read(reinterpret_cast<char*>(&l.primaryUvCenter), sizeof(Vec2));
            ifs.read(reinterpret_cast<char*>(&l.primaryUvHalf), sizeof(Vec2));
        }
        l.rgba.assign(static_cast<size_t>(width) * height * 4, 0);

        if (isCrm1) {
            if ((flags & 8) != 0) {
                uint32_t pbLen = 0;
                ifs.read(reinterpret_cast<char*>(&pbLen), 4);
                if (pbLen > 0 && pbLen <= (512u << 20)) {
                    auto& pb = l.ensureUniquePaintBuffer(width, height);
                    if (pbLen == pb.size()) {
                        ifs.read(reinterpret_cast<char*>(pb.data()), pbLen);
                        l.rgba = pb;
                    } else {
                        std::vector<uint8_t> tmp(pbLen);
                        ifs.read(reinterpret_cast<char*>(tmp.data()), pbLen);
                        size_t copyLen = std::min(tmp.size(), pb.size());
                        std::memcpy(pb.data(), tmp.data(), copyLen);
                        l.rgba = pb;
                    }
                }
            }
            uint32_t itemCnt = 0;
            ifs.read(reinterpret_cast<char*>(&itemCnt), 4);
            l.items.reserve(itemCnt);
            for (uint32_t j = 0; j < itemCnt; ++j) {
                VectorItem it;
                it.id = nextItemId++;
                readStr(it.name);
                uint8_t ik = 0, iflags = 1;
                ifs.read(reinterpret_cast<char*>(&ik), 1);
                ifs.read(reinterpret_cast<char*>(&iflags), 1);
                it.kind = static_cast<VectorItemKind>(ik);
                it.visible = (iflags & 1) != 0;
                it.locked = (iflags & 2) != 0;
                it.hasPrimaryUvBox = (iflags & 4) != 0;
                ifs.read(reinterpret_cast<char*>(&it.primaryUvCenter), sizeof(Vec2));
                ifs.read(reinterpret_cast<char*>(&it.primaryUvHalf), sizeof(Vec2));
                if (it.kind == VectorItemKind::PenPath) readPen(it.pen);
                else if (it.kind == VectorItemKind::RectShape) readRect(it.rect);
                else if (it.kind == VectorItemKind::Text) readText(it.text);
                else if (it.kind == VectorItemKind::Image) readDecal(it.image);
                l.items.push_back(std::move(it));
            }
            if (l.kind == LayerKind::Text && l.items.empty()) readText(l.text);
            else if (l.kind == LayerKind::Decal && l.items.empty()) readDecal(l.decal);
            else if (l.kind == LayerKind::RulerStripe && l.items.empty()) readPen(l.ruler);
            l.dirtyVector = (!l.items.empty() || l.kind != LayerKind::RasterPaint);
        } else {
            if (l.kind == LayerKind::RasterPaint) {
                auto& pb = l.ensureUniquePaintBuffer(width, height);
                ifs.read(reinterpret_cast<char*>(pb.data()), pb.size());
                l.rgba = pb;
                l.dirtyVector = false;
            } else if (l.kind == LayerKind::Text) {
                readText(l.text);
                l.dirtyVector = true;
            } else if (l.kind == LayerKind::Decal) {
                readDecal(l.decal);
                l.dirtyVector = true;
            } else if (l.kind == LayerKind::RulerStripe) {
                readPen(l.ruler);
                l.dirtyVector = true;
            }
        }
        layers.push_back(std::move(l));
    }

    if (isCrm1 && !layers.empty()) {
        activeLayerIdx = std::clamp(savedActiveIdx, 0, static_cast<int>(layers.size()) - 1);
        if (!layers[activeLayerIdx].items.empty()) {
            selectedItemIdx = std::clamp(savedSelectedIdx, -1, static_cast<int>(layers[activeLayerIdx].items.size()) - 1);
        } else {
            selectedItemIdx = -1;
        }
    } else {
        activeLayerIdx = layers.empty() ? 0 : static_cast<int>(layers.size()) - 1;
        selectedItemIdx = -1;
    }
    undoStack.clear();
    redoStack.clear();
    return ifs.good();
}

} // namespace lps
