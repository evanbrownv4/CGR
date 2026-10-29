# CGR 2026 – physically based renderer (CGR26_renderer)

A C++17 ray tracer written from scratch using only the C++ standard library. It
reads a subset of the PBRT (v4) scene format and provides two integrators:

* **`whitted`**: Whitted-style ray tracer (WSRT)
* **`distributed`**: distributed ray tracer (DRT)

The `Integrator` directive in the scene file selects which one is used.

## Folder layout

| Path | Contents |
|------|----------|
| `featureList.txt` | every implemented feature, one per line |
| `renderReportedImages.sh` | builds the renderer and renders every scene in `scenes/` into `output/` |
| `source/` | all C++ sources (`*.cpp`, `*.h`) and the `Makefile` |
| `source/tools/make_assets.cpp` | stand-alone generator for the PPM textures and OBJ meshes in `scenes/assets/` (not part of the renderer) |
| `scenes/` | one `.pbrt` file per rendered image; `scenes/include/` holds geometry shared by several scenes, `scenes/assets/` the textures and meshes |
| `output/` | rendered images and `.log` files; the executable `CGR26_renderer` is built here |
| `reports/` | module reports (PDF) |
| `studentTests/` | test suite (Module 3) |

## Development / test environment

* Ubuntu 24.04.5 LTS, Linux 6.18 x86_64, Intel Xeon @ 2.10 GHz (4 cores)
* g++ 13.3.0 (`-std=c++17 -O3`), GNU Make 4.3

Any C++17 compiler should work, for example clang ≥ 10 on macOS (Apple Silicon or
Intel) or MinGW / MSVC on Windows. The renderer uses `std::thread` for parallelism
and `std::filesystem` for paths.

## Building

From the top-level folder:

```bash
make -C source -j4
```

This writes the executable to `output/CGR26_renderer` and object files to
`build/`. If you use a compiler other than `g++`, pass it as `CXX`:

```bash
make -C source CXX=clang++
```

Without `make`, run this from inside `source/`:

```bash
g++ -std=c++17 -O3 -pthread main.cpp parser.cpp scene.cpp shapes.cpp lights.cpp integrators.cpp image.cpp logger.cpp -o ../output/CGR26_renderer
```

## Running

```bash
output/CGR26_renderer scenes/WSRT_simple.pbrt
```

The image is written to `output/<name>.png` (or `.ppm`), where `<name>` comes from the
Film `"string filename"` or, if that is not set, the scene file name. The log is written
to `output/<name>.log`. By default the output folder is the folder containing the
executable. The log records:

* every parse warning or error, with `file:line`
* the scene statistics
* the integrator settings
* per-stage timings (`TIMING parse= build= render= output= total=`)
* ray counts and throughput

Options:

| Option | Meaning |
|--------|---------|
| `--outdir <dir>` | write images and logs to `<dir>` instead |
| `--output <name>` | override the output base name |
| `--spp <n>` | override the pixel sample count |
| `--threads <n>` | number of render threads (default: all cores) |
| `--seed <n>` | random seed |
| `--nobvh` | disable the BVH and use brute-force intersection |
| `--hdr` | also write the linear radiance as `<name>.pfm` |
| `--quiet` | print only warnings and errors |

To render every image used in the reports:

```bash
./renderReportedImages.sh
```

Pass a name filter to render a subset, for example `./renderReportedImages.sh aa_`.
The script also writes `output/sampler_rmse.txt`, which lists the RMSE of each
sampler render against the reference.

### Image comparison tool

```bash
output/CGR26_renderer --compare <reference> <test> [--pixel x y]... [--region x0 y0 x1 y1] [--diff out.png [scale]]
```

The tool reads PPM (P3/P6) and PFM images. It prints:

* the RMSE (overall and per channel), the maximum absolute difference and the PSNR
* optionally the RMSE inside a region and the values at chosen pixels

It can also write an absolute-difference image. For quantitative work, compare the
linear `.pfm` images. PBRT's output can be converted with `imgtool convert ref.exr ref.pfm`.

### Running the student tests

The test suite is part of Module 3 and is not included yet. When it is added,
`studentTests/runAllTests.sh` will run every test.

## Supported scene format (PBRT subset)

**Transforms**

* `Translate`, `Scale`, `Rotate`, `LookAt`, `Transform`, `ConcatTransform`, `Identity`
* `CoordinateSystem`, `CoordSysTransform`
* `AttributeBegin`/`AttributeEnd`, `TransformBegin`/`TransformEnd`, `ReverseOrientation`
* As in PBRT the camera space is left-handed. Most scenes therefore start with
  `Scale -1 1 1` so that world +x appears on the right of the image.

**Structure**

