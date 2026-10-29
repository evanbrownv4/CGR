// Geometric primitives: spheres, discs, infinite planes and triangles (meshes).
// Every primitive supports ray intersection, UV generation, bounds and uniform
// area sampling (so any primitive can act as an area light).
#pragma once

#include <memory>
#include <vector>

#include "material.h"
#include "math.h"

struct AreaLight;
class Primitive;

struct Hit {
    double t = INF;
    Vec3 p;        // world-space hit point
    Vec3 ng;       // geometric normal (oriented according to the shape's orientation)
    Vec3 ns;       // shading normal (interpolated for meshes), same hemisphere as ng
    Vec2 uv;       // surface parameterisation
    const Primitive* prim = nullptr;
};

class Primitive {
public:
    virtual ~Primitive() = default;
    virtual bool intersect(const Ray& r, double tmin, double tmax, Hit& h) const = 0;
    // An invalid (empty) box means the primitive is unbounded (e.g. an infinite plane).
    virtual AABB bounds() const = 0;
    virtual double area() const = 0;
    // Uniformly samples a point (by area) given u in [0,1)^2.
    virtual void sample(const Vec2& u, Vec3& p, Vec3& n) const = 0;
    virtual const char* typeName() const = 0;

    std::shared_ptr<Material> material;
    const AreaLight* light = nullptr;
};

class Sphere : public Primitive {
public:
    Sphere(const Transform& objToWorld, double radius, bool reverse);
    bool intersect(const Ray& r, double tmin, double tmax, Hit& h) const override;
    AABB bounds() const override;
    double area() const override;
    void sample(const Vec2& u, Vec3& p, Vec3& n) const override;
    const char* typeName() const override { return "sphere"; }

private:
    Transform xf_, inv_;
    double radius_;
    bool flip_;
};

// Disc in the object-space plane z = height, normal +z (PBRT "disk").
class Disk : public Primitive {
public:
    Disk(const Transform& objToWorld, double radius, double height, bool reverse);
    bool intersect(const Ray& r, double tmin, double tmax, Hit& h) const override;
    AABB bounds() const override;
    double area() const override;
    void sample(const Vec2& u, Vec3& p, Vec3& n) const override;
    const char* typeName() const override { return "disk"; }

private:
    Transform xf_, inv_;
    double radius_, height_;
    bool flip_;
};

// Infinite plane through `point` with normal `normal` (world space).
// UVs are planar coordinates (world units) along an orthonormal tangent frame.
class Plane : public Primitive {
public:
    Plane(const Vec3& point, const Vec3& normal, const Vec3& uAxisHint);
    bool intersect(const Ray& r, double tmin, double tmax, Hit& h) const override;
    AABB bounds() const override { return AABB(); }
    double area() const override { return INF; }
    void sample(const Vec2&, Vec3& p, Vec3& n) const override { p = point_; n = n_; }
    const char* typeName() const override { return "plane"; }

private:
    Vec3 point_, n_, t_, b_;
};

// Shared vertex data for a triangle mesh (already in world space).
struct Mesh {
    std::vector<Vec3> P;
    std::vector<Vec3> N;   // optional per-vertex normals
    std::vector<Vec2> UV;  // optional per-vertex uvs
    std::vector<int> indices;
    bool flipNormals = false;  // reverse orientation XOR transform swaps handedness
};

class Triangle : public Primitive {
public:
    Triangle(std::shared_ptr<const Mesh> mesh, int tri);
    bool intersect(const Ray& r, double tmin, double tmax, Hit& h) const override;
    AABB bounds() const override;
    double area() const override;
    void sample(const Vec2& u, Vec3& p, Vec3& n) const override;
    const char* typeName() const override { return "triangle"; }
    const Vec3& vertex(int k) const { return mesh_->P[v_[k]]; }

private:
    Vec3 geometricNormal() const;
    std::shared_ptr<const Mesh> mesh_;
    int v_[3];
};
