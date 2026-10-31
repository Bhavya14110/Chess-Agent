#!/usr/bin/env bash
# Re-runs the 4 pre-competition safety checks and prints PASS/FAIL (or RISK) per check.
# Run from anywhere in Git Bash:  audit/safety/repro/runall.sh
# env: LAG_GAMES_3P2 (10), LAG_GAMES_10P5 (4), SKIP_LAG=1 to skip the ~2 h check 1,
#      LICHESS_BOT_DIR (C:/Users/Bhavya/Downloads/lichess-bot-src)
# Never connects to Lichess. The token in config.yml is never printed.
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../../.." && pwd)"
logs="$root/audit/safety/logs"; mkdir -p "$logs"
lb="${LICHESS_BOT_DIR:-/c/Users/Bhavya/Downloads/lichess-bot-src}"
declare -A verdict

# 1. network lag: Corvus vs Corvus through lagwrap, 3+2 and 10+5, 2 games at a time, BelowNormal
if [ "${SKIP_LAG:-0}" = 1 ]; then
  verdict[1]="SKIPPED"
else
  "$here/check1_lag.sh" "${LAG_GAMES_3P2:-10}" 180+2 lag-3p2
  "$here/check1_lag.sh" "${LAG_GAMES_10P5:-4}" 600+5 lag-10p5
  if python "$here/check1_analyze.py" "$logs/check1-lag-chart.png" "3+2=$logs/lag-3p2.pgn" "10+5=$logs/lag-10p5.pgn" \
       | tee "$logs/check1-summary.txt"; then verdict[1]=PASS; else verdict[1]=FAIL; fi
fi

# 2. tricky positions, on both shipped binaries (BelowNormal, one engine at a time)
"$root/source/tools/lowprio.sh" $$
v2=PASS
for exe in Corvus.exe Corvus-portable.exe; do
  python "$here/check2_tricky.py" "$root/engine/$exe" --threads 4 --json "$logs/check2-${exe%.exe}.json" \
    | tee "$logs/check2-${exe%.exe}.txt" || v2=FAIL
done
verdict[2]=$v2

# 3. bot config review (token never printed)
if python "$here/check3_config.py" "$lb/config.yml" "$root/lichess-bot/config.example.yml" | tee "$logs/check3.out"; then
  grep -q "RISK" "$logs/check3.out" && verdict[3]="PASS (RISK notes)" || verdict[3]=PASS
  grep -q "REMINDER" "$logs/check3.out" && verdict[3]="${verdict[3]} + allow_list reminder"
else verdict[3]=FAIL; fi

# 4. engine crash / hang mid-game, offline through lichess-bot's own game loop and mock lichess.org
( cd "$lb" && timeout 120 venv/Scripts/python.exe "$here/check4_runner.py" crash "$logs" > "$logs/check4-crash.out" 2>&1 )
( cd "$lb" && timeout 120 venv/Scripts/python.exe "$here/check4_runner.py" hang "$logs" > "$logs/check4-hang.out" 2>&1 )
crash="$(grep -h '^{' "$logs/check4-crash.out" | tail -1)"; hang="$(grep -h '^{' "$logs/check4-hang.out" | tail -1)"
echo "check4 crash: $crash"; echo "check4 hang:  $hang"
if [[ "$crash" == *'"bot_won": true'* ]]; then
  verdict[4]="PASS (crash -> engine restarted, game continued)"
  [[ "$hang" == *'"stuck_s"'* ]] && verdict[4]="${verdict[4]}; RISK (hang -> never restarted)"
else verdict[4]="FAIL (crash not recovered)"; fi

echo
echo "================ safety check summary ================"
for i in 1 2 3 4; do echo "check $i: ${verdict[$i]}"; done
