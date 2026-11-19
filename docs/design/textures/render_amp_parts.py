"""Amp builder layers for Encore (2x resolution), same geometry and light as render_amp.py.
Usage: python3 -I render_amp_parts.py OUTDIR
Writes, inside the 600 x 430 amp box, as WebP (only files that don't exist yet):
  body_<layout>_<tolex>_<piping>.webp  full 600x430: tolex, piping (or none), handle; panel and grille areas cut out
  layouts: tall, thin (combos), head (a short box with a deep framed panel, no grille) and top
  (a combo with its panel on the top surface, seen from slightly above; the front is all grille)
  and headg (a head with a grille above a bottom control panel)
  panel_<layout>_<material>.webp       the panel recess only, at (22,44) tall/thin, (30,128) head, (34,40) top, (28,296) headg
  grille_<layout>_<cloth>.webp         the grille recess only, at (22,194) tall / (22,128) thin / (40,172) top / (28,112) headg
  corner_<chrome|black>_<tl|tr|bl|br>.webp  22x22 corner caps
Stack them: body, panel, grille, corners. Knobs, print and the Encore logo are overlays.
"""
import sys, os
import numpy as np
from PIL import Image
from scipy.ndimage import gaussian_filter

OUT = sys.argv[1]
os.makedirs(OUT, exist_ok=True)
S = 2
L = np.array([-0.42, -0.62, 0.66]); L /= np.linalg.norm(L)
Vv = np.array([0.0, 0.0, 1.0]); Hh = L + Vv; Hh /= np.linalg.norm(Hh)


def pnoise(h, w, sy, sx, seed):
    r = np.random.default_rng(seed).standard_normal((h, w))
    fy = np.fft.fftfreq(h)[:, None]; fx = np.fft.fftfreq(w)[None, :]
    g = np.exp(-((fy * sy) ** 2 + (fx * sx) ** 2) * 2 * np.pi ** 2)
    n = np.real(np.fft.ifft2(np.fft.fft2(r) * g)); n -= n.mean()
    return n / (np.abs(n).max() + 1e-9)


def sd_rrect(X, Y, x0, y0, x1, y1, r):
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    bx, by = (x1 - x0) / 2, (y1 - y0) / 2
    qx = np.abs(X - cx) - bx + r; qy = np.abs(Y - cy) - by + r
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


def weave(X, Y, pitch, seed, a1, a2, b1, b2):
    u = X + Y; v = X - Y
    pa = np.cos(np.pi * ((u % pitch) / pitch - 0.5)); pb = np.cos(np.pi * ((v % pitch) / pitch - 0.5))
    blk = pitch * 2
    topA = (np.floor(u / blk) % 2) == (np.floor(v / blk) % 2)
    fa = 0.08 * pnoise(*X.shape, 1.5, 1.5, seed)
    hA = pa * 0.8 + np.where(topA, 0.5, 0) + 0.05 * np.sin(v * 1.7) + fa
    hB = pb * 0.8 + np.where(topA, 0, 0.5) + 0.05 * np.sin(u * 1.6) - fa
    h = np.maximum(hA, hB); useA = hA >= hB
    ga = (np.floor(u / pitch) % 4 == 0); gb = (np.floor(v / pitch) % 4 == 2)
    cA = np.where(ga[..., None], a1, a2); cB = np.where(gb[..., None], b1, b2)
    return h, np.where(useA[..., None], cA, cB).astype(float)


TOLEX = {'tweed': [204, 164, 96], 'orange': [200, 96, 30], 'white': [224, 222, 214], 'black': [27, 27, 28], 'cream': [214, 198, 160], 'brown': [78, 50, 32], 'red': [110, 28, 24], 'blonde': [196, 164, 112]}
PIPING = {'gold': [214, 192, 128], 'white': [228, 226, 220], 'black': [30, 30, 32], 'none': None}
PANEL = {  # albedo, brush strength, ks, shininess
    'gold': ([226, 205, 150], 18, 0.30, 24), 'silver': ([206, 209, 214], 16, 0.35, 28), 'black': ([30, 30, 33], 3, 0.45, 40),
    'plexi': ([236, 200, 120], 4, 0.70, 70), 'dark': ([96, 98, 104], 14, 0.35, 26),
    'white': ([236, 234, 228], 1.5, 0.22, 30), 'orange': ([222, 108, 32], 1.5, 0.28, 30)}  # painted panels
