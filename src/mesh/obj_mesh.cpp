#include "mesh/obj_mesh.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <queue>
#include <sstream>

namespace lps {

namespace {

inline uint64_t hashCellKey(int gx, int gy, int gz) {
    uint64_t ux = static_cast<uint64_t>(gx + 32768) & 0xFFFFFull;
    uint64_t uy = static_cast<uint64_t>(gy + 32768) & 0xFFFFFull;
    uint64_t uz = static_cast<uint64_t>(gz + 32768) & 0xFFFFFull;
    return (ux << 40) | (uy << 20) | uz;
}

inline int parseObjIndex(const char* tok, int& vIdx, int& vtIdx, int& vnIdx, int nV, int nVt, int nVn) {
    vIdx = -1; vtIdx = -1; vnIdx = -1;
    const char* p = tok;
    char* end = nullptr;
    long v = std::strtol(p, &end, 10);
    if (end == p) return 0;
    vIdx = (v > 0) ? static_cast<int>(v - 1) : (nV + static_cast<int>(v));
    if (*end == '/') {
        p = end + 1;
        if (*p != '/' && *p != '\0') {
            long vt = std::strtol(p, &end, 10);
            vtIdx = (vt > 0) ? static_cast<int>(vt - 1) : (nVt + static_cast<int>(vt));
        } else {
            end = const_cast<char*>(p);
        }
        if (*end == '/') {
            p = end + 1;
            if (*p != '\0') {
                long vn = std::strtol(p, &end, 10);
                vnIdx = (vn > 0) ? static_cast<int>(vn - 1) : (nVn + static_cast<int>(vn));
            }
        }
    }
    return 1;
}

float pointTriDistSq(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c,
                     float& outU, float& outV, Vec3& outClosest) {
    Vec3 ab = b - a, ac = c - a, ap = p - a;
    float d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) { outU = 0; outV = 0; outClosest = a; return lengthSq(p - a); }

    Vec3 bp = p - b;
    float d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) { outU = 1; outV = 0; outClosest = b; return lengthSq(p - b); }

    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        float v = d1 / (d1 - d3);
        outU = v; outV = 0; outClosest = a + ab * v;
        return lengthSq(p - outClosest);
    }

    Vec3 cp = p - c;
    float d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) { outU = 0; outV = 1; outClosest = c; return lengthSq(p - c); }

    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        float w = d2 / (d2 - d6);
        outU = 0; outV = w; outClosest = a + ac * w;
        return lengthSq(p - outClosest);
    }

    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        outU = 1.0f - w; outV = w; outClosest = b + (c - b) * w;
        return lengthSq(p - outClosest);
    }

    float denom = 1.0f / (va + vb + vc);
    float v = vb * denom;
    float w = vc * denom;
    outU = v; outV = w;
    outClosest = a + ab * v + ac * w;
    return lengthSq(p - outClosest);
}

inline std::string_view trimSv(std::string_view sv) {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t')) sv.remove_prefix(1);
    while (!sv.empty() && (sv.back() == '\r' || sv.back() == ' ' || sv.back() == '\t')) sv.remove_suffix(1);
    return sv;
}

