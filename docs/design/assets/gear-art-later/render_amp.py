"""Pre-rendered amp body + cab for Encore (2x resolution).
Usage: python3 -I render_amp.py OUTDIR
Geometry is defined in CSS px; rendering happens at S x that.
"""
import sys, os
import numpy as np
from PIL import Image
from scipy.ndimage import gaussian_filter

OUT = sys.argv[1]
os.makedirs(OUT, exist_ok=True)
S = 2  # render scale

L = np.array([-0.42, -0.62, 0.66]); L /= np.linalg.norm(L)
Vv = np.array([0.0, 0.0, 1.0])
Hh = L + Vv; Hh /= np.linalg.norm(Hh)


def pnoise(h, w, sy, sx, seed):
    r = np.random.default_rng(seed).standard_normal((h, w))
    fy = np.fft.fftfreq(h)[:, None]; fx = np.fft.fftfreq(w)[None, :]
    g = np.exp(-((fy * sy) ** 2 + (fx * sx) ** 2) * 2 * np.pi ** 2)
    n = np.real(np.fft.ifft2(np.fft.fft2(r) * g)); n -= n.mean()
    return n / (np.abs(n).max() + 1e-9)


def sd_rrect(X, Y, x0, y0, x1, y1, r):
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    bx, by = (x1 - x0) / 2, (y1 - y0) / 2
    qx = np.abs(X - cx) - bx + r
    qy = np.abs(Y - cy) - by + r
    out = np.hypot(np.maximum(qx, 0), np.maximum(qy, 0))
    return out + np.minimum(np.maximum(qx, qy), 0) - r


def round_profile(d, R):
    t = np.clip(d / R, 0, 1)
    return np.sqrt(1 - (1 - t) ** 2)


def chrome_env(n):
    R = np.dstack([2 * n[..., 2] * n[..., 0], 2 * n[..., 2] * n[..., 1], 2 * n[..., 2] ** 2 - 1])
    up = -R[..., 1] - 0.35 * R[..., 0]
    e = np.interp(up, [-1, -0.25, -0.05, 0.08, 0.3, 1], [110, 90, 30, 150, 245, 255])
    return np.dstack([e * 0.97, e * 0.99, e * 1.03])


def weave(X, Y, pitch, seed):
    u = X + Y; v = X - Y
    pa = np.cos(np.pi * ((u % pitch) / pitch - 0.5))
    pb = np.cos(np.pi * ((v % pitch) / pitch - 0.5))
    blk = pitch * 2
    topA = (np.floor(u / blk) % 2) == (np.floor(v / blk) % 2)
    fa = 0.08 * pnoise(*X.shape, 1.5, 1.5, seed)
    hA = pa * 0.8 + np.where(topA, 0.5, 0) + 0.05 * np.sin(v * 1.7) + fa
    hB = pb * 0.8 + np.where(topA, 0, 0.5) + 0.05 * np.sin(u * 1.6) - fa
    h = np.maximum(hA, hB)
    useA = hA >= hB
    ga = (np.floor(u / pitch) % 4 == 0); gb = (np.floor(v / pitch) % 4 == 2)
    cA = np.where(ga[..., None], [148, 108, 66], [120, 42, 31])
    cB = np.where(gb[..., None], [132, 96, 58], [96, 33, 24])
    col = np.where(useA[..., None], cA, cB).astype(float)
    return h, col


