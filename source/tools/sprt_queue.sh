#!/usr/bin/env bash
# runs a list of SPRTs one after another. each line of the queue file:
#   NEW OLD LABEL [tc] [elo0] [elo1]
# waits for any fastchess already running to finish first.
set -uo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
q="$1"
while tasklist 2>/dev/null | grep -qi fastchess; do sleep 30; done
while read -r new old label tc e0 e1; do
  [ -z "$new" ] && continue
  case "$new" in \#*) continue;; esac
  echo "=== $label ($(date)) ==="
  "$root/source/tools/sprt.sh" "$root/tools-bin/versions/$new.exe" "$root/tools-bin/versions/$old.exe" "$label" "${tc:-8+0.08}" "${e0:-0}" "${e1:-5}" "${CONC:-16}" | tail -n 12
done < "$q"
echo "=== queue done ($(date)) ==="
