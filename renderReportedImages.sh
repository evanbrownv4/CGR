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

echo "== Done in $(( $(date +%s) - start )) s, $failed failure(s) =="
exit $failed
