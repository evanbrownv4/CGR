// Floating point RGB images, tone mapping and image file I/O (PPM, PNG, PFM),
// all implemented with the standard library only.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "math.h"

struct Image {
    int width = 0, height = 0;
    std::vector<Color> pixels;  // row-major, row 0 at the top

    Image() = default;
    Image(int w, int h) : width(w), height(h), pixels((size_t)w * h) {}
    Color& at(int x, int y) { return pixels[(size_t)y * width + x]; }
    const Color& at(int x, int y) const { return pixels[(size_t)y * width + x]; }
    bool empty() const { return width <= 0 || height <= 0; }
};

struct ToneMapSettings {
    enum class Op { Clamp, Reinhard } op = Op::Clamp;
    bool perChannel = false;   // Reinhard: apply per channel (true) or to luminance (false)
    double key = 0.18;         // Reinhard key value a (middle grey). <= 0 disables log-average scaling.
    double whitePoint = -1;    // Reinhard L_white (in scaled units). <= 0 -> max scaled luminance.
    double exposure = 1.0;     // linear multiplier applied before tone mapping
    bool srgb = true;          // apply sRGB transfer curve after tone mapping
};

// Statistics computed during tone mapping (reported in the log).
struct ToneMapStats {
    double logAverageLuminance = 0;
    double maxLuminance = 0;
    double whitePointUsed = 0;
};

double srgbToLinear(double v);
double linearToSrgb(double v);

// Converts an HDR image to 8-bit RGB (row-major, 3 bytes per pixel).
std::vector<uint8_t> toneMap(const Image& img, const ToneMapSettings& s, ToneMapStats* stats = nullptr);

bool writePPM(const std::string& path, int w, int h, const std::vector<uint8_t>& rgb);
bool writePNG(const std::string& path, int w, int h, const std::vector<uint8_t>& rgb);
bool writePFM(const std::string& path, const Image& img);

// Reads binary/ASCII PPM (P3/P6, values normalised to [0,1], no gamma conversion)
// or PFM (linear float). On failure returns false and fills `err`.
bool readImage(const std::string& path, Image& img, std::string& err);
