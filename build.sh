#!/usr/bin/env bash
# builds both binaries and copies them into engine/
# usage: ./build.sh            (needs g++ >= 13 and cmake on PATH)
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
W="$(ls -d /c/Users/*/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs*/mingw64/bin 2>/dev/null | head -1 || true)"
[ -n "$W" ] && export PATH="$W:$PATH"
ext=""; case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) ext=".exe";; esac
cmake -S "$here/source" -B "$here/build-native" -G Ninja -DCMAKE_BUILD_TYPE=Release -DNATIVE=ON >/dev/null
cmake --build "$here/build-native" -j
cmake -S "$here/source" -B "$here/build-portable" -G Ninja -DCMAKE_BUILD_TYPE=Release -DNATIVE=OFF >/dev/null
cmake --build "$here/build-portable" -j
mkdir -p "$here/engine"
cp "$here/build-native/corvus$ext" "$here/engine/Corvus$ext"
cp "$here/build-portable/corvus$ext" "$here/engine/Corvus-portable$ext"
echo "built engine/Corvus$ext and engine/Corvus-portable$ext"