inline bool startsWithCI(std::string_view sv, std::string_view prefix) {
    if (sv.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(sv[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

inline std::filesystem::path utf8Path(const std::string& s) {
    return std::filesystem::path(reinterpret_cast<const char8_t*>(s.c_str()));
}

inline Vec2 wrapUv01(Vec2 uv) {
    if (uv.x < 0.0f || uv.x > 1.0f) uv.x -= std::floor(uv.x);
    if (uv.y < 0.0f || uv.y > 1.0f) uv.y -= std::floor(uv.y);
    return uv;
}

std::string stripQuotes(std::string s) {
    if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\''))) {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

std::string resolveExistingFile(const std::filesystem::path& rawPath,
                                const std::filesystem::path& primaryDir,
                                const std::filesystem::path& secondaryDir) {
    std::error_code ec;
    auto check = [&](const std::filesystem::path& p) -> std::string {
        if (!p.empty() && std::filesystem::is_regular_file(p, ec)) {
            return std::filesystem::weakly_canonical(p, ec).string();
        }
        return {};
    };
    if (rawPath.is_absolute()) {
        if (auto r = check(rawPath); !r.empty()) return r;
    }
    if (!primaryDir.empty()) {
        if (auto r = check(primaryDir / rawPath); !r.empty()) return r;
    }
    if (!secondaryDir.empty() && secondaryDir != primaryDir) {
        if (auto r = check(secondaryDir / rawPath); !r.empty()) return r;
    }
    if (!primaryDir.empty()) {
        if (auto r = check(primaryDir / rawPath.filename()); !r.empty()) return r;
    }
    if (!secondaryDir.empty() && secondaryDir != primaryDir) {
        if (auto r = check(secondaryDir / rawPath.filename()); !r.empty()) return r;
    }
    return {};
}

std::string parseMapKdTexPath(std::string_view rest) {
    rest = trimSv(rest);
    if (rest.empty()) return {};
    if (rest.front() == '"' || rest.front() == '\'') {
        return stripQuotes(std::string(rest));
    }
    // Skip common MTL texture options (-s u v w, -o u v w, -t u v w, -mm b g, -bm v, etc.)
    size_t pos = 0;
    auto nextToken = [&]() -> std::string_view {
        while (pos < rest.size() && (rest[pos] == ' ' || rest[pos] == '\t')) ++pos;
        size_t start = pos;
        while (pos < rest.size() && rest[pos] != ' ' && rest[pos] != '\t') ++pos;
        return rest.substr(start, pos - start);
    };
    auto isNumber = [](std::string_view tok) -> bool {
        if (tok.empty()) return false;
        char* endP = nullptr;
        std::string tmp(tok);
        std::strtod(tmp.c_str(), &endP);
        return endP && *endP == '\0';
    };

    while (pos < rest.size()) {
        while (pos < rest.size() && (rest[pos] == ' ' || rest[pos] == '\t')) ++pos;
        if (pos >= rest.size()) break;
        if (rest[pos] != '-') {
            return stripQuotes(std::string(trimSv(rest.substr(pos))));
        }
        std::string_view opt = nextToken();
        if (opt == "-o" || opt == "-s" || opt == "-t") {
            for (int k = 0; k < 3; ++k) {
                size_t saved = pos;
                std::string_view arg = nextToken();
                if (!isNumber(arg)) { pos = saved; break; }
            }
        } else if (opt == "-mm") {
            for (int k = 0; k < 2; ++k) {
                size_t saved = pos;
                std::string_view arg = nextToken();
                if (!isNumber(arg)) { pos = saved; break; }
            }
        } else {
            nextToken(); // 1 argument (-bm, -boost, -clamp, -blendu, -blendv, -cc, -imfchan, -texres)
        }
    }
    return {};
}

std::string findMapKdInMtl(const std::filesystem::path& mtlPath, const std::filesystem::path& objDir) {
    std::ifstream ifs(mtlPath, std::ios::binary);
    if (!ifs.is_open()) return {};
    std::filesystem::path mtlDir = mtlPath.parent_path();
    std::string line;
    while (std::getline(ifs, line)) {
        std::string_view sv = trimSv(line);
        if (sv.empty() || sv.front() == '#') continue;
        if (startsWithCI(sv, "map_Kd") && sv.size() > 6 && (sv[6] == ' ' || sv[6] == '\t')) {
            std::string texStr = parseMapKdTexPath(sv.substr(6));
            if (!texStr.empty()) {
                std::filesystem::path tp = utf8Path(texStr);
                if (auto found = resolveExistingFile(tp, mtlDir, objDir); !found.empty()) {
                    return found;
                }
                // Fallback if spaces separated options & filename wasn't full remainder
                size_t sp = texStr.find_last_of(" \t");
                if (sp != std::string::npos) {
                    std::filesystem::path tpLast = utf8Path(texStr.substr(sp + 1));
                    if (auto found = resolveExistingFile(tpLast, mtlDir, objDir); !found.empty()) {
                        return found;
                    }
                }
            }
        }
    }
    return {};
}

std::string resolveDiffuseFromMtlOrCompanion(const std::filesystem::path& objPath,
                                             const std::vector<std::string>& mtllibs) {
    std::filesystem::path objDir = objPath.parent_path();
    std::error_code ec;

    for (const std::string& lib : mtllibs) {
        std::string cleanLib = stripQuotes(lib);
        std::filesystem::path mtlCand = utf8Path(cleanLib);
        std::string resolvedMtl = resolveExistingFile(mtlCand, objDir, {});
        if (resolvedMtl.empty() && cleanLib.find(' ') != std::string::npos) {
            std::istringstream iss(cleanLib);
            std::string tok;
            while (iss >> tok) {
                resolvedMtl = resolveExistingFile(utf8Path(tok), objDir, {});
                if (!resolvedMtl.empty()) {
                    std::string tex = findMapKdInMtl(utf8Path(resolvedMtl), objDir);
                    if (!tex.empty()) return tex;
                }
            }
        } else if (!resolvedMtl.empty()) {
            std::string tex = findMapKdInMtl(utf8Path(resolvedMtl), objDir);
            if (!tex.empty()) return tex;
        }
    }

    // Fallback: <obj_stem>.png / .jpg / .jpeg / .tga / .bmp in same folder
    std::string stem = objPath.stem().string();
    const char* exts[] = {".png", ".jpg", ".jpeg", ".tga", ".bmp"};
    for (const char* ext : exts) {
        std::filesystem::path cand = objDir / (stem + ext);
        if (std::filesystem::is_regular_file(cand, ec)) {
            return std::filesystem::weakly_canonical(cand, ec).string();
        }
    }
    return {};
}

std::string findAoMapInMtl(const std::filesystem::path& mtlPath, const std::filesystem::path& objDir) {
    std::ifstream ifs(mtlPath, std::ios::binary);
    if (!ifs.is_open()) return {};
    std::filesystem::path mtlDir = mtlPath.parent_path();
    std::string line;
    while (std::getline(ifs, line)) {
        std::string_view sv = trimSv(line);
        if (sv.empty() || sv.front() == '#') continue;
        size_t kwLen = 0;
        if (startsWithCI(sv, "map_ao") && sv.size() > 6 && (sv[6] == ' ' || sv[6] == '\t')) kwLen = 6;
        else if (startsWithCI(sv, "map_Ka") && sv.size() > 6 && (sv[6] == ' ' || sv[6] == '\t')) kwLen = 6;
        if (kwLen == 0) continue;
        std::string texStr = parseMapKdTexPath(sv.substr(kwLen));
        if (!texStr.empty()) {
            if (auto found = resolveExistingFile(utf8Path(texStr), mtlDir, objDir); !found.empty()) {
                return found;
            }
        }
    }
    return {};
}

int scoreAoCandidateName(const std::string& lowerStem, const std::string& objStemLower) {
    if (lowerStem.find("showroom") != std::string::npos ||
        lowerStem.find("proof_") != std::string::npos ||
        lowerStem.find("verify_") != std::string::npos ||
        lowerStem.find("banner") != std::string::npos ||
        lowerStem.find("reference") != std::string::npos) {
        return 0;
    }
    bool hasAoToken = (lowerStem == "ao") ||
                      (lowerStem.find("_ao") != std::string::npos) ||
                      (lowerStem.find("-ao") != std::string::npos) ||
                      (lowerStem.find(" ao") != std::string::npos) ||
                      (lowerStem.find("ao_") != std::string::npos) ||
                      (lowerStem.find("ao-") != std::string::npos) ||
                      (lowerStem.find("ambient") != std::string::npos) ||
                      (lowerStem.find("occlusion") != std::string::npos) ||
                      (lowerStem.find("cavity") != std::string::npos) ||
                      (lowerStem.find("panel_shading") != std::string::npos) ||
                      (lowerStem.find("panelshading") != std::string::npos);
    if (hasAoToken) {
        return (!objStemLower.empty() && lowerStem.find(objStemLower) != std::string::npos) ? 120 : 100;
    }
    if (lowerStem.find("panels") != std::string::npos ||
        (lowerStem.find("panel") != std::string::npos &&
         (lowerStem.find("4k") != std::string::npos || lowerStem.find("2k") != std::string::npos ||
          lowerStem.find("8k") != std::string::npos || lowerStem.find("shading") != std::string::npos ||
          lowerStem.find("template") != std::string::npos))) {
        return 80;
    }
    if (lowerStem.find("shading") != std::string::npos) return 60;
    return 0;
}

std::string resolveAoFromMtlOrCompanion(const std::filesystem::path& objPath,
                                         const std::vector<std::string>& mtllibs,
                                         const std::string& diffusePath) {
    std::filesystem::path objDir = objPath.parent_path();
    std::error_code ec;
    auto canonicalLower = [&](const std::string& p) {
        if (p.empty()) return std::string{};
        std::filesystem::path cp = std::filesystem::weakly_canonical(utf8Path(p), ec);
        std::string s = ec ? p : cp.string();
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    const std::string diffLower = canonicalLower(diffusePath);

    for (const std::string& lib : mtllibs) {
        std::string resolvedMtl = resolveExistingFile(utf8Path(stripQuotes(lib)), objDir, {});
        if (!resolvedMtl.empty()) {
            std::string ao = findAoMapInMtl(utf8Path(resolvedMtl), objDir);
            if (!ao.empty() && canonicalLower(ao) != diffLower) return ao;
        }
    }

    std::string objStemLower = objPath.stem().string();
    for (char& c : objStemLower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    std::vector<std::filesystem::path> dirs = {
        objDir,
        objDir / "textures",
        objDir.parent_path() / "Shared_Templates_And_Previews",
        objDir.parent_path()
    };

    int bestScore = 0;
    std::string bestPath;
    for (const auto& dir : dirs) {
        if (dir.empty() || !std::filesystem::is_directory(dir, ec)) continue;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            std::string ext = entry.path().extension().string();
            for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".tga" && ext != ".bmp") continue;

            std::string candCanon = std::filesystem::weakly_canonical(entry.path(), ec).string();
            if (canonicalLower(candCanon) == diffLower) continue;

            std::string stemL = entry.path().stem().string();
            for (char& c : stemL) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            int sc = scoreAoCandidateName(stemL, objStemLower);
            if (sc > bestScore) {
                bestScore = sc;
                bestPath = candCanon;
            }
        }
    }
    return bestPath;
}

} // namespace

bool ObjMesh::loadFromFile(const std::string& path, bool normalizeScale) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) return false;

    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();

    uniquePositions.clear();
    resolvedDiffuseTexPath.clear();
    resolvedAoTexPath.clear();
    std::vector<Vec2> rawUvs;
    std::vector<Vec3> rawNrms;
    std::vector<std::string> mtllibFiles;
    tris.clear();
    gpuVertices.clear();
    groups.clear();
    uniqueToCorners.clear();

    SubGroup defaultGrp;
    defaultGrp.name = "BodyShell";
    groups.push_back(defaultGrp);
    int32_t curGroup = 0;

    std::vector<int> faceV, faceVt, faceVn;
    faceV.reserve(8); faceVt.reserve(8); faceVn.reserve(8);

    content.push_back('\0');
    char* ptr = content.data();
    char* end = ptr + content.size() - 1;

    auto parseFloats = [](const char* s, float* out, int maxCount) -> int {
        int cnt = 0;
        while (cnt < maxCount) {
            while (*s == ' ' || *s == '\t') ++s;
            if (*s == '\0') break;
            char* endP = nullptr;
            float val = std::strtof(s, &endP);
            if (endP == s) break;
            out[cnt++] = val;
            s = endP;
        }
        return cnt;
    };

    while (ptr < end) {
        char* lineEnd = static_cast<char*>(std::memchr(ptr, '\n', static_cast<size_t>(end - ptr)));
        if (!lineEnd) lineEnd = end;
        char* nextPtr = (lineEnd < end) ? (lineEnd + 1) : end;
        *lineEnd = '\0';

        char* lineStart = ptr;
        char* lineStop = lineEnd;
        while (lineStart < lineStop && (*lineStart == ' ' || *lineStart == '\t')) ++lineStart;
        while (lineStop > lineStart && (lineStop[-1] == '\r' || lineStop[-1] == ' ' || lineStop[-1] == '\t')) {
            --lineStop;
            *lineStop = '\0';
        }
        ptr = nextPtr;
        std::string_view line(lineStart, static_cast<size_t>(lineStop - lineStart));
        if (line.empty() || line.front() == '#') continue;

        if (line.size() >= 2 && line[0] == 'v' && line[1] == ' ') {
            float xyz[3] = {};
            if (parseFloats(lineStart + 2, xyz, 3) == 3) {
                // Convert Right-Handed OBJ coordinates into Left-Handed D3D11 space
                uniquePositions.push_back({-xyz[0], xyz[1], xyz[2]});
            }
        } else if (line.size() >= 3 && line[0] == 'v' && line[1] == 't' && line[2] == ' ') {
            float uv[2] = {};
            if (parseFloats(lineStart + 3, uv, 2) >= 2) {
                rawUvs.push_back({uv[0], 1.0f - uv[1]}); // Top-left image V convention
            }
        } else if (line.size() >= 3 && line[0] == 'v' && line[1] == 'n' && line[2] == ' ') {
            float n[3] = {0.0f, 1.0f, 0.0f};
            if (parseFloats(lineStart + 3, n, 3) == 3) {
                rawNrms.push_back(normalized(Vec3{-n[0], n[1], n[2]}));
            }
        } else if (line.size() > 7 && startsWithCI(line, "mtllib") && (line[6] == ' ' || line[6] == '\t')) {
            std::string_view mfile = trimSv(line.substr(7));
            if (!mfile.empty()) mtllibFiles.emplace_back(mfile);
        } else if ((line.size() >= 2 && (line[0] == 'g' || line[0] == 'o') && line[1] == ' ') ||
                   (line.size() > 7 && line.substr(0, 7) == "usemtl ")) {
            std::string gname;
            if (line[0] == 'u') gname = std::string(line.substr(7));
            else gname = std::string(line.substr(2));
            if (gname.empty()) gname = "Part";
            // Only create new subgroup if current group already has triangles
            if (groups[curGroup].triCount > 0) {
                SubGroup ng;
                ng.name = gname;
                ng.firstTri = static_cast<int32_t>(tris.size());
                groups.push_back(std::move(ng));
                curGroup = static_cast<int32_t>(groups.size()) - 1;
            } else {
                groups[curGroup].name = gname;
            }
        } else if (line.size() >= 2 && line[0] == 'f' && line[1] == ' ') {
            faceV.clear(); faceVt.clear(); faceVn.clear();
            const char* fp = lineStart + 2;
            int nV = static_cast<int>(uniquePositions.size());
            int nVt = static_cast<int>(rawUvs.size());
            int nVn = static_cast<int>(rawNrms.size());
            while (*fp) {
                while (*fp == ' ' || *fp == '\t') ++fp;
                if (*fp == '\0') break;
                int vi, vti, vni;
                if (parseObjIndex(fp, vi, vti, vni, nV, nVt, nVn)) {
                    faceV.push_back(vi);
                    faceVt.push_back(vti);
                    faceVn.push_back(vni);
                }
                while (*fp && *fp != ' ' && *fp != '\t') ++fp;
            }
            if (faceV.size() >= 3) {
                for (size_t k = 1; k + 1 < faceV.size(); ++k) {
                    // Swap winding 1 <-> 2 to match negated X handedness
                    int idx[3] = {0, static_cast<int>(k + 1), static_cast<int>(k)};
                    bool valid = true;
                    for (int c = 0; c < 3; ++c) {
                        if (faceV[idx[c]] < 0 || faceV[idx[c]] >= nV) { valid = false; break; }
                    }
                    if (!valid) continue;

                    Triangle t{};
                    t.p0 = static_cast<uint32_t>(faceV[idx[0]]);
                    t.p1 = static_cast<uint32_t>(faceV[idx[1]]);
                    t.p2 = static_cast<uint32_t>(faceV[idx[2]]);
                    t.uv0 = (faceVt[idx[0]] >= 0 && faceVt[idx[0]] < nVt) ? rawUvs[faceVt[idx[0]]] : Vec2{0, 0};
                    t.uv1 = (faceVt[idx[1]] >= 0 && faceVt[idx[1]] < nVt) ? rawUvs[faceVt[idx[1]]] : Vec2{0, 0};
                    t.uv2 = (faceVt[idx[2]] >= 0 && faceVt[idx[2]] < nVt) ? rawUvs[faceVt[idx[2]]] : Vec2{0, 0};

                    // Wrap tiled UV islands cleanly into [0, 1] per triangle
                    Vec2 uvCenter = (t.uv0 + t.uv1 + t.uv2) * (1.0f / 3.0f);
                    if (uvCenter.x < -1e-4f || uvCenter.x > 1.0f + 1e-4f) {
                        float shiftU = std::floor(uvCenter.x);
                        t.uv0.x -= shiftU; t.uv1.x -= shiftU; t.uv2.x -= shiftU;
                    }
                    if (uvCenter.y < -1e-4f || uvCenter.y > 1.0f + 1e-4f) {
                        float shiftV = std::floor(uvCenter.y);
                        t.uv0.y -= shiftV; t.uv1.y -= shiftV; t.uv2.y -= shiftV;
                    }
                    t.groupIdx = curGroup;

                    uint32_t baseC = static_cast<uint32_t>(gpuVertices.size());
                    t.i0 = baseC; t.i1 = baseC + 1; t.i2 = baseC + 2;
                    for (int c = 0; c < 3; ++c) {
                        uint32_t pi = (c == 0) ? t.p0 : ((c == 1) ? t.p1 : t.p2);
                        Vec2 uv = (c == 0) ? t.uv0 : ((c == 1) ? t.uv1 : t.uv2);
                        Vec3 nrm = (faceVn[idx[c]] >= 0 && faceVn[idx[c]] < nVn) ? rawNrms[faceVn[idx[c]]] : Vec3{0, 1, 0};
                        const Vec3& p = uniquePositions[pi];
                        gpuVertices.push_back({p.x, p.y, p.z, nrm.x, nrm.y, nrm.z, uv.x, uv.y, 0.0f, 0.0f});
                    }
                    tris.push_back(t);
                    groups[curGroup].triCount++;
                }
            }
        }
    }

    if (tris.empty() || uniquePositions.empty()) return false;

    AABB rawBox;
    for (const Vec3& p : uniquePositions) rawBox.expand(p);
    Vec3 ext = rawBox.extent();
    float maxExt = std::max({ext.x, ext.y, ext.z, 1e-4f});

    if (normalizeScale) {
        // Fit longest dimension to 4.62 meters centered on X=0 (Z=0 center, Y floor at y=0.02)
        float sf = 4.62f / maxExt;
        float cx = 0.5f * (rawBox.min.x + rawBox.max.x);
        float cz = 0.5f * (rawBox.min.z + rawBox.max.z);
        float minY = rawBox.min.y;
        for (Vec3& p : uniquePositions) {
            p.x = (p.x - cx) * sf;
            p.y = (p.y - minY) * sf + 0.02f;
            p.z = (p.z - cz) * sf;
        }
    }

    vertexSelected.assign(uniquePositions.size(), 0);
    uniqueToCorners.assign(uniquePositions.size(), {});
    for (const Triangle& t : tris) {
        uniqueToCorners[t.p0].push_back(t.i0);
        uniqueToCorners[t.p1].push_back(t.i1);
        uniqueToCorners[t.p2].push_back(t.i2);
    }

    if (rawNrms.empty()) {
        recomputeSmoothNormals();
    } else {
        // Also normalize uniqueNormals for symmetry query
        uniqueNormals.assign(uniquePositions.size(), {0, 0, 0});
        for (const Triangle& t : tris) {
            Vec3 p0 = uniquePositions[t.p0], p1 = uniquePositions[t.p1], p2 = uniquePositions[t.p2];
            Vec3 fn = cross(p1 - p0, p2 - p0);
            uniqueNormals[t.p0] += fn;
            uniqueNormals[t.p1] += fn;
            uniqueNormals[t.p2] += fn;
        }
        for (Vec3& n : uniqueNormals) n = normalized(n);
    }

    syncRenderVertices();
    rebuildBoundsAndBVH();
    buildUvCache(1024);

    resolvedDiffuseTexPath = resolveDiffuseFromMtlOrCompanion(utf8Path(path), mtllibFiles);
    resolvedAoTexPath = resolveAoFromMtlOrCompanion(utf8Path(path), mtllibFiles, resolvedDiffuseTexPath);
    sourcePath = path;
    loaded = true;
    return true;
}

