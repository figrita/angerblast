#!/usr/bin/env bash
# Chain 7 extra codex/menu icon generation runs (same prompts, different seeds).
# Variants land NEXT TO the originals with a run suffix: wpn_maincannon_2.png .. _8.png.
# Pick winners with tools/comfy/icon_picker.html + apply_picks.py (same format
# as the sprite picker: `common/wpn_maincannon.png = 5`).
set -u
cd "$(dirname "$0")/../.."

# ComfyUI 0.27 leaks VRAM over long batches (~150 prompts -> OOM); restart the
# server before each 53-icon pass. pkill -f is mandatory: plain kill of the
# launcher pid orphans a worker holding all VRAM.
restart_comfy() {
  echo "--- restarting ComfyUI ---"
  pkill -f "main.py --port 8188" || true
  sleep 5
  (cd /home/fig/comfy/ComfyUI && nohup python3 main.py --port 8188 --disable-smart-memory > /tmp/comfyui.log 2>&1 &)
  for t in $(seq 1 60); do
    curl -s -m 2 http://127.0.0.1:8188/system_stats > /dev/null && return 0
    sleep 2
  done
  echo "ComfyUI did not come back up" >&2
  return 1
}

# every run saves the de-outlined icon plus its unaltered twin (_No.png);
# prompting for outlines is useless ("1-pixel" means nothing to the model)
for i in 2 3 4 5 6 7 8; do
  restart_comfy || exit 1
  echo "=== icon variant run $i ==="
  python3 -u tools/comfy/gen_codex_icons.py --no-toml \
    --suffix "_$i" \
    --seed-offset $((i * 100003))
done
echo "ALL ICON VARIANT RUNS DONE"
