"""Amp pilot light: faceted red glass jewel in a knurled chrome bezel.
Usage: python3 -I render_jewel.py OUTDIR
Writes jewel_on.png (lit from inside) and jewel_off.png, 96 x 96 px (shown at 48 x 48 CSS, 2x).
Same top-left key light as the other Encore renders. Add the glow on the panel in the UI
(a soft red radial gradient behind the lit jewel), not in the image.
"""
import sys, os
import numpy as np
from PIL import Image
from scipy.ndimage import gaussian_filter

OUT = sys.argv[1]
os.makedirs(OUT, exist_ok=True)
N, SS = 96, 4
L = np.array([-0.42, -0.62, 0.66]); L /= np.linalg.norm(L)
Hv = L + np.array([0, 0, 1.0]); Hv /= np.linalg.norm(Hv)
n = N * SS
c = (np.arange(n) + 0.5) / n * 2 - 1
Y, X = np.meshgrid(c, c, indexing='ij')
R = np.hypot(X, Y)
TH = np.arctan2(Y, X)

R_JEWEL, R_BEZEL_IN, R_BEZEL_OUT = 0.58, 0.60, 0.92
FACETS = 10


def chrome(nrm, bright=1.0):
    Rv = np.dstack([2 * nrm[..., 2] * nrm[..., 0], 2 * nrm[..., 2] * nrm[..., 1], 2 * nrm[..., 2] ** 2 - 1])
    up = -Rv[..., 1] - 0.35 * Rv[..., 0]
    e = np.interp(up, [-1, -0.3, -0.05, 0.08, 0.3, 1], [105, 85, 28, 150, 245, 255]) * bright
    return np.dstack([e * 0.97, e * 0.99, e * 1.03])


def render(name, lit):
    h = np.zeros_like(R)
    mat = np.zeros(R.shape, int)  # 1 bezel, 2 jewel
    bez = (R >= R_BEZEL_IN) & (R < R_BEZEL_OUT)
    t = (R - R_BEZEL_IN) / (R_BEZEL_OUT - R_BEZEL_IN)
    h = np.where(bez, 0.30 * np.sin(np.pi * np.clip(t, 0, 1)) ** 0.7 + 0.015 * np.cos(72 * TH) * (t > 0.55), h)
    mat[bez] = 1
    # faceted dome: a flat table in the middle, FACETS planar crown facets around it
    jw = R < R_JEWEL
    sector = np.floor((TH + np.pi) / (2 * np.pi) * FACETS)
    mid = (sector + 0.5) / FACETS * 2 * np.pi - np.pi
    radial = X * np.cos(mid) + Y * np.sin(mid)          # planar facet: depends on projection onto sector axis
    table = 0.22
    crown = 0.55 - 0.85 * np.clip(radial - table, 0, None)
    hj = np.where(R < table, 0.55, crown)
    h = np.where(jw, hj, h)
    mat[jw] = 2

    hs = gaussian_filter(h, 0.35 * SS)
    gy, gx = np.gradient(hs * n / 2.0)
    nrm = np.dstack([-gx, -gy, np.ones_like(hs)]); nrm /= np.linalg.norm(nrm, axis=2, keepdims=True)
    diff = np.clip((nrm * L).sum(2), 0, 1)
    spec = np.clip((nrm * Hv).sum(2), 0, 1)

    col = np.zeros(R.shape + (3,))
    col = np.where((mat == 1)[..., None], chrome(nrm, 0.92), col)
    # glass: light from inside (bright core, darker toward the rim) plus each facet catching it differently
    core = np.clip(1 - R / R_JEWEL, 0, 1)
    facet_var = 0.75 + 0.25 * np.cos(sector * 2.3) * (R > table)
    if lit:
        inner = (0.35 + 0.95 * core ** 0.6) * facet_var
        glass = np.dstack([255 * np.clip(inner, 0, 1.2), 70 * inner ** 1.6, 45 * inner ** 2.2])
        glass[..., 1] += 160 * core ** 3          # hot, slightly orange centre
        glass[..., 2] += 90 * core ** 4
    else:
        inner = (0.25 + 0.25 * diff) * facet_var
        glass = np.dstack([120 * inner, 22 * inner, 18 * inner])
    glass += (255 * (0.9 * spec ** 80 + 0.18 * spec ** 10))[..., None]   # glass reflections of the key light
    col = np.where((mat == 2)[..., None], glass, col)
    ao = np.clip(1 - 2.0 * (gaussian_filter(h, 3 * SS) - h), 0.55, 1.0)
    col *= ao[..., None]
    alpha = (mat > 0).astype(float)
    img = Image.fromarray(np.dstack([np.clip(col, 0, 255), alpha * 255]).astype(np.uint8), 'RGBA').resize((N, N), Image.LANCZOS)
    img.save(os.path.join(OUT, name), optimize=True)
    print(name, img.size)


render('jewel_on.png', True)
render('jewel_off.png', False)
