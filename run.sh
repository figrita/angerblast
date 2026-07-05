#!/usr/bin/env bash
# Build and run Angerblast inside the raylib distrobox.
# Usage: ./run.sh
set -e
cd "$(dirname "$0")"
distrobox enter raylibbox -- bash -lc 'make && ./angerblast'
