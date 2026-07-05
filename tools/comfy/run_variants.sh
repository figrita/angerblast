#!/usr/bin/env bash
# Chain 7 extra full-game sprite generation runs (same prompts, different seeds).
# Variants land NEXT TO the originals with a run suffix: charhulk_2.png .. charhulk_8.png.
# Pick a winner by renaming it over the unsuffixed file, then delete the rest:
#   mv assets/sprites/02_ember/charhulk_5.png assets/sprites/02_ember/charhulk.png
#   rm assets/sprites/*/*_[2-8].png            # prune before shipping/web builds
set -u
cd "$(dirname "$0")/../.."
for i in 2 3 4 5 6 7 8; do
  echo "=== variant run $i ==="
  python3 -u tools/comfy/gen_all.py --no-toml \
    --suffix "_$i" \
    --seed-offset $((i * 100003))
done
echo "ALL VARIANT RUNS DONE"
