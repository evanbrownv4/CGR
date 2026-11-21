#!/usr/bin/env bash
# Builds the renderer and renders every scene in scenes/ into output/.
# Usage: ./renderReportedImages.sh [name-filter]
#   e.g. ./renderReportedImages.sh sampler_   renders only the sampler comparison scenes
set -u
cd "$(dirname "$0")"

echo "== Building =="
make -C source -j4 || { echo "build failed"; exit 1; }

filter="${1:-}"
failed=0
start=$(date +%s)
for scene in scenes/*.pbrt; do
    name=$(basename "$scene" .pbrt)
    if [ -n "$filter" ] && [[ "$name" != *"$filter"* ]]; then continue; fi
    echo "== Rendering $name =="
    if ! output/CGR26_renderer "$scene" --quiet; then
        echo "   FAILED: $scene (see output/$name.log)"
        failed=$((failed + 1))
    fi
    grep -h "TIMING" "output/$name.log" 2>/dev/null | sed 's/^\[INFO\] */   /'
done

# Quantitative sampler comparison (RMSE of linear radiance against the reference).
if [ -f output/sampler_ref.pfm ] && { [ -z "$filter" ] || [[ "sampler_" == *"$filter"* ]] || [[ "$filter" == sampler* ]]; }; then
    summary=output/sampler_rmse.txt
    echo "Sampler comparison: RMSE against output/sampler_ref.pfm" > "$summary"
    for s in unif grid halton; do
        for n in 4 16 64; do
            f="output/sampler_${s}${n}.pfm"
            [ -f "$f" ] || continue
            rmse=$(output/CGR26_renderer --compare output/sampler_ref.pfm "$f" | grep "RMSE (all" | awk '{print $NF}')
            printf "%-8s %3s samples  RMSE %s\n" "$s" "$n" "$rmse" >> "$summary"
        done
    done
    echo "== Sampler RMSE summary ($summary) =="
    cat "$summary"
fi

# Acceleration structure benchmark: each acc_* scene with and without the BVH at the
# same resolution. acc_complex is benchmarked at a reduced resolution because brute
# force on ~280k triangles takes far too long at full size.
if [ -z "$filter" ] || [[ "$filter" == acc* ]]; then
    table=output/acc_timings.txt
    {
        echo "Acceleration benchmark (render time only, $(nproc) threads)"
        printf "%-12s %-10s %10s %12s %12s %9s %14s %14s %14s\n" scene resolution primitives "BVH (s)" "brute (s)" speedup "tests/ray BVH" "tests/ray brute" "nodes/ray BVH"
    } > "$table"
    for spec in "acc_best 480 360" "acc_worst 480 360" "acc_complex 64 40"; do
        set -- $spec
        scene=$1; w=$2; h=$3
        [ -f "scenes/$scene.pbrt" ] || continue
        echo "== Benchmarking $scene (${w}x${h}) =="
        for mode in bvh brute; do
            extra=""; [ "$mode" = brute ] && extra="--nobvh"
            output/CGR26_renderer "scenes/$scene.pbrt" --quiet --resolution "$w" "$h" --output "${scene}_bench_$mode" $extra
        done
        get() { sed -n "$2" "output/${scene}_bench_$1.log" | head -1; }
        prims=$(get bvh 's/.*scene: \([0-9]*\) primitives.*/\1/p')
        tb=$(get bvh 's/.*render: \([0-9.]*\) s.*/\1/p')
        tf=$(get brute 's/.*render: \([0-9.]*\) s.*/\1/p')
        pb=$(get bvh 's/.*tests (\([0-9.]*\) per ray).*/\1/p')
        pf=$(get brute 's/.*tests (\([0-9.]*\) per ray).*/\1/p')
        nb=$(get bvh 's/.*node visits (\([0-9.]*\) per ray).*/\1/p')
        speed=$(awk -v a="$tf" -v b="$tb" 'BEGIN { if (b > 0) printf "%.2fx", a / b; else print "-" }')
        printf "%-12s %-10s %10s %12s %12s %9s %14s %14s %14s\n" "$scene" "${w}x${h}" "$prims" "$tb" "$tf" "$speed" "$pb" "$pf" "$nb" >> "$table"
    done
    echo "== Acceleration benchmark ($table) =="
    cat "$table"
fi

echo "== Done in $(( $(date +%s) - start )) s, $failed failure(s) =="
exit $failed
