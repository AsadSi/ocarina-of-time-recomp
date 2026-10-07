"""Plot the transforms RT64 left to automatic matching onto a route's screenshots.

Run a route with RT64_INTERP_STATS=1, then: python3 -I interp_overlay.py <out dir>
Writes <shot>_auto.png next to each screenshot: green dots were auto-matched, red ones found no
match (they snap instead of interpolating). Also prints the per-frame ID/auto match counts.
"""
import re, sys
from pathlib import Path
from PIL import Image, ImageDraw

out = Path(sys.argv[1])
start = int((out / "start.txt").read_text())
log = (out / "log.txt").read_text(errors="replace")
details = []
for m in re.finditer(r"\[interp-auto\] t=([\d.]+)(.*)", log):
    pts = [tuple(float(v) for v in p.split(",")) for p in m.group(2).split()]
    details.append((float(m.group(1)) - start, pts))
counts = [(float(m.group(1)) - start, m.groups()[1:]) for m in
          re.finditer(r"\[interp\] t=([\d.]+) id=(\d+)/(\d+) auto=(\d+)/(\d+) ignored=(\d+)", log)]

for shot in sorted(out.glob("t*.png")):
    if shot.stem.endswith("_auto"):
        continue
    t = float(shot.stem[1:])
    if not details:
        break
    when, pts = min(details, key=lambda d: abs(d[0] - t))
    near = [c for c in counts if abs(c[0] - t) < 0.5]
    img = Image.open(shot).convert("RGB")
    w, h = img.size
    draw = ImageDraw.Draw(img)
    unmatched = 0
    for x, y, mapped in pts:
        if -1 <= x <= 1 and -1 <= y <= 1:
            px, py = (x + 1) / 2 * w, (1 - y) / 2 * h
            color = (0, 255, 0) if mapped else (255, 0, 0)
            unmatched += not mapped
            draw.ellipse([px - 3, py - 3, px + 3, py + 3], outline=color, width=2)
    img.save(shot.with_name(shot.stem + "_auto.png"))
    summary = near[len(near) // 2][1] if near else ("?",) * 5
    print(f"{shot.name}: log at {when:.1f}s, {len(pts)} auto non-identity, {unmatched} unmatched; "
          f"id {summary[0]}/{summary[1]} auto {summary[2]}/{summary[3]} ignored {summary[4]}")
