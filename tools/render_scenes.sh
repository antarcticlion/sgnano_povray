#!/bin/sh
# Render all test scenes with the host POV-Ray. No checkpointing.
# Usage: tools/render_scenes.sh [width] [height] [outdir]
set -eu
cd "$(dirname "$0")/.."

W=${1:-320}
H=${2:-240}
OUT=${3:-build_host/scenes}
mkdir -p "$OUT"

for f in tests/scenes/*.pov; do
    b=$(basename "$f" .pov)
    echo "== $b (${W}x${H})"
    ./build_host/povray_host \
        +I"$f" +O"$OUT/$b.png" +W"$W" +H"$H" +FN -A \
        +Lthird_party/povray/include
done

echo "rendered to $OUT"
