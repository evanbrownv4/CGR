#include "integrators.h"

#include <atomic>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <thread>

// ----------------------------------------------------------------- common ---

Ray Integrator::spawn(const Vec3& p, const Vec3& ng, const Vec3& d) const {
    double scale = std::max(1.0, std::max(std::fabs(p.x), std::max(std::fabs(p.y), std::fabs(p.z))));
    double eps = scene_.settings.rayEpsilon * scale;
    Vec3 off = dot(d, ng) >= 0 ? ng * eps : ng * -eps;
    return Ray(p + off, d);
}

bool Integrator::occludedTo(const Vec3& p, const Vec3& ng, const Vec3& target, RayStats& stats) const {
    ++stats.shadow;
    Ray r = spawn(p, ng, target - p);
    r.d = target - r.o;  // unnormalised: t in (0, 1) spans the segment
    return scene_.occluded(r, 0.0, 1.0 - 1e-6);
}

Color phongBRDF(const Color& kd, const Color& ks, double e, const Vec3& n, const Vec3& wo, const Vec3& wi) {
    Color f = kd * INV_PI;
    if (!ks.isBlack()) {
        Vec3 r = n * (2 * dot(n, wi)) - wi;
        double c = dot(r, wo);
        if (c > 0) f += ks * ((e + 2) * (0.5 * INV_PI) * std::pow(c, e));
    }
    return f;
}

std::unique_ptr<Integrator> makeIntegrator(const Scene& scene) {
    if (scene.settings.integrator == "distributed") return std::make_unique<DistributedIntegrator>(scene);
    return std::make_unique<WhittedIntegrator>(scene);
}

// ---------------------------------------------------------------- Whitted ---

Color WhittedIntegrator::trace(const Ray& ray, int depth, RayStats& stats) const {
    Hit h;
    if (!scene_.intersect(ray, 0.0, INF, h)) return scene_.background;
    const Material& m = *h.prim->material;
    Vec3 wo = -ray.d;
    Color L(0);
    if (h.prim->light) L += h.prim->light->emitted(h.ng, wo);

    bool entering = dot(h.ng, ray.d) < 0;
    Vec3 ng = entering ? h.ng : -h.ng;  // geometric normal facing the viewer
    Vec3 n = h.ns;
    if (dot(n, ng) < 0) n = -n;         // shading normal on the same side

    if (!m.dielectric) {
        Color kd = m.Kd->eval(h.uv);
        // Ambient term.
        L += (m.hasKa ? m.Ka : kd) * scene_.settings.ambient;
        if (m.isNonSpecular()) {
            // Blinn-Phong: kd max(0, n.l) + ks max(0, n.h)^e, scaled by irradiance from each light.
            auto shade = [&](const Vec3& wi, const Color& E) {
                double ndl = dot(n, wi);
                if (ndl <= 0 || dot(ng, wi) <= 0) return;
                Color c = kd * ndl;
                if (m.hasGlossy()) {
                    Vec3 hv = normalize(wi + wo);
                    double ndh = std::max(0.0, dot(n, hv));
                    c += m.Ks * std::pow(ndh, m.exponent);
                }
                L += c * E;
            };
            for (const PointLight& pl : scene_.pointLights) {
                Vec3 d = pl.position - h.p;
                double d2 = lengthSquared(d);
                Vec3 wi = d / std::sqrt(d2);
                if (dot(ng, wi) <= 0) continue;
                if (occludedTo(h.p, ng, pl.position, stats)) continue;
                shade(wi, pl.intensity / d2);
            }
            for (const DistantLight& dl : scene_.distantLights) {
                if (dot(ng, dl.toLight) <= 0) continue;
                ++stats.shadow;
                if (scene_.occluded(spawn(h.p, ng, dl.toLight), 0.0, INF)) continue;
                shade(dl.toLight, dl.radiance);
            }
            // Area lights are approximated by a point light at their centroid with
            // intensity L * A * cos(theta_light) (the WSRT only models point lights).
            for (const auto& al : scene_.areaLights) {
                Vec3 c = al->centroid();
                Vec3 d = c - h.p;
                double d2 = lengthSquared(d);
                Vec3 wi = d / std::sqrt(d2);
                if (dot(ng, wi) <= 0) continue;
                // Light-facing normal: sample a representative point normal.
                Vec3 lp, ln;
                al->sample(Vec2(0.5, 0.5), lp, ln);
                double cosL = dot(ln, -wi);
                if (al->twoSided) cosL = std::fabs(cosL);
                if (al->prims.size() > 1 && !al->isParallelogram) cosL = 1;  // e.g. closed meshes
                if (dynamic_cast<const Sphere*>(al->prims[0])) cosL = 1;
                if (cosL <= 0) continue;
                // Shadow test: the closest hit towards the centroid must be the light itself.
                ++stats.shadow;
                Hit sh;
                Ray sr = spawn(h.p, ng, wi);
                if (scene_.intersect(sr, 0.0, std::sqrt(d2), sh) && sh.prim->light != al.get()) continue;
                shade(wi, al->L * (al->totalArea * cosL / d2));
            }
        }
    }

    if (depth >= scene_.settings.maxDepth) return L;

    if (m.dielectric) {
        // Fresnel-weighted reflection and refraction (Snell's law); total internal
        // reflection sends all energy into the reflected ray.
        double etaI = entering ? 1.0 : m.eta, etaT = entering ? m.eta : 1.0;
        double cosI = std::min(1.0, dot(-ray.d, n));
        double F = fresnelDielectric(cosI, etaI, etaT);
        Vec3 t;
        bool canRefract = refractDir(ray.d, n, etaI / etaT, t);
        if (!canRefract) F = 1.0;
        Vec3 r = reflect(ray.d, n);
        ++stats.secondary;
        L += m.Kr * F * trace(spawn(h.p, ng, r), depth + 1, stats);
        if (canRefract && F < 1) {
            ++stats.secondary;
            L += m.Kt * (1 - F) * trace(spawn(h.p, ng, t), depth + 1, stats);
        }
    } else if (m.hasMirror()) {
        Vec3 r = reflect(ray.d, n);
        if (dot(r, ng) <= 0) r = reflect(ray.d, ng);
        ++stats.secondary;
        L += m.Kr * trace(spawn(h.p, ng, r), depth + 1, stats);
    }
    return L;
}

