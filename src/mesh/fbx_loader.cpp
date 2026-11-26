#include "mesh/fbx_loader.hpp"
#include "mesh/obj_mesh.hpp"
#include "stb/stb_image.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace lps {

namespace {

struct FbxProp {
    enum class Kind {
        None,
        Bool,
        Int,
        Double,
        String,
        Raw,
        DoubleArr,
        IntArr
    };
    Kind kind = Kind::None;
    bool bVal = false;
    int64_t iVal = 0;
    double dVal = 0.0;
    std::string sVal;
    std::vector<double> dArr;
    std::vector<int64_t> iArr;

    double asDouble(double def = 0.0) const {
        if (kind == Kind::Double) return dVal;
        if (kind == Kind::Int) return static_cast<double>(iVal);
        if (kind == Kind::Bool) return bVal ? 1.0 : 0.0;
        return def;
    }
    int64_t asInt(int64_t def = 0) const {
        if (kind == Kind::Int) return iVal;
        if (kind == Kind::Double) return static_cast<int64_t>(std::llround(dVal));
        if (kind == Kind::Bool) return bVal ? 1 : 0;
        return def;
    }
};

struct FbxNode {
    std::string name;
    std::vector<FbxProp> props;
    std::vector<FbxNode> children;

    const FbxNode* findChild(const char* childName) const {
        for (const FbxNode& c : children) {
            if (c.name == childName) return &c;
        }
        return nullptr;
    }
};

// ---------------- Binary FBX Reader ----------------
struct BinCursor {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t pos = 0;

    bool has(size_t n) const { return pos <= size && (size - pos) >= n; }

    template <typename T>
    bool read(T& out) {
        if (!has(sizeof(T))) return false;
        std::memcpy(&out, data + pos, sizeof(T));
        pos += sizeof(T);
        return true;
    }

