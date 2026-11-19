#!/usr/bin/env bash
# Generates the PBRT-v4 reference image (testsuite_ref_N.pfm, plus a .png for viewing)
# and reference log (testsuite_ref_N.log) for every test, or for the tests named on
# the command line.
#
#   PBRT=/path/to/pbrt IMGTOOL=/path/to/imgtool ./makeReferences.sh [test3 test12 ...]
#   IMGTOOL=/path/to/imgtool ./makeReferences.sh --previews [test3 ...]
#
# --previews only converts existing testsuite_ref_N.pfm files to PNG, without
# re-rendering. The PNG previews need PBRT-v4's imgtool, which is built next to
# pbrt (e.g. ~/pbrt-v4/build/imgtool) but is often not installed with it. If
# IMGTOOL is not set, the script looks next to the pbrt executable and in
# ~/pbrt-v4/build. (The "imgtool" from the Ubuntu package mame-tools is an
# unrelated program and cannot convert PFM files.)
#
# Shading tests are rendered with the "ref_spp" sample count from their test.json
# (--spp), so the reference is nearly noise free. Error-handling tests have no
# reference image: their log records how PBRT itself reacts to the faulty scene.
set -u
cd "$(dirname "$0")"

previews_only=0
if [ "${1:-}" = "--previews" ]; then
    previews_only=1
    shift
fi

PBRT=${PBRT:-pbrt}
if [ -z "${IMGTOOL:-}" ]; then
    IMGTOOL=""
    pbrt_path=$(command -v "$PBRT" 2>/dev/null || true)
    for candidate in "${pbrt_path:+$(dirname "$pbrt_path")/imgtool}" "$HOME/pbrt-v4/build/imgtool"; do
        if [ -n "$candidate" ] && [ -x "$candidate" ]; then
            IMGTOOL=$candidate
            break
        fi
    done
fi
if [ -n "$IMGTOOL" ] && ! command -v "$IMGTOOL" >/dev/null 2>&1; then
    echo "warning: IMGTOOL '$IMGTOOL' not found; PNG previews will not be written" >&2
    IMGTOOL=""
fi
if [ -z "$IMGTOOL" ]; then
    echo "note: PBRT-v4 imgtool not found (set IMGTOOL=/path/to/imgtool); PNG previews will not be written" >&2
    if [ $previews_only -eq 1 ]; then exit 1; fi
fi
if [ $previews_only -eq 0 ] && ! command -v "$PBRT" >/dev/null 2>&1; then
    echo "error: PBRT-v4 executable '$PBRT' not found; set PBRT=/path/to/pbrt" >&2
    exit 1
fi

# Converts testN/testsuite_ref_N.pfm to a PNG preview with PBRT's imgtool.
write_preview() {
    local pfm=$1 png=${1%.pfm}.png out
    [ -n "$IMGTOOL" ] || return 0
    rm -f "$png"
    if out=$("$IMGTOOL" convert "$pfm" --outfile "$png" 2>&1) && [ -f "$png" ]; then
        echo "   preview written ($png)"
    else
        echo "   imgtool could not write the PNG preview: $(echo "$out" | tail -1)"
    fi
}

if [ $# -gt 0 ]; then tests=("$@"); else tests=($(ls -d test*/ | tr -d / | sort -V)); fi
for t in "${tests[@]}"; do
    n=${t#test}
    [ -f "$t/test.json" ] || { echo "skipping $t (no test.json)"; continue; }
    category=$(sed -n 's/.*"category": *"\([a-z]*\)".*/\1/p' "$t/test.json")
    if [ $previews_only -eq 1 ]; then
        if [ -f "$t/testsuite_ref_$n.pfm" ]; then
            echo "$t ($category):"
            write_preview "$t/testsuite_ref_$n.pfm"
        fi
        continue
    fi
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
    echo "$t ($category): reference written ($t/testsuite_ref_$n.pfm)"
    write_preview "$t/testsuite_ref_$n.pfm"
done
