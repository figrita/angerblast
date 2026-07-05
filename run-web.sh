#!/usr/bin/env bash
# Build the WebAssembly version and serve it locally, then open in a browser.
# Usage: ./run-web.sh   (builds web/angerblast.html and serves on :8099)
#
# One-time setup (inside the raylibbox distrobox):
#   git clone https://github.com/emscripten-core/emsdk ~/emsdk
#   (cd ~/emsdk && ./emsdk install latest && ./emsdk activate latest)
#   git clone https://github.com/raysan5/raylib ~/raylib
#   (source ~/emsdk/emsdk_env.sh && cd ~/raylib/src && emmake make PLATFORM=PLATFORM_WEB)
set -e
cd "$(dirname "$0")"
distrobox enter raylibbox -- bash -lc '
  source ~/emsdk/emsdk_env.sh >/dev/null 2>&1
  make web
  echo "Serving on http://localhost:8099/angerblast.html  (Ctrl-C to stop)"
  python3 -m http.server 8099 -d web
'
