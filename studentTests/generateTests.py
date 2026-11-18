#!/usr/bin/env python3
"""Generates the studentTests/testN folders (scene, test.json, run.sh). Hand-picked shading
pixels already present in a test.json are kept. Usage: python3 generateTests.py <studentTests dir>"""
import json, os, struct, sys

ROOT = sys.argv[1]
tests = []


def add(cat, title, desc, scene, cfg, files=None, res=(200, 150)):
    tests.append(dict(cat=cat, title=title, desc=desc, scene=scene, cfg=cfg, files=files or {}, res=res))


CAM_HEAD = '''Film "rgb" "integer xresolution" [{w}] "integer yresolution" [{h}] "string filename" "testsuite_ref_{n}.pfm"
Sampler "stratified" "integer xsamples" [1] "integer ysamples" [1] "bool jitter" false
PixelFilter "box"
Integrator "path" "integer maxdepth" [1]
'''

# ------------------------------------------------------------ camera / intersections
add("camera", "Perspective camera and sphere silhouettes",
    "Perspective camera (fov 40) with a centred red emissive sphere and an off-centre green one. "
    "Checks that sphere boundaries fall on exactly the same pixels as in PBRT.",
    '''LookAt 0 0 10   0 0 0   0 1 0
Camera "perspective" "float fov" [40]
{head}WorldBegin
Material "diffuse" "rgb reflectance" [0 0 0]
AttributeBegin
  AreaLightSource "diffuse" "rgb L" [1 0 0]
  Shape "sphere" "float radius" [1.5]
AttributeEnd
AttributeBegin
  Translate 3.2 1.6 -2
  AreaLightSource "diffuse" "rgb L" [0 1 0]
  Shape "sphere" "float radius" [0.8]
AttributeEnd
''', {})
add("camera", "Triangle/quad intersection and depth ordering",
    "A green two-sided triangle mesh partly in front of a red sphere and a blue bilinear quad behind both. "
    "Checks triangle edges, shared mesh edges, quad intersection and that the nearest surface wins.",
    '''LookAt 0 1 8   0 0 0   0 1 0
Camera "perspective" "float fov" [45]
{head}WorldBegin
Material "diffuse" "rgb reflectance" [0 0 0]
AttributeBegin
  AreaLightSource "diffuse" "rgb L" [1 0 0]
  Shape "sphere" "float radius" [1.2]
AttributeEnd
AttributeBegin
  AreaLightSource "diffuse" "rgb L" [0 1 0] "bool twosided" true
  Shape "trianglemesh" "integer indices" [0 1 2  0 2 3  2 4 3]
        "point3 P" [-2.5 -1.5 1   0.3 -1.2 1.5   0.6 1.4 1.8   -2.0 0.9 0.6   1.8 2.0 0.2]
AttributeEnd
AttributeBegin
  AreaLightSource "diffuse" "rgb L" [0 0 1] "bool twosided" true
  Shape "bilinearmesh" "point3 P" [-3 -2 -3   3.5 -2 -3   -3 2.5 -3   3.5 2.5 -3]
AttributeEnd
''', {})
add("camera", "Wide field of view on a 2:1 image",
    "fov 100 on a 240x120 image with spheres near the corners and edges (strong perspective stretching). "
    "Checks the fov convention (fov spans the shorter, vertical axis) and the screen-window mapping for non-square images.",
    '''LookAt 0 0 4   0 0 0   0 1 0
Camera "perspective" "float fov" [100]
{head}WorldBegin
Material "diffuse" "rgb reflectance" [0 0 0]
AttributeBegin
  AreaLightSource "diffuse" "rgb L" [1 0 0]
  Shape "sphere" "float radius" [0.6]
AttributeEnd
AttributeBegin
  Translate -7 3.2 -1
  AreaLightSource "diffuse" "rgb L" [0 1 0]
  Shape "sphere" "float radius" [0.7]
AttributeEnd
AttributeBegin
  Translate 7.5 -3.3 -1.5
  AreaLightSource "diffuse" "rgb L" [0 0 1]
  Shape "sphere" "float radius" [0.7]
AttributeEnd
AttributeBegin
  Translate 4 0 0
  AreaLightSource "diffuse" "rgb L" [0 1 0]
  Shape "sphere" "float radius" [0.5]
AttributeEnd
''', {}, res=(240, 120))
add("camera", "Narrow field of view on a portrait image",
    "fov 15 on a portrait 120x200 image. For portrait images the fov spans the horizontal axis; "
    "the red sphere touches the left border, so a wrong axis convention changes the framing.",
    '''LookAt 0 0 20   0 0 0   0 1 0
Camera "perspective" "float fov" [15]
{head}WorldBegin
Material "diffuse" "rgb reflectance" [0 0 0]
AttributeBegin
  Translate -1.15 0 0
  AreaLightSource "diffuse" "rgb L" [1 0 0]
  Shape "sphere" "float radius" [1.2]
AttributeEnd
AttributeBegin
  Translate 1.1 2.3 0
  AreaLightSource "diffuse" "rgb L" [0 0 1]
  Shape "sphere" "float radius" [0.9]
AttributeEnd
''', {}, res=(120, 200))
add("camera", "Orthographic camera with a screen window",
    "Orthographic camera with screenwindow [-4 4 -3 3]: two equal spheres at very different depths must have the same "
    "projected size, plus a tilted triangle.",
    '''LookAt 2 3 10   0 0 0   0 1 0
Camera "orthographic" "float screenwindow" [-4 4 -3 3]
{head}WorldBegin
Material "diffuse" "rgb reflectance" [0 0 0]
AttributeBegin
  Translate -2 0 3
  AreaLightSource "diffuse" "rgb L" [1 0 0]
  Shape "sphere" "float radius" [1]
AttributeEnd
AttributeBegin
  Translate 2 0.5 -6
  AreaLightSource "diffuse" "rgb L" [0 1 0]
  Shape "sphere" "float radius" [1]
AttributeEnd
AttributeBegin
  AreaLightSource "diffuse" "rgb L" [0 0 1] "bool twosided" true
  Shape "trianglemesh" "integer indices" [0 1 2] "point3 P" [-1 -2.5 0   3 -2 -2   0.5 1.5 1]
AttributeEnd
''', {})
add("camera", "Transform semantics",
    "Camera mirrored with Scale -1 1 1 before LookAt, nested AttributeBegin blocks, Rotate about an oblique axis, "
    "non-uniform Scale (an ellipsoid) and a ConcatTransform shear. A different matrix convention moves or reshapes the objects.",
    '''Scale -1 1 1
LookAt 1 2 9   0 0 0   0 1 0
Camera "perspective" "float fov" [42]
{head}WorldBegin
Material "diffuse" "rgb reflectance" [0 0 0]
AttributeBegin
  Translate 2 0 0
  Rotate 30 1 1 0
  AttributeBegin
    Scale 2 0.6 1
    AreaLightSource "diffuse" "rgb L" [1 0 0]
    Shape "sphere" "float radius" [1]
  AttributeEnd
AttributeEnd
AttributeBegin
  ConcatTransform [1 0 0 0   0.4 1 0 0   0 0 1 0   -2.5 1 0 1]
  AreaLightSource "diffuse" "rgb L" [0 1 0] "bool twosided" true
  Shape "trianglemesh" "integer indices" [0 1 2 0 2 3] "point3 P" [-1 -1 0  1 -1 0  1 1 0  -1 1 0]
AttributeEnd
AttributeBegin
  Translate 0 -2 1
  Rotate -60 0 0 1
  Scale 0.5 1.5 0.5
  AreaLightSource "diffuse" "rgb L" [0 0 1]
  Shape "sphere" "float radius" [1]
AttributeEnd
''', {})
add("camera", "Surface orientation of one-sided emitters",
    "One-sided emissive discs: the left (red) disc faces the camera and must be visible; the right (green) disc is "
    "identical but under ReverseOrientation, so it emits away from the camera and must render black. "
    "The tilted blue disc checks the disc transform.",
    '''LookAt 0 0 8   0 0 0   0 1 0
Camera "perspective" "float fov" [40]
{head}WorldBegin
Material "diffuse" "rgb reflectance" [0 0 0]
AttributeBegin
  Translate -2.2 0 0
  AreaLightSource "diffuse" "rgb L" [1 0 0]
  Shape "disk" "float radius" [1]
AttributeEnd
AttributeBegin
  Translate 2.2 0 0
  ReverseOrientation
  AreaLightSource "diffuse" "rgb L" [0 1 0]
  Shape "disk" "float radius" [1]
AttributeEnd
AttributeBegin
  Translate 0 1.5 0
  Rotate 50 1 0 0
  AreaLightSource "diffuse" "rgb L" [0 0 1]
  Shape "disk" "float radius" [0.8]
AttributeEnd
''', {})

