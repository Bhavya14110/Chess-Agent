#!/usr/bin/env bash
# robustness run: N games of one build against itself (or another of our builds) at a real
# time control, no adjudication, then count time losses / illegal moves / crashes / disconnects.
# usage: source/tools/selfplay_check.sh ENGINE.exe GAMES TC CONC LABEL [OTHER.exe]
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
eng="$1"; games="$2"; tc="$3"; conc="$4"; label="$5"; other="${6:-$1}"
fc="$root/tools-bin/fastchess/fastchess.exe"
mkdir -p "$root/logs/selfplay"
pgn="$root/logs/selfplay/${label}.pgn"
log="$root/logs/selfplay/${label}.log"
rm -f "$pgn"
"$fc" -engine cmd="$eng" name=A -engine cmd="$other" name=B \
  -each tc="$tc" option.Hash=64 option.Threads=1 proto=uci \
  -openings file="$root/source/tests/openings_v1.epd" format=epd order=random \
  -repeat -games 2 -rounds $((games / 2)) -concurrency "$conc" \
  -pgnout file="$pgn" timeleft=true \
  > "$log" 2>&1 || true
python "$root/source/tools/pgn_failures.py" "$pgn" | tee -a "$log"
