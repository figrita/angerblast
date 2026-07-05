#!/usr/bin/env python3
"""Build web/shell.html — the branded emscripten shell for the Angerblast web
build. Colors come from assets/palette.toml (BLK NX64) and the decorative art
is the game's real sprites embedded as data URIs, so the page always matches
the build it wraps. `make web` passes this file via --shell-file; emscripten
replaces {{{ SCRIPT }}} with the loader tag.

Layout: branding + controls in a left rail, the game (native 720x720, never
scaled) on the right — sized to sit above the fold on a ~1080p display.

Usage: build_shell.py [out.html]
"""

import base64
import sys
import tomllib
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
SPRITES = REPO / "assets" / "sprites" / "common"


def pal():
    p = tomllib.load(open(REPO / "assets" / "palette.toml", "rb"))
    return {n: "#" + c for n, c in zip(p["names"], p["colors"])}


def uri(name):
    return "data:image/png;base64," + base64.b64encode((SPRITES / name).read_bytes()).decode()


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else REPO / "web" / "shell.html"
    P = pal()
    S = {k: uri(v) for k, v in {
        "player":   "player.png",
        "gem":      "gem.png",
        "drone":    "drone.png",
        "move":     "pasv_thrusters.png",
        "charge":   "wpn_ioncannon.png",
        "auto":     "pasv_rapidfire.png",
        "roll":     "pasv_velocitycoils.png",
        "pause":    "fx_freeze.png",
        "codex":    "fx_mark.png",
        "r_ember":  "rune_ember.png",
        "r_frost":  "rune_frost.png",
        "r_vine":   "rune_vine.png",
        "r_spark":  "rune_spark.png",
        "r_void":   "rune_void.png",
        "p_heal":   "pickup_heal.png",
        "p_level":  "pickup_levelup.png",
        "p_wipe":   "pickup_wipe.png",
    }.items()}

    html = TEMPLATE
    for k, v in P.items():
        html = html.replace("{{pal:%s}}" % k, v)
    for k, v in S.items():
        html = html.replace("{{spr:%s}}" % k, v)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(html)
    print(f"wrote {out} ({out.stat().st_size // 1024} KB)")


