#!/usr/bin/env python3
"""Build the self-contained sprite-picker webpage (sprite_picker.html).

Embeds every base sprite + _2.._8 seed variants as data URIs, plus enough game
data (biome themes, enemy elite flags, boss part offsets) to render a simulated
in-game screenshot per biome from the user's current picks.

Usage: build_picker.py [out.html]
"""

import base64
import json
import re
import sys
import tomllib
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
SPRITES = REPO / "assets" / "sprites"
SUFFIXES = [""] + [f"_{i}" for i in range(2, 9)]


def palette():
    with open(REPO / "assets" / "palette.toml", "rb") as f:
        p = tomllib.load(f)
    return {n: "#" + c for n, c in zip(p["names"], p["colors"])}


def b64(p):
    return "data:image/png;base64," + base64.b64encode(p.read_bytes()).decode()


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("sprite_picker.html")
    pal = palette()

    sprites = {}   # rel -> {variants: {suffix: dataURI}, size}
    for p in sorted(SPRITES.rglob("*.png")):
        stem = p.stem
        m = re.match(r"^(.*)_([2-8])$", stem)
        base_rel = str((p.parent / ((m.group(1) if m else stem) + ".png")).relative_to(SPRITES))
        suf = "_" + m.group(2) if m else ""
        e = sprites.setdefault(base_rel, {"variants": {}})
        e["variants"][suf] = b64(p)

    biomes = []
    for bdir in sorted((REPO / "assets" / "biomes").iterdir()):
        if not bdir.is_dir():
            continue
        with open(bdir / "biome.toml", "rb") as f:
            bt = tomllib.load(f)
        theme = bt.get("theme", {})
        enemies = []
        for et in sorted((bdir / "enemies").glob("*.toml")):
            with open(et, "rb") as f:
                e = tomllib.load(f)
            if "sprite" not in e:
                continue
            enemies.append({
                "name": e["name"],
                "rel": e["sprite"].replace("sprites/", ""),
                "elite": bool(e.get("elite", False)),
                "radius": e.get("radius", 12),
            })
        boss = bt.get("boss", {})
        parts = [{
            "rel": p["sprite"].replace("sprites/", ""),
            "offX": p.get("off_x", 0), "offY": p.get("off_y", 0),
            "head": bool(p.get("head", False)),
        } for p in boss.get("part", []) if "sprite" in p]
        biomes.append({
            "slug": bdir.name,
            "name": bt.get("name", bdir.name),
            "bg": pal.get(theme.get("background", "black"), "#000000"),
            "stars": [pal.get(c, "#293268") for c in theme.get("star_colors", [])],
            "enemies": enemies,
            "bossName": boss.get("name", "?"),
            "boss": boss.get("sprite", "").replace("sprites/", "") if boss.get("sprite") else None,
            "parts": parts,
        })

    data = {
        "sprites": sprites,
        "biomes": biomes,
        "suffixes": SUFFIXES,
        "pal": pal,
    }

    html = TEMPLATE.replace("/*__DATA__*/", json.dumps(data, separators=(",", ":")))
    out.write_text(html)
    n_var = sum(len(e["variants"]) for e in sprites.values())
    print(f"wrote {out} — {len(sprites)} sprites, {n_var} variant images, {out.stat().st_size//1024} KB")


