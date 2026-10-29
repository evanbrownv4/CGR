#include "image.h"

#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

double srgbToLinear(double v) {
    if (v <= 0.04045) return v / 12.92;
    return std::pow((v + 0.055) / 1.055, 2.4);
}

double linearToSrgb(double v) {
    if (v <= 0.0031308) return 12.92 * v;
    return 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
}

static uint8_t quantize(double v) {
    v = clampd(v, 0.0, 1.0);
    return (uint8_t)std::lround(v * 255.0);
}

std::vector<uint8_t> toneMap(const Image& img, const ToneMapSettings& s, ToneMapStats* stats) {
    std::vector<uint8_t> out((size_t)img.width * img.height * 3);
    size_t n = img.pixels.size();

    // Reinhard et al. 2002 "Photographic tone reproduction":
    //   L_avg = exp(mean(log(delta + L_w)))      (log-average luminance)
    //   L     = a / L_avg * L_w                  (key scaling)
    //   L_d   = L (1 + L / L_white^2) / (1 + L)  (compression with burn-out)
    double scale = s.exposure;
    double logAvg = 0, maxScaled = 0, white = s.whitePoint;
    if (s.op == ToneMapSettings::Op::Reinhard) {
        const double delta = 1e-6;
        double sumLog = 0;
        for (const Color& c : img.pixels) {
            Color e = c * s.exposure;
            double L = luminance(Color(std::max(0.0, e.x), std::max(0.0, e.y), std::max(0.0, e.z)));
            sumLog += std::log(delta + L);
        }
        logAvg = n ? std::exp(sumLog / (double)n) : 1.0;
        if (s.key > 0 && logAvg > 0) scale = s.exposure * s.key / logAvg;
        for (const Color& c : img.pixels) {
            Color e = c * scale;
            double m = s.perChannel ? e.maxComponent() : luminance(e);
            maxScaled = std::max(maxScaled, m);
        }
        if (white <= 0) white = std::max(maxScaled, 1e-6);
    }
    if (stats) {
        stats->logAverageLuminance = logAvg;
        stats->maxLuminance = maxScaled;
        stats->whitePointUsed = white;
    }

    auto reinhard = [&](double L) {
        if (L <= 0) return 0.0;
        return L * (1.0 + L / (white * white)) / (1.0 + L);
    };

    for (size_t i = 0; i < n; ++i) {
        Color c = img.pixels[i] * scale;
        c = Color(std::max(0.0, c.x), std::max(0.0, c.y), std::max(0.0, c.z));
        if (c.hasNaN()) c = Color(0);
        if (s.op == ToneMapSettings::Op::Reinhard) {
            if (s.perChannel) {
                c = Color(reinhard(c.x), reinhard(c.y), reinhard(c.z));
            } else {
                double L = luminance(c);
                if (L > 0) c = c * (reinhard(L) / L);
            }
        }
        for (int k = 0; k < 3; ++k) {
            double v = clampd(c[k], 0.0, 1.0);
            if (s.srgb) v = linearToSrgb(v);
            out[i * 3 + k] = quantize(v);
        }
    }
    return out;
}

bool writePPM(const std::string& path, int w, int h, const std::vector<uint8_t>& rgb) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << "P6\n" << w << " " << h << "\n255\n";
    f.write(reinterpret_cast<const char*>(rgb.data()), (std::streamsize)rgb.size());
    return (bool)f;
}

// ---------------------------------------------------------------------------
// PNG writer. Uses zlib "stored" (uncompressed) deflate blocks, which keeps the
// encoder trivial while producing files any viewer can open.
// ---------------------------------------------------------------------------
static uint32_t crcTable[256];
static bool crcTableReady = false;

static uint32_t crc32(const uint8_t* data, size_t len, uint32_t crc = 0xffffffffu) {
    if (!crcTableReady) {
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            crcTable[n] = c;
        }
        crcTableReady = true;
    }
    for (size_t i = 0; i < len; ++i) crc = crcTable[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
    return crc;
}

static void putU32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back((x >> 24) & 0xff);
    v.push_back((x >> 16) & 0xff);
    v.push_back((x >> 8) & 0xff);
    v.push_back(x & 0xff);
}

static void writeChunk(std::ofstream& f, const char* type, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> buf;
    putU32(buf, (uint32_t)data.size());
    buf.insert(buf.end(), type, type + 4);
    buf.insert(buf.end(), data.begin(), data.end());
    uint32_t crc = crc32(buf.data() + 4, buf.size() - 4) ^ 0xffffffffu;
    putU32(buf, crc);
    f.write(reinterpret_cast<const char*>(buf.data()), (std::streamsize)buf.size());
}

bool writePNG(const std::string& path, int w, int h, const std::vector<uint8_t>& rgb) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    f.write(reinterpret_cast<const char*>(sig), 8);

    std::vector<uint8_t> ihdr;
    putU32(ihdr, (uint32_t)w);
    putU32(ihdr, (uint32_t)h);
    ihdr.push_back(8);  // bit depth
    ihdr.push_back(2);  // colour type RGB
    ihdr.push_back(0);  // compression
    ihdr.push_back(0);  // filter
    ihdr.push_back(0);  // interlace
    writeChunk(f, "IHDR", ihdr);

    // Raw scanlines, each prefixed with filter type 0.
    std::vector<uint8_t> raw;
    raw.reserve((size_t)h * (w * 3 + 1));
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), rgb.begin() + (size_t)y * w * 3, rgb.begin() + (size_t)(y + 1) * w * 3);
    }
    // zlib stream with stored blocks.
    std::vector<uint8_t> z;
    z.push_back(0x78);
    z.push_back(0x01);
    size_t pos = 0;
    do {
        size_t len = std::min<size_t>(65535, raw.size() - pos);
        bool last = pos + len == raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(len & 0xff);
        z.push_back((len >> 8) & 0xff);
        z.push_back(~len & 0xff);
        z.push_back((~len >> 8) & 0xff);
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + len);
        pos += len;
    } while (pos < raw.size());
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    putU32(z, (b << 16) | a);
    writeChunk(f, "IDAT", z);
    writeChunk(f, "IEND", {});
    return (bool)f;
}

