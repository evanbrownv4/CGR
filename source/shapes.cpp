#include "shapes.h"

#include "sampling.h"

// ---------------------------------------------------------------- Sphere ---

Sphere::Sphere(const Transform& objToWorld, double radius, bool reverse)
    : xf_(objToWorld), inv_(objToWorld.inverse()), radius_(radius), flip_(reverse != objToWorld.swapsHandedness()) {}

bool Sphere::intersect(const Ray& rw, double tmin, double tmax, Hit& h) const {
    // Intersect in object space, where the sphere is centred at the origin.
    Ray r = inv_.ray(rw);
    double a = dot(r.d, r.d);
    double b = 2 * dot(r.d, r.o);
    double c = dot(r.o, r.o) - radius_ * radius_;
    double disc = b * b - 4 * a * c;
    if (disc < 0) return false;
    double sq = std::sqrt(disc);
    // Numerically stable quadratic roots.
    double q = b < 0 ? -0.5 * (b - sq) : -0.5 * (b + sq);
    double t0 = q / a, t1 = c / q;
    if (t0 > t1) std::swap(t0, t1);
    double t = t0;
    if (t <= tmin || t >= tmax) {
        t = t1;
        if (t <= tmin || t >= tmax) return false;
    }
    Vec3 po = r.at(t);
    po = po * (radius_ / length(po));  // refine onto the surface
    double phi = std::atan2(po.y, po.x);
    if (phi < 0) phi += 2 * PI;
    double theta = std::acos(clampd(po.z / radius_, -1.0, 1.0));
    h.t = t;
    h.p = xf_.point(po);
    h.ng = normalize(xf_.normal(po));
    if (flip_) h.ng = -h.ng;
    h.ns = h.ng;
    h.uv = Vec2(phi / (2 * PI), 1.0 - theta / PI);
    h.prim = this;
    return true;
}

AABB Sphere::bounds() const {
    AABB b;
    for (int i = 0; i < 8; ++i)
        b.expand(xf_.point(Vec3((i & 1) ? radius_ : -radius_, (i & 2) ? radius_ : -radius_, (i & 4) ? radius_ : -radius_)));
    return b;
}

double Sphere::area() const {
    // Assumes (approximately) uniform scaling for area light sampling.
    double s = length(xf_.vector(Vec3(1, 0, 0)));
    return 4 * PI * radius_ * radius_ * s * s;
}

void Sphere::sample(const Vec2& u, Vec3& p, Vec3& n) const {
    Vec3 po = uniformSampleSphere(u) * radius_;
    p = xf_.point(po);
    n = normalize(xf_.normal(po));
    if (flip_) n = -n;
}

// ------------------------------------------------------------------ Disk ---

Disk::Disk(const Transform& objToWorld, double radius, double height, bool reverse)
    : xf_(objToWorld), inv_(objToWorld.inverse()), radius_(radius), height_(height), flip_(reverse != objToWorld.swapsHandedness()) {}

bool Disk::intersect(const Ray& rw, double tmin, double tmax, Hit& h) const {
    Ray r = inv_.ray(rw);
    if (r.d.z == 0) return false;
    double t = (height_ - r.o.z) / r.d.z;
    if (t <= tmin || t >= tmax) return false;
    Vec3 po = r.at(t);
    double d2 = po.x * po.x + po.y * po.y;
    if (d2 > radius_ * radius_) return false;
    double phi = std::atan2(po.y, po.x);
    if (phi < 0) phi += 2 * PI;
    h.t = t;
    h.p = xf_.point(Vec3(po.x, po.y, height_));
    h.ng = normalize(xf_.normal(Vec3(0, 0, 1)));
    if (flip_) h.ng = -h.ng;
    h.ns = h.ng;
    h.uv = Vec2(phi / (2 * PI), (radius_ - std::sqrt(d2)) / radius_);
    h.prim = this;
    return true;
}

AABB Disk::bounds() const {
    AABB b;
    for (int i = 0; i < 4; ++i)
        b.expand(xf_.point(Vec3((i & 1) ? radius_ : -radius_, (i & 2) ? radius_ : -radius_, height_)));
    // Give the box a little thickness so it is never degenerate.
    b.lo -= Vec3(1e-6);
    b.hi += Vec3(1e-6);
    return b;
}

double Disk::area() const {
    double sx = length(xf_.vector(Vec3(1, 0, 0))), sy = length(xf_.vector(Vec3(0, 1, 0)));
    return PI * radius_ * radius_ * sx * sy;
}

void Disk::sample(const Vec2& u, Vec3& p, Vec3& n) const {
    Vec2 d = concentricSampleDisk(u);
    p = xf_.point(Vec3(d.x * radius_, d.y * radius_, height_));
    n = normalize(xf_.normal(Vec3(0, 0, 1)));
    if (flip_) n = -n;
}

