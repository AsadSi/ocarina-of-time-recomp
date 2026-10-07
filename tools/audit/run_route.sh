#!/bin/bash
# Usage: run_route.sh <route.txt> <total seconds> <shot times...>
# Boots the game normally with the route as scripted controller input, screenshots the game window
# at each time, then quits and reports any new crash report.
set -u; APPDIR=${APPDIR:-appdata}
ROUTE=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); TOTAL=$2; shift 2
NAME=$(basename "$ROUTE" .txt); OUT=~/Documents/code/oot-recomp/audit/out/$NAME; rm -rf "$OUT"; mkdir -p "$OUT"
APP=~/Documents/code/oot-recomp/Zelda64Recomp/build-cmake/OoTRecompiled.app/Contents/MacOS/OoTRecompiled
pkill -9 -f OoTRecompiled.app/Contents/MacOS/OoTRecompiled 2>/dev/null; sleep 1
BEFORE=$(ls ~/Library/Logs/DiagnosticReports/ | grep -c -E "Zelda64Recompiled|OoTRecompiled")
RECOMP_INPUT_SCRIPT="$ROUTE" APP_FOLDER_PATH=~/Documents/code/oot-recomp/audit/$APPDIR "$APP" --start > "$OUT/log.txt" 2>&1 &
PID=$!; START=$(date +%s)
for T in "$@"; do
  while (( $(date +%s) - START < T )); do sleep 0.2; done
  kill -0 $PID 2>/dev/null || { echo "EXITED before ${T}s"; break; }
  W=$(swift -e "import CoreGraphics; let l = CGWindowListCopyWindowInfo([.optionOnScreenOnly], kCGNullWindowID) as! [[String: Any]]; for w in l where (w[\"kCGWindowOwnerPID\"] as? Int ?? 0) == $PID && (w[\"kCGWindowName\"] as? String ?? \"\") == \"Zelda 64: Recompiled\" { print(w[\"kCGWindowNumber\"]!); break }")
  [ -n "$W" ] && screencapture -x -o -l "$W" "$OUT/t$T.png" && sips -Z 480 "$OUT/t$T.png" >/dev/null
done
while (( $(date +%s) - START < TOTAL )) && kill -0 $PID 2>/dev/null; do sleep 0.5; done
kill -0 $PID 2>/dev/null && echo "ALIVE at ${TOTAL}s" || echo "DIED"
kill -9 $PID 2>/dev/null; sleep 2
AFTER=$(ls ~/Library/Logs/DiagnosticReports/ | grep -c -E "Zelda64Recompiled|OoTRecompiled")
(( AFTER > BEFORE )) && echo "NEW CRASH REPORT: $(ls -t ~/Library/Logs/DiagnosticReports/OoTRecompiled* | head -1)"
grep -vE 'Loaded font|SVG plugin|preload|RenderPool' "$OUT/log.txt" | tail -5