bool ObjMesh::saveToFile(const std::string& path) const {
    if (!loaded) return false;
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs.is_open()) return false;

    ofs << "# Exported by Coatroom\n";
    for (const Vec3& p : uniquePositions) {
        ofs << "v " << -p.x << " " << p.y << " " << p.z << "\n";
    }
    for (const Triangle& t : tris) {
        ofs << "vt " << t.uv0.x << " " << (1.0f - t.uv0.y) << "\n";
        ofs << "vt " << t.uv1.x << " " << (1.0f - t.uv1.y) << "\n";
        ofs << "vt " << t.uv2.x << " " << (1.0f - t.uv2.y) << "\n";
    }
    for (const Vec3& n : uniqueNormals) {
        ofs << "vn " << -n.x << " " << n.y << " " << n.z << "\n";
    }
    int32_t lastGroup = -1;
    for (size_t i = 0; i < tris.size(); ++i) {
        const Triangle& t = tris[i];
        if (t.groupIdx != lastGroup) {
            lastGroup = t.groupIdx;
            if (lastGroup >= 0 && lastGroup < static_cast<int32_t>(groups.size())) {
                ofs << "g " << groups[lastGroup].name << "\n";
            }
        }
        uint32_t vtBase = static_cast<uint32_t>(i * 3 + 1);
        ofs << "f "
            << (t.p0 + 1) << "/" << (vtBase)     << "/" << (t.p0 + 1) << " "
            << (t.p2 + 1) << "/" << (vtBase + 2) << "/" << (t.p2 + 1) << " "
            << (t.p1 + 1) << "/" << (vtBase + 1) << "/" << (t.p1 + 1) << "\n";
    }
    return ofs.good();
}

