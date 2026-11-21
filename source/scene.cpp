#include "scene.h"

#include <algorithm>

thread_local uint64_t tlPrimTests = 0, tlNodeVisits = 0;

// ------------------------------------------------------------------- BVH ---
// Binned surface-area-heuristic build over primitive bounding boxes.

void BVH::build(const std::vector<const Primitive*>& prims, double traversalCost, int maxLeafPrims) {
    travCost_ = traversalCost;
    maxLeafPrims_ = maxLeafPrims;
    items_.clear();
    for (const Primitive* p : prims) {
        AABB b = p->bounds();
        items_.push_back({p, b, b.centroid()});
    }
    nodes_.clear();
    depth_ = 0;
    prims_.clear();
    if (items_.empty()) return;
    nodes_.reserve(items_.size() * 2);
    buildRec(0, (int)items_.size(), 1);
    for (const Item& it : items_) prims_.push_back(it.prim);
    items_.clear();
    items_.shrink_to_fit();
}

int BVH::buildRec(int first, int last, int depth) {
    depth_ = std::max(depth_, depth);
    int idx = (int)nodes_.size();
    nodes_.push_back(Node());
    AABB box, cbox;
    for (int i = first; i < last; ++i) {
        box.expand(items_[i].box);
        cbox.expand(items_[i].centroid);
    }
    nodes_[idx].box = box;
    int n = last - first;
    if (n <= 2) {
        nodes_[idx].first = first;
        nodes_[idx].count = n;
        return idx;
    }

    int axis = cbox.longestAxis();
    double lo = cbox.lo[axis], hi = cbox.hi[axis];
    int mid = first + n / 2;
    if (hi - lo < 1e-12 || depth > 90) {
        // All centroids coincide (or the tree is very deep): median split.
        std::nth_element(items_.begin() + first, items_.begin() + mid, items_.begin() + last,
                         [&](const Item& a, const Item& b) { return a.centroid[axis] < b.centroid[axis]; });
    } else {
        const int NB = 16;
        struct Bin { AABB box; int count = 0; } bins[NB];
        auto binOf = [&](const Item& it) {
            int b = (int)(NB * (it.centroid[axis] - lo) / (hi - lo));
            return std::min(std::max(b, 0), NB - 1);
        };
        for (int i = first; i < last; ++i) {
            int b = binOf(items_[i]);
            bins[b].count++;
            bins[b].box.expand(items_[i].box);
        }
        const double Ct = travCost_;            // C_t: one traversal step (default C_i / 2)
        const double Ci = kSahIntersectionCost;  // C_i: one ray-primitive test (= 1, the unit)
        const double SAp = std::max(box.surfaceArea(), 1e-300);
        double bestCost = INF;
        int bestSplit = -1;
        for (int s = 0; s < NB - 1; ++s) {
            AABB b0, b1;
            int c0 = 0, c1 = 0;
            for (int i = 0; i <= s; ++i) { b0.expand(bins[i].box); c0 += bins[i].count; }
            for (int i = s + 1; i < NB; ++i) { b1.expand(bins[i].box); c1 += bins[i].count; }
            if (c0 == 0 || c1 == 0) continue;
            // SAH: C_t + sum over children of P(hit child | hit parent) * N_child * C_i,
            // with P = SA(child) / SA(parent).
            double cost = Ct + Ci * (c0 * b0.surfaceArea() + c1 * b1.surfaceArea()) / SAp;
            if (cost < bestCost) { bestCost = cost; bestSplit = s; }
        }
        if (n <= maxLeafPrims_ && bestCost >= Ci * n) {  // a leaf costs C_i * N
            nodes_[idx].first = first;
            nodes_[idx].count = n;
            return idx;
        }
        if (bestSplit >= 0) {
            auto it = std::partition(items_.begin() + first, items_.begin() + last,
                                     [&](const Item& p) { return binOf(p) <= bestSplit; });
            mid = (int)(it - items_.begin());
        }
        if (mid == first || mid == last) {
            mid = first + n / 2;
            std::nth_element(items_.begin() + first, items_.begin() + mid, items_.begin() + last,
                             [&](const Item& a, const Item& b) { return a.centroid[axis] < b.centroid[axis]; });
        }
    }
    int l = buildRec(first, mid, depth + 1);
    int r = buildRec(mid, last, depth + 1);
    nodes_[idx].left = l;
    nodes_[idx].right = r;
    nodes_[idx].axis = axis;
    return idx;
}