* `WorldBegin`, `Include`/`Import`
* `ObjectBegin`/`ObjectEnd`/`ObjectInstance`
* `MakeNamedMaterial`/`NamedMaterial`
* Relative paths are resolved against the folder of the top-level scene file, as in PBRT.

**Camera**

* `"perspective"`: `fov` (spans the shorter image axis, as in PBRT), `lensradius`,
  `focaldistance`, `screenwindow`, `frameaspectratio`
* `"orthographic"`: `screenwindow`

**Film `"rgb"`**

* `xresolution`, `yresolution`, `filename` (`.png` or `.ppm`)
* Renderer-specific parameters:
  * `"string tonemap"` `"reinhard"|"clamp"` (default: `reinhard` for the DRT, `clamp` for the WSRT)
  * `"string tonemapmode"` `"luminance"|"channel"`
  * `"float key"` (default 0.18)
  * `"float whitepoint"` (L_white; ≤ 0 means the maximum scaled luminance)
  * `"float exposure"`
  * `"bool srgb"`
  * `"bool writehdr"` (also write a `.pfm`)

**Sampler** (pixel samples / antialiasing)

* `"stratified"`: `xsamples`, `ysamples`, `jitter`. With `jitter false` it is a regular
  grid of samples at the cell centres, and 1×1 samples the pixel centre.
* `"independent"`: uniform random, `pixelsamples`
* `"halton"`: `pixelsamples`

**Integrator**

* `"whitted"`: `maxdepth` (default 5), `"rgb ambient"` (default 0.05), `rayepsilon`
* `"distributed"`:
  * `maxdepth` (MD, default 3)
  * `branching` (BF, default 4: rays per diffuse/glossy scattering event)
  * `lightsamples` (default 16: shadow rays per area light per shading point)
  * `"string lightsampler"` `"uniform"|"grid"|"halton"|"stratified"`
  * `"bool randomizehalton"` (default true: Cranley-Patterson rotation per shading point)
  * `"string hemisphere"` `"uniform"|"cosine"`
  * `"bool caustics"` (default false)
  * `rayepsilon`

**Shapes**

* `sphere`, `disk`, `trianglemesh` (`P`, `indices`, `N`, `uv`), `bilinearmesh` (quads),
  `loopsubdiv` (renders the control mesh)
* Renderer-specific extensions:
  * `plane`: `"point3 p"`, `"normal n"`. An infinite plane with planar uv coordinates in world units.
  * `objmesh`: `"string filename"`, `"bool smooth"`. A Wavefront OBJ with `v`/`vt`/`vn`/`f`, n-gons are
    fan-triangulated. With `smooth true` and no normals in the file, smooth vertex normals are computed.

**Materials**

* `diffuse`/`matte` (`reflectance`/`Kd`)
* `phong`/`blinnphong`/`plastic` (`Kd`, `Ks`, `exponent`, optional `Ka`, `Kr`)
* `mirror` (`Kr`)
* `conductor` (`reflectance`; a rough conductor becomes a Phong lobe)
* `dielectric`/`glass` (`eta`, `Kr`, `Kt`)
* `coateddiffuse` (approximated)
* Any colour parameter may use `"texture name"` instead of a constant colour.

**Textures**

* `imagemap`: PPM P3/P6 or PFM; `encoding` sRGB/linear/gamma; `filter` bilinear/point; `uscale`,
  `vscale`, `udelta`, `vdelta`
* `checkerboard` (2D)
* `constant`
* `scale`

**Lights**

* `point` (`I`, `from`, `scale`)
* `distant` (`L`, `from`, `to`)
* `infinite` (constant `L`, used as the background radiance)
* `AreaLightSource "diffuse"` (`L`, `scale`, `twosided`) on spheres, discs and meshes.
  A single quad light is sampled with a direct parallelogram mapping, so a grid of
  samples is regular on the light.

**Error handling**

* The following are logged with the file and line number, and the renderer carries on:
  * unknown directives, unterminated strings or brackets
  * malformed parameter declarations and values of the wrong type or count
  * malformed or singular transforms
  * unknown shapes, materials or lights
  * missing texture or mesh files (missing textures are replaced by magenta)
  * out-of-range parameters (clamped or reset to defaults)
  * unused parameters
* A scene file that cannot be opened produces a log and exit code 1.

## Known limitations

* Only PPM/PFM textures are read (no PNG/EXR decoder). Only PNG/PPM/PFM are written.
* PLY meshes, curves, cylinders, partial spheres, media, spectral rendering, motion blur
  and non-box pixel filters are not supported. They are reported in the log.
* Indirect illumination in the DRT samples the hemisphere uniformly (importance sampling
  is a final-phase task). Very glossy materials therefore show some sparkle noise.
* In the WSRT, area lights are approximated by a point light at their centroid.
