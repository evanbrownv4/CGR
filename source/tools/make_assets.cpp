// Generates the texture (PPM) and mesh (OBJ) assets used by the scenes in
// ../../scenes/assets. Not part of the renderer itself.
//   g++ -std=c++17 -O2 make_assets.cpp -o make_assets && ./make_assets ../../scenes/assets
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

static const double PI = 3.14159265358979323846;

struct V3 { double x, y, z; };
static V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static V3 operator*(V3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
static V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
static double len(V3 a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
static V3 norm(V3 a) { double l = len(a); return l > 0 ? a * (1 / l) : a; }

// ------------------------------------------------------------- textures ---

static double hash2(int x, int y) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((h ^ (h >> 16)) & 0xffffff) / double(0xffffff);
}
static double smooth(double t) { return t * t * (3 - 2 * t); }
static double valueNoise(double x, double y, int period) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    double fx = smooth(x - xi), fy = smooth(y - yi);
    auto h = [&](int a, int b) { return hash2(((a % period) + period) % period, ((b % period) + period) % period); };
    double a = h(xi, yi), b = h(xi + 1, yi), c = h(xi, yi + 1), d = h(xi + 1, yi + 1);
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
}
static double fbm(double x, double y, int period) {
    double s = 0, amp = 0.5;
    for (int o = 0; o < 5; ++o) {
        s += amp * valueNoise(x, y, period);
        x *= 2; y *= 2; period *= 2; amp *= 0.5;
    }
    return s;
}

static void writePPM(const std::string& path, int w, int h, const std::function<V3(double, double)>& f) {
    std::ofstream o(path, std::ios::binary);
    o << "P6\n" << w << " " << h << "\n255\n";
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            V3 c = f((x + 0.5) / w, (y + 0.5) / h);
            unsigned char px[3] = {(unsigned char)std::lround(std::fmin(1, std::fmax(0, c.x)) * 255),
                                   (unsigned char)std::lround(std::fmin(1, std::fmax(0, c.y)) * 255),
                                   (unsigned char)std::lround(std::fmin(1, std::fmax(0, c.z)) * 255)};
            o.write((char*)px, 3);
        }
    std::printf("wrote %s\n", path.c_str());
}

// ----------------------------------------------------------------- meshes ---

struct MeshOut {
    std::vector<V3> v, n;
    std::vector<std::pair<double, double>> t;
    std::vector<int> f;  // triangles, shared index for v/vt/vn
    bool hasN = true, hasT = true;
    void write(const std::string& path, const std::string& comment) const {
        std::ofstream o(path);
        o << "# " << comment << "\n";
        char buf[160];
        for (auto& p : v) { std::snprintf(buf, sizeof buf, "v %.6f %.6f %.6f\n", p.x, p.y, p.z); o << buf; }
        if (hasT) for (auto& p : t) { std::snprintf(buf, sizeof buf, "vt %.6f %.6f\n", p.first, p.second); o << buf; }
        if (hasN) for (auto& p : n) { std::snprintf(buf, sizeof buf, "vn %.6f %.6f %.6f\n", p.x, p.y, p.z); o << buf; }
        for (size_t i = 0; i < f.size(); i += 3) {
            o << "f";
            for (int k = 0; k < 3; ++k) {
                int id = f[i + k] + 1;
                if (hasT && hasN) o << " " << id << "/" << id << "/" << id;
                else if (hasN) o << " " << id << "//" << id;
                else if (hasT) o << " " << id << "/" << id;
                else o << " " << id;
            }
            o << "\n";
        }
        std::printf("wrote %s (%zu vertices, %zu triangles)\n", path.c_str(), v.size(), f.size() / 3);
    }
};

// Parametric surface sampled on a (nu+1) x (nv+1) grid with numerical normals.
static MeshOut parametric(int nu, int nv, const std::function<V3(double, double)>& P, bool flip = false) {
    MeshOut m;
    for (int j = 0; j <= nv; ++j)
        for (int i = 0; i <= nu; ++i) {
            double u = (double)i / nu, v = (double)j / nv;
            m.v.push_back(P(u, v));
            const double e = 1e-4;
            V3 du = P(std::fmin(u + e, 1), v) - P(std::fmax(u - e, 0), v);
            V3 dv = P(u, std::fmin(v + e, 1)) - P(u, std::fmax(v - e, 0));
            V3 n = norm(cross(du, dv));
            if (len(n) == 0) n = {0, 1, 0};
            m.n.push_back(flip ? n * -1 : n);
            m.t.push_back({u, v});
        }
    for (int j = 0; j < nv; ++j)
        for (int i = 0; i < nu; ++i) {
            int a = j * (nu + 1) + i, b = a + 1, c = a + nu + 1, d = c + 1;
            if (flip) m.f.insert(m.f.end(), {a, c, b, b, c, d});
            else m.f.insert(m.f.end(), {a, b, c, b, d, c});
        }
    return m;
}

