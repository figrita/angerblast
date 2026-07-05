#!/usr/bin/env bash
# Rebuild the local picker page + compare sheet while variant runs are producing files.
cd "$(dirname "$0")/../.."
while pgrep -f run_variants.sh >/dev/null; do
  python3 tools/comfy/build_picker.py tools/comfy/sprite_picker.html >/dev/null 2>&1
  python3 tools/comfy/compare_sheet.py tools/comfy/compare_sheet.png >/dev/null 2>&1
  sleep 120
done
python3 tools/comfy/build_picker.py tools/comfy/sprite_picker.html >/dev/null 2>&1
python3 tools/comfy/compare_sheet.py tools/comfy/compare_sheet.png >/dev/null 2>&1
