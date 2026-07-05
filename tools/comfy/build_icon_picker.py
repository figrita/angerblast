#!/usr/bin/env python3
"""Build the self-contained icon-picker webpage (icon_picker.html).

Like build_picker.py but for the codex/menu icon set (weapons, effects, runes,
trifectas, drones, passives from gen_codex_icons.py). Embeds the base icon +
_2.._8 seed variants as data URIs, plus a per-variant "effective pixel scale"
score: the fraction of the image reproducible by a uniform 2x2 (or 3x3) block
grid. Icons whose diffusion "pixels" didn't normalize to the real 1:1 grid
score high and get flagged 2x/3x in the page.

Preview pane mocks the two places icons appear in-game (render.c): the
level-up card menu (icon at 44px diameter) and codex rows (icon at 30px).

Picks output format matches apply_picks.py: `common/wpn_maincannon.png = 5`.

Usage: build_icon_picker.py [out.html]
"""

import base64
import json
import sys
import tomllib
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_codex_icons as gci
from spritegen import chunk_score

REPO = Path(__file__).resolve().parent.parent.parent
COMMON = REPO / "assets" / "sprites" / "common"
# each seed slot can have an outline-stripped file (_5) and its unaltered
# pre-strip twin (_5o); both are offered as picks
SUFFIXES = [""] + [x for i in range(2, 9) for x in (f"_{i}", f"_{i}o")]

CATS = [
    ("weapons",   "Weapons",   gci.WEAPONS),
    ("effects",   "Effects",   gci.EFFECTS),
    ("runes",     "Runes",     gci.RUNES),
    ("trifectas", "Trifectas", gci.TRIFECTAS),
    ("drones",    "Drones",    gci.DRONES),
    ("passives",  "Passives",  gci.PASSIVES),
]


def palette():
    with open(REPO / "assets" / "palette.toml", "rb") as f:
        p = tomllib.load(f)
    return {n: "#" + c for n, c in zip(p["names"], p["colors"])}


def b64(p):
    return "data:image/png;base64," + base64.b64encode(p.read_bytes()).decode()


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("icon_picker.html")
    pal = palette()

    cats, variants = [], {}
    for key, title, roster in CATS:
        entries = []
        for slug, (name, prompt) in roster.items():
            rel = f"common/{slug}.png"
            vs = {}
            for suf in SUFFIXES:
                p = COMMON / f"{slug}{suf}.png"
                if not p.exists():
                    continue
                img = Image.open(p)
                vs[suf] = {
                    "uri": b64(p),
                    "w": img.width, "h": img.height,
                    "s2": chunk_score(img, 2),
                    "s3": chunk_score(img, 3),
                }
            variants[rel] = vs
            entries.append({"slug": slug, "name": name, "rel": rel, "prompt": prompt})
        cats.append({"key": key, "title": title, "entries": entries})

    data = {"cats": cats, "variants": variants, "suffixes": SUFFIXES, "pal": pal}
    html = TEMPLATE.replace("/*__DATA__*/", json.dumps(data, separators=(",", ":")))
    out.write_text(html)
    n_var = sum(len(v) for v in variants.values())
    n_icons = sum(len(c["entries"]) for c in cats)
    print(f"wrote {out} — {n_icons} icons, {n_var} variant images, {out.stat().st_size // 1024} KB")


