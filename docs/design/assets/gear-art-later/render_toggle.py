"""Chrome bat-handle toggle switch (front view) for Encore amp panels.
Usage: python3 -I render_toggle.py OUTDIR
Writes toggle_lg_on.png (lever up) and toggle_lg_off.png (lever down), 80 x 140 px (2x of 40 x 70 CSS).
The earlier 64 x 112 version (toggle_on/off.png) used the same geometry with K = 1.0.
Same top-left key light as the other Encore renders.
"""
import sys, os
import numpy as np
from PIL import Image
from scipy.ndimage import gaussian_filter

OUT = sys.argv[1]
os.makedirs(OUT, exist_ok=True)
W, H, SS = 80, 140, 4
K = W / 64.0          # geometry was designed for 64 x 112; scale it up
L = np.array([-0.42, -0.62, 0.66]); L /= np.linalg.norm(L)


def chrome(n, bright=1.0):
    R = np.dstack([2 * n[..., 2] * n[..., 0], 2 * n[..., 2] * n[..., 1], 2 * n[..., 2] ** 2 - 1])
    up = -R[..., 1] - 0.35 * R[..., 0]
    e = np.interp(up, [-1, -0.3, -0.05, 0.08, 0.3, 1], [105, 85, 28, 150, 245, 255]) * bright
    return np.dstack([e * 0.97, e * 0.99, e * 1.03])


def render(name, lever_up):
    w, h = W * SS, H * SS
    y, x = np.mgrid[0:h, 0:w].astype(float) + 0.5
    x = (x / SS - W / 2) / K; y = y / SS / K   # design units, x centred
    cy = H / 2 / K                             # pivot
    hgt = np.zeros((h, w)); mat = np.zeros((h, w), int)   # 1 nut, 2 bushing, 3 lever
    # hex nut (flat sides left/right)
    ang = [0, np.pi / 3, 2 * np.pi / 3]
    hexr = np.max(np.stack([np.abs((x) * np.cos(a) + (y - cy) * np.sin(a)) for a in ang]), 0)
    nut = hexr < 14
    hgt[nut] = 4 + 1.5 * np.clip((14 - hexr[nut]) / 1.5, 0, 1)
    mat[nut] = 1
    r = np.hypot(x, y - cy)
    bush = r < 8.5
    hgt[bush] = 6.5 + 1.2 * np.cos(r[bush] * 2.2) * 0.3 + 2.5 * np.sqrt(np.clip(1 - (r[bush] / 8.5) ** 2, 0, 1))
    mat[bush] = 2
    # lever: bat shape from pivot to tip
    dirn = -1 if lever_up else 1
    t = (y - cy) * dirn                      # distance along lever (0 at pivot, + towards tip)
    length = 44
    tt = np.clip(t / length, 0, 1)
    halfw = 2.6 + 2.4 * tt ** 1.6            # widens towards the tip
    lev = (t >= 0) & (t <= length) & (np.abs(x) < halfw)
    tip = np.hypot(x, t - length) < halfw.max() * 0 + 5.0
    lever = lev | ((t > length - 1) & tip)
    prof = np.sqrt(np.clip(1 - (x / np.maximum(halfw, 1e-3)) ** 2, 0, 1))
    tipprof = np.sqrt(np.clip(1 - (np.hypot(x, t - length) / 5.0) ** 2, 0, 1))
    lh = np.where(lev, 10 + 3.5 * prof + 2.0 * tt, 0)
    lh = np.where(tip & (t > length - 1), np.maximum(lh, 12 + 4.5 * tipprof), lh)
    hgt = np.where(lever, np.maximum(hgt, lh), hgt)
    mat = np.where(lever, 3, mat)

    hs = gaussian_filter(hgt, 0.5 * SS)
    dy, dx = np.gradient(hs)
    n = np.dstack([-dx * SS, -dy * SS, np.ones_like(hs)]); n /= np.linalg.norm(n, axis=2, keepdims=True)
    diff = np.clip((n * L).sum(2), 0, 1)
    col = np.zeros((h, w, 3))
    steel = np.dstack([np.full_like(diff, 158), np.full_like(diff, 162), np.full_like(diff, 168)]) * (0.45 + 0.7 * diff)[..., None]
    spec = np.clip((n * ((L + [0, 0, 1]) / np.linalg.norm(L + [0, 0, 1]))).sum(2), 0, 1) ** 30 * 120
    steel += spec[..., None]
    col = np.where((mat == 1)[..., None], steel, col)
    col = np.where((mat == 2)[..., None], chrome(n, 0.85), col)
    col = np.where((mat == 3)[..., None], chrome(n, 1.0), col)
    # ambient occlusion where the lever meets the bushing
    ao = np.clip(1 - 0.06 * (gaussian_filter(hgt, 3 * SS) - hgt), 0.45, 1.0)
    col *= ao[..., None]
    alpha = (mat > 0).astype(float)
    img = Image.fromarray(np.dstack([np.clip(col, 0, 255), alpha * 255]).astype(np.uint8), 'RGBA').resize((W, H), Image.LANCZOS)
    img.save(os.path.join(OUT, name), optimize=True)
    print(name, img.size)


render('toggle_lg_on.png', True)
render('toggle_lg_off.png', False)