GRILLE = {  # weave colours: A stripe, A base, B stripe, B base
    'oxblood': ([148, 108, 66], [120, 42, 31], [132, 96, 58], [96, 33, 24]),
    'silver': ([176, 184, 196], [128, 138, 152], [160, 168, 180], [112, 122, 136]),
    'wheat': ([214, 186, 128], [176, 146, 92], [198, 170, 116], [160, 130, 80]),
    'salt': None, 'black': None, 'brass': None}  # brass: perforated metal sheet, big round holes

BODY = (0, 24, 600, 430, 18); HANDLE = (228, 4, 372, 30)
LAYOUT = {'tall': ((22, 44, 578, 182, 8), (22, 194, 578, 408, 8)),   # panel 138 high (the Edit amp)
          'thin': ((22, 44, 578, 116, 6), (22, 128, 578, 408, 8)),   # panel 72 high, big grille
          'head': ((30, 128, 570, 368, 6), None),                    # head: framed panel, no grille
          'top': ((34, 40, 566, 110, 6), (40, 172, 560, 400, 8)),    # panel on the top surface, wide tolex border
          'headg': ((28, 296, 572, 380, 5), (28, 112, 572, 290, 6))}  # head: grille above, control panel below
TOPEDGE = {'top': 132}   # y of the front top edge: above it the box's top surface (normals tilted up)
BODIES = {'tall': (BODY, HANDLE, 4.0), 'thin': (BODY, HANDLE, 4.0), 'top': (BODY, HANDLE, 4.0), 'head': ((0, 96, 600, 400, 14), (228, 76, 372, 102), 9.0),
          'headg': ((0, 96, 600, 400, 14), (228, 76, 372, 102), 5.0)}
Wc, Hc = 600, 430


