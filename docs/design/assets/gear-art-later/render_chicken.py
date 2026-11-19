"""Chickenhead amp knob as a filmstrip: one fully lit 3D render per position.
Usage: python3 -I render_chicken.py OUTDIR
Writes chicken_strip.png: FRAMES frames stacked vertically, each 128 x 128 px
(shown at 64 x 64 CSS, 2x). Frame 0 = fully left (-135 deg), last = fully right (+135 deg).
The knob's own soft contact shadow is baked into every frame. Same top-left key light
as every other Encore render, so highlights stay put while the pointer turns.
In JUCE: draw frame round(value * (FRAMES - 1)) from the strip.
"""
import sys, os
import numpy as np
from PIL import Image
from scipy.ndimage import gaussian_filter

OUT = sys.argv[1]
os.makedirs(OUT, exist_ok=True)
FRAMES = 41
N, SS = 128, 3
L = np.array([-0.42, -0.62, 0.66]); L /= np.linalg.norm(L)
Hv = L + np.array([0, 0, 1.0]); Hv /= np.linalg.norm(Hv)

n = N * SS
c = (np.arange(n) + 0.5) / n * 2 - 1          # -1..1
Y, X = np.meshgrid(c, c, indexing='ij')
R = np.hypot(X, Y)
TH = np.arctan2(Y, X)

# radii in knob units (1.0 = frame edge)
R_SKIRT = 0.62     # round skirt with fine knurling
R_DOME = 0.30


def frame(angle_deg):
    a = np.radians(angle_deg)
    # pointer direction: 0 deg = straight up
    ux, uy = np.sin(a), -np.cos(a)
    # coordinates along / across the pointer
    t = X * ux + Y * uy
    s = -X * uy + Y * ux
    h = np.zeros_like(R)
    mat = np.zeros(R.shape, int)  # 1 skirt, 2 body, 3 inlay line

    # skirt: low, bevelled disc with knurled rim
    sk = R < R_SKIRT
    prof = np.clip((R_SKIRT - R) / 0.08, 0, 1)
    h = np.where(sk, 0.16 * np.sqrt(1 - (1 - prof) ** 2) + 0.012 * np.cos(60 * TH) * (R > R_SKIRT - 0.07), h)
    mat[sk] = 1

    # chickenhead body: wide rounded base tapering to a point along +t, short tail along -t
    tt = np.clip((t + 0.30) / (0.80 + 0.30), 0, 1)           # 0 at tail end, 1 at tip
    halfw = 0.25 * (1 - tt) ** 0.75 + 0.045                     # wide at back, pointed at front
    inside = (t > -0.30) & (t < 0.80) & (np.abs(s) < halfw)
    tail = np.hypot(s, t + 0.08) < 0.25                         # rounded back
    body = inside | tail
    across = np.clip(np.abs(s) / np.maximum(halfw, 1e-3), 0, 1)
    ridge = np.sqrt(np.clip(1 - across ** 2, 0, 1))
    hb = 0.26 + 0.30 * ridge * (1 - 0.30 * tt)
    tailh = 0.26 + 0.30 * np.sqrt(np.clip(1 - (np.hypot(s, t + 0.08) / 0.25) ** 2, 0, 1))
    hb = np.where(tail & ~inside, tailh, np.maximum(hb, np.where(tail, tailh, 0)))
    # centre dome (screw cap)
    dome = R < R_DOME
    hd = 0.54 + 0.08 * np.sqrt(np.clip(1 - (R / R_DOME) ** 2, 0, 1))
    hb = np.where(dome & body, np.maximum(hb, hd), hb)
    h = np.where(body, np.maximum(h, hb), h)
    mat = np.where(body, 2, mat)

    # white inlay line on the pointer
    inlay = body & (np.abs(s) < 0.022) & (t > 0.26) & (t < 0.66)
    h = np.where(inlay, h - 0.012, h)
    mat = np.where(inlay, 3, mat)

    hs = gaussian_filter(h, 0.6 * SS)
    gy, gx = np.gradient(hs * n / 2.2)
    nrm = np.dstack([-gx, -gy, np.ones_like(hs)]); nrm /= np.linalg.norm(nrm, axis=2, keepdims=True)
    diff = np.clip((nrm * L).sum(2), 0, 1)
    spec = np.clip((nrm * Hv).sum(2), 0, 1)
    # glossy black bakelite: dark albedo, tight bright highlight + broad sheen
    base = np.where(mat[..., None] == 3, np.array([236, 230, 214.0]), np.array([20, 20, 21.0]))
    col = base * (0.35 + 0.75 * diff)[..., None]
    gloss = np.where(mat == 3, 0.25, 1.0)
    col += (255 * (0.55 * spec ** 60 + 0.12 * spec ** 8) * gloss)[..., None]
    ao = np.clip(1 - 2.2 * (gaussian_filter(h, 4 * SS) - h), 0.5, 1.0)
    col *= ao[..., None]
    alpha_knob = (mat > 0).astype(float)

    # baked contact shadow, offset down-right
    sh = gaussian_filter(np.roll(np.roll(alpha_knob, int(0.05 * n), 0), int(0.03 * n), 1), 0.045 * n)
    sh_a = np.clip(sh * 0.65, 0, 0.65)
    out_a = alpha_knob + sh_a * (1 - alpha_knob)
    out_rgb = np.where(alpha_knob[..., None] > 0, col, 0)
    rgba = np.dstack([np.clip(out_rgb, 0, 255), np.clip(out_a * 255, 0, 255)]).astype(np.uint8)
    return Image.fromarray(rgba, 'RGBA').resize((N, N), Image.LANCZOS)


strip = Image.new('RGBA', (N, N * FRAMES), (0, 0, 0, 0))
for i in range(FRAMES):
    ang = -135 + 270 * i / (FRAMES - 1)
    strip.paste(frame(ang), (0, i * N))
strip.save(os.path.join(OUT, 'chicken_strip.png'), optimize=True)
print('chicken_strip.png', strip.size, FRAMES, 'frames')
