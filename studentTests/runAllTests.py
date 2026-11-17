#!/usr/bin/env python3
"""CGR 2026 student test suite runner (Python 3 standard library only).

Runs every testN/ folder in numeric order, renders its scene with the renderer
under test, compares the output against the PBRT-v4 reference and writes a log.

Usage:
    python3 runAllTests.py [--renderer "CMD"] [--outdir DIR] [--only test3,test12] [--category camera]

--renderer is a command template with the placeholders
    {scene}  absolute path of the test scene (.pbrt)
    {outdir} absolute output directory
    {name}   output base name (testsuite_my_N)
The default runs this repository's renderer:
    ../output/CGR26_renderer {scene} --outdir {outdir} --output {name} --hdr --quiet
Another renderer should write {outdir}/{name}.pfm (preferred, linear) or .png/.ppm,
and optionally {outdir}/{name}.log. If no file with that name appears, the newest
image written to {outdir} (or to the test folder) during the run is used.

Categories and pass criteria (thresholds live in each test.json):
  camera   emissive objects on black, 1 centred sample per pixel. Each pixel is labelled
           background/red/green/blue; PASS if at most max_mismatch_percent of pixels
           differ from the reference.
  shading  mean linear radiance in a (2r+1)^2 window around listed pixels; PASS if every
           channel is within rel_tol (relative) or abs_tol (absolute) of the reference.
  error    scene with a deliberate error; PASS if the renderer does not crash or hang
           and its log/stdout/stderr mentions one of the test's keywords.
"""
import argparse
import datetime
import glob
import json
import math
import os
import re
import shlex
import struct
import subprocess
import sys
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_RENDERER = os.path.join(HERE, "..", "output", "CGR26_renderer") + " {scene} --outdir {outdir} --output {name} --hdr --quiet"
IMAGE_EXTS = (".pfm", ".png", ".ppm")


# ----------------------------------------------------------------- images ---
class Image:
    def __init__(self, w, h, px, linear):
        self.w, self.h, self.px, self.linear = w, h, px, linear  # px: list of (r, g, b), row 0 at the top

    def at(self, x, y):
        return self.px[y * self.w + x]


def srgb_to_linear(v):
    return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4


def read_pfm(path):
    with open(path, "rb") as f:
        data = f.read()
    tokens, pos = [], 0
    while len(tokens) < 4:  # magic, width, height, scale
        while data[pos:pos + 1].isspace():
            pos += 1
        start = pos
        while not data[pos:pos + 1].isspace():
            pos += 1
        tokens.append(data[start:pos].decode())
    pos += 1
    magic, w, h, scale = tokens[0], int(tokens[1]), int(tokens[2]), float(tokens[3])
    ch = 3 if magic == "PF" else 1
    fmt = ("<" if scale < 0 else ">") + "%df" % (w * h * ch)
    vals = struct.unpack_from(fmt, data, pos)
    px = [None] * (w * h)
    for y in range(h):  # stored bottom-to-top
        for x in range(w):
            i = ((h - 1 - y) * w + x) * ch
            px[y * w + x] = tuple(vals[i:i + 3]) if ch == 3 else (vals[i],) * 3
    return Image(w, h, px, True)


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    tokens, pos = [], 0
    while len(tokens) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            while data[pos:pos + 1] not in (b"\n", b""):
                pos += 1
            continue
        start = pos
        while pos < len(data) and not data[pos:pos + 1].isspace():
            pos += 1
        tokens.append(data[start:pos].decode())
    magic, w, h, maxv = tokens[0], int(tokens[1]), int(tokens[2]), int(tokens[3])
    if magic == "P6":
        pos += 1
        bpc = 2 if maxv > 255 else 1
        raw = data[pos:pos + w * h * 3 * bpc]
        if bpc == 1:
            vals = list(raw)
        else:
            vals = [raw[i] << 8 | raw[i + 1] for i in range(0, len(raw), 2)]
    elif magic == "P3":
        vals = [int(t) for t in data[pos:].split()[:w * h * 3]]
    else:
        raise ValueError("unsupported PPM type " + magic)
    px = [tuple(srgb_to_linear(vals[i * 3 + k] / maxv) for k in range(3)) for i in range(w * h)]
    return Image(w, h, px, False)