TEMPLATE = r"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ANGERBLAST</title>
<meta name="description" content="Angerblast — a vertical shmup roguelite. Socket runes, evolve weapons, charge the cannon, kill the boss.">
<meta property="og:title" content="ANGERBLAST">
<meta property="og:description" content="A vertical shmup roguelite.">
<link rel="icon" href="{{spr:gem}}">
<style>
  :root{
    --black:{{pal:black}}; --ink:{{pal:ink}}; --coral:{{pal:coral}}; --wine:{{pal:wine}};
    --gold:{{pal:gold}}; --peach:{{pal:peach}}; --white:{{pal:white}}; --lilac:{{pal:lilac}};
    --peri:{{pal:periwinkle}}; --grape:{{pal:grape}}; --sky:{{pal:sky}}; --mint:{{pal:mint}};
  }
  *{box-sizing:border-box}
  html{background:var(--black)}
  body{
    margin:0; min-height:100vh; color:var(--lilac);
    font:13px/1.5 ui-monospace,'Cascadia Mono',Menlo,Consolas,monospace;
    background:
      radial-gradient(1100px 650px at 62% -160px, color-mix(in srgb, var(--ink) 75%, var(--black)) 0%, var(--black) 70%);
  }
  #stars{position:fixed;inset:0;z-index:0;pointer-events:none}
  #stars i{position:absolute;display:block;background:var(--peri);animation:tw 3.2s infinite}
  #stars i.b{background:var(--lilac)} #stars i.c{background:var(--grape)}
  @keyframes tw{0%,100%{opacity:.9}50%{opacity:.25}}

  /* ---- two columns: rail left, game right; both above the fold ---- */
  .wrap{
    position:relative; z-index:1; display:flex; gap:30px; align-items:flex-start;
    justify-content:center; max-width:1230px; margin:0 auto; padding:20px 16px 24px;
  }
  .side{width:400px; flex:none; display:flex; flex-direction:column; gap:14px}
  @media (max-width:1210px){
    .wrap{flex-direction:column; align-items:center}
    .side{width:min(744px, 96vw)}
  }

  /* ---- branding ---- */
  header{display:flex; align-items:center; gap:14px}
  .ship{image-rendering:pixelated; width:72px; height:72px; flex:none;
        animation:hover 2.6s ease-in-out infinite}
  @keyframes hover{0%,100%{transform:translateY(0)}50%{transform:translateY(-6px)}}
  h1{
    margin:0; font-size:clamp(30px,3.4vw,42px); font-weight:800; letter-spacing:.05em;
    color:var(--coral); text-shadow:3px 3px 0 var(--wine), 6px 6px 0 var(--black);
  }
  .sub{color:var(--peach); letter-spacing:.32em; text-transform:uppercase; font-size:11px}
  .runes{display:flex; gap:8px; align-items:center}
  .runes img{image-rendering:pixelated; width:26px; height:26px; animation:hover 2.6s ease-in-out infinite}
  .runes img:nth-child(2){animation-delay:.2s}.runes img:nth-child(3){animation-delay:.4s}
  .runes img:nth-child(4){animation-delay:.6s}.runes img:nth-child(5){animation-delay:.8s}

  /* ---- controls rail ---- */
  h2{margin:4px 0 0; color:var(--gold); letter-spacing:.28em; text-transform:uppercase; font-size:12px;
     text-shadow:2px 2px 0 var(--black)}
  .controls{display:flex; flex-direction:column; gap:6px}
  .ctl{
    display:flex; gap:10px; align-items:center; padding:7px 10px;
    background:color-mix(in srgb, var(--ink) 80%, var(--black));
    border:1px solid color-mix(in srgb, var(--grape) 70%, var(--black));
  }
  .ctl img{image-rendering:pixelated; width:36px; height:36px; flex:none}
  .ctl b{color:var(--white); letter-spacing:.06em; text-transform:uppercase; font-size:12px; margin-right:6px}
  .ctl small{color:var(--lilac); font-size:12px}
  kbd{
    display:inline-block; padding:0 6px; margin:0 1px; border:1px solid var(--peri);
    border-bottom-width:3px; background:var(--ink); color:var(--gold); font:inherit;
    font-size:11px; border-radius:3px;
  }
  .pad{color:var(--sky)}

  .loop{color:var(--lilac); font-size:12px; line-height:1.9}
  .loop img{image-rendering:pixelated; width:22px; height:22px; vertical-align:middle; margin:0 3px 0 8px}
  .loop img:first-child{margin-left:0}
  .loop em{color:var(--peach); font-style:normal}

  .bar{display:flex; gap:8px}
  .bar button{
    font:inherit; font-size:12px; letter-spacing:.12em; text-transform:uppercase; cursor:pointer;
    color:var(--lilac); background:var(--ink); border:1px solid var(--peri); padding:6px 14px;
  }
  .bar button:hover{color:var(--gold); border-color:var(--gold)}
  footer{color:var(--grape); font-size:11px; letter-spacing:.12em}

  /* ---- game panel: native 720x720, centered, never scaled ---- */
  .stage{flex:none; padding:4px; overflow:auto; max-width:100vw}
  .panel{
    position:relative; padding:8px; background:color-mix(in srgb, var(--ink) 88%, var(--black));
    border:2px solid var(--grape);
    box-shadow:0 0 0 2px var(--black), 0 0 44px color-mix(in srgb, var(--grape) 45%, transparent);
  }
  .panel::before,.panel::after{content:""; position:absolute; width:14px; height:14px; border:3px solid var(--gold)}
  .panel::before{top:-3px;left:-3px;border-right:0;border-bottom:0}
  .panel::after{bottom:-3px;right:-3px;border-left:0;border-top:0}
  canvas.emscripten{
    display:block; width:720px; height:720px;   /* native pixels — no scaling */
    image-rendering:pixelated; background:var(--black); border:0; outline:0;
  }
  #boot{
    position:absolute; inset:8px; display:flex; flex-direction:column; gap:14px;
    align-items:center; justify-content:center; background:var(--black); z-index:2;
  }
  #boot .gem{image-rendering:pixelated; width:64px; height:64px; animation:spin 1.1s steps(8) infinite}
  @keyframes spin{to{transform:rotate(360deg)}}
  #status{color:var(--gold); letter-spacing:.18em; text-transform:uppercase; font-size:13px}
  #progress{appearance:none; width:280px; height:8px; border:1px solid var(--grape); background:var(--ink)}
  #progress::-webkit-progress-bar{background:var(--ink)}
  #progress::-webkit-progress-value{background:var(--coral)}
  #progress::-moz-progress-bar{background:var(--coral)}
