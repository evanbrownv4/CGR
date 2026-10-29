// Material description shared by both integrators.
//
// A single parameterised material covers every supported PBRT material type:
//   Kd (texture)  diffuse colour            Ks  specular colour
//   exponent      Phong/Blinn shininess     Ka  ambient colour (WSRT only)
//   Kr            ideal mirror reflectance  Kt  transmission colour
//   eta           index of refraction (a dielectric when `dielectric` is true)
#pragma once

#include <memory>
#include <string>

#include "math.h"
#include "texture.h"

struct Material {
    std::string name;       // for log messages
    std::string sourceType; // PBRT type it was created from
    std::shared_ptr<Texture> Kd = Texture::constant(Color(0.5));
    Color Ks{0, 0, 0};
    double exponent = 32;
    bool hasKa = false;
    Color Ka{0, 0, 0};
    Color Kr{0, 0, 0};
    Color Kt{0, 0, 0};
    bool dielectric = false;
    double eta = 1.5;

    bool hasDiffuse() const { return !(Kd->isConstant() && Kd->eval(Vec2()).isBlack()); }
    bool hasGlossy() const { return !Ks.isBlack(); }
    bool hasMirror() const { return !Kr.isBlack(); }
    // True if the BRDF has a non-delta component that can be lit by light sampling.
    bool isNonSpecular() const { return hasDiffuse() || hasGlossy(); }
};

// Unpolarised Fresnel reflectance for a dielectric interface.
// cosI is the cosine of the incident angle (>= 0); etaI/etaT the indices on either side.
inline double fresnelDielectric(double cosI, double etaI, double etaT) {
    cosI = clampd(cosI, 0.0, 1.0);
    double sinI = std::sqrt(std::max(0.0, 1 - cosI * cosI));
    double sinT = etaI / etaT * sinI;
    if (sinT >= 1) return 1.0;  // total internal reflection
    double cosT = std::sqrt(std::max(0.0, 1 - sinT * sinT));
    double rParl = (etaT * cosI - etaI * cosT) / (etaT * cosI + etaI * cosT);
    double rPerp = (etaI * cosI - etaT * cosT) / (etaI * cosI + etaT * cosT);
    return 0.5 * (rParl * rParl + rPerp * rPerp);
}

// Snell refraction of incident direction d (unit, pointing towards the surface)
// through a surface with normal n facing the incident side. eta = etaI / etaT.
// Returns false on total internal reflection.
inline bool refractDir(const Vec3& d, const Vec3& n, double eta, Vec3& t) {
    double cosI = -dot(d, n);
    double sin2T = eta * eta * std::max(0.0, 1 - cosI * cosI);
    if (sin2T >= 1) return false;
    double cosT = std::sqrt(1 - sin2T);
    t = normalize(d * eta + n * (eta * cosI - cosT));
    return true;
}
