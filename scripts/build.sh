#!/usr/bin/env bash
# Configure, build and test the C++ engine. Usage: ./scripts/build.sh [Release|Debug]
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_TYPE="${1:-Release}"
cmake -S "$ROOT" -B "$ROOT/build" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build "$ROOT/build" --parallel
ctest --test-dir "$ROOT/build" --output-on-failure
echo "Built $ROOT/build/flowsim_cli"