</style>
</head>
<body>
<div id="stars"></div>
<div class="wrap">

  <aside class="side">
    <header>
      <img class="ship" src="{{spr:player}}" alt="">
      <div>
        <h1>ANGERBLAST</h1>
        <div class="sub">vertical shmup roguelite</div>
      </div>
    </header>
    <div class="runes">
      <img src="{{spr:r_ember}}" alt=""><img src="{{spr:r_frost}}" alt=""><img src="{{spr:r_vine}}" alt="">
      <img src="{{spr:r_spark}}" alt=""><img src="{{spr:r_void}}" alt="">
    </div>

    <h2>Controls</h2>
    <div class="controls">
      <div class="ctl"><img src="{{spr:move}}" alt="">
        <div><b>Move</b><small><kbd>W</kbd><kbd>A</kbd><kbd>S</kbd><kbd>D</kbd> <span class="pad">/ left stick</span></small></div></div>
      <div class="ctl"><img src="{{spr:charge}}" alt="">
        <div><b>Charge Shot</b><small>hold <kbd>Space</kbd> <span class="pad">/ Cross</span>, release</small></div></div>
      <div class="ctl"><img src="{{spr:auto}}" alt="">
        <div><b>Auto-fire</b><small><kbd>T</kbd> <span class="pad">/ Triangle</span> toggles</small></div></div>
      <div class="ctl"><img src="{{spr:roll}}" alt="">
        <div><b>Barrel Roll</b><small><kbd>Shift</kbd> <span class="pad">/ R1</span> &mdash; i-frames</small></div></div>
      <div class="ctl"><img src="{{spr:pause}}" alt="">
        <div><b>Pause</b><small><kbd>Esc</kbd> or <kbd>P</kbd> <span class="pad">/ Options</span> &mdash; settings</small></div></div>
      <div class="ctl"><img src="{{spr:codex}}" alt="">
        <div><b>Codex</b><small><kbd>C</kbd> <span class="pad">/ Square</span> &mdash; the bestiary</small></div></div>
    </div>

    <div class="loop">
      <img src="{{spr:gem}}" alt=""> grab <em>XP gems</em>
      <img src="{{spr:p_level}}" alt=""> snag <em>power-ups</em>
      <img src="{{spr:r_ember}}" alt=""> socket <em>runes</em>
      <img src="{{spr:p_wipe}}" alt=""> evolve <em>weapons</em>
      <img src="{{spr:drone}}" alt=""> build <em>drones</em>
      <img src="{{spr:p_heal}}" alt=""> kill the <em>boss</em> to advance
    </div>

    <div class="loop" style="color:var(--peri)" id="padnote">gamepad: press any button on the pad to wake it
      (browsers hide controllers until then)</div>

    <div class="bar">
      <button onclick="Module.canvas.requestFullscreen()">Fullscreen</button>
      <button id="btn-audio" onclick="toggleAudio()">Mute</button>
    </div>

    <footer>ANGERBLAST &mdash; palette: BLK NX64 &mdash; art &amp; sound generated in-house</footer>
  </aside>

  <div class="stage">
    <div class="panel">
      <div id="boot">
        <img class="gem" src="{{spr:gem}}" alt="">
        <div id="status">Downloading…</div>
        <progress id="progress" value="0" max="100" hidden></progress>
      </div>
      <canvas class="emscripten" id="canvas" width="720" height="720"
              oncontextmenu="event.preventDefault()" tabindex="-1"></canvas>
    </div>
  </div>

