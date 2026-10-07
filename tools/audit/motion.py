"""Frame-to-frame motion in a wincap capture: how many delivered frames actually changed, per region.

python3 -I motion.py <capture dir> [x0 y0 x1 y1 (fractions of the frame)]
Prints the capture rate, the share of frames that differ from the previous one in the region, and a
per-frame change trace. Interpolated motion changes nearly every frame at the display rate; motion the
renderer couldn't interpolate changes only at the game's own rate (about 17 per second for PAL OoT),
with identical frames in between.
"""
import sys
from pathlib import Path
import numpy as np
from PIL import Image

cap = Path(sys.argv[1])
box = [float(v) for v in sys.argv[2:6]] if len(sys.argv) >= 6 else [0, 0, 1, 1]
frames = sorted((cap / "frames").glob("*.png"))
times = [float(l.split()[1]) for l in (cap / "frames.txt").read_text().split("\n") if l.strip()]
prev = None
diffs = []
for f in frames:
    a = np.asarray(Image.open(f).convert("L"), dtype=np.int16)
    h, w = a.shape
    a = a[int(box[1] * h):int(box[3] * h), int(box[0] * w):int(box[2] * w)]
    if prev is not None:
        diffs.append(float(np.abs(a - prev).mean()))
    prev = a
d = np.array(diffs)
span = times[-1] - times[0] if len(times) > 1 else 1
moving = d > 0.05
print(f"{len(frames)} frames in {span:.2f}s ({len(frames) / span:.1f}/s); changed in region: "
      f"{moving.sum()}/{len(d)} ({moving.sum() / span:.1f}/s)")
print("trace:", "".join("#" if v > 0.5 else "+" if v > 0.05 else "." for v in d))
