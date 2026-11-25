#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace lps {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kDeg2Rad = kPi / 180.0f;
constexpr float kRad2Deg = 180.0f / kPi;

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}
    constexpr Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(float s) const { return {x * s, y * s}; }
    constexpr Vec2 operator/(float s) const { return {x / s, y / s}; }
    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
};

inline float dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
inline float lengthSq(const Vec2& v) { return v.x * v.x + v.y * v.y; }
inline float length(const Vec2& v) { return std::sqrt(lengthSq(v)); }
inline Vec2 normalized(const Vec2& v) {
    float l = length(v);
    return (l > 1e-8f) ? (v / l) : Vec2{0.0f, 0.0f};
}

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator-() const { return {-x, -y, -z}; }
    constexpr Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator*(const Vec3& o) const { return {x * o.x, y * o.y, z * o.z}; }
    constexpr Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    float& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
    float operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
};

inline constexpr Vec3 operator*(float s, const Vec3& v) { return v * s; }
inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float lengthSq(const Vec3& v) { return dot(v, v); }
inline float length(const Vec3& v) { return std::sqrt(lengthSq(v)); }
inline Vec3 normalized(const Vec3& v) {
    float l = length(v);
    return (l > 1e-8f) ? (v / l) : Vec3{0.0f, 1.0f, 0.0f};
}
inline Vec3 lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }
inline Vec3 minVec(const Vec3& a, const Vec3& b) {
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}
inline Vec3 maxVec(const Vec3& a, const Vec3& b) {
    return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}

struct Vec4 {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
    constexpr Vec4() = default;
    constexpr Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
};

// Row-major 4x4 matrix tailored for HLSL (row_major) & Direct3D11
struct Mat4 {
    float m[4][4] = {};

    static Mat4 identity() {
        Mat4 r{};
        r.m[0][0] = r.m[1][1] = r.m[2][2] = r.m[3][3] = 1.0f;
        return r;
    }

    static Mat4 translation(const Vec3& t) {
        Mat4 r = identity();
        r.m[3][0] = t.x; r.m[3][1] = t.y; r.m[3][2] = t.z;
        return r;
    }

    static Mat4 scale(const Vec3& s) {
        Mat4 r{};
        r.m[0][0] = s.x; r.m[1][1] = s.y; r.m[2][2] = s.z; r.m[3][3] = 1.0f;
        return r;
    }

    static Mat4 lookAtLH(const Vec3& eye, const Vec3& target, const Vec3& up) {
        Vec3 zaxis = normalized(target - eye);
        Vec3 xaxis = normalized(cross(up, zaxis));
        Vec3 yaxis = cross(zaxis, xaxis);
        Mat4 r = identity();
        r.m[0][0] = xaxis.x; r.m[0][1] = yaxis.x; r.m[0][2] = zaxis.x;
        r.m[1][0] = xaxis.y; r.m[1][1] = yaxis.y; r.m[1][2] = zaxis.y;
        r.m[2][0] = xaxis.z; r.m[2][1] = yaxis.z; r.m[2][2] = zaxis.z;
        r.m[3][0] = -dot(xaxis, eye);
        r.m[3][1] = -dot(yaxis, eye);
        r.m[3][2] = -dot(zaxis, eye);
        return r;
    }

    static Mat4 perspectiveFovLH(float fovYRad, float aspect, float zn, float zf) {
        float yScale = 1.0f / std::tan(fovYRad * 0.5f);
        float xScale = yScale / std::max(aspect, 1e-4f);
        Mat4 r{};
        r.m[0][0] = xScale;
        r.m[1][1] = yScale;
        r.m[2][2] = zf / (zf - zn);
        r.m[2][3] = 1.0f;
        r.m[3][2] = (-zn * zf) / (zf - zn);
        return r;
    }