void ObjMesh::recomputeSmoothNormals() {
    uniqueNormals.assign(uniquePositions.size(), {0, 0, 0});
    for (Triangle& t : tris) {
        const Vec3& a = uniquePositions[t.p0];
        const Vec3& b = uniquePositions[t.p1];
        const Vec3& c = uniquePositions[t.p2];
        Vec3 fn = cross(b - a, c - a);
        t.faceNrm = normalized(fn);
        uniqueNormals[t.p0] += fn;
        uniqueNormals[t.p1] += fn;
        uniqueNormals[t.p2] += fn;
    }
    for (Vec3& n : uniqueNormals) n = normalized(n);
    for (const Triangle& t : tris) {
        uint32_t pi[3] = {t.p0, t.p1, t.p2};
        uint32_t ci[3] = {t.i0, t.i1, t.i2};
        for (int c = 0; c < 3; ++c) {
            const Vec3& n = uniqueNormals[pi[c]];
            gpuVertices[ci[c]].nx = n.x;
            gpuVertices[ci[c]].ny = n.y;
            gpuVertices[ci[c]].nz = n.z;
        }
    }
}

void ObjMesh::syncRenderVertices() {
    for (Triangle& t : tris) {
        const Vec3& a = uniquePositions[t.p0];
        const Vec3& b = uniquePositions[t.p1];
        const Vec3& c = uniquePositions[t.p2];
        t.faceNrm = normalized(cross(b - a, c - a));
        t.centroid = (a + b + c) * (1.0f / 3.0f);

        uint32_t pi[3] = {t.p0, t.p1, t.p2};
        uint32_t ci[3] = {t.i0, t.i1, t.i2};
        for (int k = 0; k < 3; ++k) {
            const Vec3& p = uniquePositions[pi[k]];
            VertexGPU& gv = gpuVertices[ci[k]];
            gv.px = p.x; gv.py = p.y; gv.pz = p.z;
            gv.sel = (vertexSelected[pi[k]] || t.selected) ? 1.0f : 0.0f;
            gv.maskHit = t.selected ? 1.0f : 0.0f;
        }
    }
}