TEMPLATE = r"""<title>Angerblast sprite picks</title>
<style>
  :root{
    --ink:#12173d; --deep:#000000; --panel:#1d1a59; --line:#293268;
    --text:#c1d9f2; --dim:#909edd; --gold:#ffe091; --mint:#8cff9b; --coral:#ff695a;
  }
  html{background:var(--deep)}
  body{margin:0;font:13px/1.5 ui-monospace,'Cascadia Mono',Menlo,Consolas,monospace;
       color:var(--text);background:var(--deep);font-variant-numeric:tabular-nums}
  .wrap{display:grid;grid-template-columns:minmax(430px,1fr) minmax(380px,760px);gap:0;min-height:100vh}
  header{grid-column:1/-1;padding:14px 20px 10px;border-bottom:1px solid var(--line);
         display:flex;justify-content:space-between;align-items:baseline;flex-wrap:wrap;gap:8px}
  header h1{margin:0;font-size:15px;color:var(--gold);letter-spacing:.14em;text-transform:uppercase;font-weight:700}
  header .sub{color:var(--dim)}
  /* ---- left: roster ---- */
  .roster{overflow-y:auto;max-height:calc(100vh - 47px);padding:0 8px 60px 16px}
  .bhead{position:sticky;top:0;background:var(--deep);padding:14px 4px 6px;z-index:3;
         color:var(--gold);letter-spacing:.12em;text-transform:uppercase;font-size:12px;
         border-bottom:1px solid var(--line)}
  .row{display:flex;align-items:center;gap:6px;padding:7px 2px;border-bottom:1px solid #12173d}
  .row .nm{width:118px;flex:none;color:var(--text)}
  .row .nm small{display:block;color:var(--dim)}
  .cell{flex:none;width:52px;height:52px;display:flex;align-items:center;justify-content:center;
        background:var(--ink);border:2px solid var(--line);cursor:pointer;position:relative;
        image-rendering:pixelated}
  .cell:hover{border-color:var(--dim)}
  .cell.sel{border-color:var(--gold);box-shadow:0 0 0 1px var(--gold)}
  .cell.missing{opacity:.25;cursor:default}
  .cell img{image-rendering:pixelated}
  .cell .tag{position:absolute;bottom:1px;right:3px;font-size:10px;color:var(--dim)}
  .cell.sel .tag{color:var(--gold)}
  /* ---- right: sim ---- */
  .sim{position:sticky;top:0;align-self:start;max-height:100vh;overflow-y:auto;
       padding:12px 16px 24px;border-left:1px solid var(--line);background:var(--deep)}
  .tabs{display:flex;gap:4px;flex-wrap:wrap;margin-bottom:8px}
  .tabs button{font:inherit;background:var(--ink);color:var(--dim);border:1px solid var(--line);
               padding:4px 10px;cursor:pointer}
  .tabs button.on{color:var(--gold);border-color:var(--gold)}
  canvas{width:100%;max-width:720px;image-rendering:pixelated;border:1px solid var(--line);display:block}
  .simnote{color:var(--dim);margin:6px 0 14px;font-size:12px}
  .results label{display:block;color:var(--gold);letter-spacing:.12em;text-transform:uppercase;
                 font-size:12px;margin-bottom:6px}
  textarea{width:100%;height:150px;background:var(--ink);color:var(--text);border:1px solid var(--line);
           font:12px/1.5 ui-monospace,monospace;padding:8px;box-sizing:border-box;resize:vertical}
  .btns{display:flex;gap:8px;margin-top:8px}
  .btns button{font:inherit;background:var(--panel);color:var(--text);border:1px solid var(--dim);
               padding:6px 14px;cursor:pointer;letter-spacing:.06em}
  .btns button:hover{border-color:var(--gold);color:var(--gold)}
  .btns .ok{color:var(--mint);border-color:var(--mint)}
  button:focus-visible,.cell:focus-visible{outline:2px solid var(--gold);outline-offset:1px}
</style>
<div class="wrap">
  <header>
    <h1>Angerblast &mdash; sprite seed picks</h1>
    <span class="sub">click a variant &rarr; the mock screenshot updates &rarr; paste the results box back to Claude</span>
  </header>
  <div class="roster" id="roster"></div>
  <div class="sim">
    <div class="tabs" id="tabs"></div>
    <canvas id="cv" width="720" height="720"></canvas>
    <div class="simnote" id="simnote"></div>
    <div class="results">
      <label for="out">picks &mdash; paste this back</label>
      <textarea id="out" readonly></textarea>
      <div class="btns">
        <button id="copy">copy picks</button>
        <button id="reset">reset all to base</button>
      </div>
    </div>
  </div>
</div>
<script>
const DATA = /*__DATA__*/;
const $ = s => document.querySelector(s);
const picks = JSON.parse(localStorage.getItem('angerblast-picks') || '{}');
const imgs = {};   // rel+suffix -> HTMLImageElement
function img(rel, suf) {
  const v = (DATA.sprites['sprites/'+rel] || DATA.sprites[rel] || {variants:{}}).variants[suf];
  if (!v) return null;
  const k = rel + suf;
  if (!imgs[k]) { const i = new Image(); i.src = v; imgs[k] = i; }
  return imgs[k];
}
function pick(rel) { return picks[rel] !== undefined ? picks[rel] : ''; }

// ---- roster ----
const groups = {};
for (const full of Object.keys(DATA.sprites)) {
  const rel = full.replace(/^sprites\//,'');
  const g = rel.split('/')[0];
  (groups[g] = groups[g] || []).push(rel);
}
const nameOf = {};
for (const b of DATA.biomes) for (const e of b.enemies) nameOf[e.rel] = e.name + (e.elite ? ' *' : '');
for (const b of DATA.biomes) { if (b.boss) nameOf[b.boss] = b.bossName; for (const p of b.parts) nameOf[p.rel] = nameOf[p.rel] || (b.bossName + (p.head?' head':' hand')); }
const roster = $('#roster');
for (const g of Object.keys(groups).sort()) {
  const h = document.createElement('div'); h.className = 'bhead';
  const bio = DATA.biomes.find(b => b.slug === g);
  h.textContent = bio ? bio.slug + ' — ' + bio.name : g;
  roster.appendChild(h);
  for (const rel of groups[g].sort()) {
    const row = document.createElement('div'); row.className = 'row';
    const nm = document.createElement('div'); nm.className = 'nm';
    const file = rel.split('/')[1].replace('.png','');
    nm.innerHTML = (nameOf[rel] || file) + '<small>' + file + '</small>';
    row.appendChild(nm);
    for (const suf of DATA.suffixes) {
      const cell = document.createElement('div');
      cell.className = 'cell'; cell.tabIndex = 0;
      const im = img(rel, suf);
      if (!im) { cell.classList.add('missing'); cell.title = 'not generated yet'; }
      else {
        const el = document.createElement('img');
        el.src = im.src; el.alt = file + (suf||' base');
        const k = Math.max(1, Math.min(3, Math.floor(46 / (im.width || 24))));
        el.onload = () => { el.width = el.naturalWidth * Math.max(1, Math.min(3, Math.floor(46 / el.naturalWidth))); };
        if (im.complete) el.width = im.naturalWidth * k;
        cell.appendChild(el);
      }
      const tag = document.createElement('span'); tag.className = 'tag';
      tag.textContent = suf ? suf.slice(1) : 'b';
      cell.appendChild(tag);
      if (pick(rel) === suf) cell.classList.add('sel');
      const choose = () => {
        if (!img(rel, suf)) return;
        picks[rel] = suf;
        localStorage.setItem('angerblast-picks', JSON.stringify(picks));
        row.querySelectorAll('.cell').forEach(c => c.classList.remove('sel'));
        cell.classList.add('sel');
        syncOut(); draw();
      };
      cell.addEventListener('click', choose);
      cell.addEventListener('keydown', e => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); choose(); } });
      row.appendChild(cell);
    }
    roster.appendChild(row);
  }
}

// ---- tabs ----
let curBiome = 0;
const tabs = $('#tabs');
DATA.biomes.forEach((b, i) => {
  const bt = document.createElement('button');
  bt.textContent = b.slug.replace(/^\d+_/,'');
  bt.onclick = () => { curBiome = i; tabs.querySelectorAll('button').forEach(x=>x.classList.remove('on')); bt.classList.add('on'); draw(); };
  if (i === 0) bt.classList.add('on');
  tabs.appendChild(bt);
});

// ---- simulated screenshot ----
const cv = $('#cv'), cx = cv.getContext('2d');
cx.imageSmoothingEnabled = false;
function drawSprite(rel, x, y, tint) {
  const im = img(rel, pick(rel));
  if (!im || !im.complete || !im.naturalWidth) { if (im && !im.complete) im.onload = draw; return; }
  const w = im.naturalWidth, h = im.naturalHeight;
  const px = Math.round(x - w/2), py = Math.round(y - h/2);
  if (!tint) { cx.drawImage(im, px, py); return; }
  const oc = drawSprite._oc = drawSprite._oc || document.createElement('canvas');
  oc.width = w; oc.height = h;
  const ox = oc.getContext('2d');
  ox.imageSmoothingEnabled = false;
  ox.clearRect(0,0,w,h);
  ox.drawImage(im, 0, 0);
  ox.globalCompositeOperation = 'source-atop';
  ox.globalAlpha = 0.45; ox.fillStyle = tint; ox.fillRect(0,0,w,h);
  ox.globalAlpha = 1; ox.globalCompositeOperation = 'source-over';
  cx.drawImage(oc, px, py);
}
function draw() {
  const b = DATA.biomes[curBiome];
  cx.fillStyle = b.bg; cx.fillRect(0,0,720,720);
  for (let L=0; L<b.stars.length; L++) {
    cx.fillStyle = b.stars[L];
    for (let i=0;i<40;i++) {
      const sx=(i*71.3+L*13)%720, sy=(i*47.7+L*211)%720;
      cx.fillRect(Math.floor(sx),Math.floor(sy),1+L,1+L);
    }
  }
  // boss at top
  if (b.boss) drawSprite(b.boss, 360, 128);
  for (const p of b.parts) drawSprite(p.rel, 360 + (p.offX||0), 145 + (p.offY||0));
  // elites flank, regulars in invader rows
  const elites = b.enemies.filter(e=>e.elite), regs = b.enemies.filter(e=>!e.elite);
  elites.forEach((e,i)=> drawSprite(e.rel, i%2? 508:212, 252));
  const rows = regs.length, top = 240, bottom = 566;
  const gap = rows>1 ? Math.min(46,(bottom-top)/(rows-1)) : 0;
  regs.forEach((e,i)=>{
    const y = top + i*gap, n = 6;
    for (let j=0;j<n;j++) drawSprite(e.rel, 138 + j*(444/(n-1)), y);
  });
  // common layer near the player
  drawSprite('common/pickup_magnet.png', 176, 612);
  drawSprite('common/pickup_wipe.png',   268, 612);
  drawSprite('common/pickup_levelup.png',360, 612);
  drawSprite('common/pickup_heal.png',   452, 612);
  drawSprite('common/pickup_freeze.png', 544, 612);
  drawSprite('common/gem.png', 320, 654, DATA.pal.mint);
  drawSprite('common/gem.png', 344, 662, DATA.pal.sky);
  drawSprite('common/gem.png', 300, 660, DATA.pal.coral);
  drawSprite('common/drone.png', 412, 648, DATA.pal.aquabrt);
  drawSprite('common/player.png', 360, 668);
  $('#simnote').textContent = b.name + ' — ' + b.bossName + ' + roster from your current picks';
}

// ---- results ----
function syncOut() {
  const lines = [];
  for (const full of Object.keys(DATA.sprites).sort()) {
    const rel = full.replace(/^sprites\//,'');
    lines.push(rel + ' = ' + (pick(rel) ? pick(rel).slice(1) : 'base'));
  }
  $('#out').value = lines.join('\n');
}
$('#copy').onclick = async () => {
  await navigator.clipboard.writeText($('#out').value);
  $('#copy').textContent = 'copied!'; $('#copy').classList.add('ok');
  setTimeout(()=>{ $('#copy').textContent='copy picks'; $('#copy').classList.remove('ok'); }, 1200);
};
$('#reset').onclick = () => {
  for (const k of Object.keys(picks)) delete picks[k];
  localStorage.setItem('angerblast-picks','{}');
  document.querySelectorAll('.cell.sel').forEach(c=>c.classList.remove('sel'));
  document.querySelectorAll('.row').forEach(r=>r.querySelector('.cell') && r.querySelector('.cell').classList.add('sel'));
  syncOut(); draw();
};
syncOut(); draw();
setTimeout(draw, 400);   // once images decode
</script>
"""

if __name__ == "__main__":
    main()