</div>

<script>
// twinkling starfield in the page palette (deterministic; sizes snap to whole px)
(function () {
  var box = document.getElementById('stars'), s = 1337;
  function rnd() { s = (s * 1664525 + 1013904223) >>> 0; return s / 4294967296; }
  for (var i = 0; i < 110; i++) {
    var el = document.createElement('i');
    var z = rnd(); el.className = z > .8 ? 'b' : (z > .45 ? '' : 'c');
    var px = z > .8 ? 2 : 1;
    el.style.cssText = 'left:' + (rnd() * 100).toFixed(2) + 'vw;top:' + (rnd() * 100).toFixed(2) +
      'vh;width:' + px + 'px;height:' + px + 'px;animation-delay:' + (rnd() * 3).toFixed(2) + 's';
    box.appendChild(el);
  }
})();

// browsers restrict the Gamepad API to secure contexts (https / localhost):
// on plain http getGamepads() is silently empty — say so instead of confusing people
if (!window.isSecureContext) {
  var pn = document.getElementById('padnote');
  pn.style.color = 'var(--coral)';
  pn.textContent = 'gamepad DISABLED on plain http — browsers only expose controllers on https or localhost';
}

var statusElement = document.getElementById('status'),
    progressElement = document.getElementById('progress'),
    bootElement = document.getElementById('boot');

var Module = {
  preRun: [], postRun: [],
  print: function (t) { console.log(t); },
  printErr: function (t) { console.error(t); },
  canvas: (function () {
    var c = document.getElementById('canvas');
    c.addEventListener('webglcontextlost', function (e) {
      alert('WebGL context lost. Reload the page.'); e.preventDefault();
    }, false);
    return c;
  })(),
  setStatus: function (text) {
    var m = text.match(/([^(]+)\((\d+(\.\d+)?)\/(\d+)\)/);
    if (m) {
      statusElement.textContent = m[1].trim();
      progressElement.hidden = false;
      progressElement.value = parseInt(m[2]); progressElement.max = parseInt(m[4]);
    } else {
      statusElement.textContent = text || '';
      progressElement.hidden = true;
      if (!text) bootElement.style.display = 'none';   // engine is running
    }
  },
  totalDependencies: 0,
  monitorRunDependencies: function (left) {
    this.totalDependencies = Math.max(this.totalDependencies, left);
    Module.setStatus(left ? 'Preparing… (' + (this.totalDependencies - left) + '/' + this.totalDependencies + ')'
                          : 'All downloads complete.');
  }
};
Module.setStatus('Downloading…');
window.onerror = function () { Module.setStatus('Exception thrown — see console'); };

// browsers gate audio behind a user gesture: track contexts so Mute works and
// the first click resumes a suspended context
var audioBtn = document.getElementById('btn-audio');
const audioContexList = [];
function toggleAudio() {
  var resumed = false;
  audioContexList.forEach(function (ctx) {
    if (ctx.state === 'suspended') { ctx.resume(); resumed = true; }
    else if (ctx.state === 'running') ctx.suspend();
  });
  audioBtn.textContent = resumed ? 'Mute' : 'Unmute';
}
self.AudioContext = new Proxy(self.AudioContext, {
  construct(target, args) {
    const ctx = new target(...args);
    audioContexList.push(ctx);
    if (ctx.state === 'suspended') audioBtn.textContent = 'Unmute';
    return ctx;
  }
});
</script>
{{{ SCRIPT }}}
</body>
</html>
"""

if __name__ == "__main__":
    main()
