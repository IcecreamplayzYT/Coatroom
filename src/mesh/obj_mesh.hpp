#pragma once

#include "math/geom.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace lps {

struct VertexGPU {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
    float sel;      // >0.5 when selected in 3D Edit mode or highlighted
    float maskHit;  // reserved for per-vertex highlight/paint mask preview
};

struct SubGroup {
    std::string name;
    int32_t firstTri = 0;
    int32_t triCount = 0;
    bool visible = true;
};

struct Triangle {
    uint32_t i0, i1, i2; // indices into render vertices (corner vertices)
    uint32_t p0, p1, p2; // indices into unique weld positions (for 3D editing)
    Vec2 uv0, uv1, uv2;
    Vec3 faceNrm;
    Vec3 centroid;
    int32_t groupIdx = 0;
    bool selected = false; // used both for 3D Face Edit & Face Paint Mask!
};

struct RayHit {
    bool hit = false;
    float t = 1e30f;
    int32_t triIdx = -1;
    Vec3 pos;
    Vec3 normal;
    Vec2 uv;
    float uBary = 0.0f;
    float vBary = 0.0f;
};

struct BVHNode {
    AABB box;
    int32_t left = -1;
    int32_t right = -1;
    int32_t first = 0;
    int32_t count = 0;
};

// Rasterized UV -> Triangle lookup at working resolution (for 2D UV Canvas <-> 3D Mirror symmetry)
struct UvSurfaceCell {
    int32_t triIdx = -1;
    float uBary = 0.0f;
    float vBary = 0.0f;
};

class ObjMesh {
public:
    bool loadFromFile(const std::string& path, bool normalizeScale = true);
    bool saveToFile(const std::string& path) const;

    void rebuildBoundsAndBVH();
    void recomputeSmoothNormals();
    void syncRenderVertices(); // call after modifying uniquePositions or vertex colors/selection
    void buildUvCache(int cacheSize = 1024);

    // 3D Raycast against visible triangles
    RayHit raycast(const Ray& r, bool respectGroupVisibility = true) const;
    // Fast occlusion/distance raycast clipped to maxDist (returns < maxDist if occluded)
    float raycastDist(const Ray& r, float maxDist, bool respectGroupVisibility = true) const;
    // Collect visible triangles overlapping [bmin, bmax] via BVH (thread-safe)
    void collectTrianglesInAABB(const Vec3& bmin, const Vec3& bmax, std::vector<int32_t>& outTris) const;

    // Project a 3D point + surface normal to the closest triangle within maxDist
    RayHit projectPointToSurface(const Vec3& targetPos, const Vec3& targetNrm, float maxDist) const;

    // Query mirrored 3D -> UV correspondence for bilateral L<->R symmetry
    bool queryUvAtPixel(float uNorm, float vNorm, Vec3& outPos, Vec3& outNrm, int32_t& outTri) const;
    float estimateUvPerMeter(const Vec3& pos3D, const Vec3& nrm3D) const;

    // 3D Edit Mode tools
    void clearSelection();
    int32_t selectNearRay(const Ray& r, float radius, bool add, bool symmetricX);
    int32_t selectFacePaintMaskByRay(const Ray& r, bool selectConnectedAngle, float angleDeg, bool add, bool symmetricX);
    void translateSelectedVertices(const Vec3& delta, bool mirrorX);
    void scaleSelectedVertices(const Vec3& pivot, const Vec3& factor, bool mirrorX);
    void smoothSelectedVertices(float strength, int iterations, bool mirrorX);

    std::string sourcePath;
    std::string resolvedDiffuseTexPath;
    std::string resolvedAoTexPath;
    AABB bounds;
    float centerSymmetryX = 0.0f;
    bool loaded = false;

    // Unique spatial positions for clean watertight 3D vertex manipulation
    std::vector<Vec3> uniquePositions;
    std::vector<Vec3> uniqueNormals;
    std::vector<uint8_t> vertexSelected;

    // Renderable corner vertices (3 * tris.size(), or indexed corners)
    std::vector<VertexGPU> gpuVertices;
    std::vector<Triangle> tris;
    std::vector<SubGroup> groups;
    std::vector<int32_t> triGroupAdjacency; // optional smooth connectivity built lazily

    // Vertex -> incident corner indices
    std::vector<std::vector<uint32_t>> uniqueToCorners;

private:
    void buildSpatialGrid();
    int32_t buildBVHNode(int32_t start, int32_t end);

    std::vector<BVHNode> bvhNodes;
    std::vector<int32_t> bvhTriIndices;

    // Fast uniform 3D spatial hash for nearest-surface mirror lookup
    float gridCellSize = 0.12f;
    std::unordered_map<uint64_t, std::vector<int32_t>> spatialGrid;

    // UV lookup grid (cacheSize x cacheSize)
    int uvCacheDim = 0;
    std::vector<UvSurfaceCell> uvCache;
};

} // namespace lps