# ------------------------------------------------------------ shading
SH_HEAD = '''Film "rgb" "integer xresolution" [{w}] "integer yresolution" [{h}] "string filename" "testsuite_ref_{n}.pfm"
Sampler "independent" "integer pixelsamples" [{spp}]
PixelFilter "box"
Integrator "path" "integer maxdepth" [{md}]
'''


def shading(title, desc, body, md, spp=256, files=None, rel=0.06, abs_=0.005):
    add("shading", title, desc, body,
        {"maxdepth": md, "spp": spp, "ref_spp": 8192, "radius": 2, "rel_tol": rel, "abs_tol": abs_, "pixels": []},
        files=files, res=(160, 120))


shading("Lambertian BRDF under a point light",
        "Grey diffuse sphere on a grey floor lit by one point light, direct lighting only (PBRT maxdepth 1). "
        "Expected radiance is reflectance/pi * I cos(theta) / d^2: checks BRDF normalisation, inverse-square falloff "
        "and the hard shadow.",
        '''LookAt 0 3 6   0 0.6 0   0 1 0
Camera "perspective" "float fov" [45]
{head}WorldBegin
LightSource "point" "point3 from" [2 5 3] "rgb I" [40 40 40]
Material "diffuse" "rgb reflectance" [0.5 0.5 0.5]
Shape "bilinearmesh" "point3 P" [-5 0 -5   5 0 -5   -5 0 5   5 0 5]
AttributeBegin
  Translate 0 1 0
  Material "diffuse" "rgb reflectance" [0.8 0.8 0.8]
  Shape "sphere" "float radius" [1]
AttributeEnd
''', 1)
shading("Area light and soft shadows",
        "A 2x2 rectangular area light above a diffuse sphere and floor, direct lighting only. Pixels in the umbra, the "
        "penumbra and the fully lit floor check the area-light estimator (cosine terms, 1/d^2, one-sided emission).",
        '''LookAt 0 4 6   0 0.5 0   0 1 0
Camera "perspective" "float fov" [45]
{head}WorldBegin
AttributeBegin
  AreaLightSource "diffuse" "rgb L" [8 8 8]
  Material "diffuse" "rgb reflectance" [0 0 0]
  Shape "bilinearmesh" "point3 P" [-1 4 -1   1 4 -1   -1 4 1   1 4 1]
AttributeEnd
Material "diffuse" "rgb reflectance" [0.6 0.6 0.6]
Shape "bilinearmesh" "point3 P" [-6 0 -6   6 0 -6   -6 0 6   6 0 6]
AttributeBegin
  Translate 0.3 0.8 0.2
  Material "diffuse" "rgb reflectance" [0.7 0.3 0.2]
  Shape "sphere" "float radius" [0.8]
AttributeEnd
''', 1)
shading("Coloured reflectances under a distant light",
        "Saturated red, green and blue diffuse spheres under a distant light. Checks the RGB albedo pipeline against "
        "PBRT's spectral upsampling (small differences are expected for saturated colours).",
        '''LookAt 0 2 7   0 0.6 0   0 1 0
Camera "perspective" "float fov" [40]
{head}WorldBegin
LightSource "distant" "point3 from" [1 3 2] "point3 to" [0 0 0] "rgb L" [3 3 3]
Material "diffuse" "rgb reflectance" [0.4 0.4 0.4]
Shape "bilinearmesh" "point3 P" [-6 0 -6   6 0 -6   -6 0 6   6 0 6]
AttributeBegin
  Translate -2 0.8 0
  Material "diffuse" "rgb reflectance" [0.8 0.1 0.1]
  Shape "sphere" "float radius" [0.8]
AttributeEnd
AttributeBegin
  Translate 0 0.8 0
  Material "diffuse" "rgb reflectance" [0.1 0.8 0.1]
  Shape "sphere" "float radius" [0.8]
AttributeEnd
AttributeBegin
  Translate 2 0.8 0
  Material "diffuse" "rgb reflectance" [0.1 0.1 0.8]
  Shape "sphere" "float radius" [0.8]
AttributeEnd
''', 1)
shading("Image and procedural textures",
        "quadrants.pfm (red bottom-left, green bottom-right, blue top-left, white top-right) on an upright quad and a "
        "6x6 checkerboard on the floor. Checks bilinear-patch uv coordinates, the image v-flip convention and texture scaling.",
        '''LookAt 0 1.5 6   0 1.2 0   0 1 0
Camera "perspective" "float fov" [45]
{head}WorldBegin
LightSource "point" "point3 from" [0 3 4] "rgb I" [30 30 30]
Texture "quad" "spectrum" "imagemap" "string filename" "quadrants.pfm"
Texture "chk" "spectrum" "checkerboard" "float uscale" [6] "float vscale" [6] "rgb tex1" [0.8 0.8 0.8] "rgb tex2" [0.1 0.1 0.1]
Material "diffuse" "texture reflectance" "chk"
Shape "bilinearmesh" "point3 P" [-4 0 -4   4 0 -4   -4 0 4   4 0 4]
Material "diffuse" "texture reflectance" "quad"
Shape "bilinearmesh" "point3 P" [-1.5 0.5 -1   1.5 0.5 -1   -1.5 2.5 -1   1.5 2.5 -1]
''', 1, files={"quadrants.pfm": "quadrants"})
shading("Indirect illumination in a Cornell box",
        "Closed Cornell box with red/green side walls and a small ceiling light, PBRT maxdepth 3 (two indirect bounces). "
        "Checks colour bleeding and indirect light in shadowed regions; noisier, so the tolerance is wider.",
        '''LookAt 0 1 3.4   0 1 0   0 1 0
Camera "perspective" "float fov" [40]
{head}WorldBegin
AttributeBegin
  AreaLightSource "diffuse" "rgb L" [18 16 12]
  Material "diffuse" "rgb reflectance" [0 0 0]
  Shape "bilinearmesh" "point3 P" [-0.3 1.99 -0.3   0.3 1.99 -0.3   -0.3 1.99 0.3   0.3 1.99 0.3]
AttributeEnd
Material "diffuse" "rgb reflectance" [0.75 0.75 0.75]
Shape "bilinearmesh" "point3 P" [-1 0 -1   1 0 -1   -1 0 1   1 0 1]
Shape "bilinearmesh" "point3 P" [-1 2 1   1 2 1   -1 2 -1   1 2 -1]
Shape "bilinearmesh" "point3 P" [-1 0 -1   -1 2 -1   1 0 -1   1 2 -1]
Material "diffuse" "rgb reflectance" [0.65 0.06 0.05]
Shape "bilinearmesh" "point3 P" [-1 0 -1   -1 0 1   -1 2 -1   -1 2 1]
Material "diffuse" "rgb reflectance" [0.12 0.5 0.08]
Shape "bilinearmesh" "point3 P" [1 0 1   1 0 -1   1 2 1   1 2 -1]
AttributeBegin
  Translate -0.35 0.4 -0.3
  Material "diffuse" "rgb reflectance" [0.75 0.75 0.75]
  Shape "sphere" "float radius" [0.4]
AttributeEnd
''', 3, spp=512, rel=0.10, abs_=0.015)
shading("Dielectric refraction",
        "A glass sphere (eta 1.5) in front of a checkerboard wall lit by an area light (PBRT maxdepth 6). "
        "Pixels seen through the sphere check refraction directions and Fresnel weighting. Pixels inside the caustic are "
        "avoided, because the CGR26 DRT does not render caustics by default.",
        '''LookAt 0 1 5   0 1 0   0 1 0
Camera "perspective" "float fov" [40]
{head}WorldBegin
AttributeBegin
  AreaLightSource "diffuse" "rgb L" [6 6 6]
  Material "diffuse" "rgb reflectance" [0 0 0]
  Shape "bilinearmesh" "point3 P" [-1.5 4 1   1.5 4 1   -1.5 4 3   1.5 4 3]
AttributeEnd
Texture "chk" "spectrum" "checkerboard" "float uscale" [8] "float vscale" [8] "rgb tex1" [0.8 0.8 0.8] "rgb tex2" [0.15 0.15 0.5]
Material "diffuse" "texture reflectance" "chk"
Shape "bilinearmesh" "point3 P" [-4 -1 -2   4 -1 -2   -4 5 -2   4 5 -2]
Material "diffuse" "rgb reflectance" [0.5 0.5 0.5]
Shape "bilinearmesh" "point3 P" [-4 0 -2   4 0 -2   -4 0 4   4 0 4]
AttributeBegin
  Translate 0 1 0.5
  Material "dielectric" "float eta" [1.5]
  Shape "sphere" "float radius" [0.7]
AttributeEnd
''', 6, spp=256, rel=0.08, abs_=0.015)