void ObjMesh::rebuildBoundsAndBVH() {
    bounds = AABB{};
    for (const Vec3& p : uniquePositions) bounds.expand(p);
    centerSymmetryX = 0.5f * (bounds.min.x + bounds.max.x);

    bvhTriIndices.resize(tris.size());
    std::iota(bvhTriIndices.begin(), bvhTriIndices.end(), 0);
    bvhNodes.clear();
    bvhNodes.reserve(tris.size() * 2);
    if (!tris.empty()) {
        buildBVHNode(0, static_cast<int32_t>(tris.size()));
    }
    buildSpatialGrid();
}

int32_t ObjMesh::buildBVHNode(int32_t start, int32_t end) {
    int32_t nodeIdx = static_cast<int32_t>(bvhNodes.size());
    bvhNodes.push_back(BVHNode{});

    AABB box;
    AABB cbox;
    for (int32_t i = start; i < end; ++i) {
        const Triangle& t = tris[bvhTriIndices[i]];
        box.expand(uniquePositions[t.p0]);
        box.expand(uniquePositions[t.p1]);
        box.expand(uniquePositions[t.p2]);
        cbox.expand(t.centroid);
    }
    bvhNodes[nodeIdx].box = box;

    int32_t cnt = end - start;
    if (cnt <= 4) {
        bvhNodes[nodeIdx].first = start;
        bvhNodes[nodeIdx].count = cnt;
        return nodeIdx;
    }

    Vec3 ext = cbox.extent();
    int axis = 0;
    if (ext.y > ext.x) axis = 1;
    if (ext.z > ext[axis]) axis = 2;
    if (ext[axis] < 1e-6f) {
        bvhNodes[nodeIdx].first = start;
        bvhNodes[nodeIdx].count = cnt;
        return nodeIdx;
    }

    int32_t mid = start + cnt / 2;
    std::nth_element(bvhTriIndices.begin() + start, bvhTriIndices.begin() + mid, bvhTriIndices.begin() + end,
        [&](int32_t a, int32_t b) { return tris[a].centroid[axis] < tris[b].centroid[axis]; });

    int32_t left = buildBVHNode(start, mid);
    int32_t right = buildBVHNode(mid, end);
    bvhNodes[nodeIdx].left = left;
    bvhNodes[nodeIdx].right = right;
    bvhNodes[nodeIdx].count = 0;
    return nodeIdx;
}

void ObjMesh::buildSpatialGrid() {
    spatialGrid.clear();
    Vec3 ext = bounds.extent();
    float maxE = std::max({ext.x, ext.y, ext.z, 0.5f});
    gridCellSize = maxE / 40.0f;
    float invCell = 1.0f / gridCellSize;

    for (int32_t ti = 0; ti < static_cast<int32_t>(tris.size()); ++ti) {
        const Triangle& t = tris[ti];
        AABB tb;
        tb.expand(uniquePositions[t.p0]);
        tb.expand(uniquePositions[t.p1]);
        tb.expand(uniquePositions[t.p2]);
        int gx0 = static_cast<int>(std::floor(tb.min.x * invCell));
        int gx1 = static_cast<int>(std::floor(tb.max.x * invCell));
        int gy0 = static_cast<int>(std::floor(tb.min.y * invCell));
        int gy1 = static_cast<int>(std::floor(tb.max.y * invCell));
        int gz0 = static_cast<int>(std::floor(tb.min.z * invCell));
        int gz1 = static_cast<int>(std::floor(tb.max.z * invCell));
        // Clamp span on unusually giant single triangles
        if (gx1 - gx0 > 6) gx1 = gx0 + 6;
        if (gy1 - gy0 > 6) gy1 = gy0 + 6;
        if (gz1 - gz0 > 6) gz1 = gz0 + 6;
        for (int gx = gx0; gx <= gx1; ++gx)
            for (int gy = gy0; gy <= gy1; ++gy)
                for (int gz = gz0; gz <= gz1; ++gz)
                    spatialGrid[hashCellKey(gx, gy, gz)].push_back(ti);
    }
}