// ----------------------------------------------------------------- Plane ---

Plane::Plane(const Vec3& point, const Vec3& normal, const Vec3& uAxisHint) : point_(point), n_(normalize(normal)) {
    Vec3 t = uAxisHint - n_ * dot(uAxisHint, n_);
    if (lengthSquared(t) < 1e-12) {
        makeBasis(n_, t_, b_);
    } else {
        t_ = normalize(t);
        b_ = cross(n_, t_);
    }
}

bool Plane::intersect(const Ray& r, double tmin, double tmax, Hit& h) const {
    double denom = dot(r.d, n_);
    if (std::fabs(denom) < 1e-12) return false;
    double t = dot(point_ - r.o, n_) / denom;
    if (t <= tmin || t >= tmax) return false;
    h.t = t;
    h.p = r.at(t);
    h.ng = h.ns = n_;
    Vec3 d = h.p - point_;
    h.uv = Vec2(dot(d, t_), dot(d, b_));
    h.prim = this;
    return true;
}

// -------------------------------------------------------------- Triangle ---

Triangle::Triangle(std::shared_ptr<const Mesh> mesh, int tri) : mesh_(std::move(mesh)) {
    for (int k = 0; k < 3; ++k) v_[k] = mesh_->indices[3 * tri + k];
}

Vec3 Triangle::geometricNormal() const {
    Vec3 n = normalize(cross(vertex(1) - vertex(0), vertex(2) - vertex(0)));
    return mesh_->flipNormals ? -n : n;
}

// Moller-Trumbore ray/triangle intersection.
bool Triangle::intersect(const Ray& r, double tmin, double tmax, Hit& h) const {
    const Vec3& p0 = vertex(0);
    Vec3 e1 = vertex(1) - p0, e2 = vertex(2) - p0;
    Vec3 pv = cross(r.d, e2);
    double det = dot(e1, pv);
    if (std::fabs(det) < 1e-14) return false;
    double inv = 1.0 / det;
    Vec3 tv = r.o - p0;
    double b1 = dot(tv, pv) * inv;
    if (b1 < 0 || b1 > 1) return false;
    Vec3 qv = cross(tv, e1);
    double b2 = dot(r.d, qv) * inv;
    if (b2 < 0 || b1 + b2 > 1) return false;
    double t = dot(e2, qv) * inv;
    if (t <= tmin || t >= tmax) return false;
    double b0 = 1 - b1 - b2;

    h.t = t;
    h.p = p0 * b0 + vertex(1) * b1 + vertex(2) * b2;
    Vec3 ng = normalize(cross(e1, e2));
    if (!mesh_->UV.empty()) {
        const Vec2 &a = mesh_->UV[v_[0]], &b = mesh_->UV[v_[1]], &c = mesh_->UV[v_[2]];
        h.uv = Vec2(a.x * b0 + b.x * b1 + c.x * b2, a.y * b0 + b.y * b1 + c.y * b2);
    } else {
        // PBRT default parameterisation (0,0), (1,0), (1,1).
        h.uv = Vec2(b1 + b2, b2);
    }
    if (!mesh_->N.empty()) {
        Vec3 ns = normalize(mesh_->N[v_[0]] * b0 + mesh_->N[v_[1]] * b1 + mesh_->N[v_[2]] * b2);
        if (lengthSquared(ns) == 0) ns = ng;
        // As in PBRT, the geometric normal is aligned with the shading normal.
        if (dot(ng, ns) < 0) ng = -ng;
        h.ns = ns;
    } else {
        if (mesh_->flipNormals) ng = -ng;
        h.ns = ng;
    }
    h.ng = ng;
    h.prim = this;
    return true;
}

AABB Triangle::bounds() const {
    AABB b;
    for (int k = 0; k < 3; ++k) b.expand(vertex(k));
    b.lo -= Vec3(1e-9);
    b.hi += Vec3(1e-9);
    return b;
}

double Triangle::area() const { return 0.5 * length(cross(vertex(1) - vertex(0), vertex(2) - vertex(0))); }

void Triangle::sample(const Vec2& u, Vec3& p, Vec3& n) const {
    Vec2 b = uniformSampleTriangle(u);
    p = vertex(0) * b.x + vertex(1) * b.y + vertex(2) * (1 - b.x - b.y);
    n = geometricNormal();
    if (!mesh_->N.empty()) {
        Vec3 ns = mesh_->N[v_[0]] * b.x + mesh_->N[v_[1]] * b.y + mesh_->N[v_[2]] * (1 - b.x - b.y);
        if (dot(ns, n) < 0) n = -n;
    }
}
