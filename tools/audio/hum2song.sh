#!/usr/bin/env bash
# hum2song wrapper: runs under the basic-pitch venv (py3.11 — 3.14 can't build its deps)
exec "$HOME/venvs/basicpitch/bin/python" "$(dirname "$0")/hum2song.py" "$@"