def build(tolex='black', piping='gold', panel='gold', grille='oxblood', layout='tall', corners=None, seed=11):
    PANR, GRR = LAYOUT[layout]
    BODY, HANDLE, PDEPTH = BODIES[layout]
    W, H = Wc * S, Hc * S
    Y, X = np.mgrid[0:H, 0:W].astype(float) + 0.5
    X /= S; Y /= S
    alb = np.zeros((H, W, 3)); ks = np.zeros((H, W)); shin = np.full((H, W), 20.0); chrome = np.zeros((H, W), bool)
    sdb = sd_rrect(X, Y, *BODY); inb = sdb < 0
    grain = 0.35 * pnoise(H, W, 1.6, 1.6, seed) - 0.5 * np.abs(pnoise(H, W, 2.8, 2.8, seed + 1)) ** 0.6 + 0.15 * pnoise(H, W, 60, 60, seed + 2)
    h = np.where(inb, 14 * round_profile(-sdb, 14) + 0.55 * grain, 0)
    alb[:] = TOLEX[tolex]
    if tolex == 'tweed':  # lacquered tweed: diagonal twill with a fine weft, amber lacquer
        u = X + Y
        thread = 0.5 + 0.5 * np.cos(2 * np.pi * (u % 3.4) / 3.4)
        weft = 0.5 + 0.5 * np.cos(2 * np.pi * Y / 1.7)
        mix = (thread ** 1.4)[..., None]
        alb = np.array([118, 80, 40], float) * (1 - mix) + np.array([224, 188, 120], float) * mix
        alb *= (0.95 + 0.08 * weft)[..., None]
        h = np.where(inb, h + 0.3 * thread, h)
    alb *= (1 + 0.06 * pnoise(H, W, 40, 40, seed + 3))[..., None]
    ks[:] = 0.22 if tolex == 'black' else 0.3 if tolex == 'tweed' else 0.14; shin[:] = 28 if tolex == 'tweed' else 16

    def recess(rect, depth):
        nonlocal h
        sd = sd_rrect(X, Y, *rect); m = sd < 0
        h = np.where(m, 14.0 - depth + 2.0 * round_profile(-sd, 2.0) - 2.0, h)
        pd = np.abs(sd + 0.5) - 2.2; pm = (pd < 0) & inb
        prof = np.sqrt(np.clip(1 - ((sd + 0.5) / 2.2) ** 2, 0, 1))
        if PIPING[piping] is None: return m, sd
        h = np.where(pm, np.maximum(h, 14.5 + 2.2 * prof), h)
        alb[pm] = PIPING[piping]; ks[pm] = 0.75 if piping != 'black' else 0.4; shin[pm] = 55
        return m, sd

    pm, psd = recess(PANR, PDEPTH)
    pm = psd < -2.7   # material only inside the piping cord
    pal_c, brush, pks, pshin = PANEL[panel]
    br = 0.7 * pnoise(H, W, 0.5, 90, seed + 4) + 0.3 * pnoise(H, W, 0.3, 200, seed + 5)
    pal = np.array(pal_c, float)[None, None, :] + br[..., None] * brush
    streak = 0.82 + 0.38 * np.exp(-((X - PANR[0] - (PANR[2] - PANR[0]) * 0.33) / ((PANR[2] - PANR[0]) * 0.16)) ** 2)
    if panel in ('black', 'white', 'orange'): streak = 0.92 + 0.2 * (streak - 0.82)  # painted: little sheen
    if panel in ('white', 'orange'): pal = pal * 1.12
    alb[pm] = (pal * streak[..., None])[pm]; ks[pm] = pks; shin[pm] = pshin
    h = np.where(pm, h + 0.12 * br * (brush / 18), h)

    if GRR is None:
        gsd = np.full((H, W), 99.0); gm = np.zeros((H, W), bool)
    else:
        gm, gsd = recess(GRR, 7.0)
        gm = gsd < -2.7
    sag = 1.5 * np.clip(-gsd / 30, 0, 1)
    if GRR is None: pass
    elif GRILLE[grille]:
        wh, wc = weave(X, Y, 3.2, seed + 6, *GRILLE[grille])
        wc = wc * (1 + 0.08 * pnoise(H, W, 30, 30, seed + 9))[..., None]
        h = np.where(gm, h + 0.9 * wh - sag, h)
        alb[gm] = wc[gm] * (0.45 + 0.55 * np.clip(wh[gm] / 1.3, 0, 1))[:, None]
        ks[gm] = 0.12; shin[gm] = 10
    elif grille == 'salt':
        rng = np.random.default_rng(seed + 12)
        fleck = rng.random((H, W))
        base = np.where(fleck[..., None] > 0.62, [196, 192, 182], np.where(fleck[..., None] < 0.30, [34, 32, 30], [110, 104, 96]))
        wh, _ = weave(X, Y, 2.4, seed + 6, [0, 0, 0], [0, 0, 0], [0, 0, 0], [0, 0, 0])
        h = np.where(gm, h + 0.6 * wh - sag, h)
        alb[gm] = (base * (0.6 + 0.4 * np.clip(wh / 1.3, 0, 1))[..., None])[gm]
        ks[gm] = 0.1; shin[gm] = 10
    elif grille == 'brass':  # perforated brass sheet: 7.6 px hex pitch, 2.4 px holes, dark behind
        rh = 7.6 * 0.866; row = np.floor(Y / rh); off = (row % 2) * 3.8
        hole = np.hypot(((X - off) % 7.6) - 3.8, (Y % rh) - rh / 2) < 2.4
        bb = 0.7 * pnoise(H, W, 0.5, 90, seed + 13)
        h = np.where(gm, h - 0.6 * sag - np.where(hole, 2.0, 0), h)
        metal = (np.array([212, 184, 112], float)[None, None, :] + bb[..., None] * 14)
        alb[gm] = np.where(hole[..., None], np.array([10, 10, 11], float)[None, None, :], metal)[gm]
        ks[gm] = np.where(hole, 0.05, 0.4)[gm]; shin[gm] = 34
    else:  # black mesh: perforated
        hole = (np.hypot((X % 4.0) - 2.0, (Y % 4.0) - 2.0) < 1.05)
        h = np.where(gm, h - sag - np.where(hole, 1.2, 0), h)
        alb[gm] = np.where(hole[..., None], [8, 8, 9], [34, 34, 37])[gm]
        ks[gm] = 0.3; shin[gm] = 30

    x0, y0, x1, y1, r = BODY; corner = 22
    capall = np.zeros((H, W), bool)
    for cx, cy, sx, sy in [(x0, y0, 1, 1), (x1, y0, -1, 1), (x0, y1, 1, -1), (x1, y1, -1, -1)]:
        lx = (X - cx) * sx; ly = (Y - cy) * sy
        cap = (lx >= 0) & (ly >= 0) & (lx < corner) & (ly < corner) & inb
        sdc = sd_rrect(lx, ly, -corner, -corner, corner, corner, corner * 0.55)
        capd = round_profile(np.clip(-sdc, 0, None), 6)
        if corners is None: continue
        h = np.where(cap, np.maximum(h, 15 + 4 * capd), h); capall |= cap
        if corners == 'chrome': chrome |= cap
        else: alb[cap] = [22, 22, 24]; ks[cap] = 0.5; shin[cap] = 40
        rv = cap & ((np.hypot(lx - corner * 0.45, ly - corner * 0.45)) < 2.4)
        h = np.where(rv, h + 1.5, h)

    hx0, hy0, hx1, hy1 = HANDLE
    sdh = sd_rrect(X, Y, hx0, hy0, hx1, hy1, (hy1 - hy0) / 2); hm = sdh < 0
    h = np.where(hm & ~inb, 8 * round_profile(-sdh, 8) + 0.4 * grain, h)
    alb[hm & ~inb] = [30, 26, 24]; ks[hm & ~inb] = 0.35; shin[hm & ~inb] = 25
    for ex in (hx0 - 4, hx1 - 10):
        sde = sd_rrect(X, Y, ex, hy1 - 6, ex + 14, hy1 + 6, 3); em = sde < 0
        h = np.where(em, 10 + 3 * round_profile(-sde, 3), h); chrome |= em
    hmask = hm | (chrome & ~inb)
    alpha = (inb | hmask).astype(float)

    hs = gaussian_filter(h, 0.6 * S)
    dy, dx = np.gradient(hs)
    n = np.dstack([-dx * S, -dy * S, np.ones_like(hs)])
    if layout in TOPEDGE:  # the top surface faces up: tilt its normals, rounding into the front over 8 px
        w = np.clip((TOPEDGE[layout] - Y) / 8, 0, 1); w = w * w * (3 - 2 * w) * (inb & ~chrome)
        w = np.where(psd < 0, 0.3 * w, w)  # the panel sits a little more upright, so metal doesn't flare
        n[..., 1] -= 0.62 * w
    n /= np.linalg.norm(n, axis=2, keepdims=True)
    diff = np.clip((n * L).sum(2), 0, 1)
    spec = np.clip((n * Hh).sum(2), 0, 1) ** shin
    ao = np.clip(1 - 0.09 * (gaussian_filter(h, 6 * S) - h), 0.35, 1.08)
    ao2 = np.clip(1 - 0.05 * (gaussian_filter(h, 18 * S) - h), 0.5, 1.05)
    key = 1.18 - 0.28 * (X / Wc) - 0.38 * (Y / Hc)
    key += 0.12 * np.exp(-(((X - Wc * 0.3) / (Wc * 0.45)) ** 2 + ((Y - Hc * 0.2) / (Hc * 0.5)) ** 2))
    col = alb * (0.32 + 0.78 * diff)[..., None] * (ao * ao2 * key)[..., None] + (255 * ks * spec * key)[..., None]
    col = np.where(chrome[..., None], chrome_env(n) * (ao * (0.85 + 0.25 * key))[..., None], col)
    rgba = np.dstack([np.clip(col, 0, 255), alpha * 255]).astype(np.uint8)
    return rgba, psd, gsd, capall


