#!/usr/bin/env bash
# dev build: native only, snapshot the binary as tools-bin/versions/NAME.exe for SPRT
# usage: source/tools/devbuild.sh NAME
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
W="$(ls -d /c/Users/*/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs*/mingw64/bin 2>/dev/null | head -1 || true)"
[ -n "$W" ] && export PATH="$W:$PATH"
cmake -S "$root/source" -B "$root/build-native" -G Ninja -DCMAKE_BUILD_TYPE=Release -DNATIVE=ON >/dev/null
cmake --build "$root/build-native" -j | grep -E "error|warning" || true
mkdir -p "$root/tools-bin/versions"
cp "$root/build-native/corvus.exe" "$root/tools-bin/versions/$1.exe"
"$root/tools-bin/versions/$1.exe" bench 7