    bool readBytes(void* dst, size_t n) {
        if (!has(n)) return false;
        if (n > 0) std::memcpy(dst, data + pos, n);
        pos += n;
        return true;
    }
};

bool readBinProp(BinCursor& cur, FbxProp& out) {
    char typeCode = 0;
    if (!cur.read(typeCode)) return false;
    switch (typeCode) {
        case 'Y': {
            int16_t v = 0;
            if (!cur.read(v)) return false;
            out.kind = FbxProp::Kind::Int;
            out.iVal = v;
            return true;
        }
        case 'C': {
            uint8_t v = 0;
            if (!cur.read(v)) return false;
            out.kind = FbxProp::Kind::Bool;
            out.bVal = (v != 0);
            return true;
        }
        case 'I': {
            int32_t v = 0;
            if (!cur.read(v)) return false;
            out.kind = FbxProp::Kind::Int;
            out.iVal = v;
            return true;
        }
        case 'F': {
            float v = 0.0f;
            if (!cur.read(v)) return false;
            out.kind = FbxProp::Kind::Double;
            out.dVal = static_cast<double>(v);
            return true;
        }
        case 'D': {
            double v = 0.0;
            if (!cur.read(v)) return false;
            out.kind = FbxProp::Kind::Double;
            out.dVal = v;
            return true;
        }
        case 'L': {
            int64_t v = 0;
            if (!cur.read(v)) return false;
            out.kind = FbxProp::Kind::Int;
            out.iVal = v;
            return true;
        }
        case 'S':
        case 'R': {
            uint32_t len = 0;
            if (!cur.read(len) || !cur.has(len)) return false;
            out.kind = (typeCode == 'S') ? FbxProp::Kind::String : FbxProp::Kind::Raw;
            out.sVal.resize(len);
            if (len > 0) cur.readBytes(out.sVal.data(), len);
            if (typeCode == 'S') {
                // Binary FBX strings sometimes use "Name\x00\x01Class"; keep full or normalize? Keep as-is, parse cleanly later.
            }
            return true;
        }
        case 'f':
        case 'd':
        case 'i':
        case 'l':
        case 'b':
        case 'c': {
            uint32_t arrLen = 0, encoding = 0, compLen = 0;
            if (!cur.read(arrLen) || !cur.read(encoding) || !cur.read(compLen)) return false;
            if (!cur.has(compLen)) return false;
            size_t elemSize = 4;
            if (typeCode == 'd' || typeCode == 'l') elemSize = 8;
            else if (typeCode == 'b' || typeCode == 'c') elemSize = 1;
            if (arrLen > (128u << 20) / elemSize) return false;

            const size_t rawBytes = static_cast<size_t>(arrLen) * elemSize;
            std::vector<uint8_t> rawBuf;
            const uint8_t* srcPtr = nullptr;

            if (encoding == 0) {
                if (compLen < rawBytes) return false;
                srcPtr = cur.data + cur.pos;
                cur.pos += compLen;
            } else if (encoding == 1) {
                rawBuf.resize(rawBytes);
                if (rawBytes > 0) {
                    int dec = stbi_zlib_decode_buffer(
                        reinterpret_cast<char*>(rawBuf.data()), static_cast<int>(rawBytes),
                        reinterpret_cast<const char*>(cur.data + cur.pos), static_cast<int>(compLen));
                    if (dec < 0 || static_cast<size_t>(dec) != rawBytes) return false;
                }
                cur.pos += compLen;
                srcPtr = rawBuf.data();
            } else {
                return false;
            }

            if (typeCode == 'f') {
                out.kind = FbxProp::Kind::DoubleArr;
                out.dArr.resize(arrLen);
                for (uint32_t k = 0; k < arrLen; ++k) {
                    float v = 0.0f;
                    std::memcpy(&v, srcPtr + static_cast<size_t>(k) * 4, 4);
                    out.dArr[k] = static_cast<double>(v);
                }
            } else if (typeCode == 'd') {
                out.kind = FbxProp::Kind::DoubleArr;
                out.dArr.resize(arrLen);
                if (arrLen > 0) std::memcpy(out.dArr.data(), srcPtr, rawBytes);
            } else if (typeCode == 'i') {
                out.kind = FbxProp::Kind::IntArr;
                out.iArr.resize(arrLen);
                for (uint32_t k = 0; k < arrLen; ++k) {
                    int32_t v = 0;
                    std::memcpy(&v, srcPtr + static_cast<size_t>(k) * 4, 4);
                    out.iArr[k] = v;
                }
            } else if (typeCode == 'l') {
                out.kind = FbxProp::Kind::IntArr;
                out.iArr.resize(arrLen);
                if (arrLen > 0) std::memcpy(out.iArr.data(), srcPtr, rawBytes);
            } else {
                out.kind = FbxProp::Kind::IntArr;
                out.iArr.resize(arrLen);
                for (uint32_t k = 0; k < arrLen; ++k) out.iArr[k] = srcPtr[k];
            }
            return true;
        }
        default:
            return false;
    }
}

bool readBinNode(BinCursor& cur, bool is64, FbxNode& out, bool& isNullSentinel) {
    isNullSentinel = false;
    uint64_t endOffset = 0, numProps = 0, propListLen = 0;
    if (is64) {
        if (!cur.read(endOffset) || !cur.read(numProps) || !cur.read(propListLen)) return false;
    } else {
        uint32_t a = 0, b = 0, c = 0;
        if (!cur.read(a) || !cur.read(b) || !cur.read(c)) return false;
        endOffset = a; numProps = b; propListLen = c;
    }
    uint8_t nameLen = 0;
    if (!cur.read(nameLen)) return false;

    if (endOffset == 0 && numProps == 0 && propListLen == 0 && nameLen == 0) {
        isNullSentinel = true;
        return true;
    }
    if (!cur.has(nameLen) || endOffset > cur.size || endOffset < cur.pos + nameLen) return false;
    out.name.resize(nameLen);
    if (nameLen > 0) cur.readBytes(out.name.data(), nameLen);

    size_t propsEnd = cur.pos + static_cast<size_t>(propListLen);
    if (propsEnd > endOffset) return false;
    out.props.resize(static_cast<size_t>(numProps));
    for (uint64_t i = 0; i < numProps; ++i) {
        if (!readBinProp(cur, out.props[static_cast<size_t>(i)])) return false;
    }
    cur.pos = propsEnd;

    while (cur.pos < endOffset) {
        FbxNode child;
        bool childNull = false;
        if (!readBinNode(cur, is64, child, childNull)) return false;
        if (childNull) break;
        out.children.push_back(std::move(child));
    }
    cur.pos = static_cast<size_t>(endOffset);
    return true;
}

bool parseBinaryFbx(const std::vector<uint8_t>& fileBytes, FbxNode& rootOut) {
    if (fileBytes.size() < 27) return false;
    static const char kMagic[23] = "Kaydara FBX Binary  \x00\x1a";
    if (std::memcmp(fileBytes.data(), kMagic, 22) != 0) return false;
    uint32_t version = 0;
    std::memcpy(&version, fileBytes.data() + 23, 4);
    bool is64 = (version >= 7500);

    BinCursor cur{fileBytes.data(), fileBytes.size(), 27};
    rootOut.name = "Root";
    while (cur.pos < cur.size) {
        FbxNode top;
        bool isNull = false;
        if (!readBinNode(cur, is64, top, isNull)) break;
        if (isNull) break;
        rootOut.children.push_back(std::move(top));
    }
    return !rootOut.children.empty();
}

// ---------------- ASCII FBX Reader ----------------
struct AsciiTok {
    enum class Kind { Ident, String, Number, Colon, LBrace, RBrace, Star, Comma, End };
    Kind kind = Kind::End;
    std::string text;
};

std::vector<AsciiTok> tokenizeAsciiFbx(const std::vector<uint8_t>& bytes) {
    std::vector<AsciiTok> toks;
    const size_t n = bytes.size();
    size_t i = 0;
    while (i < n) {
        char c = static_cast<char>(bytes[i]);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            ++i;
            continue;
        }
        if (c == ';') {
            while (i < n && bytes[i] != '\n') ++i;
            continue;
        }
        if (c == '{') { toks.push_back({AsciiTok::Kind::LBrace, "{"}); ++i; continue; }
        if (c == '}') { toks.push_back({AsciiTok::Kind::RBrace, "}"}); ++i; continue; }
        if (c == ':') { toks.push_back({AsciiTok::Kind::Colon, ":"}); ++i; continue; }
        if (c == '*') { toks.push_back({AsciiTok::Kind::Star, "*"}); ++i; continue; }
        if (c == ',') { toks.push_back({AsciiTok::Kind::Comma, ","}); ++i; continue; }
        if (c == '"') {
            ++i;
            std::string s;
            while (i < n && bytes[i] != '"') {
                s.push_back(static_cast<char>(bytes[i]));
                ++i;
            }
            if (i < n && bytes[i] == '"') ++i;
            toks.push_back({AsciiTok::Kind::String, std::move(s)});
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+' || c == '.') {
            size_t st = i;
            ++i;
            while (i < n) {
                char ch = static_cast<char>(bytes[i]);
                if (std::isdigit(static_cast<unsigned char>(ch)) || ch == '.' || ch == 'e' || ch == 'E' ||
                    ch == '+' || ch == '-') {
                    ++i;
                } else {
                    break;
                }
            }
            toks.push_back({AsciiTok::Kind::Number, std::string(reinterpret_cast<const char*>(bytes.data() + st), i - st)});
            continue;
        }
        size_t st = i;
        while (i < n) {
            char ch = static_cast<char>(bytes[i]);
            if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-' || ch == '.') {
                ++i;
            } else {
                break;
            }
        }
        if (i == st) { ++i; continue; }
        toks.push_back({AsciiTok::Kind::Ident, std::string(reinterpret_cast<const char*>(bytes.data() + st), i - st)});
    }
    toks.push_back({AsciiTok::Kind::End, ""});
    return toks;
}