// PFM stores rows bottom-to-top, little endian when the scale is negative.
bool writePFM(const std::string& path, const Image& img) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << "PF\n" << img.width << " " << img.height << "\n-1.0\n";
    std::vector<float> row((size_t)img.width * 3);
    for (int y = img.height - 1; y >= 0; --y) {
        for (int x = 0; x < img.width; ++x) {
            const Color& c = img.at(x, y);
            row[x * 3 + 0] = (float)c.x;
            row[x * 3 + 1] = (float)c.y;
            row[x * 3 + 2] = (float)c.z;
        }
        f.write(reinterpret_cast<const char*>(row.data()), (std::streamsize)(row.size() * sizeof(float)));
    }
    return (bool)f;
}

// Reads the next whitespace-separated header token, skipping '#' comments.
static bool nextHeaderToken(std::istream& in, std::string& tok) {
    tok.clear();
    int c;
    while ((c = in.get()) != EOF) {
        if (c == '#') {
            while ((c = in.get()) != EOF && c != '\n') {}
            continue;
        }
        if (!std::isspace(c)) break;
    }
    if (c == EOF) return false;
    tok.push_back((char)c);
    while ((c = in.peek()) != EOF && !std::isspace(c) && c != '#') tok.push_back((char)in.get());
    return true;
}

static bool parseInt(const std::string& s, int& v) {
    try {
        size_t idx;
        v = std::stoi(s, &idx);
        return idx == s.size();
    } catch (...) {
        return false;
    }
}

bool readImage(const std::string& path, Image& img, std::string& err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        err = "cannot open image file '" + path + "'";
        return false;
    }
    std::string magic, ws, hs, ms;
    if (!nextHeaderToken(f, magic)) {
        err = "empty image file '" + path + "'";
        return false;
    }
    if (magic == "PF" || magic == "Pf") {
        bool color = magic == "PF";
        int w, h;
        if (!nextHeaderToken(f, ws) || !nextHeaderToken(f, hs) || !nextHeaderToken(f, ms) || !parseInt(ws, w) ||
            !parseInt(hs, h) || w <= 0 || h <= 0) {
            err = "malformed PFM header in '" + path + "'";
            return false;
        }
        double scale = std::atof(ms.c_str());
        bool little = scale < 0;
        f.get();  // single whitespace after header
        int ch = color ? 3 : 1;
        std::vector<float> data((size_t)w * h * ch);
        f.read(reinterpret_cast<char*>(data.data()), (std::streamsize)(data.size() * sizeof(float)));
        if (!f) {
            err = "truncated PFM data in '" + path + "'";
            return false;
        }
        uint16_t probe = 1;
        bool hostLittle = *reinterpret_cast<uint8_t*>(&probe) == 1;
        if (little != hostLittle) {
            for (float& v : data) {
                uint8_t* b = reinterpret_cast<uint8_t*>(&v);
                std::swap(b[0], b[3]);
                std::swap(b[1], b[2]);
            }
        }
        img = Image(w, h);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                size_t i = ((size_t)(h - 1 - y) * w + x) * ch;
                img.at(x, y) = color ? Color(data[i], data[i + 1], data[i + 2]) : Color(data[i]);
            }
        return true;
    }
    if (magic != "P3" && magic != "P6") {
        err = "unsupported image format in '" + path + "' (only PPM P3/P6 and PFM are supported)";
        return false;
    }
    int w, h, maxv;
    if (!nextHeaderToken(f, ws) || !nextHeaderToken(f, hs) || !nextHeaderToken(f, ms) || !parseInt(ws, w) ||
        !parseInt(hs, h) || !parseInt(ms, maxv) || w <= 0 || h <= 0 || maxv <= 0 || maxv > 65535) {
        err = "malformed PPM header in '" + path + "'";
        return false;
    }
    img = Image(w, h);
    if (magic == "P6") {
        f.get();
        int bpc = maxv > 255 ? 2 : 1;
        std::vector<uint8_t> data((size_t)w * h * 3 * bpc);
        f.read(reinterpret_cast<char*>(data.data()), (std::streamsize)data.size());
        if (!f) {
            err = "truncated PPM data in '" + path + "'";
            return false;
        }
        for (size_t i = 0; i < (size_t)w * h; ++i) {
            double c[3];
            for (int k = 0; k < 3; ++k) {
                size_t j = (i * 3 + k) * bpc;
                int v = bpc == 2 ? (data[j] << 8) | data[j + 1] : data[j];
                c[k] = (double)v / maxv;
            }
            img.pixels[i] = Color(c[0], c[1], c[2]);
        }
    } else {
        for (size_t i = 0; i < (size_t)w * h; ++i) {
            double c[3];
            for (int k = 0; k < 3; ++k) {
                std::string t;
                int v;
                if (!nextHeaderToken(f, t) || !parseInt(t, v)) {
                    err = "truncated or malformed ASCII PPM data in '" + path + "'";
                    return false;
                }
                c[k] = clampd((double)v / maxv, 0.0, 1.0);
            }
            img.pixels[i] = Color(c[0], c[1], c[2]);
        }
    }
    return true;
}
