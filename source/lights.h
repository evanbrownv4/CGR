// Light sources: point lights, distant lights and diffuse area lights.
#pragma once

#include <vector>

#include "math.h"
#include "shapes.h"

struct PointLight {
    Vec3 position;
    Color intensity;  // radiant intensity I (W/sr); irradiance falls off as I / r^2
};

struct DistantLight {
    Vec3 toLight;  // unit direction pointing towards the light
    Color radiance;
};

// A diffuse emitter made of one or more primitives (a sphere, disc or mesh).
struct AreaLight {
    Color L;                 // emitted radiance
    bool twoSided = false;
    std::vector<const Primitive*> prims;
    std::vector<double> cdf;  // cumulative area, for choosing a primitive
    double totalArea = 0;
    std::string shapeName;

    // If the light is a single parallelogram (e.g. a quad given as two triangles)
    // samples map directly onto it, so a regular grid in [0,1)^2 becomes a regular
    // grid on the light surface.
    bool isParallelogram = false;
    Vec3 corner, edgeU, edgeV, quadNormal;

    // Call once after all primitives have been added.
    void finalize();
    // Samples a point uniformly by area. pdf (w.r.t. area) = 1 / totalArea.
    void sample(const Vec2& u, Vec3& p, Vec3& n) const;
    // Radiance leaving the surface at a hit with normal ng towards direction wo.
    Color emitted(const Vec3& ng, const Vec3& wo) const {
        return (twoSided || dot(ng, wo) > 0) ? L : Color(0);
    }
    Vec3 centroid() const;
};