RayHit ObjMesh::raycast(const Ray& r, bool respectGroupVisibility) const {
    RayHit best;
    if (bvhNodes.empty()) return best;

    Vec3 invDir{
        (std::fabs(r.dir.x) > 1e-8f) ? (1.0f / r.dir.x) : 1e8f,
        (std::fabs(r.dir.y) > 1e-8f) ? (1.0f / r.dir.y) : 1e8f,
        (std::fabs(r.dir.z) > 1e-8f) ? (1.0f / r.dir.z) : 1e8f
    };

    int32_t stack[128];
    int sp = 0;
    stack[sp++] = 0;

    while (sp > 0) {
        int32_t ni = stack[--sp];
        const BVHNode& node = bvhNodes[ni];
        float boxT = 0.0f;
        if (!rayAABB(r, invDir, node.box, best.t, boxT)) continue;

        if (node.count > 0) {
            for (int32_t k = 0; k < node.count; ++k) {
                int32_t ti = bvhTriIndices[node.first + k];
                const Triangle& tri = tris[ti];
                if (respectGroupVisibility && !groups[tri.groupIdx].visible) continue;
                float tHit = 0.0f, ub = 0.0f, vb = 0.0f;
                if (rayTriangle(r, uniquePositions[tri.p0], uniquePositions[tri.p1], uniquePositions[tri.p2],
                                best.t, tHit, ub, vb)) {
                    best.hit = true;
                    best.t = tHit;
                    best.triIdx = ti;
                    best.uBary = ub;
                    best.vBary = vb;
                    float w = 1.0f - ub - vb;
                    best.pos = uniquePositions[tri.p0] * w + uniquePositions[tri.p1] * ub + uniquePositions[tri.p2] * vb;
                    best.normal = normalized(Vec3{
                        gpuVertices[tri.i0].nx * w + gpuVertices[tri.i1].nx * ub + gpuVertices[tri.i2].nx * vb,
                        gpuVertices[tri.i0].ny * w + gpuVertices[tri.i1].ny * ub + gpuVertices[tri.i2].ny * vb,
                        gpuVertices[tri.i0].nz * w + gpuVertices[tri.i1].nz * ub + gpuVertices[tri.i2].nz * vb
                    });
                    best.uv = wrapUv01(tri.uv0 * w + tri.uv1 * ub + tri.uv2 * vb);
                }
            }
        } else {
            if (node.left >= 0 && sp < 126) stack[sp++] = node.left;
            if (node.right >= 0 && sp < 126) stack[sp++] = node.right;
        }
    }
    return best;
}

float ObjMesh::raycastDist(const Ray& r, float maxDist, bool respectGroupVisibility) const {
    float closest = maxDist;
    if (bvhNodes.empty()) return closest;

    Vec3 invDir{
        (std::fabs(r.dir.x) > 1e-8f) ? (1.0f / r.dir.x) : 1e8f,
        (std::fabs(r.dir.y) > 1e-8f) ? (1.0f / r.dir.y) : 1e8f,
        (std::fabs(r.dir.z) > 1e-8f) ? (1.0f / r.dir.z) : 1e8f
    };

    int32_t stack[128];
    int sp = 0;
    stack[sp++] = 0;

    while (sp > 0) {
        const BVHNode& node = bvhNodes[stack[--sp]];
        float boxT = 0.0f;
        if (!rayAABB(r, invDir, node.box, closest, boxT)) continue;

        if (node.count > 0) {
            for (int32_t k = 0; k < node.count; ++k) {
                int32_t ti = bvhTriIndices[node.first + k];
                const Triangle& tri = tris[ti];
                if (respectGroupVisibility && !groups[tri.groupIdx].visible) continue;
                float tHit = 0.0f, ub = 0.0f, vb = 0.0f;
                if (rayTriangle(r, uniquePositions[tri.p0], uniquePositions[tri.p1], uniquePositions[tri.p2],
                                closest, tHit, ub, vb)) {
                    if (tHit > 1e-4f && tHit < closest) {
                        closest = tHit;
                    }
                }
            }
        } else {
            if (node.left >= 0 && sp < 126) stack[sp++] = node.left;
            if (node.right >= 0 && sp < 126) stack[sp++] = node.right;
        }
    }
    return closest;
}

void ObjMesh::collectTrianglesInAABB(const Vec3& bmin, const Vec3& bmax, std::vector<int32_t>& outTris) const {
    outTris.clear();
    if (bvhNodes.empty()) return;

    int32_t stack[128];
    int sp = 0;
    stack[sp++] = 0;

    while (sp > 0) {
        const BVHNode& node = bvhNodes[stack[--sp]];
        if (bmax.x < node.box.min.x || bmin.x > node.box.max.x ||
            bmax.y < node.box.min.y || bmin.y > node.box.max.y ||
            bmax.z < node.box.min.z || bmin.z > node.box.max.z) {
            continue;
        }
        if (node.count > 0) {
            for (int32_t k = 0; k < node.count; ++k) {
                int32_t ti = bvhTriIndices[node.first + k];
                const Triangle& tri = tris[ti];
                if (!groups[tri.groupIdx].visible) continue;
                outTris.push_back(ti);
            }
        } else {
            if (node.left >= 0 && sp < 126) stack[sp++] = node.left;
            if (node.right >= 0 && sp < 126) stack[sp++] = node.right;
        }
    }
}

RayHit ObjMesh::projectPointToSurface(const Vec3& targetPos, const Vec3& targetNrm, float maxDist) const {
    RayHit best;
    float bestScore = maxDist * maxDist;
    float invCell = 1.0f / gridCellSize;
    int cx = static_cast<int>(std::floor(targetPos.x * invCell));
    int cy = static_cast<int>(std::floor(targetPos.y * invCell));
    int cz = static_cast<int>(std::floor(targetPos.z * invCell));
    int rCells = std::clamp(static_cast<int>(std::ceil(maxDist * invCell)), 1, 3);

    for (int dx = -rCells; dx <= rCells; ++dx) {
        for (int dy = -rCells; dy <= rCells; ++dy) {
            for (int dz = -rCells; dz <= rCells; ++dz) {
                auto it = spatialGrid.find(hashCellKey(cx + dx, cy + dy, cz + dz));
                if (it == spatialGrid.end()) continue;
                for (int32_t ti : it->second) {
                    const Triangle& t = tris[ti];
                    if (!groups[t.groupIdx].visible) continue;
                    // Check compatible normal facing
                    if (dot(t.faceNrm, targetNrm) < -0.25f) continue;
                    float ub = 0.0f, vb = 0.0f;
                    Vec3 cl{};
                    float dSq = pointTriDistSq(targetPos, uniquePositions[t.p0], uniquePositions[t.p1], uniquePositions[t.p2],
                                               ub, vb, cl);
                    if (dSq < bestScore) {
                        bestScore = dSq;
                        best.hit = true;
                        best.triIdx = ti;
                        best.t = std::sqrt(dSq);
                        best.pos = cl;
                        best.uBary = ub;
                        best.vBary = vb;
                        float w = 1.0f - ub - vb;
                        best.normal = normalized(Vec3{
                            gpuVertices[t.i0].nx * w + gpuVertices[t.i1].nx * ub + gpuVertices[t.i2].nx * vb,
                            gpuVertices[t.i0].ny * w + gpuVertices[t.i1].ny * ub + gpuVertices[t.i2].ny * vb,
                            gpuVertices[t.i0].nz * w + gpuVertices[t.i1].nz * ub + gpuVertices[t.i2].nz * vb
                        });
                        best.uv = wrapUv01(t.uv0 * w + t.uv1 * ub + t.uv2 * vb);
                    }
                }
            }
        }
    }
    return best;
}

