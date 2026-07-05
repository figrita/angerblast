#!/usr/bin/env python3
"""Aggregate all reports/forksearch-*.md into a per-card strategy summary:
for every card, how do branches that TOOK it fare vs branches that didn't?"""

import re
import sys
from collections import defaultdict
from pathlib import Path

rows = []   # (cards[list], stage, level, result)
for f in sorted(Path('reports').glob('forksearch-*.md')):
    for line in f.read_text().splitlines():
        m = re.match(r'\| (.+) \| (\d+) \| (\d+) \| (\w+[^|]*) \| ([\d.]+)x \|', line)
        if not m or m.group(1).startswith('build path'):
            continue
        cards = [c.strip() for c in m.group(1).split('>')]
        rows.append((cards, int(m.group(2)), int(m.group(3)), m.group(4).strip(), float(m.group(5))))

print(f"{len(rows)} branched games from {len(list(Path('reports').glob('forksearch-*.md')))} seeds\n")
surv = sum(1 for r in rows if r[3] == 'survived')
unfair = sum(1 for r in rows if 'UNFAIR' in r[3])
print(f"overall: {surv}/{len(rows)} survived ({100*surv//max(len(rows),1)}%), {unfair} unfair deaths\n")

stat = defaultdict(lambda: [0, 0, 0])   # card -> [taken, survived, stageSum]
for cards, stage, lvl, res, dps in rows:
    for c in set(cards):
        s = stat[c]
        s[0] += 1
        s[1] += 1 if res == 'survived' else 0
        s[2] += stage

print(f"{'card':<22} {'picked':>6} {'surv%':>6} {'avg stage':>9}")
for c, (n, sv, ss) in sorted(stat.items(), key=lambda kv: -kv[1][1]/max(kv[1][0],1)):
    if n < 20: continue
    print(f"{c:<22} {n:>6} {100*sv//n:>5}% {ss/n:>9.2f}")