    static Mat4 orthoOffCenterLH(float halfW, float halfH, float zn, float zf) {
        Mat4 r = identity();
        r.m[0][0] = 1.0f / std::max(halfW, 1e-4f);
        r.m[1][1] = 1.0f / std::max(halfH, 1e-4f);
        r.m[2][2] = 1.0f / (zf - zn);
        r.m[3][2] = -zn / (zf - zn);
        return r;
    }
};

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j]
                      + a.m[i][2] * b.m[2][j] + a.m[i][3] * b.m[3][j];
        }
    }
    return r;
}

inline Vec4 transformPoint4(const Mat4& m, const Vec3& v) {
    return {
        v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0] + m.m[3][0],
        v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1] + m.m[3][1],
        v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2] + m.m[3][2],
        v.x * m.m[0][3] + v.y * m.m[1][3] + v.z * m.m[2][3] + m.m[3][3]
    };
}

inline Mat4 inverseMat4(const Mat4& mat) {
    const float* a = &mat.m[0][0];
    float inv[16];
    inv[0]  =  a[5]*a[10]*a[15] - a[5]*a[11]*a[14] - a[9]*a[6]*a[15] + a[9]*a[7]*a[14] + a[13]*a[6]*a[11] - a[13]*a[7]*a[10];
    inv[4]  = -a[4]*a[10]*a[15] + a[4]*a[11]*a[14] + a[8]*a[6]*a[15] - a[8]*a[7]*a[14] - a[12]*a[6]*a[11] + a[12]*a[7]*a[10];
    inv[8]  =  a[4]*a[9]*a[15]  - a[4]*a[11]*a[13] - a[8]*a[5]*a[15] + a[8]*a[7]*a[13] + a[12]*a[5]*a[11] - a[12]*a[7]*a[9];
    inv[12] = -a[4]*a[9]*a[14]  + a[4]*a[10]*a[13] + a[8]*a[5]*a[14] - a[8]*a[6]*a[13] - a[12]*a[5]*a[10] + a[12]*a[6]*a[9];
    inv[1]  = -a[1]*a[10]*a[15] + a[1]*a[11]*a[14] + a[9]*a[2]*a[15] - a[9]*a[3]*a[14] - a[13]*a[2]*a[11] + a[13]*a[3]*a[10];
    inv[5]  =  a[0]*a[10]*a[15] - a[0]*a[11]*a[14] - a[8]*a[2]*a[15] + a[8]*a[3]*a[14] + a[12]*a[2]*a[11] - a[12]*a[3]*a[10];
    inv[9]  = -a[0]*a[9]*a[15]  + a[0]*a[11]*a[13] + a[8]*a[1]*a[15] - a[8]*a[3]*a[13] - a[12]*a[1]*a[11] + a[12]*a[3]*a[9];
    inv[13] =  a[0]*a[9]*a[14]  - a[0]*a[10]*a[13] - a[8]*a[1]*a[14] + a[8]*a[2]*a[13] + a[12]*a[1]*a[10] - a[12]*a[2]*a[9];
    inv[2]  =  a[1]*a[6]*a[15]  - a[1]*a[7]*a[14]  - a[5]*a[2]*a[15] + a[5]*a[3]*a[14] + a[13]*a[2]*a[7]  - a[13]*a[3]*a[6];
    inv[6]  = -a[0]*a[6]*a[15]  + a[0]*a[7]*a[14]  + a[4]*a[2]*a[15] - a[4]*a[3]*a[14] - a[12]*a[2]*a[7]  + a[12]*a[3]*a[6];
    inv[10] =  a[0]*a[5]*a[15]  - a[0]*a[7]*a[13]  - a[4]*a[1]*a[15] + a[4]*a[3]*a[13] + a[12]*a[1]*a[7]  - a[12]*a[3]*a[5];
    inv[14] = -a[0]*a[5]*a[14]  + a[0]*a[6]*a[13]  + a[4]*a[1]*a[14] - a[4]*a[2]*a[13] - a[12]*a[1]*a[6]  + a[12]*a[2]*a[5];
    inv[3]  = -a[1]*a[6]*a[11]  + a[1]*a[7]*a[10]  + a[5]*a[2]*a[11] - a[5]*a[3]*a[10] - a[9]*a[2]*a[7]   + a[9]*a[3]*a[6];
    inv[7]  =  a[0]*a[6]*a[11]  - a[0]*a[7]*a[10]  - a[4]*a[2]*a[11] + a[4]*a[3]*a[10] + a[8]*a[2]*a[7]   - a[8]*a[3]*a[6];
    inv[11] = -a[0]*a[5]*a[11]  + a[0]*a[7]*a[9]   + a[4]*a[1]*a[11] - a[4]*a[3]*a[9]  - a[8]*a[1]*a[7]   + a[8]*a[3]*a[5];
    inv[15] =  a[0]*a[5]*a[10]  - a[0]*a[6]*a[9]   - a[4]*a[1]*a[10] + a[4]*a[2]*a[9]  + a[8]*a[1]*a[6]   - a[8]*a[2]*a[5];

    float det = a[0]*inv[0] + a[1]*inv[4] + a[2]*inv[8] + a[3]*inv[12];
    if (std::fabs(det) < 1e-12f) return Mat4::identity();
    float id = 1.0f / det;
    Mat4 out{};
    float* dst = &out.m[0][0];
    for (int i = 0; i < 16; ++i) dst[i] = inv[i] * id;
    return out;
}

