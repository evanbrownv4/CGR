// Textures evaluated at surface (u, v) coordinates.
#pragma once

#include <memory>
#include <string>

#include "image.h"
#include "math.h"

struct Texture {
    enum class Type { Constant, ImageMap, Checkerboard } type = Type::Constant;
    Color value{0.5, 0.5, 0.5};               // Constant
    std::shared_ptr<Image> image;              // ImageMap (linear RGB)
    bool bilinear = true;                      // ImageMap filtering
    std::shared_ptr<Texture> tex1, tex2;       // Checkerboard
    double uscale = 1, vscale = 1, udelta = 0, vdelta = 0;
    Color scale{1, 1, 1};                      // multiplier applied to the result

    static std::shared_ptr<Texture> constant(const Color& c) {
        auto t = std::make_shared<Texture>();
        t->value = c;
        return t;
    }
    bool isConstant() const { return type == Type::Constant; }

    Color eval(const Vec2& uvIn) const {
        double u = uvIn.x * uscale + udelta;
        double v = uvIn.y * vscale + vdelta;
        switch (type) {
            case Type::Constant:
                return value * scale;
            case Type::Checkerboard: {
                long iu = (long)std::floor(u), iv = (long)std::floor(v);
                const auto& t = ((iu + iv) & 1) == 0 ? tex1 : tex2;
                return (t ? t->eval(Vec2(u, v)) : Color(0)) * scale;
            }
            case Type::ImageMap:
                return lookup(u, v) * scale;
        }
        return Color(0);
    }

private:
    // Repeat-wrapped image lookup. As in PBRT, v = 0 is the bottom row of the image.
    Color lookup(double u, double v) const {
        if (!image || image->empty()) return Color(1, 0, 1);
        int w = image->width, h = image->height;
        double s = u - std::floor(u);
        double t = 1.0 - (v - std::floor(v));
        auto texel = [&](int x, int y) {
            x %= w; if (x < 0) x += w;
            y %= h; if (y < 0) y += h;
            return image->at(x, y);
        };
        double x = s * w - 0.5, y = t * h - 0.5;
        if (!bilinear) return texel((int)std::floor(x + 0.5), (int)std::floor(y + 0.5));
        int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
        double fx = x - x0, fy = y - y0;
        return texel(x0, y0) * ((1 - fx) * (1 - fy)) + texel(x0 + 1, y0) * (fx * (1 - fy)) +
               texel(x0, y0 + 1) * ((1 - fx) * fy) + texel(x0 + 1, y0 + 1) * (fx * fy);
    }
};