# ------------------------------------------------------------ error handling
ERR_BASE = '''LookAt 0 1 5   0 0 0   0 1 0
Camera "perspective" "float fov" [40]
Film "rgb" "integer xresolution" [64] "integer yresolution" [48] "string filename" "testsuite_ref_{n}.pfm"
Sampler "independent" "integer pixelsamples" [4]
Integrator "path" "integer maxdepth" [1]
WorldBegin
LightSource "point" "point3 from" [2 4 3] "rgb I" [20 20 20]
Material "diffuse" "rgb reflectance" [0.5 0.5 0.5]
Shape "sphere" "float radius" [1]
'''


def err(title, desc, scene, keywords, files=None):
    """scene: full scene text; the line containing '#ERR' (marker removed) is the faulty line."""
    lines = scene.split("\n")
    line = next((i + 1 for i, l in enumerate(lines) if "#ERR" in l), None)
    scene = "\n".join(l.replace("  #ERR", "").replace(" #ERR", "") for l in lines)
    add("error", title, desc, scene, {"keywords": keywords, "line": line}, files=files)


def base_with(extra, film=None):
    s = ERR_BASE
    if film:
        s = s.replace('Film "rgb" "integer xresolution" [64] "integer yresolution" [48]', film)
    return s + extra


err("Unknown directive", "A misspelled directive ('Shpae'). The renderer must name the unknown directive and continue.",
    base_with('Shpae "sphere" "float radius" [0.5]  #ERR\n'), ["Shpae"])