def render(name, Wc, Hc, body, panel=None, grille=None, handle=None, corner=22, seed=1):
    W, H = Wc * S, Hc * S
    Y, X = np.mgrid[0:H, 0:W].astype(float) + 0.5
    X /= S; Y /= S  # work in css px
    h = np.zeros((H, W)); alb = np.zeros((H, W, 3)); ks = np.zeros((H, W)); shin = np.full((H, W), 20.0)
    chrome = np.zeros((H, W), bool)

    # --- body (tolex) ---
    sdb = sd_rrect(X, Y, *body)
    inb = sdb < 0
    grain = 0.35 * pnoise(H, W, 1.6, 1.6, seed) - 0.5 * np.abs(pnoise(H, W, 2.8, 2.8, seed + 1)) ** 0.6 + 0.15 * pnoise(H, W, 60, 60, seed + 2)
    h = np.where(inb, 14 * round_profile(-sdb, 14) + 0.55 * grain, 0)
    alb[:] = [27, 27, 28]
    alb *= (1 + 0.06 * pnoise(H, W, 40, 40, seed + 3))[..., None]
    ks[:] = 0.22; shin[:] = 16

    def recess(rect, depth, mat):
        nonlocal h
        sd = sd_rrect(X, Y, *rect)
        m = sd < 0
        h = np.where(m, 14.0 - depth + 2.0 * round_profile(-sd, 2.0) - 2.0, h)
        # piping cord around the recess
        pd = np.abs(sd + 0.5) - 2.2
        pm = (pd < 0) & inb
        prof = np.sqrt(np.clip(1 - ((sd + 0.5) / 2.2) ** 2, 0, 1))
        h = np.where(pm, np.maximum(h, 14.5 + 2.2 * prof), h)
        alb[pm] = [214, 192, 128]; ks[pm] = 0.75; shin[pm] = 55
        return m, sd

    if panel is not None:
        pm, psd = recess(panel, 4.0, 'panel')
        br = 0.7 * pnoise(H, W, 0.5, 90, seed + 4) + 0.3 * pnoise(H, W, 0.3, 200, seed + 5)
        pal = np.array([226, 205, 150])[None, None, :] + br[..., None] * 18
        alb[pm] = pal[pm]
        streak = 0.82 + 0.38 * np.exp(-((X - panel[0] - (panel[2] - panel[0]) * 0.33) / ((panel[2] - panel[0]) * 0.16)) ** 2)
        alb[pm] = (pal * streak[..., None])[pm]
        ks[pm] = 0.3; shin[pm] = 24
        h = np.where(pm, h + 0.12 * br, h)
    if grille is not None:
        gm, gsd = recess(grille, 7.0, 'grille')
        wh, wc = weave(X * 1.0, Y * 1.0, 3.2, seed + 6)
        wc = wc * (1 + 0.08 * pnoise(H, W, 30, 30, seed + 9))[..., None]
        sag = 1.5 * np.clip(-gsd / 30, 0, 1)
        h = np.where(gm, h - 0 + 0.9 * wh - sag, h)
        alb[gm] = wc[gm] * (0.45 + 0.55 * np.clip(wh[gm] / 1.3, 0, 1))[:, None]
        ks[gm] = 0.12; shin[gm] = 10

    # --- chrome corner caps ---
    x0, y0, x1, y1, r = body
    for cx, cy, sx, sy in [(x0, y0, 1, 1), (x1, y0, -1, 1), (x0, y1, 1, -1), (x1, y1, -1, -1)]:
        lx = (X - cx) * sx; ly = (Y - cy) * sy
        cap = (lx >= 0) & (ly >= 0) & (lx < corner) & (ly < corner) & inb
        sdc = sd_rrect(lx, ly, -corner, -corner, corner, corner, corner * 0.55)
        capd = round_profile(np.clip(-sdc, 0, None), 6)
        h = np.where(cap, np.maximum(h, 15 + 4 * capd) + 0 * lx, h)
        chrome |= cap
        rv = cap & ((np.hypot(lx - corner * 0.45, ly - corner * 0.45)) < 2.4)
        h = np.where(rv, h + 1.5, h)

    # --- handle ---
    hmask = np.zeros((H, W), bool)
    if handle is not None:
        hx0, hy0, hx1, hy1 = handle
        sdh = sd_rrect(X, Y, hx0, hy0, hx1, hy1, (hy1 - hy0) / 2)
        hm = sdh < 0
        h = np.where(hm & ~inb, 8 * round_profile(-sdh, 8) + 0.4 * grain, h)
        alb[hm & ~inb] = [30, 26, 24]; ks[hm & ~inb] = 0.35; shin[hm & ~inb] = 25
        for ex in (hx0 - 4, hx1 - 10):
            sde = sd_rrect(X, Y, ex, hy1 - 6, ex + 14, hy1 + 6, 3)
            em = sde < 0
            h = np.where(em, 10 + 3 * round_profile(-sde, 3), h)
            chrome |= em
        hmask = hm | (chrome & ~inb)

    alpha = (inb | hmask).astype(float)

    # --- shading ---
    hs = gaussian_filter(h, 0.6 * S)
    dy, dx = np.gradient(hs)
    n = np.dstack([-dx * S, -dy * S, np.ones_like(hs)]); n /= np.linalg.norm(n, axis=2, keepdims=True)
    diff = np.clip((n * L).sum(2), 0, 1)
    spec = np.clip((n * Hh).sum(2), 0, 1) ** shin
    ao = np.clip(1 - 0.09 * (gaussian_filter(h, 6 * S) - h), 0.35, 1.08)
    ao2 = np.clip(1 - 0.05 * (gaussian_filter(h, 18 * S) - h), 0.5, 1.05)
    key = 1.18 - 0.28 * (X / Wc) - 0.38 * (Y / Hc)
    key += 0.12 * np.exp(-(((X - Wc * 0.3) / (Wc * 0.45)) ** 2 + ((Y - Hc * 0.2) / (Hc * 0.5)) ** 2))
    col = alb * (0.32 + 0.78 * diff)[..., None] * (ao * ao2 * key)[..., None] + (255 * ks * spec * key)[..., None]
    ch = chrome_env(n) * (ao * (0.85 + 0.25 * key))[..., None]
    col = np.where(chrome[..., None], ch, col)
    rgba = np.dstack([np.clip(col, 0, 255), alpha * 255]).astype(np.uint8)
    img = Image.fromarray(rgba, 'RGBA')
    img.save(os.path.join(OUT, name), optimize=True)
    print(name, img.size)


# Amp combo: 600 x 430 css px
render('amp_body.png', 600, 430,
       body=(0, 24, 600, 430, 18),
       panel=(22, 44, 578, 182, 8),
       grille=(22, 194, 578, 408, 8),
       handle=(228, 4, 372, 30),
       seed=11)

# Cab 2x12: 230 x 300 css px
render('cab_body.png', 230, 300,
       body=(0, 0, 230, 300, 16),
       grille=(16, 16, 214, 284, 8),
       corner=20, seed=21)
