#!/usr/bin/env bash
# set a running process (cygwin/msys pid) to a Windows priority class, BelowNormal by default.
# engines that fastchess starts afterwards inherit it. Idle starved the engines and caused
# time losses when anything else ran, so only use it for jobs without a clock.
pid="$1"; cls="${2:-${PRIO:-BelowNormal}}"
win="$(cat /proc/"$pid"/winpid 2>/dev/null || echo "$pid")"
powershell -NoProfile -Command "try { (Get-Process -Id $win).PriorityClass = '$cls' } catch {}" >/dev/null 2>&1
