// CGR26_renderer: command-line entry point.
//
//   CGR26_renderer <scene.pbrt> [options]       render a scene
//   CGR26_renderer --compare <ref> <test> [...]  compare two images (RMSE etc.)
//
// See README.md for the full list of options.

#include <cstring>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

#include "image.h"
#include "integrators.h"
#include "logger.h"
#include "parser.h"
#include "scene.h"

namespace fs = std::filesystem;

static void usage() {
    std::cout <<
        "Usage:\n"
        "  CGR26_renderer <scene.pbrt> [options]\n"
        "    --outdir <dir>     directory for images and logs (default: directory of the executable)\n"
        "    --output <name>    override the output base name (Film \"filename\")\n"
        "    --spp <n>          override pixel samples per pixel\n"
        "    --threads <n>      number of render threads (default: all cores)\n"
        "    --seed <n>         random seed\n"
        "    --nobvh            disable the BVH (brute-force intersection)\n"
        "    --hdr              also write linear radiance as <name>.pfm\n"
        "    --quiet            only print warnings and errors\n"
        "  CGR26_renderer --compare <reference> <test> [options]   (PPM or PFM images)\n"
        "    --pixel <x> <y>    report values at a pixel (repeatable)\n"
        "    --region <x0> <y0> <x1> <y1>  also report RMSE inside a region (exclusive max)\n"
        "    --diff <out.png> [scale]       write an absolute difference image\n";
}

static std::string fmt(double v, int prec = 4) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(prec) << v;
    return ss.str();
}

static bool parseIntArg(const char* s, int& v) {
    try {
        size_t idx;
        v = std::stoi(s, &idx);
        return idx == std::strlen(s);
    } catch (...) {
        return false;
    }
}

// ------------------------------------------------------------- compare ---

static int compareMain(int argc, char** argv) {
    if (argc < 4) {
        usage();
        return 2;
    }
    std::string refPath = argv[2], testPath = argv[3];
    std::vector<std::pair<int, int>> pixels;
    int region[4] = {-1, -1, -1, -1};
    std::string diffPath;
    double diffScale = 1.0;
    for (int i = 4; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--pixel" && i + 2 < argc) {
            int x, y;
            if (!parseIntArg(argv[i + 1], x) || !parseIntArg(argv[i + 2], y)) {
                std::cerr << "invalid --pixel arguments\n";
                return 2;
            }
            pixels.emplace_back(x, y);
            i += 2;
        } else if (a == "--region" && i + 4 < argc) {
            for (int k = 0; k < 4; ++k)
                if (!parseIntArg(argv[i + 1 + k], region[k])) {
                    std::cerr << "invalid --region arguments\n";
                    return 2;
                }
            i += 4;
        } else if (a == "--diff" && i + 1 < argc) {
            diffPath = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') diffScale = std::atof(argv[++i]);
        } else {
            std::cerr << "unknown compare option '" << a << "'\n";
            return 2;
        }
    }
    Image ref, test;
    std::string err;
    if (!readImage(refPath, ref, err) || !readImage(testPath, test, err)) {
        std::cerr << "error: " << err << "\n";
        return 1;
    }
    if (ref.width != test.width || ref.height != test.height) {
        std::cerr << "error: image sizes differ (" << ref.width << "x" << ref.height << " vs " << test.width << "x"
                  << test.height << ")\n";
        return 1;
    }
    if (fs::path(refPath).extension() != fs::path(testPath).extension())
        std::cerr << "warning: comparing images of different formats (PPM values are display-encoded, PFM are linear)\n";

    auto rmse = [&](int x0, int y0, int x1, int y1, Color& perChannel, double& maxAbs) {
        Color se(0);
        maxAbs = 0;
        long n = 0;
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) {
                Color d = test.at(x, y) - ref.at(x, y);
                se += d * d;
                maxAbs = std::max(maxAbs, std::max(std::fabs(d.x), std::max(std::fabs(d.y), std::fabs(d.z))));
                ++n;
            }
        perChannel = Color(std::sqrt(se.x / n), std::sqrt(se.y / n), std::sqrt(se.z / n));
        return std::sqrt((se.x + se.y + se.z) / (3.0 * n));
    };
    Color pc;
    double maxAbs;
    double e = rmse(0, 0, ref.width, ref.height, pc, maxAbs);
    double peak = 0;
    for (const Color& c : ref.pixels) peak = std::max(peak, c.maxComponent());
    std::cout << "reference: " << refPath << "\ntest:      " << testPath << "\n";
    std::cout << "size: " << ref.width << "x" << ref.height << "\n";
    std::cout << "RMSE (all channels): " << fmt(e, 6) << "\n";
    std::cout << "RMSE per channel (R G B): " << fmt(pc.x, 6) << " " << fmt(pc.y, 6) << " " << fmt(pc.z, 6) << "\n";
    std::cout << "max abs difference: " << fmt(maxAbs, 6) << "\n";
    if (e > 0 && peak > 0) std::cout << "PSNR (peak = max reference value " << fmt(peak) << "): " << fmt(20 * std::log10(peak / e), 2) << " dB\n";
    if (region[0] >= 0) {
        int x0 = std::max(0, region[0]), y0 = std::max(0, region[1]);
        int x1 = std::min(ref.width, region[2]), y1 = std::min(ref.height, region[3]);
        if (x1 <= x0 || y1 <= y0) {
            std::cerr << "error: empty region\n";
            return 1;
        }
        double re = rmse(x0, y0, x1, y1, pc, maxAbs);
        std::cout << "region [" << x0 << "," << y0 << ")-(" << x1 << "," << y1 << ") RMSE: " << fmt(re, 6)
                  << "  max abs: " << fmt(maxAbs, 6) << "\n";
    }
    for (auto& px : pixels) {
        if (px.first < 0 || px.second < 0 || px.first >= ref.width || px.second >= ref.height) {
            std::cerr << "warning: pixel (" << px.first << "," << px.second << ") outside image\n";
            continue;
        }
        Color a = ref.at(px.first, px.second), b = test.at(px.first, px.second);
        Color d = b - a;
        std::cout << "pixel (" << px.first << "," << px.second << ") ref=(" << fmt(a.x) << " " << fmt(a.y) << " " << fmt(a.z)
                  << ") test=(" << fmt(b.x) << " " << fmt(b.y) << " " << fmt(b.z) << ") absdiff=(" << fmt(std::fabs(d.x))
                  << " " << fmt(std::fabs(d.y)) << " " << fmt(std::fabs(d.z)) << ")\n";
    }
    if (!diffPath.empty()) {
        Image diff(ref.width, ref.height);
        for (size_t i = 0; i < diff.pixels.size(); ++i) {
            Color d = test.pixels[i] - ref.pixels[i];
            diff.pixels[i] = Color(std::fabs(d.x), std::fabs(d.y), std::fabs(d.z)) * diffScale;
        }
        ToneMapSettings tm;
        tm.srgb = false;
        auto rgb = toneMap(diff, tm);
        bool ok = fs::path(diffPath).extension() == ".ppm" ? writePPM(diffPath, diff.width, diff.height, rgb)
                                                           : writePNG(diffPath, diff.width, diff.height, rgb);
        if (!ok) {
            std::cerr << "error: cannot write '" << diffPath << "'\n";
            return 1;
        }
        std::cout << "difference image written to " << diffPath << " (scale " << diffScale << ")\n";
    }
    return 0;
}