err("Unknown shape type", "Shape \"cube\" is not a PBRT shape. The shape must be reported and skipped.",
    base_with('Shape "cube" "float side" [1]  #ERR\n'), ["cube"])
err("Malformed transform", "Translate with only two numbers.",
    base_with('Translate 1 2  #ERR\nShape "sphere" "float radius" [0.3]\n'), ["Translate"])
err("Singular transform", "Scale 0 1 1 makes the transformation matrix singular (not invertible).",
    base_with('AttributeBegin\nScale 0 1 1  #ERR\nShape "sphere" "float radius" [0.5]\nAttributeEnd\n'), ["singular", "Scale"])
err("Non-numeric parameter value", "\"float radius\" [abc]: a parameter value that is not a number.",
    base_with('Shape "sphere" "float radius" [abc]  #ERR\n'), ["abc"])
err("Missing texture file", "An imagemap texture whose file does not exist.",
    base_with('Texture "t" "spectrum" "imagemap" "string filename" "missing_texture.pfm"  #ERR\n'
              'Material "diffuse" "texture reflectance" "t"\nShape "sphere" "float radius" [0.5]\n'), ["missing_texture.pfm"])
err("Missing included file", "Include of a scene file that does not exist.",
    base_with('Include "does_not_exist.pbrt"  #ERR\n'), ["does_not_exist.pbrt"])
