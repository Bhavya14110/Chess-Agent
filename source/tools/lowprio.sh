#!/usr/bin/env bash
# drop a running process (cygwin/msys pid) to Windows "Idle" priority. engines that fastchess
# starts afterwards inherit it, so long test runs stay out of the way of whoever uses the laptop.
pid="$1"
win="$(cat /proc/"$pid"/winpid 2>/dev/null || echo "$pid")"
powershell -NoProfile -Command "try { (Get-Process -Id $win).PriorityClass = 'Idle' } catch {}" >/dev/null 2>&1
