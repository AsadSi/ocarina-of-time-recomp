#!/bin/bash
# Usage: run_route.sh <route.txt> <total seconds> <shots...>
# Boots the game with the route as scripted controller input and captures the game window only: a shot
# is a time in seconds (screenshot) or v<start>:<seconds> (frames of the window via bin/wincap, built from
# wincap.swift). Then quits and reports any new crash report. Runs queue on a lock, since only one game
# instance can run at a time. Env: APPDIR (app data folder under audit/), ARGS (launch flags, default --start), APP (game binary).
set -u; APPDIR=${APPDIR:-appdata}
ROUTE=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); TOTAL=$2; shift 2
NAME=$(basename "$ROUTE" .txt); OUT=~/Documents/code/oot-recomp/audit/out/$NAME; rm -rf "$OUT"; mkdir -p "$OUT"
APP=${APP:-~/Documents/code/oot-recomp/Zelda64Recomp/build-cmake/OoTRecompiled.app/Contents/MacOS/OoTRecompiled}
LOCK=~/Documents/code/oot-recomp/audit/.run.lock
until mkdir "$LOCK" 2>/dev/null; do sleep 2; done; trap 'rmdir "$LOCK"' EXIT
pkill -9 -f OoTRecompiled.app/Contents/MacOS/OoTRecompiled 2>/dev/null; sleep 1
BEFORE=$(ls ~/Library/Logs/DiagnosticReports/ | grep -c -E "Zelda64Recompiled|OoTRecompiled")
RECOMP_INPUT_SCRIPT="$ROUTE" APP_FOLDER_PATH=~/Documents/code/oot-recomp/audit/$APPDIR "$APP" ${ARGS:---start} > "$OUT/log.txt" 2>&1 &
PID=$!; START=$(date +%s); echo "$START" > "$OUT/start.txt"
elapsed_lt() { perl -MTime::HiRes=time -e "exit !(time - $START < $1)"; }
CLIPS=()
for SHOT in "$@"; do
  T=${SHOT#v}; T=${T%%:*}
  while elapsed_lt "$T"; do sleep 0.05; done
  kill -0 $PID 2>/dev/null || { echo "EXITED before ${T}s"; break; }
  [ -n "${W:-}" ] || W=$(swift -e "import CoreGraphics; let l = CGWindowListCopyWindowInfo([.optionOnScreenOnly], kCGNullWindowID) as! [[String: Any]]; for w in l where (w[\"kCGWindowOwnerPID\"] as? Int ?? 0) == $PID && (w[\"kCGWindowName\"] as? String ?? \"\") == \"Ocarina of Time: Recompiled\" { print(w[\"kCGWindowNumber\"]!); break }")
  [ -n "$W" ] || continue
  if [[ $SHOT == v* ]]; then
    ~/Documents/code/oot-recomp/audit/bin/wincap $PID "${SHOT#*:}" "$OUT/v$T" > /dev/null & CLIPS+=($!)
  else
    screencapture -x -o -l "$W" "$OUT/t$T.png" && sips -Z 480 "$OUT/t$T.png" >/dev/null
  fi
done
while elapsed_lt "$TOTAL" && kill -0 $PID 2>/dev/null; do sleep 0.5; done
kill -0 $PID 2>/dev/null && echo "ALIVE at ${TOTAL}s" || echo "DIED"
(( ${#CLIPS[@]} )) && wait "${CLIPS[@]}"
kill -9 $PID 2>/dev/null; sleep 2
AFTER=$(ls ~/Library/Logs/DiagnosticReports/ | grep -c -E "Zelda64Recompiled|OoTRecompiled")
(( AFTER > BEFORE )) && echo "NEW CRASH REPORT: $(ls -t ~/Library/Logs/DiagnosticReports/OoTRecompiled* | head -1)"
grep -vE 'Loaded font|SVG plugin|preload|RenderPool|\[interp' "$OUT/log.txt" | tail -5