err("Out-of-range parameter: negative radius", "A sphere with a negative radius.",
    base_with('Shape "sphere" "float radius" [-2]  #ERR\n'), ["radius"])
err("Out-of-range parameter: field of view", "Camera fov of 200 degrees (must be in (0, 180)).",
    ERR_BASE.replace('Camera "perspective" "float fov" [40]', 'Camera "perspective" "float fov" [200]  #ERR'), ["fov"])
err("Unterminated string", "A quoted string that is never closed.",
    base_with('Shape "sphere  #ERR\n'), ["unterminated", "string"])
err("Missing closing bracket", "A parameter value list opened with '[' but never closed.",
    base_with('LightSource "point" "point3 from" [0 4 0] "rgb I" [5 5 5  #ERR\nShape "sphere" "float radius" [0.3]\n'),
    ["unterminated", "bracket", "'['", "missing ']'"])
err("Wrong number of parameter values", "\"rgb reflectance\" with only two values.",
    base_with('Material "diffuse" "rgb reflectance" [0.5 0.5]  #ERR\nShape "sphere" "float radius" [0.5]\n'), ["reflectance"])
err("Unmatched AttributeEnd", "An AttributeEnd without a matching AttributeBegin.",
    base_with('AttributeEnd  #ERR\n'), ["AttributeEnd"])
