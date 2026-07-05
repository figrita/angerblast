#!/usr/bin/env python3
"""Build the piano-roll editor (piano_roll.html) for the biome music loops.

Embeds tools/audio/songs/*.json. Edit notes on the grid, audition with the
WebAudio preview synth (a close cousin of gen_music.py's renderer), then
"copy songs JSON" and paste it back to Claude (or save each slug object to
tools/audio/songs/<slug>.json yourself) and rerun gen_music.py to re-render
the oggs.

Usage: build_piano_roll.py [out.html]
"""

import json
import sys
import tomllib
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else HERE / "piano_roll.html"
    songs = {}
    for p in sorted((HERE / "songs").glob("*.json")):
        songs[p.stem] = json.loads(p.read_text())
    pal = tomllib.load(open(REPO / "assets" / "palette.toml", "rb"))
    colors = {n: "#" + c for n, c in zip(pal["names"], pal["colors"])}
    html = (TEMPLATE
            .replace("/*__SONGS__*/", json.dumps(songs, separators=(",", ":")))
            .replace("/*__PAL__*/", json.dumps(colors, separators=(",", ":"))))
    out.write_text(html)
    print(f"wrote {out} ({out.stat().st_size // 1024} KB, {len(songs)} songs)")


TEMPLATE = r"""<title>Angerblast piano roll</title>
<style>
  :root{--ink:#12173d;--deep:#000;--line:#293268;--text:#c1d9f2;--dim:#909edd;
        --gold:#ffe091;--mint:#8cff9b;--coral:#ff695a;--peri:#6b74b2}
  html{background:var(--deep)}
  body{margin:0;font:13px/1.5 ui-monospace,'Cascadia Mono',Menlo,Consolas,monospace;
       color:var(--text);background:var(--deep)}
  header{padding:12px 18px 8px;border-bottom:1px solid var(--line);display:flex;
         align-items:baseline;gap:16px;flex-wrap:wrap}
  h1{margin:0;font-size:15px;color:var(--gold);letter-spacing:.14em;text-transform:uppercase}
  header .sub{color:var(--dim)}
  .bar{padding:8px 18px;display:flex;gap:8px;flex-wrap:wrap;align-items:center}
  button,select{font:inherit;background:var(--ink);color:var(--dim);border:1px solid var(--line);
         padding:4px 12px;cursor:pointer}
  button.on{color:var(--gold);border-color:var(--gold)}
  button:hover{color:var(--text)}
  button.play{color:var(--mint);border-color:var(--mint)}
  .lbl{color:var(--dim);margin-left:10px}
  input[type=number]{width:60px;font:inherit;background:var(--ink);color:var(--text);
                     border:1px solid var(--line);padding:3px 6px}
  .scroll{overflow:auto;max-height:calc(100vh - 220px);border-top:1px solid var(--line)}
  canvas{display:block;image-rendering:pixelated;cursor:crosshair}
  .results{padding:10px 18px}
  textarea{width:100%;height:70px;background:var(--ink);color:var(--text);border:1px solid var(--line);
           font:11px/1.4 ui-monospace,monospace;padding:6px;box-sizing:border-box}
</style>
<header>
  <h1>Angerblast &mdash; piano roll</h1>
  <span class="sub">click: add note &middot; click a note: delete &middot; edits autosave locally &middot; copy JSON back to Claude, then rerun gen_music.py</span>
</header>
<div class="bar" id="tabs"></div>
<div class="bar">
  <button id="play" class="play">&#9654; play</button>
  <span class="lbl">track:</span><span id="tracks"></span>
  <span class="lbl">note len:</span><span id="durs"></span>
  <span class="lbl">bpm:</span><input type="number" id="bpm" min="50" max="180">
  <button id="revert">revert song</button>
</div>
<div class="scroll"><canvas id="cv"></canvas></div>
<div class="results">
  <button id="copy">copy songs JSON</button>
  <textarea id="out" readonly></textarea>
</div>
<script>
const EMBEDDED = /*__SONGS__*/;
const PAL = /*__PAL__*/;
const LO = 30, HI = 90;                    // midi range shown
const CW = 7, RH = 9;                      // cell size
const DRUMS = ['kick','snare','hat'];
const TCOL = { bass: PAL.coral, pad: PAL.periwinkle, arp: PAL.gold };
const $ = s => document.querySelector(s);

// local edits persist per song, but a REGENERATED song (hum2song / gen_music
// --recompose changed the embedded copy) always wins over the stale cache
const stored = JSON.parse(localStorage.getItem('angerblast-songs-v2') || 'null');
let songs = {};
for (const slug of Object.keys(EMBEDDED)) {
  const emb = JSON.stringify(EMBEDDED[slug]);
  songs[slug] = (stored && stored.base && stored.base[slug] === emb && stored.songs[slug])
    ? stored.songs[slug] : structuredClone(EMBEDDED[slug]);
}
let cur = Object.keys(songs)[0], track = 'arp', dur = 1;

function song() { return songs[cur]; }
function steps() { return song().bars * 16; }
function save() {
  localStorage.setItem('angerblast-songs-v2', JSON.stringify({
    base: Object.fromEntries(Object.keys(EMBEDDED).map(s => [s, JSON.stringify(EMBEDDED[s])])),
    songs: songs
  }));
  syncOut();
}

// ---- layout: pitch grid + drum lane ----
const cv = $('#cv'), cx = cv.getContext('2d');
const ROWS = HI - LO + 1;
const DRUM_Y = () => ROWS * RH + 14;
function resize() {
  cv.width = steps() * CW + 40;
  cv.height = DRUM_Y() + DRUMS.length * 14 + 6;
}
function midiRow(n) { return HI - n; }

function draw(playCol) {
  cx.fillStyle = '#05060f'; cx.fillRect(0, 0, cv.width, cv.height);
  const S = steps();
  for (let r = 0; r < ROWS; r++) {           // row shading: octave stripes
    const n = HI - r;
    cx.fillStyle = (n % 12 === 0) ? '#141a45' : ([1,3,6,8,10].includes(n % 12) ? '#090c22' : '#0c1030');
    cx.fillRect(40, r * RH, S * CW, RH - 1);
    if (n % 12 === 0) { cx.fillStyle = PAL.lilac; cx.font = '9px monospace'; cx.fillText('C' + (n/12 - 1), 4, r * RH + 8); }
  }
  for (let s = 0; s <= S; s++) {             // bar/beat lines
    if (s % 4) continue;
    cx.fillStyle = s % 16 === 0 ? '#3a4386' : '#1a2050';
    cx.fillRect(40 + s * CW, 0, 1, ROWS * RH);
  }
  for (const tk of ['pad','bass','arp']) {   // notes: unselected tracks dimmed
    for (const ev of song().tracks[tk]) {
      const r = midiRow(ev.n); if (r < 0 || r >= ROWS) continue;
      cx.globalAlpha = (tk === track ? 1 : 0.30) * (0.45 + 0.55 * ev.v);
      cx.fillStyle = TCOL[tk];
      cx.fillRect(40 + ev.t * CW + 1, r * RH, ev.d * CW - 2, RH - 1);
    }
  }
  cx.globalAlpha = 1;
  // drum lane
  const dy = DRUM_Y();
  cx.fillStyle = PAL.lilac; cx.font = '9px monospace';
  DRUMS.forEach((k, i) => {
    cx.fillText(k, 4, dy + i * 14 + 10);
    cx.fillStyle = '#0c1030'; cx.fillRect(40, dy + i * 14, S * CW, 12);
    cx.fillStyle = PAL.lilac;
  });
  for (let s = 0; s <= S; s += 4) {
    cx.fillStyle = s % 16 === 0 ? '#3a4386' : '#1a2050';
    cx.fillRect(40 + s * CW, dy, 1, DRUMS.length * 14);
  }
  for (const ev of song().drums) {
    const i = DRUMS.indexOf(ev.k);
    cx.fillStyle = PAL.mint; cx.globalAlpha = 0.45 + 0.55 * ev.v;
    cx.fillRect(40 + ev.t * CW + 1, DRUM_Y() + i * 14 + 1, CW - 2, 10);
  }
  cx.globalAlpha = 1;
  if (playCol >= 0) { cx.fillStyle = 'rgba(255,224,145,.75)'; cx.fillRect(40 + playCol * CW, 0, 1.5, cv.height); }
}

// ---- editing ----
cv.addEventListener('mousedown', e => {
  const rect = cv.getBoundingClientRect();
  const x = e.clientX - rect.left - 40, y = e.clientY - rect.top;
  if (x < 0) return;
  const t = Math.floor(x / CW);
  if (t >= steps()) return;
  if (y >= DRUM_Y()) {                                    // drum lane toggle
    const i = Math.floor((y - DRUM_Y()) / 14);
    if (i < 0 || i >= DRUMS.length) return;
    const k = DRUMS[i];
    const hit = song().drums.findIndex(ev => ev.k === k && ev.t === t);
    if (hit >= 0) song().drums.splice(hit, 1);
    else song().drums.push({ t: t, k: k, v: k === 'snare' ? 0.5 : k === 'kick' ? 0.9 : 1.0 });
    save(); draw(-1); return;
  }
  const n = HI - Math.floor(y / RH);
  const evs = song().tracks[track];
  const hit = evs.findIndex(ev => ev.n === n && t >= ev.t && t < ev.t + ev.d);
  if (hit >= 0) evs.splice(hit, 1);
  else { evs.push({ t: t, n: n, d: dur, v: 1.0 }); previewNote(n); }
  save(); draw(-1);
});

// ---- WebAudio preview synth (cousin of gen_music.py's renderer) ----
let actx = null, playing = false, loopTimer = null, liveNodes = [];
function ctxNow() { if (!actx) actx = new AudioContext(); return actx; }
function oscNode(type, freq, t0, durS, gain, duty) {
  const ac = ctxNow(), o = ac.createOscillator(), g = ac.createGain();
  o.type = type; o.frequency.value = freq;
  g.gain.setValueAtTime(gain, t0);
  g.gain.exponentialRampToValueAtTime(0.001, t0 + durS);
  o.connect(g).connect(ac.destination);
  o.start(t0); o.stop(t0 + durS + 0.05);
  liveNodes.push(o);
}
let noiseBuf = null;
function noise(t0, durS, gain, hp) {
  const ac = ctxNow();
  if (!noiseBuf) {
    noiseBuf = ac.createBuffer(1, ac.sampleRate, ac.sampleRate);
    const d = noiseBuf.getChannelData(0);
    for (let i = 0; i < d.length; i++) d[i] = Math.random() * 2 - 1;
  }
  const src = ac.createBufferSource(); src.buffer = noiseBuf;
  const g = ac.createGain();
  g.gain.setValueAtTime(gain, t0);
  g.gain.exponentialRampToValueAtTime(0.001, t0 + durS);
  let node = src;
  if (hp) { const f = ac.createBiquadFilter(); f.type = 'highpass'; f.frequency.value = hp; src.connect(f); node = f; }
  node.connect(g).connect(ac.destination);
  src.start(t0); src.stop(t0 + durS + 0.02);
  liveNodes.push(src);
}
function hz(m) { return 440 * Math.pow(2, (m - 69) / 12); }
function scheduleLoop(t0) {
  const s = song(), m = s.meta, stepS = 60 / s.bpm / 4;
  const wave = m.bass_wave === 'sq' ? 'square' : 'triangle';
  for (const ev of s.tracks.bass) oscNode(wave, hz(ev.n), t0 + ev.t * stepS, 0.35, 0.22 * ev.v);
  for (const ev of s.tracks.pad) {
    oscNode('triangle', hz(ev.n) * 1.003, t0 + ev.t * stepS, ev.d * stepS, 0.5 * m.pad_gain * ev.v / 3);
    oscNode('triangle', hz(ev.n) * 0.997, t0 + ev.t * stepS, ev.d * stepS, 0.5 * m.pad_gain * ev.v / 3);
  }
  for (const ev of s.tracks.arp) oscNode('square', hz(ev.n), t0 + ev.t * stepS, ev.d * stepS * 0.8, 0.8 * m.arp_gain * ev.v);
  for (const ev of s.drums) {
    const tt = t0 + ev.t * stepS, g = m.drum_gain * ev.v;
    if (ev.k === 'kick') {
      const ac = ctxNow(), o = ac.createOscillator(), gg = ac.createGain();
      o.frequency.setValueAtTime(95, tt); o.frequency.exponentialRampToValueAtTime(38, tt + 0.12);
      gg.gain.setValueAtTime(0.8 * g, tt); gg.gain.exponentialRampToValueAtTime(0.001, tt + 0.12);
      o.connect(gg).connect(ac.destination); o.start(tt); o.stop(tt + 0.15); liveNodes.push(o);
    }
    else if (ev.k === 'snare') noise(tt, 0.09, 0.4 * g, 900);
    else noise(tt, 0.025, 0.22 * g, 5000);
  }
}
function previewNote(n) {
  const m = song().meta;
  if (track === 'bass') oscNode(m.bass_wave === 'sq' ? 'square' : 'triangle', hz(n), ctxNow().currentTime, 0.3, 0.22);
  else oscNode('square', hz(n), ctxNow().currentTime, 0.2, 0.5 * m.arp_gain);
}
function stop() {
  playing = false; clearTimeout(loopTimer);
  for (const nd of liveNodes) { try { nd.stop(); } catch (e) {} }
  liveNodes = [];
  $('#play').innerHTML = '&#9654; play'; draw(-1);
}
$('#play').onclick = () => {
  if (playing) return stop();
  playing = true; $('#play').innerHTML = '&#9632; stop';
  const loopS = steps() * (60 / song().bpm / 4);
  let t0 = ctxNow().currentTime + 0.08;
  const iterate = () => {
    if (!playing) return;
    scheduleLoop(t0);
    const headStart = t0;
    const anim = () => {
      if (!playing) return;
      const el = ctxNow().currentTime - headStart;
      if (el < loopS) { draw(Math.floor(el / (60 / song().bpm / 4)) % steps()); requestAnimationFrame(anim); }
    };
    anim();
    t0 += loopS;
    loopTimer = setTimeout(iterate, (t0 - ctxNow().currentTime - 0.25) * 1000);
  };
  iterate();
};

// ---- chrome ----
const tabs = $('#tabs');
for (const slug of Object.keys(songs)) {
  const b = document.createElement('button');
  b.textContent = slug;
  b.onclick = () => { stop(); cur = slug; syncChrome(); resize(); draw(-1); };
  tabs.appendChild(b);
}
const trkBox = $('#tracks');
for (const tk of ['bass','pad','arp']) {
  const b = document.createElement('button');
  b.textContent = tk; b.style.color = TCOL[tk];
  b.onclick = () => { track = tk; syncChrome(); draw(-1); };
  trkBox.appendChild(b);
}
const durBox = $('#durs');
for (const d of [1,2,4,8,16]) {
  const b = document.createElement('button');
  b.textContent = d;
  b.onclick = () => { dur = d; syncChrome(); };
  durBox.appendChild(b);
}
$('#bpm').addEventListener('change', () => { song().bpm = parseInt($('#bpm').value) || song().bpm; save(); });
$('#revert').onclick = () => { songs[cur] = structuredClone(EMBEDDED[cur]); save(); syncChrome(); resize(); draw(-1); };
function syncChrome() {
  [...tabs.children].forEach(b => b.classList.toggle('on', b.textContent === cur));
  [...trkBox.children].forEach(b => b.classList.toggle('on', b.textContent === track));
  [...durBox.children].forEach(b => b.classList.toggle('on', parseInt(b.textContent) === dur));
  $('#bpm').value = song().bpm;
}
function syncOut() { $('#out').value = JSON.stringify(songs); }
$('#copy').onclick = async () => {
  await navigator.clipboard.writeText($('#out').value);
  $('#copy').textContent = 'copied!';
  setTimeout(() => { $('#copy').textContent = 'copy songs JSON'; }, 1200);
};
syncChrome(); resize(); draw(-1); syncOut();
</script>
"""

if __name__ == "__main__":
    main()
