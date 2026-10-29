// Random number generation, low-discrepancy sequences and sample warping functions.
#pragma once

#include <cstdint>
#include <vector>

#include "math.h"

// PCG32 random number generator (O'Neill 2014). Small, fast and deterministic
// so that renders are reproducible for a given seed.
class RNG {
public:
    explicit RNG(uint64_t seed = 0x853c49e6748fea9bULL, uint64_t seq = 0xda3e39cb94b95bdbULL) { setSeed(seed, seq); }
    void setSeed(uint64_t seed, uint64_t seq) {
        state = 0u;
        inc = (seq << 1u) | 1u;
        nextU32();
        state += seed;
        nextU32();
    }
    uint32_t nextU32() {
        uint64_t old = state;
        state = old * 6364136223846793005ULL + inc;
        uint32_t xorshifted = (uint32_t)(((old >> 18u) ^ old) >> 27u);
        uint32_t rot = (uint32_t)(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((~rot + 1u) & 31));
    }
    // Uniform double in [0, 1).
    double uniform() { return nextU32() * (1.0 / 4294967296.0); }
    Vec2 uniform2() { double a = uniform(); return Vec2(a, uniform()); }

private:
    uint64_t state = 0, inc = 0;
};

// Mix bits to derive well-distributed seeds from pixel coordinates.
inline uint64_t mixBits(uint64_t v) {
    v ^= (v >> 31);
    v *= 0x7fb5d329728ea185ULL;
    v ^= (v >> 27);
    v *= 0x81dadef4bc2dd44dULL;
    v ^= (v >> 33);
    return v;
}

// Van der Corput radical inverse of index i in the given prime base.
inline double radicalInverse(uint64_t i, int base) {
    double invBase = 1.0 / base, invBi = 1.0;
    uint64_t reversed = 0;
    while (i) {
        uint64_t next = i / base;
        uint64_t digit = i - next * base;
        reversed = reversed * base + digit;
        invBi *= invBase;
        i = next;
    }
    return std::min(reversed * invBi, 0.99999999999999989);
}

// 2D Halton point (bases 2 and 3).
inline Vec2 halton2D(uint64_t i) { return Vec2(radicalInverse(i, 2), radicalInverse(i, 3)); }

// Generates a set of n 2D samples in [0,1)^2 according to a named strategy.
enum class SampleStrategy { Uniform, Grid, Halton, Stratified };

inline const char* strategyName(SampleStrategy s) {
    switch (s) {
        case SampleStrategy::Uniform: return "uniform";
        case SampleStrategy::Grid: return "grid";
        case SampleStrategy::Halton: return "halton";
        case SampleStrategy::Stratified: return "stratified";
    }
    return "?";
}

// Fill `out` with n 2D samples.
//  Uniform    : independent uniform random points.
//  Grid       : n = k*k cells, one sample at the centre of each cell (deterministic).
//  Stratified : n = k*k cells, one jittered sample per cell.
//  Halton     : first n points of the 2D Halton sequence; if `randomize` is set a
//               random toroidal shift (Cranley-Patterson rotation) is applied so that
//               neighbouring shading points do not use identical patterns.
// For Grid/Stratified, n must be a perfect square (caller rounds it).
inline void generateSamples2D(SampleStrategy s, int n, RNG& rng, bool randomize, std::vector<Vec2>& out) {
    out.resize(n);
    switch (s) {
        case SampleStrategy::Uniform:
            for (int i = 0; i < n; ++i) out[i] = rng.uniform2();
            break;
        case SampleStrategy::Grid:
        case SampleStrategy::Stratified: {
            int k = (int)std::lround(std::sqrt((double)n));
            if (k < 1) k = 1;
            out.resize(k * k);
            int idx = 0;
            for (int j = 0; j < k; ++j)
                for (int i = 0; i < k; ++i) {
                    double jx = 0.5, jy = 0.5;
                    if (s == SampleStrategy::Stratified) { jx = rng.uniform(); jy = rng.uniform(); }
                    out[idx++] = Vec2((i + jx) / k, (j + jy) / k);
                }
            break;
        }
        case SampleStrategy::Halton: {
            Vec2 shift(0, 0);
            if (randomize) shift = rng.uniform2();
            for (int i = 0; i < n; ++i) {
                // Index starts at 1: index 0 of the Halton sequence is (0,0).
                Vec2 h = halton2D((uint64_t)i + 1);
                double x = h.x + shift.x, y = h.y + shift.y;
                if (x >= 1) x -= 1;
                if (y >= 1) y -= 1;
                out[i] = Vec2(x, y);
            }
            break;
        }
    }
}

// Concentric mapping of the unit square to the unit disk (Shirley & Chiu).
inline Vec2 concentricSampleDisk(const Vec2& u) {
    Vec2 o(2 * u.x - 1, 2 * u.y - 1);
    if (o.x == 0 && o.y == 0) return Vec2(0, 0);
    double r, theta;
    if (std::fabs(o.x) > std::fabs(o.y)) {
        r = o.x;
        theta = (PI / 4) * (o.y / o.x);
    } else {
        r = o.y;
        theta = (PI / 2) - (PI / 4) * (o.x / o.y);
    }
    return Vec2(r * std::cos(theta), r * std::sin(theta));
}

// Uniform direction on the hemisphere around +z. pdf = 1 / (2 pi).
inline Vec3 uniformSampleHemisphere(const Vec2& u) {
    double z = u.x;
    double r = std::sqrt(std::max(0.0, 1 - z * z));
    double phi = 2 * PI * u.y;
    return Vec3(r * std::cos(phi), r * std::sin(phi), z);
}

// Cosine-weighted direction around +z. pdf = cos(theta) / pi.
inline Vec3 cosineSampleHemisphere(const Vec2& u) {
    Vec2 d = concentricSampleDisk(u);
    double z = std::sqrt(std::max(0.0, 1 - d.x * d.x - d.y * d.y));
    return Vec3(d.x, d.y, z);
}

// Uniform direction on the unit sphere. pdf = 1 / (4 pi).
inline Vec3 uniformSampleSphere(const Vec2& u) {
    double z = 1 - 2 * u.x;
    double r = std::sqrt(std::max(0.0, 1 - z * z));
    double phi = 2 * PI * u.y;
    return Vec3(r * std::cos(phi), r * std::sin(phi), z);
}

// Uniform barycentrics on a triangle; returns (b0, b1).
inline Vec2 uniformSampleTriangle(const Vec2& u) {
    double su = std::sqrt(u.x);
    return Vec2(1 - su, u.y * su);
}