struct AABB {
    Vec3 min{ std::numeric_limits<float>::max(),  std::numeric_limits<float>::max(),  std::numeric_limits<float>::max()};
    Vec3 max{-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()};
    void expand(const Vec3& p) { min = minVec(min, p); max = maxVec(max, p); }
    void expand(const AABB& b) { min = minVec(min, b.min); max = maxVec(max, b.max); }
    Vec3 center() const { return (min + max) * 0.5f; }
    Vec3 extent() const { return max - min; }
};

struct Ray {
    Vec3 origin;
    Vec3 dir; // normalized
};

inline bool rayAABB(const Ray& r, const Vec3& invDir, const AABB& b, float maxT, float& tMinOut) {
    float t1 = (b.min.x - r.origin.x) * invDir.x;
    float t2 = (b.max.x - r.origin.x) * invDir.x;
    float tmin = std::min(t1, t2);
    float tmax = std::max(t1, t2);

    t1 = (b.min.y - r.origin.y) * invDir.y;
    t2 = (b.max.y - r.origin.y) * invDir.y;
    tmin = std::max(tmin, std::min(t1, t2));
    tmax = std::min(tmax, std::max(t1, t2));

    t1 = (b.min.z - r.origin.z) * invDir.z;
    t2 = (b.max.z - r.origin.z) * invDir.z;
    tmin = std::max(tmin, std::min(t1, t2));
    tmax = std::min(tmax, std::max(t1, t2));

    if (tmax < 0.0f || tmin > tmax || tmin > maxT) return false;
    tMinOut = tmin;
    return true;
}

inline bool rayTriangle(const Ray& r, const Vec3& v0, const Vec3& v1, const Vec3& v2,
                        float maxT, float& tOut, float& uBary, float& vBary) {
    Vec3 e1 = v1 - v0;
    Vec3 e2 = v2 - v0;
    Vec3 p = cross(r.dir, e2);
    float det = dot(e1, p);
    if (std::fabs(det) < 1e-9f) return false;
    float invDet = 1.0f / det;
    Vec3 s = r.origin - v0;
    uBary = dot(s, p) * invDet;
    if (uBary < 0.0f || uBary > 1.0f) return false;
    Vec3 q = cross(s, e1);
    vBary = dot(r.dir, q) * invDet;
    if (vBary < 0.0f || (uBary + vBary) > 1.0f) return false;
    float t = dot(e2, q) * invDet;
    if (t < 1e-5f || t > maxT) return false;
    tOut = t;
    return true;
}

} // namespace lps
