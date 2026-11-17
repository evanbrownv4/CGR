#!/usr/bin/env bash
# Generates the PBRT-v4 reference image (testsuite_ref_N.pfm, plus a .png for viewing)
# and reference log (testsuite_ref_N.log) for every test, or for the tests named on
# the command line.
#
#   PBRT=/path/to/pbrt IMGTOOL=/path/to/imgtool ./makeReferences.sh [test3 test12 ...]
#
# Shading tests are rendered with the "ref_spp" sample count from their test.json
# (--spp), so the reference is nearly noise free. Error-handling tests have no
# reference image: their log records how PBRT itself reacts to the faulty scene.
set -u
cd "$(dirname "$0")"
PBRT=${PBRT:-pbrt}
IMGTOOL=${IMGTOOL:-imgtool}
if ! command -v "$PBRT" >/dev/null 2>&1; then
    echo "error: PBRT-v4 executable '$PBRT' not found; set PBRT=/path/to/pbrt" >&2
    exit 1
fi

if [ $# -gt 0 ]; then tests=("$@"); else tests=($(ls -d test*/ | tr -d / | sort -V)); fi
for t in "${tests[@]}"; do
    n=${t#test}
    [ -f "$t/test.json" ] || { echo "skipping $t (no test.json)"; continue; }
    category=$(sed -n 's/.*"category": *"\([a-z]*\)".*/\1/p' "$t/test.json")
    spp=$(sed -n 's/.*"ref_spp": *\([0-9]*\).*/\1/p' "$t/test.json")
    args=()
    [ -n "$spp" ] && args=(--spp "$spp")
    rm -f "$t/testsuite_ref_$n.pfm" "$t/testsuite_ref_$n.png"
    start=$(date +%s)
    (cd "$t" && "$PBRT" "${args[@]}" --outfile "testsuite_ref_$n.pfm" "testscene_$n.pbrt") > "$t/testsuite_ref_$n.log" 2>&1
    code=$?
    echo "pbrt exit code: $code (${spp:-scene} spp, $(( $(date +%s) - start )) s)" >> "$t/testsuite_ref_$n.log"
    if [ "$category" = "error" ]; then
        rm -f "$t/testsuite_ref_$n.pfm" "$t/testsuite_ref_$n.png"   # PBRT may still render if it accepts the input
        echo "$t ($category): pbrt exit code $code, log in $t/testsuite_ref_$n.log"
        continue
    fi
    if [ $code -ne 0 ] || [ ! -f "$t/testsuite_ref_$n.pfm" ]; then
        echo "$t ($category): PBRT FAILED (exit code $code), see $t/testsuite_ref_$n.log"
        continue
    fi
    if command -v "$IMGTOOL" >/dev/null 2>&1; then
        "$IMGTOOL" convert --outfile "$t/testsuite_ref_$n.png" "$t/testsuite_ref_$n.pfm" >/dev/null 2>&1 ||
            echo "   (imgtool could not write the PNG preview)"
    fi
    echo "$t ($category): reference written ($t/testsuite_ref_$n.pfm)"
done
