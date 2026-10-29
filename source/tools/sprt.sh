#!/usr/bin/env bash
# SPRT one of our builds against another of our builds with fastchess.
# usage: source/tools/sprt.sh NEW.exe OLD.exe LABEL [tc=8+0.08] [elo0=0] [elo1=5] [conc=16]
# only our own binaries are ever used as opponents. adjudication here is just to save
# testing time (it never touches the real engine).
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
new="$(cygpath -w "$(realpath "$1")")"; old="$(cygpath -w "$(realpath "$2")")"; label="$3"
tc="${4:-8+0.08}"; elo0="${5:-0}"; elo1="${6:-5}"; conc="${7:-16}"
fc="$root/tools-bin/fastchess/fastchess.exe"
# fastchess is linked against the mingw runtime dlls
W="$(ls -d /c/Users/*/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs*/mingw64/bin 2>/dev/null | head -1 || true)"
[ -n "$W" ] && export PATH="$W:$PATH"
book="${BOOK:-$root/source/tests/openings_v1.epd}"
mkdir -p "$root/logs/sprt"
stamp="$(date +%Y%m%d-%H%M%S)"
log="$root/logs/sprt/${label}-${stamp}.log"
"$fc" -engine cmd="$new" name=new -engine cmd="$old" name=old \
  -each tc="$tc" option.Hash=16 option.Threads=1 proto=uci \
  -openings file="$book" format=epd order=random \
  -repeat -games 2 -rounds "${ROUNDS:-50000}" -concurrency "$conc" \
  -sprt elo0="$elo0" elo1="$elo1" alpha=0.05 beta=0.05 model=normalized \
  -draw movenumber=40 movecount=8 score=8 \
  -resign movecount=4 score=800 twosided=true \
  -ratinginterval 200 -recover \
  -pgnout file="$root/logs/sprt/${label}-${stamp}.pgn" min=true \
  > "$log" 2>&1 &
fcpid=$!
"$root/source/tools/lowprio.sh" "$fcpid" "${PRIO:-Normal}"
wait "$fcpid" || true
echo "$log"
tail -n 25 "$log"