// Fixes normals at seams/poles by averaging normals of coincident vertices.
static void weldNormals(MeshOut& m) {
    for (size_t i = 0; i < m.v.size(); ++i)
        for (size_t j = i + 1; j < m.v.size() && j < i + 2000; ++j)
            if (len(m.v[i] - m.v[j]) < 1e-9) {
                V3 n = norm(m.n[i] + m.n[j]);
                m.n[i] = m.n[j] = n;
            }
}

static void append(MeshOut& a, const MeshOut& b) {
    int off = (int)a.v.size();
    a.v.insert(a.v.end(), b.v.begin(), b.v.end());
    a.n.insert(a.n.end(), b.n.begin(), b.n.end());
    a.t.insert(a.t.end(), b.t.begin(), b.t.end());
    for (int i : b.f) a.f.push_back(i + off);
}

static MeshOut box(V3 lo, V3 hi) {
    MeshOut m;
    V3 c[8];
    for (int i = 0; i < 8; ++i) c[i] = {(i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z};
    int faces[6][4] = {{0, 2, 3, 1}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}};
    V3 normals[6] = {{0, 0, -1}, {0, 0, 1}, {0, -1, 0}, {0, 1, 0}, {-1, 0, 0}, {1, 0, 0}};
    for (int f = 0; f < 6; ++f) {
        int base = (int)m.v.size();
        double uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        for (int k = 0; k < 4; ++k) {
            m.v.push_back(c[faces[f][k]]);
            m.n.push_back(normals[f]);
            m.t.push_back({uv[k][0], uv[k][1]});
        }
        // Ensure outward winding.
        V3 gn = cross(m.v[base + 1] - m.v[base], m.v[base + 2] - m.v[base]);
        double d = gn.x * normals[f].x + gn.y * normals[f].y + gn.z * normals[f].z;
        if (d > 0) m.f.insert(m.f.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        else m.f.insert(m.f.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
    }
    return m;
}

// Surface of revolution around +y of the profile (r(t), y(t)).
static MeshOut lathe(int nu, int nv, const std::function<std::pair<double, double>(double)>& profile, bool flip = false) {
    return parametric(nu, nv, [&](double u, double v) {
        auto ry = profile(v);
        double phi = 2 * PI * u;
        return V3{ry.first * std::cos(phi), ry.second, -ry.first * std::sin(phi)};
    }, flip);
}

int main(int argc, char** argv) {
    std::string dir = argc > 1 ? argv[1] : ".";

    // UV test grid: 8x8 coloured cells with thin dark lines.
    writePPM(dir + "/uvgrid.ppm", 256, 256, [](double u, double v) {
        int cu = (int)(u * 8), cv = (int)(v * 8);
        double fu = u * 8 - cu, fv = v * 8 - cv;
        if (fu < 0.04 || fv < 0.04) return V3{0.05, 0.05, 0.05};
        V3 c{0.25 + 0.7 * cu / 7.0, 0.25 + 0.7 * (7 - cv) / 7.0, ((cu + cv) & 1) ? 0.85 : 0.35};
        if (cu == 0 && cv == 7) c = {1, 1, 1};  // marks the uv origin (bottom-left)
        return c;
    });
    // Brick wall.
    writePPM(dir + "/brick.ppm", 256, 256, [](double u, double v) {
        double rows = 8, cols = 4;
        double y = v * rows;
        int row = (int)y;
        double x = u * cols + (row % 2 ? 0.5 : 0.0);
        double fx = x - std::floor(x), fy = y - row;
        double n = fbm(u * 16, v * 16, 16);
        if (fx < 0.04 || fy < 0.08) return V3{0.75, 0.73, 0.68} * (0.8 + 0.3 * n);
        double var = hash2((int)std::floor(x), row);
        return V3{0.55 + 0.15 * var, 0.20 + 0.08 * var, 0.12} * (0.75 + 0.45 * n);
    });
    // Wood grain.
    writePPM(dir + "/wood.ppm", 256, 256, [](double u, double v) {
        double n = fbm(u * 4, v * 16, 4);
        double rings = std::sin((u * 18 + n * 4) * PI);
        double t = 0.5 + 0.5 * rings;
        V3 a{0.55, 0.33, 0.16}, b{0.36, 0.20, 0.09};
        return a * t + b * (1 - t);
    });
    // Marble.
    writePPM(dir + "/marble.ppm", 256, 256, [](double u, double v) {
        double n = fbm(u * 8, v * 8, 8);
        double t = 0.5 + 0.5 * std::sin((u * 6 + v * 3 + 5 * n) * PI);
        t = std::pow(t, 0.4);
        return V3{0.92, 0.91, 0.88} * t + V3{0.35, 0.37, 0.42} * (1 - t);
    });
    // Planet-like texture for spheres.
    writePPM(dir + "/planet.ppm", 512, 256, [](double u, double v) {
        double n = fbm(u * 8, v * 4, 8);
        if (n > 0.52) return V3{0.25 + 0.4 * (n - 0.52), 0.45, 0.15};
        double lat = std::fabs(v - 0.5) * 2;
        if (lat > 0.85) return V3{0.95, 0.95, 0.97};
        return V3{0.05, 0.18 + 0.3 * n, 0.45 + 0.3 * n};
    });

    // Torus (major radius 1, minor 0.35) with normals and uvs.
    {
        MeshOut m = parametric(96, 48, [](double u, double v) {
            double phi = 2 * PI * u, th = 2 * PI * v, R = 1, r = 0.35;
            return V3{(R + r * std::cos(th)) * std::cos(phi), r * std::sin(th), (R + r * std::cos(th)) * std::sin(phi)};
        }, true);
        weldNormals(m);
        m.write(dir + "/torus.obj", "torus R=1 r=0.35, per-vertex normals and uvs");
    }
    // Trefoil knot tube.
    {
        auto curve = [](double t) {
            double a = 2 * PI * t;
            return V3{std::sin(a) + 2 * std::sin(2 * a), std::cos(a) - 2 * std::cos(2 * a), -std::sin(3 * a)} * 0.33;
        };
        MeshOut m = parametric(400, 24, [&](double u, double v) {
            V3 p = curve(u);
            V3 tng = norm(curve(u + 1e-4) - curve(u - 1e-4));
            V3 up{0, 0, 1};
            V3 b = norm(cross(tng, up));
            V3 nn = cross(b, tng);
            double th = 2 * PI * v, r = 0.12;
            return p + b * (r * std::cos(th)) + nn * (r * std::sin(th));
        });
        weldNormals(m);
        m.write(dir + "/knot.obj", "trefoil knot tube, per-vertex normals and uvs");
    }
    // Vase (surface of revolution).
    {
        MeshOut m = lathe(64, 48, [](double t) {
            double y = t * 2.0;
            double r = 0.35 + 0.3 * std::sin(PI * t * 1.2 + 0.3) + 0.08 * std::cos(6 * PI * t);
            return std::make_pair(r, y);
        });
        weldNormals(m);
        m.write(dir + "/vase.obj", "open vase (lathe), height 2, per-vertex normals and uvs");
    }
    // Mug: outer wall, inner wall, bottom and a torus-segment handle.
    {
        MeshOut m = lathe(64, 8, [](double t) { return std::make_pair(0.5, t * 1.1); });
        MeshOut inner = lathe(64, 8, [](double t) { return std::make_pair(0.45, 0.08 + t * 1.02); }, true);
        MeshOut rim = lathe(64, 1, [](double t) { return std::make_pair(0.5 - 0.05 * t, 1.1); });
        MeshOut bottom = lathe(64, 1, [](double t) { return std::make_pair(0.45 * t, 0.08); }, true);
        MeshOut base = lathe(64, 1, [](double t) { return std::make_pair(0.5 * (1 - t), 0.0); });
        MeshOut handle = parametric(32, 16, [](double u, double v) {
            double phi = -PI * 0.5 + PI * u, th = 2 * PI * v, R = 0.32, r = 0.06;
            return V3{0.5 + (R + r * std::cos(th)) * std::cos(phi), 0.55 + (R + r * std::cos(th)) * std::sin(phi), r * std::sin(th)};
        }, true);
        append(m, inner); append(m, rim); append(m, bottom); append(m, base); append(m, handle);
        weldNormals(m);
        m.write(dir + "/mug.obj", "coffee mug (radius 0.5, height 1.1) with handle along +x");
    }
    // Faceted low-poly sphere without normals (flat shading test).
    {
        MeshOut m = parametric(12, 8, [](double u, double v) {
            double phi = 2 * PI * u, th = PI * v;
            return V3{std::sin(th) * std::cos(phi), -std::cos(th), -std::sin(th) * std::sin(phi)};
        });
        m.hasN = false;
        m.write(dir + "/lowpoly_sphere.obj", "low-poly UV sphere without normals (flat shaded)");
    }
    // Picket fence of thin boxes (aliasing tests).
    {
        MeshOut m;
        for (int i = 0; i < 40; ++i) {
            double x = -10 + i * 0.5;
            append(m, box({x, 0, -0.02}, {x + 0.06, 1.2, 0.02}));
        }
        append(m, box({-10, 0.85, -0.04}, {10, 0.9, -0.02}));
        append(m, box({-10, 0.25, -0.04}, {10, 0.3, -0.02}));
        m.write(dir + "/fence.obj", "picket fence, 40 thin posts and 2 rails, along x from -10 to 10");
    }
    // Unit cube [-0.5, 0.5]^3 with per-face uvs (books, boxes).
    {
        MeshOut m = box({-0.5, -0.5, -0.5}, {0.5, 0.5, 0.5});
        m.write(dir + "/cube.obj", "unit cube centred at the origin, per-face normals and uvs");
    }
    return 0;
}
