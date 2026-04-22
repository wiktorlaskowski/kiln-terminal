#!/usr/bin/env bash
# Build (if needed) and run Kiln Terminal.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$ROOT/build"

if [[ ! -f "$BUILD/build.ninja" && ! -f "$BUILD/Makefile" ]]; then
    cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
fi
cmake --build "$BUILD" -j"$(nproc)"

exec "$BUILD/kiln-terminal" "$@"
