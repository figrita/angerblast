#!/usr/bin/env python3
"""Aggregate reports/evomatrix-seed*.md: per weapon x trifecta cell, average the
damage ratio vs the unevolved baseline across seeds; flag broken (<0.9x) and
non-improving (<1.05x) cells, and list the dominant outliers."""

import re
from collections import defaultdict
from pathlib import Path

cells = defaultdict(list)   # (weapon, trifecta, evoname) -> [ratios]
alive = defaultdict(list)
for f in sorted(Path('reports').glob('evomatrix-seed*.md')):
    for line in f.read_text().splitlines():
        m = re.match(r'\| ([^|]+) \| ([^|]+) \| ([^|]+) \| [\d.]+ \| ([\d.]+)x \| \d+ \| (\d+) \| ([^|]+) \|', line)
        if not m: continue
        wpn, tri, evo = m.group(1).strip(), m.group(2).strip(), m.group(3).strip()
        if tri == '(none)': continue
        cells[(wpn, tri, evo)].append(float(m.group(4)))
        alive[(wpn, tri, evo)].append('alive' in m.group(6))

rows = []
for k, ratios in cells.items():
    avg = sum(ratios)/len(ratios)
    rows.append((avg, k, len(ratios)))
rows.sort()

n = len(rows)
print(f"{n} unique cells measured, {sum(len(v) for v in cells.values())} evolved runs total\n")
broken = [r for r in rows if r[0] < 0.9]
weak   = [r for r in rows if 0.9 <= r[0] < 1.05]
print(f"BROKEN (<0.9x baseline): {len(broken)}")
for avg, (wpn, tri, evo), c in broken:
    print(f"  {avg:.2f}x  {wpn} x {tri} ({evo})  [{c} seeds]")
print(f"\nNOT AN IMPROVEMENT (0.9-1.05x): {len(weak)}")
for avg, (wpn, tri, evo), c in weak:
    print(f"  {avg:.2f}x  {wpn} x {tri} ({evo})")
print(f"\nTOP 10 dominant:")
for avg, (wpn, tri, evo), c in rows[-10:][::-1]:
    print(f"  {avg:.2f}x  {wpn} x {tri} ({evo})")
med = rows[n//2][0]
print(f"\nmedian lift: {med:.2f}x")
