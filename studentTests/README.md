# CGR 2026 student test suite

30 self-contained tests in three categories. Each one compares a renderer's output
against PBRT-v4.

| Tests | Category | What is checked | Pass criterion |
|-------|----------|-----------------|----------------|
| test1 – test7 | **camera** (cameras and intersections) | Object boundaries and framing for perspective and orthographic cameras, field of view, non-square images, transforms, triangles, quads, discs and surface orientation | Emissive red/green/blue objects on black, rendered with 1 sample at each pixel centre. Each pixel is labelled background, red, green or blue. At most `max_mismatch_percent` (0.25 %) of the pixels may have a different label from the PBRT reference. |
| test8 – test13 | **shading** | Lambertian BRDF with point, area and distant lights; coloured albedo; image and checkerboard textures; indirect light (Cornell box); dielectric refraction | Mean linear radiance in a 5×5 window around each listed pixel. Every channel must be within `rel_tol` (6–10 %) **or** `abs_tol` of the reference. |
| test14 – test30 | **error** handling | 17 scenes, each with one deliberate error: unknown directive, unknown shape, malformed or singular transform, non-numeric value, missing texture or include file, negative radius, fov out of range, unterminated string, missing `]`, wrong value count, unmatched `AttributeEnd`, negative resolution, vertex index out of range, undefined named material, unknown material | The renderer must neither crash nor hang (timeout), and its log, stdout or stderr must mention one of the test's `keywords`. The log also reports whether the faulty line number is named. |

## Folder contents

Each `testN/` folder contains:

| File | Purpose |
|------|---------|
| `testscene_N.pbrt` | The scene. Its header comment describes the test. Camera and shading scenes use only standard PBRT-v4 syntax. |
| `test.json` | Category, description and pass thresholds. For shading tests it also lists the pixels to compare; for error tests, the keywords and the faulty line. |
| `testsuite_ref_N.pfm` / `.png` | Reference image rendered by PBRT-v4 (camera and shading tests). The `.pfm` is linear and is the one compared; the `.png` is for viewing. |
| `testsuite_ref_N.log` | PBRT-v4's output for the scene. For error tests this records how PBRT itself reacts. |
| `cgr26_expected_N.log` | Error tests only: the log CGR26_renderer produced, as an example of the expected diagnostics. |
| `run.sh` | Runs this test alone (`./run.sh`). |
| `quadrants.pfm` | test11 only: the image texture. |

## Running

You need Python 3; no extra packages are required.

```bash
./runAllTests.sh
```

This is the same as `python3 runAllTests.py`. It runs every test in order and writes:

* the rendered images `testsuite_my_N.*`
* the camera mismatch images `testsuite_diff_N.ppm`
* the log `test_results.log` (per-test details and a pass/fail/skip summary per category)
* `test_results.csv`

All outputs go to `../output/tests/` by default. Useful options:

```bash
python3 runAllTests.py --category shading      # only one category
python3 runAllTests.py --only test3,test21     # selected tests
python3 runAllTests.py --outdir /tmp/results   # different output folder
```

### Testing a different renderer

Pass a command template with `{scene}`, `{outdir}` and `{name}` placeholders:

```bash
python3 runAllTests.py --renderer "/path/to/my_renderer {scene} -o {outdir}/{name}.png"
```

* **Output image:** the renderer should write `{outdir}/{name}.pfm` (preferred, linear radiance)
  or `.png`/`.ppm`. If no file with that name appears, the newest image written to
  `{outdir}` or the test folder during the run is used. PNG and PPM are treated as sRGB and
  decoded to linear values, so PFM output gives the most accurate shading comparison.
* **Diagnostics:** for error tests the runner reads `{outdir}/{name}.log` (or the newest
  `.log` file) together with stdout and stderr.
* **Working directory:** the renderer is run inside the test folder, and `{scene}` is an
  absolute path.
* **Integrator:** the scenes use PBRT's `Integrator "path"`, where `maxdepth 1` means direct
  lighting only. A renderer that names its integrators differently should map `"path"` to
  its own path or distributed tracer.

## Generating the PBRT-v4 references

```bash
PBRT=/path/to/pbrt IMGTOOL=/path/to/imgtool ./makeReferences.sh            # all tests
PBRT=/path/to/pbrt ./makeReferences.sh test9 test12                         # selected tests
```

* Camera tests render at 1 sample per pixel; shading tests at `ref_spp` (8192), so the
  reference is almost noise free.
* `IMGTOOL` is optional and only used to write the `.png` previews, with
  `imgtool convert in.pfm --outfile out.png`. It is PBRT-v4's own `imgtool`, built next to `pbrt`
  but often not installed with it. If `IMGTOOL` is not set, the script looks next to the `pbrt`
  executable and in `~/pbrt-v4/build/`. Do not use the `imgtool` from the Ubuntu package
  `mame-tools`: it is an unrelated program.
* To write previews for references that already exist, without re-rendering:

  ```bash
  IMGTOOL=/path/to/imgtool ./makeReferences.sh --previews
  ```
* Tests whose reference image is missing are reported as SKIP.

## Known differences between CGR26_renderer and PBRT-v4

* **Light paths:** PBRT renders caustics (for example light focused through the glass sphere
  in test13). CGR26's distributed tracer skips caustic paths unless `"bool caustics" true`,
  so test13 only samples pixels outside the caustic.
* **Colour:** PBRT is spectral. Saturated RGB reflectances are upsampled to spectra, which can
  shift colours by a few percent (test10).
* **Noise:** at 256 to 512 samples, CGR26's estimates have a few percent of Monte Carlo noise.
  The 5×5 averaging window and the tolerances allow for this. The noise was measured at
  ≤ 1.3 % against a 4096-sample render.

## Regenerating the test folders

`generateTests.py` rewrites every `testN/` folder (scene, `test.json`, `run.sh`) from one
definition file. Pixel lists already chosen in a `test.json` are kept.
