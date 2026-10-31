#!/usr/bin/env bash
# Check 1: Corvus vs Corvus through lagwrap (every engine->GUI line delayed 100-300 ms),
# Move Overhead 150 (lichess-bot value), Threads 1, Hash 64, no adjudication, 2 games at a time,
# BelowNormal priority. usage: check1_lag.sh GAMES TC LABEL   e.g. check1_lag.sh 10 180+2 lag-3p2
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../../.." && pwd)"
games="$1"; tc="$2"; label="$3"
logs="$root/audit/safety/logs"; mkdir -p "$logs"
W="$(ls -d /c/Users/*/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs*/mingw64/bin 2>/dev/null | head -1 || true)"
[ -n "$W" ] && export PATH="$W:$PATH"
if [ ! -x "$here/lagwrap.exe" ] || [ "$here/lagwrap.cpp" -nt "$here/lagwrap.exe" ]; then
  g++ -O2 -std=c++20 -static "$here/lagwrap.cpp" -o "$here/lagwrap.exe"
fi
export LAGWRAP_ENGINE="$(cygpath -w "$root/engine/Corvus.exe")"
export LAGWRAP_MIN=100 LAGWRAP_MAX=300
export LAGWRAP_LOG="$(cygpath -w "$logs/$label-bestmove-delays.txt")"
pgn="$logs/$label.pgn"; log="$logs/$label.log"
rm -f "$pgn" "$log" "$logs/$label-bestmove-delays.txt"
# this shell goes to BelowNormal first, so fastchess, lagwrap and both engines inherit it
"$root/source/tools/lowprio.sh" $$
"$root/tools-bin/fastchess/fastchess.exe" \
  -engine cmd="$(cygpath -w "$here/lagwrap.exe")" name=CorvusA \
  -engine cmd="$(cygpath -w "$here/lagwrap.exe")" name=CorvusB \
  -each tc="$tc" option.Hash=64 option.Threads=1 "option.Move Overhead=150" proto=uci \
  -openings file="$(cygpath -w "$root/source/tests/openings_v1.epd")" format=epd order=random \
  -repeat -games 2 -rounds $(( (games + 1) / 2 )) -concurrency 2 \
  -pgnout file="$(cygpath -w "$pgn")" timeleft=true latency=true \
  > "$log" 2>&1 || true
python "$root/source/tools/pgn_failures.py" "$pgn" | tee -a "$log"