// ------------------------------------------------------------ Distributed ---

Color DistributedIntegrator::directLighting(const Vec3& p, const Vec3& n, const Vec3& ng, const Vec3& wo,
                                            const Color& kd, const Color& ks, double exponent, RNG& rng,
                                            RayStats& stats) const {
    const RenderSettings& s = scene_.settings;
    Color L(0);
    for (const PointLight& pl : scene_.pointLights) {
        Vec3 d = pl.position - p;
        double d2 = lengthSquared(d);
        Vec3 wi = d / std::sqrt(d2);
        double cosS = dot(n, wi);
        if (cosS <= 0 || dot(ng, wi) <= 0) continue;
        if (occludedTo(p, ng, pl.position, stats)) continue;
        L += phongBRDF(kd, ks, exponent, n, wo, wi) * pl.intensity * (cosS / d2);
    }
    for (const DistantLight& dl : scene_.distantLights) {
        double cosS = dot(n, dl.toLight);
        if (cosS <= 0 || dot(ng, dl.toLight) <= 0) continue;
        ++stats.shadow;
        if (scene_.occluded(spawn(p, ng, dl.toLight), 0.0, INF)) continue;
        L += phongBRDF(kd, ks, exponent, n, wo, dl.toLight) * dl.radiance * cosS;
    }
    // Area lights: Monte Carlo estimate of the area form of the rendering equation
    //   L = 1/N sum f Le cos(theta_s) cos(theta_l) / d^2 / pdf_A,  pdf_A = 1 / area.
    thread_local std::vector<Vec2> samples;
    for (const auto& al : scene_.areaLights) {
        generateSamples2D(s.lightSampler, s.lightSamples, rng, s.randomizeHalton, samples);
        Color sum(0);
        for (const Vec2& u : samples) {
            Vec3 lp, ln;
            al->sample(u, lp, ln);
            Vec3 d = lp - p;
            double d2 = lengthSquared(d);
            if (d2 <= 0) continue;
            Vec3 wi = d / std::sqrt(d2);
            double cosS = dot(n, wi);
            if (cosS <= 0 || dot(ng, wi) <= 0) continue;
            double cosL = dot(ln, -wi);
            if (al->twoSided) cosL = std::fabs(cosL);
            if (cosL <= 0) continue;
            if (occludedTo(p, ng, lp, stats)) continue;
            sum += phongBRDF(kd, ks, exponent, n, wo, wi) * al->L * (cosS * cosL / d2);
        }
        L += sum * (al->totalArea / (double)samples.size());
    }
    return L;
}

