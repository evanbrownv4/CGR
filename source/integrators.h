// The two ray-tracing integrators and the multithreaded render driver.
#pragma once

#include <memory>

#include "image.h"
#include "logger.h"
#include "scene.h"

class Integrator {
public:
    explicit Integrator(const Scene& scene) : scene_(scene) {}
    virtual ~Integrator() = default;
    // Radiance arriving at the camera along `ray`.
    virtual Color Li(const Ray& ray, RNG& rng, RayStats& stats) const = 0;
    virtual const char* name() const = 0;

protected:
    // Secondary ray leaving surface point p (geometric normal ng) in direction d,
    // offset along the normal to avoid self-intersection ("shadow acne").
    Ray spawn(const Vec3& p, const Vec3& ng, const Vec3& d) const;
    // True if the segment from p (offset) to target is blocked.
    bool occludedTo(const Vec3& p, const Vec3& ng, const Vec3& target, RayStats& stats) const;
    const Scene& scene_;
};

// Whitted-style ray tracer: Blinn-Phong local illumination from point lights
// with hard shadows, plus recursive ideal reflection and refraction.
class WhittedIntegrator : public Integrator {
public:
    using Integrator::Integrator;
    Color Li(const Ray& ray, RNG&, RayStats& stats) const override { return trace(ray, 0, stats); }
    const char* name() const override { return "whitted"; }

private:
    Color trace(const Ray& ray, int depth, RayStats& stats) const;
};

// Distributed ray tracer: energy-conserving Phong BRDF, soft shadows from
// sampled area lights, branching indirect illumination (BF rays per scattering
// event up to depth MD), ideal specular reflection/refraction.
class DistributedIntegrator : public Integrator {
public:
    using Integrator::Integrator;
    Color Li(const Ray& ray, RNG& rng, RayStats& stats) const override { return trace(ray, 0, true, rng, stats); }
    const char* name() const override { return "distributed"; }

private:
    Color trace(const Ray& ray, int depth, bool countEmission, RNG& rng, RayStats& stats) const;
    Color directLighting(const Vec3& p, const Vec3& n, const Vec3& ng, const Vec3& wo, const Color& kd,
                         const Color& ks, double exponent, RNG& rng, RayStats& stats) const;
};

// Normalised (energy-conserving) Phong BRDF:
//   f(wi, wo) = kd / pi + ks (e + 2) / (2 pi) max(0, r . wo)^e,  r = mirror of wi about n.
Color phongBRDF(const Color& kd, const Color& ks, double e, const Vec3& n, const Vec3& wo, const Vec3& wi);

std::unique_ptr<Integrator> makeIntegrator(const Scene& scene);

// Renders the full image with the configured number of threads.
void renderImage(const Scene& scene, const Integrator& integrator, Image& out, RayStats& stats, Logger& log,
                 bool showProgress);
