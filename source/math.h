// Basic linear algebra utilities: vectors, rays, 4x4 matrices and transforms.
// Implemented from scratch (no external maths libraries are allowed).
#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

constexpr double PI = 3.14159265358979323846;
constexpr double INV_PI = 1.0 / PI;
constexpr double INF = std::numeric_limits<double>::infinity();

inline double radians(double deg) { return deg * PI / 180.0; }
inline double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct Vec2 {
    double x = 0, y = 0;
    Vec2() = default;
    Vec2(double x_, double y_) : x(x_), y(y_) {}
    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(double s) const { return {x * s, y * s}; }
};

struct Vec3 {
    double x = 0, y = 0, z = 0;
    Vec3() = default;
    explicit Vec3(double v) : x(v), y(v), z(v) {}
    Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(double s) const { double inv = 1.0 / s; return {x * inv, y * inv, z * inv}; }
    // Component-wise product (used for colours).
    Vec3 operator*(const Vec3& o) const { return {x * o.x, y * o.y, z * o.z}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(double s) { x *= s; y *= s; z *= s; return *this; }
    Vec3& operator*=(const Vec3& o) { x *= o.x; y *= o.y; z *= o.z; return *this; }
    Vec3& operator/=(double s) { return *this *= (1.0 / s); }
    double operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
    double& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }

    double maxComponent() const { return std::max(x, std::max(y, z)); }
    double minComponent() const { return std::min(x, std::min(y, z)); }
    bool isBlack() const { return x == 0 && y == 0 && z == 0; }
    bool hasNaN() const { return std::isnan(x) || std::isnan(y) || std::isnan(z); }
};

using Color = Vec3;

inline Vec3 operator*(double s, const Vec3& v) { return v * s; }
inline double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline double absDot(const Vec3& a, const Vec3& b) { return std::fabs(dot(a, b)); }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double length(const Vec3& v) { return std::sqrt(dot(v, v)); }
inline double lengthSquared(const Vec3& v) { return dot(v, v); }
inline Vec3 normalize(const Vec3& v) {
    double l = length(v);
    return l > 0 ? v / l : v;
}
inline Vec3 vmin(const Vec3& a, const Vec3& b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }
inline Vec3 vmax(const Vec3& a, const Vec3& b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }
inline Vec3 lerp(double t, const Vec3& a, const Vec3& b) { return a * (1 - t) + b * t; }
inline double luminance(const Color& c) { return 0.2126 * c.x + 0.7152 * c.y + 0.0722 * c.z; }

// Mirror reflection of incident direction d about normal n: r = d - 2 (d.n) n
inline Vec3 reflect(const Vec3& d, const Vec3& n) { return d - n * (2.0 * dot(d, n)); }

// Build an orthonormal basis (t, b) around unit vector n (Duff et al. 2017).
inline void makeBasis(const Vec3& n, Vec3& t, Vec3& b) {
    double sign = std::copysign(1.0, n.z);
    double a = -1.0 / (sign + n.z);
    double bb = n.x * n.y * a;
    t = Vec3(1.0 + sign * n.x * n.x * a, sign * bb, -sign * n.x);
    b = Vec3(bb, sign + n.y * n.y * a, -n.y);
}

struct Ray {
    Vec3 o, d;
    Ray() = default;
    Ray(const Vec3& o_, const Vec3& d_) : o(o_), d(d_) {}
    Vec3 at(double t) const { return o + d * t; }
};

struct AABB {
    Vec3 lo{INF, INF, INF};
    Vec3 hi{-INF, -INF, -INF};
    void expand(const Vec3& p) { lo = vmin(lo, p); hi = vmax(hi, p); }
    void expand(const AABB& b) { lo = vmin(lo, b.lo); hi = vmax(hi, b.hi); }
    Vec3 centroid() const { return (lo + hi) * 0.5; }
    Vec3 extent() const { return hi - lo; }
    bool valid() const { return lo.x <= hi.x && lo.y <= hi.y && lo.z <= hi.z; }
    double surfaceArea() const {
        if (!valid()) return 0;
        Vec3 e = extent();
        return 2.0 * (e.x * e.y + e.y * e.z + e.z * e.x);
    }
    int longestAxis() const {
        Vec3 e = extent();
        return (e.x > e.y && e.x > e.z) ? 0 : (e.y > e.z ? 1 : 2);
    }
    // Slab test; invD = 1/ray.d precomputed.
    bool intersect(const Ray& r, const Vec3& invD, double tmin, double tmax) const {
        for (int a = 0; a < 3; ++a) {
            double t0 = (lo[a] - r.o[a]) * invD[a];
            double t1 = (hi[a] - r.o[a]) * invD[a];
            if (invD[a] < 0) std::swap(t0, t1);
            tmin = t0 > tmin ? t0 : tmin;
            tmax = t1 < tmax ? t1 : tmax;
            if (tmax < tmin) return false;
        }
        return true;
    }
};