TEMPLATE = r"""<title>Angerblast icon picks</title>
<style>
  :root{
    --ink:#12173d; --deep:#000000; --panel:#1d1a59; --line:#293268;
    --text:#c1d9f2; --dim:#909edd; --gold:#ffe091; --mint:#8cff9b; --coral:#ff695a;
  }
  html{background:var(--deep)}
  body{margin:0;font:13px/1.5 ui-monospace,'Cascadia Mono',Menlo,Consolas,monospace;
       color:var(--text);background:var(--deep);font-variant-numeric:tabular-nums}
  .wrap{display:grid;grid-template-columns:minmax(560px,1fr) minmax(380px,600px);gap:0;min-height:100vh}
  header{grid-column:1/-1;padding:14px 20px 10px;border-bottom:1px solid var(--line);
         display:flex;justify-content:space-between;align-items:baseline;flex-wrap:wrap;gap:8px}
  header h1{margin:0;font-size:15px;color:var(--gold);letter-spacing:.14em;text-transform:uppercase;font-weight:700}
  header .sub{color:var(--dim)}
  /* ---- left: roster ---- */
  .roster{overflow-y:auto;max-height:calc(100vh - 47px);padding:0 8px 60px 16px}
  .bhead{position:sticky;top:0;background:var(--deep);padding:14px 4px 6px;z-index:3;
         color:var(--gold);letter-spacing:.12em;text-transform:uppercase;font-size:12px;
         border-bottom:1px solid var(--line)}
  .row{display:flex;align-items:center;gap:6px;padding:7px 2px;border-bottom:1px solid #12173d;flex-wrap:wrap}
  .row .nm{width:128px;flex:none;color:var(--text)}
  .row .nm small{display:block;color:var(--dim)}
  .cell{flex:none;width:60px;height:60px;display:flex;align-items:center;justify-content:center;
        background:var(--ink);border:2px solid var(--line);cursor:pointer;position:relative;
        image-rendering:pixelated}
  .cell:hover{border-color:var(--dim)}
  .cell.sel{border-color:var(--gold);box-shadow:0 0 0 1px var(--gold)}
  .cell.missing{opacity:.25;cursor:default}
  .cell img{image-rendering:pixelated}
  .cell .tag{position:absolute;bottom:1px;right:3px;font-size:10px;color:var(--dim)}
  .cell.sel .tag{color:var(--gold)}
  .cell .px{position:absolute;top:0;left:2px;font-size:10px}
  .px.ok{color:var(--mint)} .px.soft{color:var(--gold)} .px.bad{color:var(--coral)}
  /* ---- right: preview ---- */
  .sim{position:sticky;top:0;align-self:start;max-height:100vh;overflow-y:auto;
       padding:12px 16px 24px;border-left:1px solid var(--line);background:var(--deep)}
  .tabs{display:flex;gap:4px;flex-wrap:wrap;margin-bottom:8px}
  .tabs button{font:inherit;background:var(--ink);color:var(--dim);border:1px solid var(--line);
               padding:4px 10px;cursor:pointer}
  .tabs button.on{color:var(--gold);border-color:var(--gold)}
  canvas{width:100%;max-width:568px;image-rendering:pixelated;border:1px solid var(--line);display:block}
  .simnote{color:var(--dim);margin:6px 0 10px;font-size:12px}
  .loupe{display:flex;gap:12px;align-items:flex-start;min-height:224px;margin-bottom:10px;
         border:1px solid var(--line);background:var(--ink);padding:8px}
  .loupe img{image-rendering:pixelated;width:208px;height:208px;object-fit:contain;background:#000}
  .loupe .info{font-size:12px;color:var(--dim);flex:1}
  .loupe .info b{color:var(--text);display:block;margin-bottom:4px}
  .loupe .info .scr{margin-top:6px}
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
    <h1>Angerblast &mdash; icon seed picks</h1>
    <span class="sub">corner tag = effective pixel scale (<span style="color:var(--mint)">1x crisp</span> /
      <span style="color:var(--gold)">~2x soft</span> / <span style="color:var(--coral)">2x&#8211;3x chunky</span>)
      &middot; hover = loupe &middot; click = pick &middot; paste the results box back to Claude</span>
  </header>
  <div class="roster" id="roster"></div>
  <div class="sim">
    <div class="loupe" id="loupe"><div class="info"><b>loupe</b>hover any variant to inspect it at 8x</div></div>
    <div class="tabs" id="tabs"></div>
    <canvas id="cv" width="568" height="620"></canvas>
    <div class="simnote">level-up cards (icons at 44px, like in-game) + codex rows (30px) from your picks</div>
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
const picks = JSON.parse(localStorage.getItem('angerblast-icon-picks') || '{}');
const imgs = {};
function vinfo(rel, suf) { return (DATA.variants[rel] || {})[suf] || null; }
function img(rel, suf) {
  const v = vinfo(rel, suf);
  if (!v) return null;
  const k = rel + suf;
  if (!imgs[k]) { const i = new Image(); i.src = v.uri; imgs[k] = i; }
  return imgs[k];
}
function pick(rel) { return picks[rel] !== undefined ? picks[rel] : ''; }
// effective-pixel-scale bucket from block-match scores
function pxTag(v) {
  if (!v) return null;
  if (v.s3 >= 0.92) return {t:'3x', c:'bad'};
  if (v.s2 >= 0.88) return {t:'2x', c:'bad'};
  if (v.s2 >= 0.80) return {t:'~2x', c:'soft'};
  return {t:'1x', c:'ok'};
}

// ---- roster ----
const roster = $('#roster');
const allEntries = [];
for (const cat of DATA.cats) {
  const h = document.createElement('div'); h.className = 'bhead';
  h.textContent = cat.title;
  roster.appendChild(h);
  for (const e of cat.entries) {
    allEntries.push(e);
    const row = document.createElement('div'); row.className = 'row';
    const nm = document.createElement('div'); nm.className = 'nm';
    nm.innerHTML = e.name + '<small>' + e.slug + '</small>';
    nm.title = e.prompt;
    row.appendChild(nm);
    for (const suf of DATA.suffixes) {
      const v = vinfo(e.rel, suf);
      if (!v && suf.endsWith('o')) continue;   // unstripped twin never saved for this slot
      const cell = document.createElement('div');
      cell.className = 'cell'; cell.tabIndex = 0;
      if (!v) { cell.classList.add('missing'); cell.title = 'not generated yet'; }
      else {
        const el = document.createElement('img');
        el.src = v.uri; el.alt = e.slug + (suf || ' base');
        el.width = v.w * 2;   // icons are ~26px: show at 2x
        cell.appendChild(el);
        const px = pxTag(v);
        const pt = document.createElement('span'); pt.className = 'px ' + px.c;
        pt.textContent = px.t;
        cell.appendChild(pt);
        cell.addEventListener('mouseenter', () => loupe(e, suf, v));
        cell.addEventListener('focus', () => loupe(e, suf, v));
      }
      const tag = document.createElement('span'); tag.className = 'tag';
      tag.textContent = suf ? suf.slice(1) : 'b';
      cell.appendChild(tag);
      if (pick(e.rel) === suf) cell.classList.add('sel');
      const choose = () => {
        if (!vinfo(e.rel, suf)) return;
        picks[e.rel] = suf;
        localStorage.setItem('angerblast-icon-picks', JSON.stringify(picks));
        row.querySelectorAll('.cell').forEach(c => c.classList.remove('sel'));
        cell.classList.add('sel');
        syncOut(); draw();
      };
      cell.addEventListener('click', choose);
      cell.addEventListener('keydown', ev => { if (ev.key === 'Enter' || ev.key === ' ') { ev.preventDefault(); choose(); } });
      row.appendChild(cell);
    }
    roster.appendChild(row);
  }
}

// ---- loupe ----
function loupe(e, suf, v) {
  const px = pxTag(v);
  $('#loupe').innerHTML =
    '<img src="' + v.uri + '" alt="">' +
    '<div class="info"><b>' + e.name + ' &middot; ' + (suf ? 'variant ' + suf.slice(1) : 'base') + '</b>' +
    e.slug + suf + '.png &middot; ' + v.w + 'x' + v.h +
    '<div class="scr">pixel scale: <span class="px ' + px.c + '" style="position:static">' + px.t + '</span>' +
    ' &nbsp; (2x2 block-match ' + Math.round(v.s2 * 100) + '%, 3x3 ' + Math.round(v.s3 * 100) + '%)</div>' +
    '<div class="scr" style="margin-top:8px;color:var(--dim)">' + e.prompt + '</div></div>';
}

// ---- tabs ----
let curCat = 0;
const tabs = $('#tabs');
DATA.cats.forEach((c, i) => {
  const bt = document.createElement('button');
  bt.textContent = c.key;
  bt.onclick = () => { curCat = i; tabs.querySelectorAll('button').forEach(x => x.classList.remove('on')); bt.classList.add('on'); draw(); };
  if (i === 0) bt.classList.add('on');
  tabs.appendChild(bt);
});

// ---- preview: level-up cards + codex rows, drawn like render.c ----
const cv = $('#cv'), cx = cv.getContext('2d');
cx.imageSmoothingEnabled = false;
function drawIcon(rel, x, y, d) {   // sprite_draw: scale so max(w,h) -> d, centered
  const im = img(rel, pick(rel)), v = vinfo(rel, pick(rel));
  if (!im || !v) return;
  if (!im.complete) { im.onload = draw; return; }
  const s = d / Math.max(v.w, v.h);
  const w = Math.round(v.w * s), h = Math.round(v.h * s);
  cx.drawImage(im, Math.round(x - w / 2), Math.round(y - h / 2), w, h);
}
function draw() {
  const P = DATA.pal, c = DATA.cats[curCat];
  cx.fillStyle = '#000'; cx.fillRect(0, 0, cv.width, cv.height);
  // level-up card menu mock: 3 cards, icon at 44px like render.c
  cx.fillStyle = P.gold; cx.font = 'bold 22px monospace'; cx.textAlign = 'center';
  cx.fillText('CHOOSE UPGRADE', cv.width / 2, 34);
  const three = c.entries.slice(0, 3), cw = 170, ch = 200, gap = 16;
  const totW = three.length * cw + (three.length - 1) * gap;
  three.forEach((e, i) => {
    const x = (cv.width - totW) / 2 + i * (cw + gap), y = 56;
    cx.fillStyle = P.ink; cx.fillRect(x, y, cw, ch);
    cx.strokeStyle = i === 1 ? P.gold : P.periwinkle; cx.lineWidth = i === 1 ? 3 : 2;
    cx.strokeRect(x, y, cw, ch);
    drawIcon(e.rel, x + cw / 2, y + 58, 44);
    cx.fillStyle = P.white; cx.font = 'bold 14px monospace';
    cx.fillText(e.name, x + cw / 2, y + 108, cw - 16);
    cx.fillStyle = P.lilac; cx.font = '11px monospace';
    cx.fillText(e.slug, x + cw / 2, y + 128, cw - 16);
    cx.fillText('[' + (i + 1) + ']', x + cw / 2, y + ch - 14);
  });
  // codex rows mock: icon at 30px like codex_row_ex
  cx.textAlign = 'left';
  cx.fillStyle = P.gold; cx.font = 'bold 18px monospace';
  cx.fillText('CODEX — ' + c.title.toUpperCase(), 24, 306);
  const colW = (cv.width - 48) / 2, rowH = 46;
  c.entries.forEach((e, i) => {
    const col = i % 2, r = Math.floor(i / 2);
    const x = 24 + col * colW, y = 324 + r * rowH;
    drawIcon(e.rel, x + 16, y + 20, 30);
    cx.fillStyle = P.white; cx.font = 'bold 14px monospace';
    cx.fillText(e.name, x + 38, y + 16, colW - 44);
    cx.fillStyle = P.lilac; cx.font = '11px monospace';
    cx.fillText(e.slug, x + 38, y + 32, colW - 44);
  });
}

// ---- results ----
function syncOut() {
  const lines = [];
  for (const e of allEntries)
    lines.push(e.rel + ' = ' + (pick(e.rel) ? pick(e.rel).slice(1) : 'base'));
  $('#out').value = lines.join('\n');
}
$('#copy').onclick = async () => {
  await navigator.clipboard.writeText($('#out').value);
  $('#copy').textContent = 'copied!'; $('#copy').classList.add('ok');
  setTimeout(() => { $('#copy').textContent = 'copy picks'; $('#copy').classList.remove('ok'); }, 1200);
};
$('#reset').onclick = () => {
  for (const k of Object.keys(picks)) delete picks[k];
  localStorage.setItem('angerblast-icon-picks', '{}');
  document.querySelectorAll('.cell.sel').forEach(c => c.classList.remove('sel'));
  document.querySelectorAll('.row').forEach(r => { const c = r.querySelector('.cell'); if (c && !c.classList.contains('missing')) c.classList.add('sel'); });
  syncOut(); draw();
};
syncOut(); draw();
setTimeout(draw, 400);
</script>
"""

if __name__ == "__main__":
    main()