def read_png(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG file")
    pos, idat, w = 8, b"", None
    while pos < len(data):
        length, ctype = struct.unpack(">I4s", data[pos:pos + 8])
        chunk = data[pos + 8:pos + 8 + length]
        if ctype == b"IHDR":
            w, h, depth, ctype_, _, _, interlace = struct.unpack(">IIBBBBB", chunk)
        elif ctype == b"IDAT":
            idat += chunk
        elif ctype == b"IEND":
            break
        pos += 12 + length
    if w is None or interlace != 0 or ctype_ not in (0, 2, 4, 6) or depth not in (8, 16):
        raise ValueError("unsupported PNG variant (need non-interlaced 8/16-bit grey/RGB/RGBA)")
    channels = {0: 1, 2: 3, 4: 2, 6: 4}[ctype_]
    bpp = channels * depth // 8
    stride = w * bpp
    raw = zlib.decompress(idat)
    rows, prev, pos = [], bytearray(stride), 0
    for _ in range(h):
        ftype = raw[pos]
        line = bytearray(raw[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if ftype == 1:
                line[i] = (line[i] + a) & 255
            elif ftype == 2:
                line[i] = (line[i] + b) & 255
            elif ftype == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif ftype == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    px = []
    maxv = 255.0 if depth == 8 else 65535.0
    for line in rows:
        for x in range(w):
            def sample(k):
                o = x * bpp + k * depth // 8
                return line[o] if depth == 8 else line[o] << 8 | line[o + 1]
            if channels in (1, 2):
                g = srgb_to_linear(sample(0) / maxv)
                px.append((g, g, g))
            else:
                px.append(tuple(srgb_to_linear(sample(k) / maxv) for k in range(3)))
    return Image(w, h, px, False)


def read_image(path):
    ext = os.path.splitext(path)[1].lower()
    if ext == ".pfm":
        return read_pfm(path)
    if ext == ".png":
        return read_png(path)
    if ext == ".ppm":
        return read_ppm(path)
    raise ValueError("unsupported image format: " + path)


def write_ppm(path, w, h, px):
    with open(path, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (w, h))
        f.write(bytes(max(0, min(255, int(round(c * 255)))) for p in px for c in p))


def find_image(directory, base):
    for ext in IMAGE_EXTS:
        p = os.path.join(directory, base + ext)
        if os.path.exists(p):
            return p
    return None


# ---------------------------------------------------------------- metrics ---
def label(p, thr):
    m = max(p)
    if m < thr:
        return 0
    return 1 + p.index(m)


def compare_camera(cfg, ref, mine, diff_path):
    thr = 0.25 * max(max(p) for p in ref.px) if ref.px else 0.25
    labels_r = [label(p, thr) for p in ref.px]
    labels_m = [label(p, thr) for p in mine.px]
    mismatched = sum(1 for a, b in zip(labels_r, labels_m) if a != b)
    pct = 100.0 * mismatched / len(labels_r)
    lines = ["pixels with a different object label: %d of %d (%.3f%%), limit %.3f%%"
             % (mismatched, len(labels_r), pct, cfg.get("max_mismatch_percent", 0.25))]
    names = {1: "red", 2: "green", 3: "blue"}
    for lab in (1, 2, 3):
        def stats(labels):
            pts = [(i % ref.w, i // ref.w) for i, l in enumerate(labels) if l == lab]
            if not pts:
                return None
            xs, ys = [p[0] for p in pts], [p[1] for p in pts]
            return len(pts), sum(xs) / len(xs), sum(ys) / len(ys), (min(xs), min(ys), max(xs), max(ys))
        a, b = stats(labels_r), stats(labels_m)
        if a is None and b is None:
            continue
        if a is None or b is None:
            lines.append("  %-5s object: present in %s only" % (names[lab], "reference" if b is None else "test image"))
            continue
        lines.append("  %-5s object: area %d vs %d px, centroid shift %.2f px, bbox ref %s test %s"
                     % (names[lab], a[0], b[0], math.hypot(a[1] - b[1], a[2] - b[2]), a[3], b[3]))
    # Difference image: white = mismatched pixel, dim reference elsewhere.
    write_ppm(diff_path, ref.w, ref.h,
              [(1, 1, 1) if a != b else tuple(0.25 * min(1, c) for c in p)
               for a, b, p in zip(labels_r, labels_m, ref.px)])
    lines.append("  mismatch image: " + diff_path)
    return pct <= cfg.get("max_mismatch_percent", 0.25), lines


def window_mean(img, x, y, r):
    acc, n = [0.0, 0.0, 0.0], 0
    for yy in range(max(0, y - r), min(img.h, y + r + 1)):
        for xx in range(max(0, x - r), min(img.w, x + r + 1)):
            p = img.at(xx, yy)
            for k in range(3):
                acc[k] += p[k]
            n += 1
    return [a / n for a in acc]


def compare_shading(cfg, ref, mine):
    r, rel_tol, abs_tol = cfg.get("radius", 2), cfg.get("rel_tol", 0.06), cfg.get("abs_tol", 0.01)
    lines, ok = [], True
    if not ref.linear or not mine.linear:
        lines.append("  note: comparing display-encoded (8-bit sRGB) data; use PFM output for exact linear values")
    se = sum((a - b) ** 2 for pr, pm in zip(ref.px, mine.px) for a, b in zip(pr, pm))
    lines.append("whole-image RMSE (linear): %.5f" % math.sqrt(se / (3 * len(ref.px))))
    if not cfg.get("pixels"):
        lines.append("  no pixels listed in test.json")
        return False, lines
    worst = 0.0
    for p in cfg["pixels"]:
        x, y = p["x"], p["y"]
        a, b = window_mean(ref, x, y, r), window_mean(mine, x, y, r)
        status = "ok"
        rels = []
        for k in range(3):
            d = abs(a[k] - b[k])
            rel = d / max(abs(a[k]), 1e-6)
            rels.append(rel)
            if d > abs_tol and rel > rel_tol:
                status = "FAIL"
        worst = max(worst, max(rels))
        ok = ok and status == "ok"
        lines.append("  %-4s (%3d,%3d) %-28s ref (%.4f %.4f %.4f)  test (%.4f %.4f %.4f)  rel err (%.1f%% %.1f%% %.1f%%)"
                     % (status, x, y, p.get("label", ""), a[0], a[1], a[2], b[0], b[1], b[2],
                        100 * rels[0], 100 * rels[1], 100 * rels[2]))
    lines.append("tolerance: relative %.1f%% or absolute %.3f per channel, window %dx%d"
                 % (100 * rel_tol, abs_tol, 2 * r + 1, 2 * r + 1))
    return ok, lines


# ------------------------------------------------------------------ runner ---
def run_renderer(template, scene, outdir, name, cwd, timeout):
    cmd = template.format(scene=scene, outdir=outdir, name=name)
    start = time.time()
    try:
        proc = subprocess.run(shlex.split(cmd), cwd=cwd, capture_output=True, text=True, timeout=timeout)
        return dict(cmd=cmd, code=proc.returncode, out=proc.stdout, err=proc.stderr,
                    seconds=time.time() - start, timeout=False, start=start)
    except subprocess.TimeoutExpired as e:
        return dict(cmd=cmd, code=None, out=e.stdout or "", err=e.stderr or "", seconds=timeout, timeout=True, start=start)
    except OSError as e:
        return dict(cmd=cmd, code=None, out="", err=str(e), seconds=0, timeout=False, start=start, oserror=True)


def newest_output(dirs, exts, since):
    best = None
    for d in dirs:
        for ext in exts:
            for p in glob.glob(os.path.join(d, "*" + ext)):
                t = os.path.getmtime(p)
                if t >= since - 1 and (best is None or t > best[0]):
                    best = (t, p)
    return best[1] if best else None


def crashed(res):
    if res.get("timeout") or res.get("oserror"):
        return True
    code = res["code"]
    return code is None or code < 0 or code >= 128  # negative / >=128: killed by a signal


def run_test(tdir, cfg, args, outdir):
    n = re.sub(r"\D", "", cfg["name"])
    name = "testsuite_my_" + n
    scene = os.path.join(tdir, cfg["scene"])
    for ext in IMAGE_EXTS + (".log",):  # remove stale outputs
        p = os.path.join(outdir, name + ext)
        if os.path.exists(p):
            os.remove(p)
    res = run_renderer(args.renderer, scene, outdir, name, tdir, args.timeout)
    lines = ["command: " + res["cmd"], "exit code: %s, time %.2f s" % (res["code"], res["seconds"])]
    log_path = os.path.join(outdir, name + ".log")
    if not os.path.exists(log_path):
        log_path = newest_output([outdir, tdir], (".log",), res["start"])
    log_text = open(log_path, errors="replace").read() if log_path else ""
    cat = cfg["category"]

    if cat == "error":
        text = "\n".join([log_text, res["out"], res["err"]])
        if crashed(res):
            why = "timed out" if res.get("timeout") else "could not be started" if res.get("oserror") else "crashed"
            return "FAIL", lines + ["renderer %s (%s)" % (why, res["err"].strip()[-200:])]
        found = [k for k in cfg["keywords"] if k.lower() in text.lower()]
        line = cfg.get("line")
        mentions_line = bool(line) and re.search(r"(:%d\b|line\s*%d\b)" % (line, line), text) is not None
        diag = []
        for l in text.splitlines():
            if re.search(r"error|warning", l, re.I) and l not in diag:
                diag.append(l)
        lines.append("expected a message mentioning any of %s; found %s" % (cfg["keywords"], found or "none"))
        if line:
            lines.append("faulty line %d %s in the message" % (line, "is named" if mentions_line else "is NOT named"))
        lines += ["  > " + l for l in diag[:6]]
        return ("PASS" if found else "FAIL"), lines

    if crashed(res) or res["code"] != 0:
        lines.append("renderer failed: " + (res["err"].strip()[-300:] or "non-zero exit code"))
        return "FAIL", lines
    mine_path = find_image(outdir, name) or newest_output([outdir, tdir], IMAGE_EXTS, res["start"])
    ref_path = find_image(tdir, "testsuite_ref_" + n)
    if ref_path is None:
        return "SKIP", lines + ["no reference image testsuite_ref_%s.(pfm|png) - run makeReferences.sh with PBRT-v4" % n]
    if mine_path is None:
        return "FAIL", lines + ["the renderer produced no image"]
    try:
        ref, mine = read_image(ref_path), read_image(mine_path)
    except (ValueError, OSError, struct.error) as e:
        return "FAIL", lines + ["could not read images: %s" % e]
    lines.append("reference: %s\nmine:      %s" % (ref_path, mine_path))
    if (ref.w, ref.h) != (mine.w, mine.h):
        return "FAIL", lines + ["image sizes differ: %dx%d vs %dx%d" % (ref.w, ref.h, mine.w, mine.h)]
    if cat == "camera":
        ok, more = compare_camera(cfg, ref, mine, os.path.join(outdir, "testsuite_diff_%s.ppm" % n))
    else:
        ok, more = compare_shading(cfg, ref, mine)
    return ("PASS" if ok else "FAIL"), lines + more


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--renderer", default=DEFAULT_RENDERER, help="renderer command template")
    ap.add_argument("--outdir", default=os.path.join(HERE, "..", "output", "tests"), help="where outputs and the log go")
    ap.add_argument("--only", default="", help="comma-separated test names, e.g. test3,test12")
    ap.add_argument("--category", default="", help="run only camera, shading or error tests")
    ap.add_argument("--timeout", type=float, default=900, help="seconds per test")
    args = ap.parse_args()
    outdir = os.path.abspath(args.outdir)
    os.makedirs(outdir, exist_ok=True)

    dirs = sorted(glob.glob(os.path.join(HERE, "test*")), key=lambda p: int(re.sub(r"\D", "", os.path.basename(p)) or 0))
    only = {s.strip() for s in args.only.split(",") if s.strip()}
    log_lines = ["CGR 2026 student test suite - %s" % datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
                 "renderer: " + args.renderer, ""]
    results = []
    for tdir in dirs:
        if not os.path.isdir(tdir):
            continue
        cfg_path = os.path.join(tdir, "test.json")
        if not os.path.exists(cfg_path):
            continue
        cfg = json.load(open(cfg_path))
        if only and cfg["name"] not in only:
            continue
        if args.category and cfg["category"] != args.category:
            continue
        status, lines = run_test(tdir, cfg, args, outdir)
        results.append((cfg, status))
        header = "[%s] %s (%s): %s" % (status, cfg["name"], cfg["category"], cfg["title"])
        print(header, flush=True)
        log_lines.append(header)
        log_lines += ["    " + l for l in "\n".join(lines).splitlines()]
        log_lines.append("")

    summary = ["SUMMARY", "%-10s %6s %6s %6s %6s" % ("category", "total", "pass", "fail", "skip")]
    for cat in ("camera", "shading", "error"):
        rs = [s for c, s in results if c["category"] == cat]
        if rs:
            summary.append("%-10s %6d %6d %6d %6d" % (cat, len(rs), rs.count("PASS"), rs.count("FAIL"), rs.count("SKIP")))
    allr = [s for _, s in results]
    summary.append("%-10s %6d %6d %6d %6d" % ("all", len(allr), allr.count("PASS"), allr.count("FAIL"), allr.count("SKIP")))
    log_lines += summary
    log_path = os.path.join(outdir, "test_results.log")
    with open(log_path, "w") as f:
        f.write("\n".join(log_lines) + "\n")
    with open(os.path.join(outdir, "test_results.csv"), "w") as f:
        f.write("test,category,title,result\n")
        for cfg, s in results:
            f.write('%s,%s,"%s",%s\n' % (cfg["name"], cfg["category"], cfg["title"], s))
    print("\n".join(summary))
    print("log written to " + log_path)
    return 1 if "FAIL" in allr else 0


if __name__ == "__main__":
    sys.exit(main())
