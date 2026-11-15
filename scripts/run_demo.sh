#!/usr/bin/env bash
# Builds the engine if needed, prepares a Python venv and launches the Streamlit app.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ ! -x "$ROOT/build/flowsim_cli" ]]; then
  "$ROOT/scripts/build.sh"
fi
if [[ ! -d "$ROOT/.venv" ]]; then
  python3 -m venv "$ROOT/.venv"
  "$ROOT/.venv/bin/pip" install -r "$ROOT/ui/requirements.txt"
fi
cd "$ROOT/ui"
exec "$ROOT/.venv/bin/streamlit" run app.py --server.port "${PORT:-8501}"