void ObjMesh::buildUvCache(int cacheSize) {
    uvCacheDim = cacheSize;
    uvCache.assign(static_cast<size_t>(cacheSize) * cacheSize, UvSurfaceCell{});

    auto edgeFn = [](const Vec2& a, const Vec2& b, const Vec2& c) {
        return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    };

    float S = static_cast<float>(cacheSize);
    for (int32_t ti = 0; ti < static_cast<int32_t>(tris.size()); ++ti) {
        const Triangle& t = tris[ti];
        Vec2 p0 = {t.uv0.x * S, t.uv0.y * S};
        Vec2 p1 = {t.uv1.x * S, t.uv1.y * S};
        Vec2 p2 = {t.uv2.x * S, t.uv2.y * S};
        float area = edgeFn(p0, p1, p2);
        if (std::fabs(area) < 1e-4f) continue;

        int x0 = static_cast<int>(std::floor(std::min({p0.x, p1.x, p2.x})));
        int x1 = static_cast<int>(std::ceil(std::max({p0.x, p1.x, p2.x})));
        int y0 = static_cast<int>(std::floor(std::min({p0.y, p1.y, p2.y})));
        int y1 = static_cast<int>(std::ceil(std::max({p0.y, p1.y, p2.y})));
        if (x1 - x0 > cacheSize * 2) { x0 = std::clamp(x0, 0, cacheSize - 1); x1 = std::clamp(x1, 0, cacheSize - 1); }
        if (y1 - y0 > cacheSize * 2) { y0 = std::clamp(y0, 0, cacheSize - 1); y1 = std::clamp(y1, 0, cacheSize - 1); }

        for (int py = y0; py <= y1; ++py) {
            int wy = ((py % cacheSize) + cacheSize) % cacheSize;
            for (int px = x0; px <= x1; ++px) {
                Vec2 p{px + 0.5f, py + 0.5f};
                float w0 = edgeFn(p1, p2, p) / area;
                float u  = edgeFn(p2, p0, p) / area;
                float v  = edgeFn(p0, p1, p) / area;
                if (w0 >= -0.02f && u >= -0.02f && v >= -0.02f) {
                    int wx = ((px % cacheSize) + cacheSize) % cacheSize;
                    size_t idx = static_cast<size_t>(wy) * cacheSize + wx;
                    if (uvCache[idx].triIdx < 0) {
                        uvCache[idx] = {ti, std::clamp(u, 0.0f, 1.0f), std::clamp(v, 0.0f, 1.0f)};
                    }
                }
            }
        }
    }
}

bool ObjMesh::queryUvAtPixel(float uNorm, float vNorm, Vec3& outPos, Vec3& outNrm, int32_t& outTri) const {
    if (uvCacheDim <= 0) return false;
    Vec2 uv = wrapUv01(Vec2{uNorm, vNorm});
    int px = std::clamp(static_cast<int>(uv.x * uvCacheDim), 0, uvCacheDim - 1);
    int py = std::clamp(static_cast<int>(uv.y * uvCacheDim), 0, uvCacheDim - 1);
    const UvSurfaceCell& cell = uvCache[static_cast<size_t>(py) * uvCacheDim + px];
    if (cell.triIdx < 0) return false;
    const Triangle& t = tris[cell.triIdx];
    float w = std::max(0.0f, 1.0f - cell.uBary - cell.vBary);
    outPos = uniquePositions[t.p0] * w + uniquePositions[t.p1] * cell.uBary + uniquePositions[t.p2] * cell.vBary;
    outNrm = normalized(Vec3{
        gpuVertices[t.i0].nx * w + gpuVertices[t.i1].nx * cell.uBary + gpuVertices[t.i2].nx * cell.vBary,
        gpuVertices[t.i0].ny * w + gpuVertices[t.i1].ny * cell.uBary + gpuVertices[t.i2].ny * cell.vBary,
        gpuVertices[t.i0].nz * w + gpuVertices[t.i1].nz * cell.uBary + gpuVertices[t.i2].nz * cell.vBary
    });
    outTri = cell.triIdx;
    return true;
}

float ObjMesh::estimateUvPerMeter(const Vec3& pos3D, const Vec3& nrm3D) const {
    if (!loaded || tris.empty()) return 0.16f;
    Vec3 pad{0.15f, 0.15f, 0.15f};
    std::vector<int32_t> candTris;
    collectTrianglesInAABB(pos3D - pad, pos3D + pad, candTris);
    if (candTris.empty()) return 0.16f;

    Vec3 nUse = (lengthSq(nrm3D) > 1e-6f) ? normalized(nrm3D) : Vec3{0.0f, 1.0f, 0.0f};
    double totalWorldArea = 0.0;
    double totalUvArea = 0.0;
    for (int32_t ti : candTris) {
        const Triangle& t = tris[ti];
        if (!groups[t.groupIdx].visible) continue;
        if (dot(t.faceNrm, nUse) <= 0.3f) continue;

        const Vec3& a = uniquePositions[t.p0];
        const Vec3& b = uniquePositions[t.p1];
        const Vec3& c = uniquePositions[t.p2];
        float worldArea = 0.5f * length(cross(b - a, c - a));
        float uvArea = 0.5f * std::fabs((t.uv1.x - t.uv0.x) * (t.uv2.y - t.uv0.y) -
                                        (t.uv1.y - t.uv0.y) * (t.uv2.x - t.uv0.x));
        if (worldArea > 1e-7f && uvArea > 1e-9f) {
            totalWorldArea += worldArea;
            totalUvArea += uvArea;
        }
    }
    if (totalWorldArea <= 1e-7 || totalUvArea <= 1e-9) return 0.16f;
    float uvPerM = std::sqrt(static_cast<float>(totalUvArea / std::max(totalWorldArea, 1e-7)));
    return std::clamp(uvPerM, 0.03f, 1.50f);
}

