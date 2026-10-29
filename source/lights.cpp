#include "lights.h"

#include <algorithm>

void AreaLight::finalize() {
    cdf.clear();
    totalArea = 0;
    for (const Primitive* p : prims) {
        totalArea += p->area();
        cdf.push_back(totalArea);
    }

    // Detect a parallelogram made of exactly two triangles sharing an edge.
    isParallelogram = false;
    if (prims.size() == 2) {
        auto* t0 = dynamic_cast<const Triangle*>(prims[0]);
        auto* t1 = dynamic_cast<const Triangle*>(prims[1]);
        if (t0 && t1) {
            std::vector<Vec3> v;
            auto addUnique = [&](const Vec3& p) {
                for (const Vec3& q : v)
                    if (lengthSquared(p - q) < 1e-18) return;
                v.push_back(p);
            };
            for (int k = 0; k < 3; ++k) addUnique(t0->vertex(k));
            for (int k = 0; k < 3; ++k) addUnique(t1->vertex(k));
            if (v.size() == 4) {
                double scale = 0;
                for (const Vec3& p : v) scale = std::max(scale, length(p));
                double tol = 1e-9 * (1 + scale);
                for (int o = 0; o < 4 && !isParallelogram; ++o)
                    for (int a = 0; a < 4 && !isParallelogram; ++a)
                        for (int b = a + 1; b < 4 && !isParallelogram; ++b) {
                            if (a == o || b == o) continue;
                            int d = 6 - o - a - b;
                            if (length(v[a] + v[b] - v[o] - v[d]) < tol) {
                                isParallelogram = true;
                                corner = v[o];
                                edgeU = v[a] - v[o];
                                edgeV = v[b] - v[o];
                            }
                        }
                if (isParallelogram) {
                    Vec3 p, n;
                    prims[0]->sample(Vec2(0.3, 0.3), p, n);
                    quadNormal = n;
                }
            }
        }
    }
}

void AreaLight::sample(const Vec2& u, Vec3& p, Vec3& n) const {
    if (isParallelogram) {
        p = corner + edgeU * u.x + edgeV * u.y;
        n = quadNormal;
        return;
    }
    if (prims.size() == 1) {
        prims[0]->sample(u, p, n);
        return;
    }
    // Choose a primitive proportionally to its area using u.x, then remap u.x
    // to [0,1) so the same sample can be reused on the chosen primitive.
    double target = u.x * totalArea;
    size_t i = std::lower_bound(cdf.begin(), cdf.end(), target) - cdf.begin();
    if (i >= prims.size()) i = prims.size() - 1;
    double lo = i == 0 ? 0 : cdf[i - 1];
    double width = cdf[i] - lo;
    double ux = width > 0 ? clampd((target - lo) / width, 0.0, 0.99999999) : 0.5;
    prims[i]->sample(Vec2(ux, u.y), p, n);
}

Vec3 AreaLight::centroid() const {
    Vec3 c(0);
    double wsum = 0;
    for (const Primitive* p : prims) {
        AABB b = p->bounds();
        double w = p->area();
        c += b.centroid() * w;
        wsum += w;
    }
    return wsum > 0 ? c / wsum : c;
}
