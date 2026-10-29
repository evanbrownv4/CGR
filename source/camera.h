// Perspective (thin lens) and orthographic cameras following PBRT conventions:
// camera space looks down +z, the "fov" parameter is the angle spanned by the
// shorter image axis, and the screen window defaults to [-aspect, aspect] x [-1, 1]
// (or [-1, 1] x [-1/aspect, 1/aspect] for portrait images).
#pragma once

#include "math.h"
#include "sampling.h"

struct Camera {
    enum class Type { Perspective, Orthographic } type = Type::Perspective;
    Transform cameraToWorld;
    double fov = 90;               // degrees (perspective)
    double screenWindow[4] = {0, 0, 0, 0};  // xmin xmax ymin ymax; all zero -> derive from aspect
    double lensRadius = 0;         // thin-lens aperture radius (0 = pinhole)
    double focalDistance = 1e6;    // distance to the plane of focus
    int width = 640, height = 480;

    void setup() {
        if (screenWindow[0] == 0 && screenWindow[1] == 0 && screenWindow[2] == 0 && screenWindow[3] == 0) {
            double aspect = (double)width / height;
            if (aspect > 1) {
                screenWindow[0] = -aspect; screenWindow[1] = aspect;
                screenWindow[2] = -1; screenWindow[3] = 1;
            } else {
                screenWindow[0] = -1; screenWindow[1] = 1;
                screenWindow[2] = -1 / aspect; screenWindow[3] = 1 / aspect;
            }
        }
        tanHalf_ = std::tan(radians(fov) * 0.5);
    }

    // (px, py): continuous raster coordinates, (0,0) at the top-left corner.
    // lensU: sample in [0,1)^2 for the aperture.
    Ray generateRay(double px, double py, const Vec2& lensU) const {
        double sx = screenWindow[0] + (px / width) * (screenWindow[1] - screenWindow[0]);
        double sy = screenWindow[3] - (py / height) * (screenWindow[3] - screenWindow[2]);
        Ray r;
        if (type == Type::Perspective) {
            r.o = Vec3(0, 0, 0);
            r.d = normalize(Vec3(sx * tanHalf_, sy * tanHalf_, 1));
        } else {
            r.o = Vec3(sx, sy, 0);
            r.d = Vec3(0, 0, 1);
        }
        if (lensRadius > 0) {
            // Thin lens: jitter the origin over the aperture and aim at the point
            // where the original ray crosses the plane of focus.
            Vec2 l = concentricSampleDisk(lensU) * lensRadius;
            double ft = focalDistance / r.d.z;
            Vec3 pFocus = r.at(ft);
            r.o = Vec3(r.o.x + l.x, r.o.y + l.y, 0);
            r.d = normalize(pFocus - r.o);
        }
        Ray w = cameraToWorld.ray(r);
        w.d = normalize(w.d);
        return w;
    }

private:
    double tanHalf_ = 1;
};
