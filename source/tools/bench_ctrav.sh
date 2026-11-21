#!/usr/bin/env bash
# Sweeps the BVH's SAH traversal cost C_trav over 1/8 ... 8/8 (with leaves of up to 4
# and up to 16 primitives allowed) and records tree size, intersection work per ray
# and render time (best of N runs, default 3) for each scene.
# Usage (from the repository root): source/tools/bench_ctrav.sh [runs]
set -u
export LC_ALL=C   # decimal point, not comma, in the C_trav values passed to the renderer
cd "$(dirname "$0")/../.."
runs=${1:-3}
exe=output/CGR26_renderer
if [ ! -x "$exe" ] || ! "$exe" --help 2>&1 | grep -q -- "--ctrav"; then
    echo "error: $exe is missing or older than this script (no --ctrav option); rebuild with: make -C source" >&2
    exit 1
fi
csv=output/ctrav_sweep.csv
echo "scene,resolution,maxleaf,ctrav,nodes,leaves,depth,build_s,tests_per_ray,nodes_per_ray,render_s_best" > "$csv"
for spec in "acc_best 1280 960" "acc_worst 480 360" "acc_complex 640 400" "DRT_simple 320 240"; do
    set -- $spec
    scene=$1; w=$2; h=$3
    for maxleaf in 4 16; do
        for k in 1 2 3 4 5 6 7 8; do
            ctrav=$(awk -v k=$k 'BEGIN { printf "%.3f", k / 8 }')
            best=""
            for r in $(seq "$runs"); do
                if ! "$exe" "scenes/$scene.pbrt" --quiet --resolution "$w" "$h" --ctrav "$ctrav" --maxleaf "$maxleaf" \
                    --output ctrav_tmp > output/ctrav_tmp.out 2>&1; then
                    echo "render failed: $scene (C_trav $ctrav, maxleaf $maxleaf); renderer output:" >&2
                    tail -5 output/ctrav_tmp.out >&2
                    exit 1
                fi
                t=$(sed -n 's/.*render: \([0-9.]*\) s.*/\1/p' output/ctrav_tmp.log)
                best=$(awk -v a="$best" -v b="$t" 'BEGIN { print (a == "" || b < a) ? b : a }')
            done
            log=output/ctrav_tmp.log
            nodes=$(sed -n 's/.*BVH: [0-9]* bounded primitives, \([0-9]*\) nodes.*/\1/p' $log)
            leaves=$(sed -n 's/.* nodes, \([0-9]*\) leaves.*/\1/p' $log)
            depth=$(sed -n 's/.* leaves, depth \([0-9]*\).*/\1/p' $log)
            build=$(sed -n 's/.*acceleration structure build: \([0-9.]*\) s.*/\1/p' $log)
            tpr=$(sed -n 's/.*tests (\([0-9.]*\) per ray).*/\1/p' $log)
            npr=$(sed -n 's/.*node visits (\([0-9.]*\) per ray).*/\1/p' $log)
            echo "$scene,${w}x$h,$maxleaf,$ctrav,$nodes,$leaves,$depth,$build,$tpr,$npr,$best" | tee -a "$csv"
        done
    done
done
rm -f output/ctrav_tmp.*
echo "results written to $csv"