bool BVH::intersect(const Ray& r, double tmin, double tmax, Hit& h) const {
    if (nodes_.empty()) return false;
    Vec3 invD(1.0 / r.d.x, 1.0 / r.d.y, 1.0 / r.d.z);
    int stack[128];
    int sp = 0;
    stack[sp++] = 0;
    bool hit = false;
    while (sp) {
        const Node& node = nodes_[stack[--sp]];
        ++tlNodeVisits;
        if (!node.box.intersect(r, invD, tmin, tmax)) continue;
        if (node.count > 0) {
            tlPrimTests += node.count;
            for (int i = node.first; i < node.first + node.count; ++i)
                if (prims_[i]->intersect(r, tmin, tmax, h)) {
                    hit = true;
                    tmax = h.t;  // shrink the interval: farther boxes are culled
                }
        } else if (r.d[node.axis] < 0) {
            // Visit the child on the ray's side of the split first (pushed last),
            // so the closest hit is usually found early and tmax culls the other.
            stack[sp++] = node.left;
            stack[sp++] = node.right;
        } else {
            stack[sp++] = node.right;
            stack[sp++] = node.left;
        }
    }
    return hit;
}

bool BVH::occluded(const Ray& r, double tmin, double tmax) const {
    if (nodes_.empty()) return false;
    Vec3 invD(1.0 / r.d.x, 1.0 / r.d.y, 1.0 / r.d.z);
    int stack[128];
    int sp = 0;
    stack[sp++] = 0;
    Hit h;
    while (sp) {
        const Node& node = nodes_[stack[--sp]];
        ++tlNodeVisits;
        if (!node.box.intersect(r, invD, tmin, tmax)) continue;
        if (node.count > 0) {
            for (int i = node.first; i < node.first + node.count; ++i) {
                ++tlPrimTests;
                if (prims_[i]->intersect(r, tmin, tmax, h)) return true;
            }
        } else {
            stack[sp++] = node.left;
            stack[sp++] = node.right;
        }
    }
    return false;
}

size_t BVH::leafCount() const {
    size_t n = 0;
    for (const Node& node : nodes_)
        if (node.count > 0) ++n;
    return n;
}

// ----------------------------------------------------------------- Scene ---

void Scene::prepare() {
    bounded_.clear();
    unbounded_.clear();
    bounds_ = AABB();
    for (const auto& p : primitives) {
        AABB b = p->bounds();
        if (b.valid()) {
            bounded_.push_back(p.get());
            bounds_.expand(b);
        } else {
            unbounded_.push_back(p.get());
        }
    }
    if (settings.useBVH) bvh_.build(bounded_, settings.bvhTraversalCost, settings.bvhMaxLeafPrims);
    for (auto& l : areaLights) l->finalize();
}

bool Scene::intersect(const Ray& r, double tmin, double tmax, Hit& h) const {
    bool hit = false;
    if (settings.useBVH) {
        if (bvh_.intersect(r, tmin, tmax, h)) {
            hit = true;
            tmax = h.t;
        }
    } else {
        tlPrimTests += bounded_.size();
        for (const Primitive* p : bounded_)
            if (p->intersect(r, tmin, tmax, h)) {
                hit = true;
                tmax = h.t;
            }
    }
    tlPrimTests += unbounded_.size();
    for (const Primitive* p : unbounded_)
        if (p->intersect(r, tmin, tmax, h)) {
            hit = true;
            tmax = h.t;
        }
    return hit;
}

bool Scene::occluded(const Ray& r, double tmin, double tmax) const {
    Hit h;
    for (const Primitive* p : unbounded_) {
        ++tlPrimTests;
        if (p->intersect(r, tmin, tmax, h)) return true;
    }
    if (settings.useBVH) return bvh_.occluded(r, tmin, tmax);
    for (const Primitive* p : bounded_) {
        ++tlPrimTests;
        if (p->intersect(r, tmin, tmax, h)) return true;
    }
    return false;
}