def cut(rgba, sd, rect):
    inner = sd < -2.7
    x0, y0, x1, y1 = [int(v * S) for v in rect[:4]]
    part = rgba[y0:y1, x0:x1].copy()
    part[..., 3] = np.where(inner[y0:y1, x0:x1], 255, 0)
    return part


def save(a, name):
    if os.path.exists(os.path.join(OUT, name)): return
    Image.fromarray(a, 'RGBA').save(os.path.join(OUT, name), 'WEBP', quality=90, method=6); print(name)


def todo(name): return not os.path.exists(os.path.join(OUT, name))


for lay in LAYOUT:
    PANR, GRR = LAYOUT[lay]
    for t in TOLEX:
        for p in PIPING:
            if not todo(f'body_{lay}_{t}_{p}.webp'): continue
            rgba, psd, gsd, _ = build(tolex=t, piping=p, layout=lay)
            body = rgba.copy()
            body[..., 3] = np.where((psd < -2.7) | (gsd < -2.7), 0, body[..., 3])
            save(body, f'body_{lay}_{t}_{p}.webp')
    for m in PANEL:
        if not todo(f'panel_{lay}_{m}.webp'): continue
        rgba, psd, gsd, _ = build(panel=m, layout=lay)
        save(cut(rgba, psd, PANR), f'panel_{lay}_{m}.webp')
    for g in GRILLE if GRR else []:
        if not todo(f'grille_{lay}_{g}.webp'): continue
        rgba, psd, gsd, _ = build(grille=g, layout=lay)
        save(cut(rgba, gsd, GRR), f'grille_{lay}_{g}.webp')
for c in ('chrome', 'black'):
    if not todo(f'corner_{c}_tl.webp'): continue
    rgba, psd, gsd, cap = build(corners=c)
    for nm, (x0, y0) in {'tl': (0, 24), 'tr': (578, 24), 'bl': (0, 408), 'br': (578, 408)}.items():
        part = rgba[y0 * S:(y0 + 22) * S, x0 * S:(x0 + 22) * S].copy()
        part[..., 3] = np.where(cap[y0 * S:(y0 + 22) * S, x0 * S:(x0 + 22) * S], 255, 0)
        save(part, f'corner_{c}_{nm}.webp')