// -------------------------------------------------------------- render ---

static std::string executableDir(const char* argv0) {
    std::error_code ec;
    fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    if (ec || exe.empty()) exe = fs::absolute(argv0, ec);
    return exe.parent_path().string();
}

int main(int argc, char** argv) {
    if (argc < 2 || !std::strcmp(argv[1], "--help") || !std::strcmp(argv[1], "-h")) {
        usage();
        return argc < 2 ? 2 : 0;
    }
    if (!std::strcmp(argv[1], "--compare")) return compareMain(argc, argv);

    std::string scenePath = argv[1];
    std::string outDir = executableDir(argv[0]);
    std::string outputOverride;
    int sppOverride = -1, threads = 0, seed = -1;
    bool noBVH = false, hdr = false, quiet = false;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        auto needInt = [&](int& v) {
            if (i + 1 >= argc || !parseIntArg(argv[i + 1], v)) {
                std::cerr << "error: option " << a << " expects an integer\n";
                std::exit(2);
            }
            ++i;
        };
        if (a == "--outdir" && i + 1 < argc) outDir = argv[++i];
        else if (a == "--output" && i + 1 < argc) outputOverride = argv[++i];
        else if (a == "--spp") needInt(sppOverride);
        else if (a == "--threads") needInt(threads);
        else if (a == "--seed") needInt(seed);
        else if (a == "--nobvh") noBVH = true;
        else if (a == "--hdr") hdr = true;
        else if (a == "--quiet") quiet = true;
        else {
            std::cerr << "error: unknown option '" << a << "'\n";
            usage();
            return 2;
        }
    }

    std::error_code ec;
    fs::create_directories(outDir, ec);

    Logger log;
    log.setQuiet(quiet);
    {
        std::time_t now = std::time(nullptr);
        char buf[64];
        std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", std::localtime(&now));
        log.info(std::string("CGR26_renderer - render started ") + buf);
    }
    log.info("scene file: " + scenePath);

    Scene scene;
    Timer total, stage;
    bool parsed = parseSceneFile(scenePath, scene, log);
    double tParse = stage.seconds();
    RenderSettings& s = scene.settings;
    if (!outputOverride.empty()) s.outputName = outputOverride;
    if (s.outputName.empty()) s.outputName = fs::path(scenePath).stem().string();
    std::string base = (fs::path(outDir) / s.outputName).string();
    std::string logPath = base + ".log";

    if (!parsed) {
        log.error("scene could not be loaded; nothing rendered");
        if (!log.writeTo(logPath)) std::cerr << "error: cannot write log file '" << logPath << "'\n";
        else std::cerr << "log written to " << logPath << "\n";
        return 1;
    }
    log.info("parsing: " + fmt(tParse) + " s (" + std::to_string(log.errorCount()) + " error(s), " +
             std::to_string(log.warningCount()) + " warning(s))");

    if (sppOverride > 0) s.spp = sppOverride;
    if (threads > 0) s.threads = threads;
    if (seed >= 0) s.seed = (uint64_t)seed;
    if (noBVH) s.useBVH = false;
    if (hdr) s.writeHDR = true;
    if (!s.tonemapSpecified) {
        s.tonemap.op = s.integrator == "distributed" ? ToneMapSettings::Op::Reinhard : ToneMapSettings::Op::Clamp;
    }

    stage.reset();
    scene.prepare();
    double tBuild = stage.seconds();
    log.info("scene: " + std::to_string(scene.primitives.size()) + " primitives (" + std::to_string(scene.meshes.size()) +
             " meshes), " + std::to_string(scene.pointLights.size()) + " point, " +
             std::to_string(scene.distantLights.size()) + " distant, " + std::to_string(scene.areaLights.size()) +
             " area light(s)");
    for (const auto& al : scene.areaLights)
        log.info("area light (" + al->shapeName + "): area " + fmt(al->totalArea) + ", L = (" + fmt(al->L.x) + " " +
                 fmt(al->L.y) + " " + fmt(al->L.z) + ")" + (al->isParallelogram ? ", parallelogram mapping" : ""));
    log.info("acceleration structure build: " + fmt(tBuild) + " s" +
             (s.useBVH ? "" : " (BVH disabled: brute force intersection)"));

    std::ostringstream cfg;
    cfg << "integrator: " << s.integrator << ", maxdepth " << s.maxDepth;
    if (s.integrator == "distributed")
        cfg << ", branching " << s.branching << ", light samples " << s.lightSamples << " (" << strategyName(s.lightSampler)
            << (s.lightSampler == SampleStrategy::Halton ? (s.randomizeHalton ? ", rotated" : ", unrandomised") : "")
            << "), hemisphere " << (s.cosineHemisphere ? "cosine" : "uniform");
    else
        cfg << ", ambient (" << s.ambient.x << " " << s.ambient.y << " " << s.ambient.z << ")";
    log.info(cfg.str());
    log.info("film: " + std::to_string(s.width) + "x" + std::to_string(s.height) + ", " + std::to_string(s.spp) +
             " sample(s) per pixel (" + strategyName(s.pixelSampler) + ")");

    if (scene.areaLights.size() > 0 && s.integrator == "whitted")
        log.warning("WSRT: area lights are approximated by point lights at their centroids (hard shadows)");

    auto integrator = makeIntegrator(scene);
    Image img;
    RayStats stats;
    stage.reset();
    renderImage(scene, *integrator, img, stats, log, !quiet);
    double tRender = stage.seconds();
    log.info("render: " + fmt(tRender) + " s");
    log.info("rays: " + std::to_string(stats.total()) + " total (" + std::to_string(stats.camera) + " camera, " +
             std::to_string(stats.secondary) + " secondary, " + std::to_string(stats.shadow) + " shadow), " +
             fmt(stats.total() / std::max(tRender, 1e-9) / 1e6, 3) + " Mrays/s");

    stage.reset();
    ToneMapStats tms;
    auto rgb = toneMap(img, s.tonemap, &tms);
    if (s.tonemap.op == ToneMapSettings::Op::Reinhard)
        log.info(std::string("tone mapping: Reinhard (") + (s.tonemap.perChannel ? "per channel" : "luminance") +
                 "), key " + fmt(s.tonemap.key) + ", log-average luminance " + fmt(tms.logAverageLuminance) +
                 ", white point " + fmt(tms.whitePointUsed) + (s.tonemap.whitePoint <= 0 ? " (auto = max)" : ""));
    else
        log.info("tone mapping: clamp to [0,1]" + std::string(s.tonemap.exposure != 1 ? ", exposure " + fmt(s.tonemap.exposure) : ""));
    std::string imgPath = base + s.outputExt;
    bool ok = s.outputExt == ".ppm" ? writePPM(imgPath, img.width, img.height, rgb) : writePNG(imgPath, img.width, img.height, rgb);
    if (!ok) log.error("cannot write image '" + imgPath + "'");
    else log.info("image written: " + imgPath);
    if (s.writeHDR) {
        if (writePFM(base + ".pfm", img)) log.info("HDR image written: " + base + ".pfm");
        else log.error("cannot write HDR image '" + base + ".pfm'");
    }
    double tOutput = stage.seconds();
    log.info("tone mapping + output: " + fmt(tOutput) + " s");
    log.info("TIMING parse=" + fmt(tParse) + "s build=" + fmt(tBuild) + "s render=" + fmt(tRender) +
             "s output=" + fmt(tOutput) + "s total=" + fmt(total.seconds()) + "s");
    if (log.errorCount() > 0) log.warning("render completed with " + std::to_string(log.errorCount()) + " error(s); see messages above");

    if (!log.writeTo(logPath)) {
        std::cerr << "error: cannot write log file '" << logPath << "'\n";
        return 1;
    }
    if (!quiet) std::cout << "log written to " << logPath << "\n";
    return ok ? 0 : 1;
}
