// Scene container: geometry, lights, camera and all render settings parsed from
// the scene file, plus a bounding volume hierarchy for ray queries.
#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "camera.h"
#include "image.h"
#include "lights.h"
#include "sampling.h"
#include "shapes.h"

struct RenderSettings {
    // Integrator ("whitted" or "distributed")
    std::string integrator = "whitted";
    int maxDepth = 5;                // maximum ray-tree depth (MD)
    Color ambient{0.05, 0.05, 0.05}; // WSRT ambient light intensity
    int branching = 4;               // DRT branching factor (BF): rays per scattering event
    int lightSamples = 16;           // DRT shadow rays per area light per shading point
    SampleStrategy lightSampler = SampleStrategy::Uniform;
    bool randomizeHalton = true;     // Cranley-Patterson rotation per shading point
    bool cosineHemisphere = false;   // DRT indirect: cosine-weighted (true) or uniform (false)
    bool caustics = false;           // DRT: count emitters reached via specular bounces after a diffuse bounce
    double rayEpsilon = 1e-4;        // offset for secondary rays (self-intersection)

    // Pixel sampling (antialiasing)
    int spp = 1;
    SampleStrategy pixelSampler = SampleStrategy::Grid;
    uint64_t seed = 0;

    // Film
    int width = 1280, height = 720;
    std::string outputName;          // base name without extension
    std::string outputExt = ".png";  // ".png" or ".ppm"
    ToneMapSettings tonemap;
    bool tonemapSpecified = false;
    bool writeHDR = false;           // additionally write linear radiance as .pfm

    bool useBVH = true;
    int threads = 0;                 // 0 = hardware concurrency
};

// Per-thread ray counters, aggregated for profiling output.
struct RayStats {
    uint64_t camera = 0, shadow = 0, secondary = 0;
    void add(const RayStats& o) { camera += o.camera; shadow += o.shadow; secondary += o.secondary; }
    uint64_t total() const { return camera + shadow + secondary; }
};

class BVH {
public:
    void build(const std::vector<const Primitive*>& prims);
    bool intersect(const Ray& r, double tmin, double tmax, Hit& h) const;
    bool occluded(const Ray& r, double tmin, double tmax) const;
    size_t nodeCount() const { return nodes_.size(); }
    int depth() const { return depth_; }

private:
    struct Node {
        AABB box;
        int left = -1, right = -1;  // children (interior)
        int first = 0, count = 0;   // primitive range (leaf when count > 0)
    };
    struct Item {
        const Primitive* prim;
        AABB box;
        Vec3 centroid;
    };
    int buildRec(int first, int last, int depth);
    std::vector<Item> items_;  // build-time only
    std::vector<Node> nodes_;
    std::vector<const Primitive*> prims_;
    int depth_ = 0;
};

class Scene {
public:
    Camera camera;
    RenderSettings settings;
    Color background{0, 0, 0};

    std::vector<std::shared_ptr<Primitive>> primitives;
    std::vector<std::shared_ptr<const Mesh>> meshes;
    std::vector<PointLight> pointLights;
    std::vector<DistantLight> distantLights;
    std::vector<std::unique_ptr<AreaLight>> areaLights;

    // Builds acceleration structures; call after parsing.
    void prepare();
    bool intersect(const Ray& r, double tmin, double tmax, Hit& h) const;
    bool occluded(const Ray& r, double tmin, double tmax) const;
    AABB worldBounds() const { return bounds_; }
    size_t boundedCount() const { return bounded_.size(); }

private:
    std::vector<const Primitive*> bounded_, unbounded_;
    BVH bvh_;
    AABB bounds_;
};
