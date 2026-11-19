#!/usr/bin/env python3
"""Compare UI snapshots from the app with the design's target images.

    python docs/design/tools/ui-compare.py <snapshots-dir> docs/design/reference/target <out-dir>

For every PNG present in both folders (same file name), writes to <out-dir>:
    <name>-side.png     app | design side by side
    <name>-overlay.png  50/50 blend (misplaced edges show as doubles)
    <name>-diff.png     per-pixel difference, amplified (bright = different)
and prints a score per board: mean difference (0 = identical, < 6 is usually
"same layout, different anti-aliasing") and the share of strongly differing pixels.
Needs Pillow (pip install pillow). Snapshots are scaled to the target's size first.
"""
import sys
from pathlib import Path

try:
    from PIL import Image, ImageChops, ImageOps, ImageStat
except ImportError:
    sys.exit("Pillow is needed: pip install pillow")


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    snaps, targets, out = (Path(a) for a in sys.argv[1:])
    out.mkdir(parents=True, exist_ok=True)
    rows = []
    for target in sorted(targets.glob("*.png")):
        if "@2x" in target.name:
            continue
        snap = snaps / target.name
        if not snap.exists():
            rows.append((target.stem, None, None))
            continue
        t = Image.open(target).convert("RGB")
        s = Image.open(snap).convert("RGB").resize(t.size, Image.LANCZOS)
        side = Image.new("RGB", (t.width * 2 + 8, t.height), (255, 0, 255))
        side.paste(s, (0, 0))
        side.paste(t, (t.width + 8, 0))
        side.save(out / f"{target.stem}-side.png")
        Image.blend(s, t, 0.5).save(out / f"{target.stem}-overlay.png")
        diff = ImageChops.difference(s, t)
        ImageOps.autocontrast(diff.convert("L"), cutoff=1).save(out / f"{target.stem}-diff.png")
        mean = sum(ImageStat.Stat(diff).mean) / 3
        hist = diff.convert("L").histogram()
        strong = sum(hist[49:]) / (t.width * t.height)
        rows.append((target.stem, mean, strong))
    print(f"{'board':32} {'mean diff':>10} {'>48 px':>8}")
    for name, mean, strong in rows:
        if mean is None:
            print(f"{name:32} {'missing':>10}")
        else:
            print(f"{name:32} {mean:10.2f} {strong * 100:7.2f}%")


if __name__ == "__main__":
    main()