bool parseAsciiNodeList(const std::vector<AsciiTok>& toks, size_t& idx, bool stopAtRBrace, std::vector<FbxNode>& out) {
    while (idx < toks.size()) {
        if (toks[idx].kind == AsciiTok::Kind::End) break;
        if (stopAtRBrace && toks[idx].kind == AsciiTok::Kind::RBrace) {
            ++idx;
            return true;
        }
        if (toks[idx].kind != AsciiTok::Kind::Ident) {
            ++idx;
            continue;
        }
        FbxNode node;
        node.name = toks[idx].text;
        ++idx;
        if (idx < toks.size() && toks[idx].kind == AsciiTok::Kind::Colon) {
            ++idx;
            // Read properties until '{' or next 'Ident :' or '}' or End
            while (idx < toks.size()) {
                const AsciiTok& t = toks[idx];
                if (t.kind == AsciiTok::Kind::LBrace || t.kind == AsciiTok::Kind::RBrace || t.kind == AsciiTok::Kind::End) {
                    break;
                }
                if (t.kind == AsciiTok::Kind::Ident && idx + 1 < toks.size() && toks[idx + 1].kind == AsciiTok::Kind::Colon) {
                    break;
                }
                if (t.kind == AsciiTok::Kind::Comma) {
                    ++idx;
                    continue;
                }
                if (t.kind == AsciiTok::Kind::Star) {
                    // Array block: *N { a: v0,v1,... }
                    ++idx;
                    if (idx < toks.size() && toks[idx].kind == AsciiTok::Kind::Number) ++idx;
                    if (idx < toks.size() && toks[idx].kind == AsciiTok::Kind::LBrace) {
                        ++idx;
                        if (idx + 1 < toks.size() && toks[idx].kind == AsciiTok::Kind::Ident && toks[idx].text == "a" &&
                            toks[idx + 1].kind == AsciiTok::Kind::Colon) {
                            idx += 2;
                        }
                        FbxProp arrP;
                        arrP.kind = FbxProp::Kind::DoubleArr;
                        while (idx < toks.size() && toks[idx].kind != AsciiTok::Kind::RBrace &&
                               toks[idx].kind != AsciiTok::Kind::End) {
                            if (toks[idx].kind == AsciiTok::Kind::Number) {
                                arrP.dArr.push_back(std::strtod(toks[idx].text.c_str(), nullptr));
                            }
                            ++idx;
                        }
                        if (idx < toks.size() && toks[idx].kind == AsciiTok::Kind::RBrace) ++idx;
                        node.props.push_back(std::move(arrP));
                        break;
                    }
                    continue;
                }
                if (t.kind == AsciiTok::Kind::String) {
                    FbxProp p;
                    p.kind = FbxProp::Kind::String;
                    p.sVal = t.text;
                    node.props.push_back(std::move(p));
                    ++idx;
                } else if (t.kind == AsciiTok::Kind::Number) {
                    FbxProp p;
                    if (t.text.find('.') != std::string::npos || t.text.find('e') != std::string::npos ||
                        t.text.find('E') != std::string::npos) {
                        p.kind = FbxProp::Kind::Double;
                        p.dVal = std::strtod(t.text.c_str(), nullptr);
                    } else {
                        p.kind = FbxProp::Kind::Int;
                        p.iVal = std::strtoll(t.text.c_str(), nullptr, 10);
                    }
                    node.props.push_back(std::move(p));
                    ++idx;
                } else if (t.kind == AsciiTok::Kind::Ident) {
                    FbxProp p;
                    p.kind = FbxProp::Kind::String;
                    p.sVal = t.text;
                    node.props.push_back(std::move(p));
                    ++idx;
                } else {
                    ++idx;
                }
            }
        }
        if (idx < toks.size() && toks[idx].kind == AsciiTok::Kind::LBrace) {
            ++idx;
            parseAsciiNodeList(toks, idx, true, node.children);
        }
        out.push_back(std::move(node));
    }
    return true;
}

bool parseAsciiFbx(const std::vector<uint8_t>& fileBytes, FbxNode& rootOut) {
    auto toks = tokenizeAsciiFbx(fileBytes);
    size_t idx = 0;
    rootOut.name = "Root";
    parseAsciiNodeList(toks, idx, false, rootOut.children);
    return !rootOut.children.empty();
}

// ---------------- Scene Assembly Helpers ----------------
std::string cleanFbxObjectName(const std::string& raw) {
    size_t nul = raw.find('\0');
    std::string s = (nul != std::string::npos) ? raw.substr(0, nul) : raw;
    size_t sep = s.find("::");
    if (sep != std::string::npos) s = s.substr(sep + 2);
    return s;
}

Mat4 rotX(float rad) {
    Mat4 r = Mat4::identity();
    float c = std::cos(rad), s = std::sin(rad);
    r.m[1][1] = c; r.m[1][2] = s;
    r.m[2][1] = -s; r.m[2][2] = c;
    return r;
}

Mat4 rotY(float rad) {
    Mat4 r = Mat4::identity();
    float c = std::cos(rad), s = std::sin(rad);
    r.m[0][0] = c; r.m[0][2] = -s;
    r.m[2][0] = s; r.m[2][2] = c;
    return r;
}

Mat4 rotZ(float rad) {
    Mat4 r = Mat4::identity();
    float c = std::cos(rad), s = std::sin(rad);
    r.m[0][0] = c; r.m[0][1] = s;
    r.m[1][0] = -s; r.m[1][1] = c;
    return r;
}

Mat4 eulerXYZ(const Vec3& deg) {
    return rotX(deg.x * kDeg2Rad) * rotY(deg.y * kDeg2Rad) * rotZ(deg.z * kDeg2Rad);
}

struct FbxModelData {
    int64_t id = 0;
    std::string name;
    int64_t parentId = 0;
    Vec3 lclT{0, 0, 0};
    Vec3 preRot{0, 0, 0};
    Vec3 lclR{0, 0, 0};
    Vec3 lclS{1, 1, 1};
    Vec3 geomT{0, 0, 0};
    Vec3 geomR{0, 0, 0};
    Vec3 geomS{1, 1, 1};
    bool hasWorld = false;
    Mat4 worldM = Mat4::identity();
};

struct FbxGeomData {
    int64_t id = 0;
    std::string name;
    const FbxNode* node = nullptr;
};

void extractDoubleVec(const FbxNode* n, std::vector<double>& out) {
    if (!n) return;
    for (const FbxProp& p : n->props) {
        if (p.kind == FbxProp::Kind::DoubleArr) {
            out = p.dArr;
            return;
        }
        if (p.kind == FbxProp::Kind::IntArr) {
            out.resize(p.iArr.size());
            for (size_t i = 0; i < p.iArr.size(); ++i) out[i] = static_cast<double>(p.iArr[i]);
            return;
        }
    }
    if (n->props.size() >= 3) {
        for (const FbxProp& p : n->props) {
            if (p.kind == FbxProp::Kind::Double || p.kind == FbxProp::Kind::Int) {
                out.push_back(p.asDouble());
            }
        }
    }
}

void extractIntVec(const FbxNode* n, std::vector<int64_t>& out) {
    if (!n) return;
    for (const FbxProp& p : n->props) {
        if (p.kind == FbxProp::Kind::IntArr) {
            out = p.iArr;
            return;
        }
        if (p.kind == FbxProp::Kind::DoubleArr) {
            out.resize(p.dArr.size());
            for (size_t i = 0; i < p.dArr.size(); ++i) out[i] = static_cast<int64_t>(std::llround(p.dArr[i]));
            return;
        }
    }
}