err("Invalid film resolution", "A negative xresolution.",
    ERR_BASE.replace('"integer xresolution" [64]', '"integer xresolution" [-100]').replace(
        'Film "rgb"', 'Film "rgb"', 1).replace('"string filename" "testsuite_ref_{n}.pfm"', '"string filename" "testsuite_ref_{n}.pfm"  #ERR'),
    ["resolution", "xresolution"])
err("Vertex index out of range", "A trianglemesh whose indices refer to a vertex that does not exist.",
    base_with('Shape "trianglemesh" "integer indices" [0 1 7]  #ERR\n      "point3 P" [0 0 0  1 0 0  0 1 0]\n'),
    ["out of range", "index", "indices"])
err("Undefined named material", "NamedMaterial refers to a material that was never defined.",
    base_with('NamedMaterial "nosuchmaterial"  #ERR\nShape "sphere" "float radius" [0.5]\n'), ["nosuchmaterial"])
err("Unknown material type", "Material \"velvet\" is not a PBRT material.",
    base_with('Material "velvet" "rgb reflectance" [0.5 0.5 0.5]  #ERR\nShape "sphere" "float radius" [0.5]\n'), ["velvet"])


# ------------------------------------------------------------ write
def write_pfm(path, w, h, f):
    with open(path, "wb") as o:
        o.write(b"PF\n%d %d\n-1.0\n" % (w, h))
        for y in range(h):          # PFM rows are stored bottom-to-top
            for x in range(w):
                o.write(struct.pack("<3f", *f(x, y, w, h)))


def quadrants(x, y, w, h):          # y counts from the bottom row in PFM storage
    left, bottom = x < w // 2, y < h // 2
    if bottom:
        return (0.8, 0.1, 0.1) if left else (0.1, 0.8, 0.1)
    return (0.1, 0.1, 0.8) if left else (0.85, 0.85, 0.85)


for n, t in enumerate(tests, 1):
    d = os.path.join(ROOT, "test%d" % n)
    os.makedirs(d, exist_ok=True)
    w, h = t["res"]
    head = (CAM_HEAD if t["cat"] == "camera" else SH_HEAD if t["cat"] == "shading" else "").format(
        w=w, h=h, n=n, spp=t["cfg"].get("spp", 0), md=t["cfg"].get("maxdepth", 1))
    body = t["scene"].replace("{head}", head).replace("{n}", str(n))
    comment = "# testscene_%d (%s): %s\n# %s\n" % (n, t["cat"], t["title"], t["desc"])
    with open(os.path.join(d, "testscene_%d.pbrt" % n), "w") as o:
        o.write(comment + body)
    cfg = {"name": "test%d" % n, "category": t["cat"], "title": t["title"], "description": t["desc"],
           "scene": "testscene_%d.pbrt" % n}
    if t["cfg"].get("line"):
        # the faulty line number in the written file (comment adds 2 lines)
        t["cfg"]["line"] += 2
    cfg.update({k: v for k, v in t["cfg"].items() if k not in ("spp", "maxdepth")})
    old = os.path.join(d, "test.json")
    if os.path.exists(old) and json.load(open(old)).get("pixels"):
        cfg["pixels"] = json.load(open(old))["pixels"]  # keep hand-picked pixels
    if t["cat"] == "camera":
        cfg.setdefault("max_mismatch_percent", 0.25)
    with open(os.path.join(d, "test.json"), "w") as o:
        json.dump(cfg, o, indent=2)
        o.write("\n")
    for fn, kind in t["files"].items():
        if kind == "quadrants":
            write_pfm(os.path.join(d, fn), 64, 64, quadrants)
    with open(os.path.join(d, "run.sh"), "w") as o:
        o.write("#!/usr/bin/env bash\n# Runs only this test; extra arguments are passed to runAllTests.py\n"
                "# (e.g. --renderer \"/path/to/renderer {scene}\").\n"
                "cd \"$(dirname \"$0\")/..\" && exec python3 runAllTests.py --only test%d \"$@\"\n" % n)
    os.chmod(os.path.join(d, "run.sh"), 0o755)
print(len(tests), "tests written")