Color DistributedIntegrator::trace(const Ray& ray, int depth, bool countEmission, RNG& rng, RayStats& stats) const {
    Hit h;
    if (!scene_.intersect(ray, 0.0, INF, h)) return scene_.background;
    const RenderSettings& s = scene_.settings;
    const Material& m = *h.prim->material;
    Vec3 wo = -ray.d;
    Color L(0);
    // Emission is only added when the ray could not have been accounted for by
    // explicit light sampling (camera rays and specular bounces).
    if (h.prim->light && countEmission) L += h.prim->light->emitted(h.ng, wo);

    bool entering = dot(h.ng, ray.d) < 0;
    Vec3 ng = entering ? h.ng : -h.ng;
    Vec3 n = h.ns;
    if (dot(n, ng) < 0) n = -n;

    if (!m.dielectric && m.isNonSpecular()) {
        Color kd = m.Kd->eval(h.uv);
        Color ks = m.Ks;
        // Energy conservation: kd + ks <= 1 per channel.
        double mx = (kd + ks).maxComponent();
        if (mx > 1) {
            kd /= mx;
            ks /= mx;
        }
        L += directLighting(h.p, n, ng, wo, kd, ks, m.exponent, rng, stats);

        if (depth < s.maxDepth && s.branching > 0) {
            Vec3 t, b;
            makeBasis(n, t, b);
            Color ind(0);
            for (int i = 0; i < s.branching; ++i) {
                Vec2 u = rng.uniform2();
                Vec3 local = s.cosineHemisphere ? cosineSampleHemisphere(u) : uniformSampleHemisphere(u);
                double pdf = s.cosineHemisphere ? local.z * INV_PI : 0.5 * INV_PI;
                if (pdf <= 0) continue;
                Vec3 wi = t * local.x + b * local.y + n * local.z;
                if (dot(wi, ng) <= 0) continue;
                ++stats.secondary;
                Color Li = trace(spawn(h.p, ng, wi), depth + 1, false, rng, stats);
                ind += phongBRDF(kd, ks, m.exponent, n, wo, wi) * Li * (local.z / pdf);
            }
            L += ind / (double)s.branching;
        }
    }

    if (depth >= s.maxDepth) return L;

    // Light sources seen through ideal specular chains are always counted for camera
    // paths. After a diffuse/glossy bounce they form caustic paths, which uniform
    // hemisphere sampling finds only by chance (fireflies); they are skipped unless
    // the scene enables "bool caustics".
    bool specEmission = countEmission || s.caustics;
    if (m.dielectric) {
        double etaI = entering ? 1.0 : m.eta, etaT = entering ? m.eta : 1.0;
        double cosI = std::min(1.0, dot(-ray.d, n));
        double F = fresnelDielectric(cosI, etaI, etaT);
        Vec3 tdir;
        bool canRefract = refractDir(ray.d, n, etaI / etaT, tdir);
        if (!canRefract) F = 1.0;
        ++stats.secondary;
        L += m.Kr * F * trace(spawn(h.p, ng, reflect(ray.d, n)), depth + 1, specEmission, rng, stats);
        if (canRefract && F < 1) {
            ++stats.secondary;
            L += m.Kt * (1 - F) * trace(spawn(h.p, ng, tdir), depth + 1, specEmission, rng, stats);
        }
    } else if (m.hasMirror()) {
        Vec3 r = reflect(ray.d, n);
        if (dot(r, ng) <= 0) r = reflect(ray.d, ng);
        ++stats.secondary;
        L += m.Kr * trace(spawn(h.p, ng, r), depth + 1, specEmission, rng, stats);
    }
    return L;
}

// ----------------------------------------------------------------- driver ---

void renderImage(const Scene& scene, const Integrator& integrator, Image& out, RayStats& stats, Logger& log,
                 bool showProgress) {
    const RenderSettings& s = scene.settings;
    const Camera& cam = scene.camera;
    int W = s.width, H = s.height;
    out = Image(W, H);
    int nThreads = s.threads > 0 ? s.threads : (int)std::max(1u, std::thread::hardware_concurrency());
    std::atomic<int> nextRow{0}, rowsDone{0};
    std::atomic<uint64_t> badSamples{0};
    std::mutex statsMutex, progressMutex;
    int lastPct = -1;

    auto worker = [&]() {
        RayStats local;
        std::vector<Vec2> pixelSamples;
        for (;;) {
            int y = nextRow.fetch_add(1);
            if (y >= H) break;
            for (int x = 0; x < W; ++x) {
                uint64_t pixelIndex = (uint64_t)y * W + x;
                RNG rng(mixBits(pixelIndex ^ (s.seed * 0x9E3779B97F4A7C15ULL)), mixBits(pixelIndex + 1));
                generateSamples2D(s.pixelSampler, s.spp, rng, true, pixelSamples);
                Color sum(0);
                for (const Vec2& u : pixelSamples) {
                    Vec2 lensU = rng.uniform2();
                    Ray r = cam.generateRay(x + u.x, y + u.y, lensU);
                    ++local.camera;
                    Color c = integrator.Li(r, rng, local);
                    if (c.hasNaN() || std::isinf(c.x) || std::isinf(c.y) || std::isinf(c.z)) {
                        ++badSamples;
                        continue;
                    }
                    sum += c;
                }
                out.at(x, y) = sum / (double)pixelSamples.size();
            }
            int done = ++rowsDone;
            if (showProgress) {
                int pct = (int)(100.0 * done / H);
                std::lock_guard<std::mutex> lock(progressMutex);
                if (pct > lastPct) {
                    lastPct = pct;
                    std::fprintf(stderr, "\rRendering: %3d%%", pct);
                    std::fflush(stderr);
                }
            }
        }
        std::lock_guard<std::mutex> lock(statsMutex);
        stats.add(local);
    };

    std::vector<std::thread> pool;
    for (int i = 0; i < nThreads; ++i) pool.emplace_back(worker);
    for (auto& t : pool) t.join();
    if (showProgress) std::fprintf(stderr, "\n");
    log.info("render threads: " + std::to_string(nThreads));
    if (badSamples > 0)
        log.warning(std::to_string(badSamples.load()) + " sample(s) produced NaN/Inf radiance and were discarded");
}