std::string firstStringProp(const FbxNode* n) {
    if (!n) return {};
    for (const FbxProp& p : n->props) {
        if (p.kind == FbxProp::Kind::String) return p.sVal;
    }
    return {};
}

Mat4 evalModelWorld(int64_t modelId, std::unordered_map<int64_t, FbxModelData>& models, int depth = 0) {
    auto it = models.find(modelId);
    if (it == models.end() || depth > 32) return Mat4::identity();
    FbxModelData& m = it->second;
    if (m.hasWorld) return m.worldM;

    Mat4 local = Mat4::scale(m.lclS) * eulerXYZ(m.lclR) * eulerXYZ(m.preRot) * Mat4::translation(m.lclT);
    Mat4 geom = Mat4::scale(m.geomS) * eulerXYZ(m.geomR) * Mat4::translation(m.geomT);
    Mat4 parentW = (m.parentId != 0 && m.parentId != m.id) ? evalModelWorld(m.parentId, models, depth + 1) : Mat4::identity();
    m.worldM = geom * local * parentW;
    m.hasWorld = true;
    return m.worldM;
}

// ---------------- Test Zlib Stream Helper (RFC 1950 Stored DEFLATE) ----------------
void writeZlibStoredStream(const uint8_t* raw, size_t len, std::vector<uint8_t>& out) {
    out.clear();
    out.push_back(0x78);
    out.push_back(0x01);
    size_t off = 0;
    do {
        uint16_t chunk = static_cast<uint16_t>(std::min<size_t>(len - off, 65535u));
        bool isLast = (off + chunk >= len);
        out.push_back(isLast ? 0x01 : 0x00);
        out.push_back(static_cast<uint8_t>(chunk & 0xFF));
        out.push_back(static_cast<uint8_t>((chunk >> 8) & 0xFF));
        uint16_t nlen = static_cast<uint16_t>(~chunk);
        out.push_back(static_cast<uint8_t>(nlen & 0xFF));
        out.push_back(static_cast<uint8_t>((nlen >> 8) & 0xFF));
        if (chunk > 0) out.insert(out.end(), raw + off, raw + off + chunk);
        off += chunk;
    } while (off < len);

    uint32_t s1 = 1, s2 = 0;
    for (size_t i = 0; i < len; ++i) {
        s1 = (s1 + raw[i]) % 65521u;
        s2 = (s2 + s1) % 65521u;
    }
    uint32_t adler = (s2 << 16) | s1;
    out.push_back(static_cast<uint8_t>((adler >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((adler >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((adler >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(adler & 0xFF));
}

void buildTestCarGeometry(std::vector<double>& outVerts, std::vector<int32_t>& outPolyIdx,
                          std::vector<double>& outUvs, std::vector<int32_t>& outUvIdx) {
    // Stylized low-poly GT car body: 8-station side profile extruded symmetrically across X=[-0.95, +0.95]
    struct Pt2 { double z, y; };
    const Pt2 prof[] = {
        { 2.20, 0.14}, { 2.05, 0.50}, { 1.15, 0.64}, { 0.35, 1.14},
        {-0.65, 1.12}, {-1.45, 0.74}, {-2.15, 0.72}, {-2.25, 0.16}
    };
    const int nP = static_cast<int>(sizeof(prof) / sizeof(prof[0]));
    // Right (+X) ring [0..nP-1], Left (-X) ring [nP..2*nP-1]
    for (int side = 0; side < 2; ++side) {
        double x = (side == 0) ? 0.92 : -0.92;
        for (int i = 0; i < nP; ++i) {
            outVerts.push_back(x);
            outVerts.push_back(prof[i].y);
            outVerts.push_back(prof[i].z);
        }
    }
    auto addQuad = [&](int a, int b, int c, int d, double u0, double v0, double u1, double v1) {
        int baseUv = static_cast<int>(outUvs.size() / 2);
        outUvs.push_back(u0); outUvs.push_back(v0);
        outUvs.push_back(u1); outUvs.push_back(v0);
        outUvs.push_back(u1); outUvs.push_back(v1);
        outUvs.push_back(u0); outUvs.push_back(v1);
        outPolyIdx.push_back(a); outPolyIdx.push_back(b); outPolyIdx.push_back(c); outPolyIdx.push_back(~d);
        outUvIdx.push_back(baseUv + 0); outUvIdx.push_back(baseUv + 1);
        outUvIdx.push_back(baseUv + 2); outUvIdx.push_back(baseUv + 3);
    };
    // Top / front / rear wrap quads
    for (int i = 0; i < nP; ++i) {
        int ni = (i + 1) % nP;
        double vA = 0.08 + 0.38 * (static_cast<double>(i) / nP);
        double vB = 0.08 + 0.38 * (static_cast<double>(i + 1) / nP);
        addQuad(i, ni, nP + ni, nP + i, 0.12, vA, 0.88, vB);
    }
    // Right (+X) side fan triangles as quads
    for (int i = 1; i + 1 < nP; ++i) {
        int baseUv = static_cast<int>(outUvs.size() / 2);
        outUvs.push_back(0.25); outUvs.push_back(0.60);
        outUvs.push_back(0.25 + 0.06 * i); outUvs.push_back(0.85);
        outUvs.push_back(0.25 + 0.06 * (i + 1)); outUvs.push_back(0.85);
        outPolyIdx.push_back(0); outPolyIdx.push_back(i + 1); outPolyIdx.push_back(~i);
        outUvIdx.push_back(baseUv + 0); outUvIdx.push_back(baseUv + 1); outUvIdx.push_back(baseUv + 2);
    }
    // Left (-X) side fan
    for (int i = 1; i + 1 < nP; ++i) {
        int baseUv = static_cast<int>(outUvs.size() / 2);
        outUvs.push_back(0.75); outUvs.push_back(0.60);
        outUvs.push_back(0.75 - 0.06 * i); outUvs.push_back(0.85);
        outUvs.push_back(0.75 - 0.06 * (i + 1)); outUvs.push_back(0.85);
        outPolyIdx.push_back(nP); outPolyIdx.push_back(nP + i); outPolyIdx.push_back(~(nP + i + 1));
        outUvIdx.push_back(baseUv + 0); outUvIdx.push_back(baseUv + 1); outUvIdx.push_back(baseUv + 2);
    }
}

} // namespace

bool loadFbxIntoObjMesh(const std::string& path, ObjMesh& outMesh, bool normalizeScale) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) return false;
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();
    if (bytes.size() < 27) return false;

    FbxNode root;
    bool isBin = (std::memcmp(bytes.data(), "Kaydara FBX Binary  ", 20) == 0);
    if (isBin) {
        if (!parseBinaryFbx(bytes, root)) return false;
    } else {
        if (!parseAsciiFbx(bytes, root)) return false;
    }

    int upAxis = 1;
    int upAxisSign = 1;
    if (const FbxNode* gs = root.findChild("GlobalSettings")) {
        if (const FbxNode* p70 = gs->findChild("Properties70")) {
            for (const FbxNode& p : p70->children) {
                if (p.name != "P" || p.props.size() < 5) continue;
                std::string pname = firstStringProp(&p);
                if (pname == "UpAxis") upAxis = static_cast<int>(p.props.back().asInt(1));
                else if (pname == "UpAxisSign") upAxisSign = static_cast<int>(p.props.back().asInt(1));
            }
        }
    }

    std::unordered_map<int64_t, FbxModelData> models;
    std::unordered_map<int64_t, FbxGeomData> geoms;
    std::unordered_map<int64_t, std::string> materials;
    std::unordered_map<std::string, int64_t> modelNameToId;

    if (const FbxNode* objs = root.findChild("Objects")) {
        for (const FbxNode& obj : objs->children) {
            int64_t id = 0;
            std::string rawName;
            if (!obj.props.empty() && obj.props[0].kind == FbxProp::Kind::Int) {
                id = obj.props[0].asInt();
                if (obj.props.size() >= 2 && obj.props[1].kind == FbxProp::Kind::String) rawName = obj.props[1].sVal;
            } else if (!obj.props.empty() && obj.props[0].kind == FbxProp::Kind::String) {
                rawName = obj.props[0].sVal;
                id = static_cast<int64_t>(geoms.size() + models.size() + 1000);
            }
            std::string cname = cleanFbxObjectName(rawName);
            if (cname.empty()) cname = obj.name;

            if (obj.name == "Geometry") {
                geoms[id] = {id, cname, &obj};
            } else if (obj.name == "Model") {
                FbxModelData md;
                md.id = id;
                md.name = cname;
                if (const FbxNode* p70 = obj.findChild("Properties70")) {
                    for (const FbxNode& p : p70->children) {
                        if (p.name != "P" || p.props.size() < 7) continue;
                        std::string pn = firstStringProp(&p);
                        size_t nP = p.props.size();
                        Vec3 v{static_cast<float>(p.props[nP - 3].asDouble()),
                               static_cast<float>(p.props[nP - 2].asDouble()),
                               static_cast<float>(p.props[nP - 1].asDouble())};
                        if (pn == "Lcl Translation") md.lclT = v;
                        else if (pn == "PreRotation") md.preRot = v;
                        else if (pn == "Lcl Rotation") md.lclR = v;
                        else if (pn == "Lcl Scaling") md.lclS = v;
                        else if (pn == "GeometricTranslation") md.geomT = v;
                        else if (pn == "GeometricRotation") md.geomR = v;
                        else if (pn == "GeometricScaling") md.geomS = v;
                    }
                }
                models[id] = md;
                modelNameToId[cname] = id;
                // FBX 6.x inline geometry fallback
                if (obj.findChild("Vertices") && obj.findChild("PolygonVertexIndex")) {
                    geoms[id + 1000000LL] = {id + 1000000LL, cname, &obj};
                }
            } else if (obj.name == "Material") {
                materials[id] = cname;
            }
        }
    }

    std::unordered_map<int64_t, int64_t> geomToModel;
    std::unordered_map<int64_t, std::string> modelToMaterial;
    if (const FbxNode* conns = root.findChild("Connections")) {
        for (const FbxNode& c : conns->children) {
            if (c.name != "C" || c.props.size() < 3) continue;
            if (c.props[1].kind == FbxProp::Kind::Int && c.props[2].kind == FbxProp::Kind::Int) {
                int64_t childId = c.props[1].asInt();
                int64_t parentId = c.props[2].asInt();
                if (geoms.count(childId) && models.count(parentId)) {
                    geomToModel[childId] = parentId;
                } else if (models.count(childId) && models.count(parentId)) {
                    models[childId].parentId = parentId;
                } else if (materials.count(childId) && models.count(parentId)) {
                    modelToMaterial[parentId] = materials[childId];
                }
            }
        }
    }

    outMesh.uniquePositions.clear();
    outMesh.resolvedDiffuseTexPath.clear();
    outMesh.resolvedAoTexPath.clear();
    outMesh.tris.clear();
    outMesh.gpuVertices.clear();
    outMesh.groups.clear();
    outMesh.uniqueToCorners.clear();
    outMesh.loaded = false;

    auto convertUpAxis = [&](Vec3 p) -> Vec3 {
        if (upAxis == 2) {
            float yUp = p.z * static_cast<float>(upAxisSign);
            float zFwd = -p.y * static_cast<float>(upAxisSign);
            return {p.x, yUp, zFwd};
        }
        return {p.x, p.y * static_cast<float>(upAxisSign), p.z};
    };

    bool anyMeshHasNormals = false;
    for (const auto& kv : geoms) {
        const FbxGeomData& gd = kv.second;
        const FbxNode* gNode = gd.node;
        if (!gNode) continue;
        std::vector<double> vertsRaw;
        std::vector<int64_t> polyIdx;
        extractDoubleVec(gNode->findChild("Vertices"), vertsRaw);
        extractIntVec(gNode->findChild("PolygonVertexIndex"), polyIdx);
        if (vertsRaw.size() < 9 || polyIdx.empty()) continue;

        Mat4 worldM = Mat4::identity();
        std::string groupName = gd.name;
        if (auto mit = geomToModel.find(gd.id); mit != geomToModel.end()) {
            worldM = evalModelWorld(mit->second, models);
            groupName = models[mit->second].name;
            if (auto matIt = modelToMaterial.find(mit->second); matIt != modelToMaterial.end() && !matIt->second.empty()) {
                groupName = matIt->second;
            }
        } else if (gd.id >= 1000000LL && models.count(gd.id - 1000000LL)) {
            worldM = evalModelWorld(gd.id - 1000000LL, models);
            groupName = models[gd.id - 1000000LL].name;
        }
        if (groupName.empty()) groupName = "BodyShell";

        int32_t grpIdx = static_cast<int32_t>(outMesh.groups.size());
        SubGroup sg;
        sg.name = groupName;
        sg.firstTri = static_cast<int32_t>(outMesh.tris.size());
        sg.triCount = 0;
        sg.visible = true;

        const uint32_t baseV = static_cast<uint32_t>(outMesh.uniquePositions.size());
        const size_t nCtrl = vertsRaw.size() / 3;
        outMesh.uniquePositions.reserve(baseV + nCtrl);
        for (size_t i = 0; i < nCtrl; ++i) {
            Vec3 lp{static_cast<float>(vertsRaw[i * 3 + 0]),
                    static_cast<float>(vertsRaw[i * 3 + 1]),
                    static_cast<float>(vertsRaw[i * 3 + 2])};
            Vec4 wp4 = transformPoint4(worldM, lp);
            Vec3 yp = convertUpAxis({wp4.x, wp4.y, wp4.z});
            // Convert RH FBX to LH D3D11 (-X) matching ObjMesh
            outMesh.uniquePositions.push_back({-yp.x, yp.y, yp.z});
        }

        // UV layer 0
        std::string uvMapping = "ByPolygonVertex";
        std::string uvRef = "Direct";
        std::vector<double> uvRaw;
        std::vector<int64_t> uvIdx;
        if (const FbxNode* luv = gNode->findChild("LayerElementUV")) {
            uvMapping = firstStringProp(luv->findChild("MappingInformationType"));
            uvRef = firstStringProp(luv->findChild("ReferenceInformationType"));
            extractDoubleVec(luv->findChild("UV"), uvRaw);
            extractIntVec(luv->findChild("UVIndex"), uvIdx);
        }

        // Normal layer 0
        std::string nrmMapping = "ByPolygonVertex";
        std::string nrmRef = "Direct";
        std::vector<double> nrmRaw;
        std::vector<int64_t> nrmIdx;
        if (const FbxNode* lnrm = gNode->findChild("LayerElementNormal")) {
            nrmMapping = firstStringProp(lnrm->findChild("MappingInformationType"));
            nrmRef = firstStringProp(lnrm->findChild("ReferenceInformationType"));
            extractDoubleVec(lnrm->findChild("Normals"), nrmRaw);
            extractIntVec(lnrm->findChild("NormalsIndex"), nrmIdx);
        }

        auto getUv = [&](size_t polyVertIdx, uint32_t ctrlIdx) -> Vec2 {
            if (uvRaw.size() < 2) return {0.0f, 0.0f};
            size_t elem = (uvMapping == "ByControlPoint") ? static_cast<size_t>(ctrlIdx) : polyVertIdx;
            if (uvRef == "IndexToDirect") {
                if (elem >= uvIdx.size()) return {0.0f, 0.0f};
                int64_t di = uvIdx[elem];
                if (di < 0 || static_cast<size_t>(di * 2 + 1) >= uvRaw.size()) return {0.0f, 0.0f};
                elem = static_cast<size_t>(di);
            }
            if (elem * 2 + 1 >= uvRaw.size()) return {0.0f, 0.0f};
            return {static_cast<float>(uvRaw[elem * 2 + 0]), 1.0f - static_cast<float>(uvRaw[elem * 2 + 1])};
        };

        bool geomHasNormals = (nrmRaw.size() >= 3);
        if (geomHasNormals) anyMeshHasNormals = true;
        auto getNrm = [&](size_t polyVertIdx, uint32_t ctrlIdx) -> Vec3 {
            if (!geomHasNormals) return {0.0f, 1.0f, 0.0f};
            size_t elem = (nrmMapping == "ByControlPoint") ? static_cast<size_t>(ctrlIdx) : polyVertIdx;
            if (nrmRef == "IndexToDirect") {
                if (elem >= nrmIdx.size()) return {0.0f, 1.0f, 0.0f};
                int64_t di = nrmIdx[elem];
                if (di < 0 || static_cast<size_t>(di * 3 + 2) >= nrmRaw.size()) return {0.0f, 1.0f, 0.0f};
                elem = static_cast<size_t>(di);
            }
            if (elem * 3 + 2 >= nrmRaw.size()) return {0.0f, 1.0f, 0.0f};
            Vec3 ln{static_cast<float>(nrmRaw[elem * 3 + 0]),
                    static_cast<float>(nrmRaw[elem * 3 + 1]),
                    static_cast<float>(nrmRaw[elem * 3 + 2])};
            Vec4 wn4{
                ln.x * worldM.m[0][0] + ln.y * worldM.m[1][0] + ln.z * worldM.m[2][0],
                ln.x * worldM.m[0][1] + ln.y * worldM.m[1][1] + ln.z * worldM.m[2][1],
                ln.x * worldM.m[0][2] + ln.y * worldM.m[1][2] + ln.z * worldM.m[2][2],
                0.0f
            };
            Vec3 yn = convertUpAxis({wn4.x, wn4.y, wn4.z});
            return normalized(Vec3{-yn.x, yn.y, yn.z});
        };

        std::vector<uint32_t> polyVerts;
        std::vector<Vec2> polyUvs;
        std::vector<Vec3> polyNrms;
        polyVerts.reserve(8); polyUvs.reserve(8); polyNrms.reserve(8);

        for (size_t pv = 0; pv < polyIdx.size(); ++pv) {
            int64_t rawI = polyIdx[pv];
            bool endPoly = (rawI < 0);
            uint32_t ci = static_cast<uint32_t>(endPoly ? (~rawI) : rawI);
            if (ci < nCtrl) {
                polyVerts.push_back(baseV + ci);
                polyUvs.push_back(getUv(pv, ci));
                polyNrms.push_back(getNrm(pv, ci));
            }
            if (endPoly) {
                if (polyVerts.size() >= 3) {
                    for (size_t k = 1; k + 1 < polyVerts.size(); ++k) {
                        // Swap winding 1 <-> 2 to match negated X handedness
                        size_t idx[3] = {0, k + 1, k};
                        Triangle t{};
                        t.p0 = polyVerts[idx[0]];
                        t.p1 = polyVerts[idx[1]];
                        t.p2 = polyVerts[idx[2]];
                        t.uv0 = polyUvs[idx[0]];
                        t.uv1 = polyUvs[idx[1]];
                        t.uv2 = polyUvs[idx[2]];
                        Vec2 uvC = (t.uv0 + t.uv1 + t.uv2) * (1.0f / 3.0f);
                        if (uvC.x < -1e-4f || uvC.x > 1.0f + 1e-4f) {
                            float sh = std::floor(uvC.x);
                            t.uv0.x -= sh; t.uv1.x -= sh; t.uv2.x -= sh;
                        }
                        if (uvC.y < -1e-4f || uvC.y > 1.0f + 1e-4f) {
                            float sh = std::floor(uvC.y);
                            t.uv0.y -= sh; t.uv1.y -= sh; t.uv2.y -= sh;
                        }
                        t.groupIdx = grpIdx;
                        uint32_t baseC = static_cast<uint32_t>(outMesh.gpuVertices.size());
                        t.i0 = baseC; t.i1 = baseC + 1; t.i2 = baseC + 2;
                        for (int c = 0; c < 3; ++c) {
                            uint32_t pi = (c == 0) ? t.p0 : ((c == 1) ? t.p1 : t.p2);
                            const Vec2& uv = (c == 0) ? t.uv0 : ((c == 1) ? t.uv1 : t.uv2);
                            const Vec3& nrm = polyNrms[idx[c]];
                            const Vec3& p = outMesh.uniquePositions[pi];
                            outMesh.gpuVertices.push_back({p.x, p.y, p.z, nrm.x, nrm.y, nrm.z, uv.x, uv.y, 0.0f, 0.0f});
                        }
                        outMesh.tris.push_back(t);
                        sg.triCount++;
                    }
                }
                polyVerts.clear();
                polyUvs.clear();
                polyNrms.clear();
            }
        }
        if (sg.triCount > 0) {
            outMesh.groups.push_back(std::move(sg));
        }
    }

    if (outMesh.tris.empty() || outMesh.uniquePositions.empty()) return false;

    if (normalizeScale) {
        AABB rawBox;
        for (const Vec3& p : outMesh.uniquePositions) rawBox.expand(p);
        Vec3 ext = rawBox.extent();
        float maxExt = std::max({ext.x, ext.y, ext.z, 1e-4f});
        float sf = 4.62f / maxExt;
        float cx = 0.5f * (rawBox.min.x + rawBox.max.x);
        float cz = 0.5f * (rawBox.min.z + rawBox.max.z);
        float minY = rawBox.min.y;
        for (Vec3& p : outMesh.uniquePositions) {
            p.x = (p.x - cx) * sf;
            p.y = (p.y - minY) * sf + 0.02f;
            p.z = (p.z - cz) * sf;
        }
    }

    outMesh.vertexSelected.assign(outMesh.uniquePositions.size(), 0);
    outMesh.uniqueToCorners.assign(outMesh.uniquePositions.size(), {});
    for (const Triangle& t : outMesh.tris) {
        outMesh.uniqueToCorners[t.p0].push_back(t.i0);
        outMesh.uniqueToCorners[t.p1].push_back(t.i1);
        outMesh.uniqueToCorners[t.p2].push_back(t.i2);
    }

    if (!anyMeshHasNormals) {
        outMesh.recomputeSmoothNormals();
    } else {
        outMesh.uniqueNormals.assign(outMesh.uniquePositions.size(), {0, 0, 0});
        for (const Triangle& t : outMesh.tris) {
            Vec3 p0 = outMesh.uniquePositions[t.p0], p1 = outMesh.uniquePositions[t.p1], p2 = outMesh.uniquePositions[t.p2];
            Vec3 fn = cross(p1 - p0, p2 - p0);
            outMesh.uniqueNormals[t.p0] += fn;
            outMesh.uniqueNormals[t.p1] += fn;
            outMesh.uniqueNormals[t.p2] += fn;
        }
        for (Vec3& n : outMesh.uniqueNormals) n = normalized(n);
    }

    outMesh.syncRenderVertices();
    outMesh.rebuildBoundsAndBVH();
    outMesh.buildUvCache(1024);
    outMesh.sourcePath = path;
    outMesh.loaded = true;
    return true;
}

bool writeTestBinaryFbxCar(const std::string& path) {
    std::vector<double> verts, uvs;
    std::vector<int32_t> polyIdx, uvIdx;
    buildTestCarGeometry(verts, polyIdx, uvs, uvIdx);

    std::vector<uint8_t> out;
    auto pushU8 = [&](uint8_t v) { out.push_back(v); };
    auto pushU32 = [&](uint32_t v) {
        size_t o = out.size(); out.resize(o + 4); std::memcpy(out.data() + o, &v, 4);
    };
    auto pushBytes = [&](const void* p, size_t n) {
        size_t o = out.size(); out.resize(o + n); if (n) std::memcpy(out.data() + o, p, n);
    };
    auto pushStrProp = [&](const std::string& s) {
        pushU8('S');
        pushU32(static_cast<uint32_t>(s.size()));
        pushBytes(s.data(), s.size());
    };
    auto pushI64Prop = [&](int64_t v) {
        pushU8('L');
        pushBytes(&v, 8);
    };
    auto pushZlibArrayProp = [&](char tc, uint32_t count, const void* raw, size_t rawBytes) {
        std::vector<uint8_t> comp;
        writeZlibStoredStream(reinterpret_cast<const uint8_t*>(raw), rawBytes, comp);
        pushU8(static_cast<uint8_t>(tc));
        pushU32(count);
        pushU32(1); // encoding = 1 (zlib)
        pushU32(static_cast<uint32_t>(comp.size()));
        pushBytes(comp.data(), comp.size());
    };

    auto beginNode = [&](const std::string& name, uint32_t numProps) -> size_t {
        size_t hdrPos = out.size();
        pushU32(0); // endOffset placeholder
        pushU32(numProps);
        pushU32(0); // propListLen placeholder
        pushU8(static_cast<uint8_t>(name.size()));
        pushBytes(name.data(), name.size());
        return hdrPos;
    };
    auto finishProps = [&](size_t hdrPos) {
        uint32_t propsStart = static_cast<uint32_t>(hdrPos + 13 + out[hdrPos + 12]);
        uint32_t propLen = static_cast<uint32_t>(out.size()) - propsStart;
        std::memcpy(out.data() + hdrPos + 8, &propLen, 4);
    };
    auto endNode = [&](size_t hdrPos, bool withNullChild) {
        if (withNullChild) {
            uint8_t zeros[13] = {};
            pushBytes(zeros, 13);
        }
        uint32_t endOff = static_cast<uint32_t>(out.size());
        std::memcpy(out.data() + hdrPos, &endOff, 4);
    };

    // 27-byte FBX 7400 header
    const char kHdr[23] = "Kaydara FBX Binary  \x00\x1a";
    pushBytes(kHdr, 23);
    pushU32(7400);

    // Objects
    size_t objsHdr = beginNode("Objects", 0);
    finishProps(objsHdr);
    {
        // Geometry (ID 1001)
        size_t gHdr = beginNode("Geometry", 3);
        pushI64Prop(1001);
        pushStrProp(std::string("TestCarGeo\x00\x01Geometry", 20));
        pushStrProp("Mesh");
        finishProps(gHdr);

        size_t vHdr = beginNode("Vertices", 1);
        pushZlibArrayProp('d', static_cast<uint32_t>(verts.size()), verts.data(), verts.size() * sizeof(double));
        finishProps(vHdr);
        endNode(vHdr, false);

        size_t pHdr = beginNode("PolygonVertexIndex", 1);
        pushZlibArrayProp('i', static_cast<uint32_t>(polyIdx.size()), polyIdx.data(), polyIdx.size() * sizeof(int32_t));
        finishProps(pHdr);
        endNode(pHdr, false);

        size_t luvHdr = beginNode("LayerElementUV", 1);
        pushU8('I'); int32_t z0 = 0; pushBytes(&z0, 4);
        finishProps(luvHdr);
        {
            size_t mHdr = beginNode("MappingInformationType", 1);
            pushStrProp("ByPolygonVertex");
            finishProps(mHdr);
            endNode(mHdr, false);
            size_t rHdr = beginNode("ReferenceInformationType", 1);
            pushStrProp("IndexToDirect");
            finishProps(rHdr);
            endNode(rHdr, false);
            size_t uHdr = beginNode("UV", 1);
            pushZlibArrayProp('d', static_cast<uint32_t>(uvs.size()), uvs.data(), uvs.size() * sizeof(double));
            finishProps(uHdr);
            endNode(uHdr, false);
            size_t uiHdr = beginNode("UVIndex", 1);
            pushZlibArrayProp('i', static_cast<uint32_t>(uvIdx.size()), uvIdx.data(), uvIdx.size() * sizeof(int32_t));
            finishProps(uiHdr);
            endNode(uiHdr, false);
        }
        endNode(luvHdr, true);
        endNode(gHdr, true);

        // Model (ID 2001)
        size_t mHdr = beginNode("Model", 3);
        pushI64Prop(2001);
        pushStrProp(std::string("TestCarBody\x00\x01Model", 18));
        pushStrProp("Mesh");
        finishProps(mHdr);
        endNode(mHdr, false);
    }
    endNode(objsHdr, true);

    // Connections
    size_t connsHdr = beginNode("Connections", 0);
    finishProps(connsHdr);
    {
        size_t c1 = beginNode("C", 3);
        pushStrProp("OO"); pushI64Prop(1001); pushI64Prop(2001);
        finishProps(c1);
        endNode(c1, false);
        size_t c2 = beginNode("C", 3);
        pushStrProp("OO"); pushI64Prop(2001); pushI64Prop(0);
        finishProps(c2);
        endNode(c2, false);
    }
    endNode(connsHdr, true);

    // Footer sentinel
    uint8_t zeros[13] = {};
    pushBytes(zeros, 13);

    std::ofstream ofs(path, std::ios::binary);
    if (!ofs.is_open()) return false;
    ofs.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    return ofs.good();
}

bool writeTestAsciiFbxCar(const std::string& path) {
    std::vector<double> verts, uvs;
    std::vector<int32_t> polyIdx, uvIdx;
    buildTestCarGeometry(verts, polyIdx, uvs, uvIdx);

    std::ofstream ofs(path, std::ios::binary);
    if (!ofs.is_open()) return false;
    ofs << "; FBX 7.4.0 project file\n";
    ofs << "FBXHeaderExtension:  {\n\tFBXHeaderVersion: 1003\n\tFBXVersion: 7400\n}\n";
    ofs << "GlobalSettings:  {\n\tProperties70:  {\n";
    ofs << "\t\tP: \"UpAxis\", \"int\", \"Integer\", \"\", 1\n";
    ofs << "\t\tP: \"UpAxisSign\", \"int\", \"Integer\", \"\", 1\n";
    ofs << "\t}\n}\n";
    ofs << "Objects:  {\n";
    ofs << "\tGeometry: 1001, \"Geometry::TestCarGeo\", \"Mesh\" {\n";
    ofs << "\t\tVertices: *" << verts.size() << " {\n\t\t\ta: ";
    for (size_t i = 0; i < verts.size(); ++i) {
        if (i) ofs << ",";
        ofs << verts[i];
    }
    ofs << "\n\t\t}\n";
    ofs << "\t\tPolygonVertexIndex: *" << polyIdx.size() << " {\n\t\t\ta: ";
    for (size_t i = 0; i < polyIdx.size(); ++i) {
        if (i) ofs << ",";
        ofs << polyIdx[i];
    }
    ofs << "\n\t\t}\n";
    ofs << "\t\tLayerElementUV: 0 {\n";
    ofs << "\t\t\tMappingInformationType: \"ByPolygonVertex\"\n";
    ofs << "\t\t\tReferenceInformationType: \"IndexToDirect\"\n";
    ofs << "\t\t\tUV: *" << uvs.size() << " {\n\t\t\t\ta: ";
    for (size_t i = 0; i < uvs.size(); ++i) {
        if (i) ofs << ",";
        ofs << uvs[i];
    }
    ofs << "\n\t\t\t}\n";
    ofs << "\t\t\tUVIndex: *" << uvIdx.size() << " {\n\t\t\t\ta: ";
    for (size_t i = 0; i < uvIdx.size(); ++i) {
        if (i) ofs << ",";
        ofs << uvIdx[i];
    }
    ofs << "\n\t\t\t}\n";
    ofs << "\t\t}\n";
    ofs << "\t}\n";
    ofs << "\tModel: 2001, \"Model::TestCarBody\", \"Mesh\" {\n";
    ofs << "\t\tProperties70:  {\n";
    ofs << "\t\t\tP: \"Lcl Translation\", \"Lcl Translation\", \"\", \"A\", 0, 0, 0\n";
    ofs << "\t\t}\n";
    ofs << "\t}\n";
    ofs << "}\n";
    ofs << "Connections:  {\n";
    ofs << "\tC: \"OO\", 1001, 2001\n";
    ofs << "\tC: \"OO\", 2001, 0\n";
    ofs << "}\n";
    return ofs.good();
}

} // namespace lps