void ObjMesh::clearSelection() {
    std::fill(vertexSelected.begin(), vertexSelected.end(), 0);
    for (Triangle& t : tris) t.selected = false;
    syncRenderVertices();
}

int32_t ObjMesh::selectNearRay(const Ray& r, float radius, bool add, bool symmetricX) {
    RayHit hit = raycast(r);
    if (!add) clearSelection();
    if (!hit.hit) return 0;

    float r2 = radius * radius;
    Vec3 symPos{2.0f * centerSymmetryX - hit.pos.x, hit.pos.y, hit.pos.z};
    int32_t cnt = 0;
    for (size_t i = 0; i < uniquePositions.size(); ++i) {
        if (lengthSq(uniquePositions[i] - hit.pos) <= r2 ||
            (symmetricX && lengthSq(uniquePositions[i] - symPos) <= r2)) {
            vertexSelected[i] = 1;
            ++cnt;
        }
    }
    syncRenderVertices();
    return cnt;
}

int32_t ObjMesh::selectFacePaintMaskByRay(const Ray& r, bool selectConnectedAngle, float angleDeg, bool add, bool symmetricX) {
    RayHit hit = raycast(r);
    if (!add) clearSelection();
    if (!hit.hit) return 0;

    auto markCluster = [&](int32_t startTri) {
        if (startTri < 0) return 0;
        if (!selectConnectedAngle) {
            tris[startTri].selected = true;
            return 1;
        }
        float cosLim = std::cos(angleDeg * kDeg2Rad);
        const Triangle& root = tris[startTri];
        int32_t grp = root.groupIdx;
        int32_t total = 0;
        // Fast spatial radius/angle flood inside same group
        std::vector<uint8_t> vis(tris.size(), 0);
        std::queue<int32_t> q;
        q.push(startTri);
        vis[startTri] = 1;
        float invCell = 1.0f / gridCellSize;
        while (!q.empty()) {
            int32_t cur = q.front(); q.pop();
            const Triangle& ct = tris[cur];
            tris[cur].selected = true;
            ++total;
            if (total > 25000) break;
            int gx = static_cast<int>(std::floor(ct.centroid.x * invCell));
            int gy = static_cast<int>(std::floor(ct.centroid.y * invCell));
            int gz = static_cast<int>(std::floor(ct.centroid.z * invCell));
            for (int dx = -1; dx <= 1; ++dx)
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dz = -1; dz <= 1; ++dz) {
                        auto it = spatialGrid.find(hashCellKey(gx + dx, gy + dy, gz + dz));
                        if (it == spatialGrid.end()) continue;
                        for (int32_t nt : it->second) {
                            if (vis[nt]) continue;
                            const Triangle& tr = tris[nt];
                            if (tr.groupIdx != grp) continue;
                            // Shared vertex check
                            bool shared = (tr.p0==ct.p0||tr.p0==ct.p1||tr.p0==ct.p2||
                                           tr.p1==ct.p0||tr.p1==ct.p1||tr.p1==ct.p2||
                                           tr.p2==ct.p0||tr.p2==ct.p1||tr.p2==ct.p2);
                            if (shared && dot(tr.faceNrm, ct.faceNrm) >= cosLim) {
                                vis[nt] = 1;
                                q.push(nt);
                            }
                        }
                    }
        }
        return total;
    };

    int32_t marked = markCluster(hit.triIdx);
    if (symmetricX) {
        Vec3 mp{2.0f * centerSymmetryX - hit.pos.x, hit.pos.y, hit.pos.z};
        Vec3 mn{-hit.normal.x, hit.normal.y, hit.normal.z};
        RayHit mh = projectPointToSurface(mp, mn, gridCellSize * 2.5f);
        if (mh.hit && mh.triIdx != hit.triIdx) marked += markCluster(mh.triIdx);
    }
    syncRenderVertices();
    return marked;
}

void ObjMesh::translateSelectedVertices(const Vec3& delta, bool mirrorX) {
    for (size_t i = 0; i < uniquePositions.size(); ++i) {
        if (!vertexSelected[i]) continue;
        Vec3 d = delta;
        if (mirrorX && uniquePositions[i].x < centerSymmetryX) d.x = -d.x;
        uniquePositions[i] += d;
    }
    recomputeSmoothNormals();
    syncRenderVertices();
    rebuildBoundsAndBVH();
}

void ObjMesh::scaleSelectedVertices(const Vec3& pivot, const Vec3& factor, bool mirrorX) {
    (void)mirrorX;
    for (size_t i = 0; i < uniquePositions.size(); ++i) {
        if (!vertexSelected[i]) continue;
        Vec3 d = uniquePositions[i] - pivot;
        uniquePositions[i] = pivot + Vec3{d.x * factor.x, d.y * factor.y, d.z * factor.z};
    }
    recomputeSmoothNormals();
    syncRenderVertices();
    rebuildBoundsAndBVH();
}

void ObjMesh::smoothSelectedVertices(float strength, int iterations, bool mirrorX) {
    (void)mirrorX;
    std::vector<Vec3> adjSum(uniquePositions.size());
    std::vector<int> adjCnt(uniquePositions.size());
    for (int it = 0; it < iterations; ++it) {
        std::fill(adjSum.begin(), adjSum.end(), Vec3{0, 0, 0});
        std::fill(adjCnt.begin(), adjCnt.end(), 0);
        for (const Triangle& t : tris) {
            uint32_t p[3] = {t.p0, t.p1, t.p2};
            for (int e = 0; e < 3; ++e) {
                uint32_t a = p[e], b = p[(e + 1) % 3];
                adjSum[a] += uniquePositions[b]; adjCnt[a]++;
                adjSum[b] += uniquePositions[a]; adjCnt[b]++;
            }
        }
        for (size_t i = 0; i < uniquePositions.size(); ++i) {
            if (!vertexSelected[i] || adjCnt[i] == 0) continue;
            Vec3 avg = adjSum[i] * (1.0f / static_cast<float>(adjCnt[i]));
            uniquePositions[i] = lerp(uniquePositions[i], avg, std::clamp(strength, 0.0f, 1.0f));
        }
    }
    recomputeSmoothNormals();
    syncRenderVertices();
    rebuildBoundsAndBVH();
}

} // namespace lps