// Row-major 4x4 matrix. Points are column vectors: p' = M p.
struct Mat4 {
    double m[4][4];
    Mat4() {
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) m[i][j] = (i == j) ? 1.0 : 0.0;
    }
    static Mat4 identity() { return Mat4(); }
    Mat4 operator*(const Mat4& o) const {
        Mat4 r;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                double s = 0;
                for (int k = 0; k < 4; ++k) s += m[i][k] * o.m[k][j];
                r.m[i][j] = s;
            }
        return r;
    }
    Mat4 transposed() const {
        Mat4 r;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) r.m[i][j] = m[j][i];
        return r;
    }
    // Gauss-Jordan elimination with partial pivoting. Returns false if singular.
    bool inverse(Mat4& out) const {
        double a[4][8];
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                a[i][j] = m[i][j];
                a[i][j + 4] = (i == j) ? 1.0 : 0.0;
            }
        for (int c = 0; c < 4; ++c) {
            int piv = c;
            for (int r = c + 1; r < 4; ++r)
                if (std::fabs(a[r][c]) > std::fabs(a[piv][c])) piv = r;
            if (std::fabs(a[piv][c]) < 1e-14) return false;
            if (piv != c)
                for (int j = 0; j < 8; ++j) std::swap(a[c][j], a[piv][j]);
            double inv = 1.0 / a[c][c];
            for (int j = 0; j < 8; ++j) a[c][j] *= inv;
            for (int r = 0; r < 4; ++r) {
                if (r == c) continue;
                double f = a[r][c];
                if (f == 0) continue;
                for (int j = 0; j < 8; ++j) a[r][j] -= f * a[c][j];
            }
        }
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) out.m[i][j] = a[i][j + 4];
        return true;
    }
    double det3() const {
        return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
               m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
               m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    }
};

// An affine transform with its cached inverse.
struct Transform {
    Mat4 m, inv;
    Transform() = default;
    Transform(const Mat4& m_, const Mat4& inv_) : m(m_), inv(inv_) {}
    // Returns false (and leaves identity inverse) if the matrix is singular.
    static bool fromMatrix(const Mat4& mat, Transform& out) {
        Mat4 i;
        if (!mat.inverse(i)) return false;
        out = Transform(mat, i);
        return true;
    }
    Transform operator*(const Transform& o) const { return Transform(m * o.m, o.inv * inv); }
    Transform inverse() const { return Transform(inv, m); }

    Vec3 point(const Vec3& p) const {
        double x = m.m[0][0] * p.x + m.m[0][1] * p.y + m.m[0][2] * p.z + m.m[0][3];
        double y = m.m[1][0] * p.x + m.m[1][1] * p.y + m.m[1][2] * p.z + m.m[1][3];
        double z = m.m[2][0] * p.x + m.m[2][1] * p.y + m.m[2][2] * p.z + m.m[2][3];
        double w = m.m[3][0] * p.x + m.m[3][1] * p.y + m.m[3][2] * p.z + m.m[3][3];
        return (w == 1.0 || w == 0.0) ? Vec3(x, y, z) : Vec3(x, y, z) / w;
    }
    Vec3 vector(const Vec3& v) const {
        return {m.m[0][0] * v.x + m.m[0][1] * v.y + m.m[0][2] * v.z,
                m.m[1][0] * v.x + m.m[1][1] * v.y + m.m[1][2] * v.z,
                m.m[2][0] * v.x + m.m[2][1] * v.y + m.m[2][2] * v.z};
    }
    // Normals transform with the inverse transpose.
    Vec3 normal(const Vec3& n) const {
        return {inv.m[0][0] * n.x + inv.m[1][0] * n.y + inv.m[2][0] * n.z,
                inv.m[0][1] * n.x + inv.m[1][1] * n.y + inv.m[2][1] * n.z,
                inv.m[0][2] * n.x + inv.m[1][2] * n.y + inv.m[2][2] * n.z};
    }
    Ray ray(const Ray& r) const { return Ray(point(r.o), vector(r.d)); }
    bool swapsHandedness() const { return m.det3() < 0; }

    static Transform translate(const Vec3& d) {
        Mat4 a, b;
        a.m[0][3] = d.x; a.m[1][3] = d.y; a.m[2][3] = d.z;
        b.m[0][3] = -d.x; b.m[1][3] = -d.y; b.m[2][3] = -d.z;
        return Transform(a, b);
    }
    static Transform scale(const Vec3& s) {
        Mat4 a, b;
        a.m[0][0] = s.x; a.m[1][1] = s.y; a.m[2][2] = s.z;
        b.m[0][0] = 1.0 / s.x; b.m[1][1] = 1.0 / s.y; b.m[2][2] = 1.0 / s.z;
        return Transform(a, b);
    }
    // Rotation by angle (degrees) about an arbitrary axis (Rodrigues), as in PBRT.
    static Transform rotate(double deg, const Vec3& axis) {
        Vec3 a = normalize(axis);
        double s = std::sin(radians(deg)), c = std::cos(radians(deg));
        Mat4 r;
        r.m[0][0] = a.x * a.x + (1 - a.x * a.x) * c;
        r.m[0][1] = a.x * a.y * (1 - c) - a.z * s;
        r.m[0][2] = a.x * a.z * (1 - c) + a.y * s;
        r.m[1][0] = a.x * a.y * (1 - c) + a.z * s;
        r.m[1][1] = a.y * a.y + (1 - a.y * a.y) * c;
        r.m[1][2] = a.y * a.z * (1 - c) - a.x * s;
        r.m[2][0] = a.x * a.z * (1 - c) - a.y * s;
        r.m[2][1] = a.y * a.z * (1 - c) + a.x * s;
        r.m[2][2] = a.z * a.z + (1 - a.z * a.z) * c;
        return Transform(r, r.transposed());
    }
};
