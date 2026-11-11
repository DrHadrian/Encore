import sys, os
import numpy as np
from PIL import Image

OUT = sys.argv[1]
os.makedirs(OUT, exist_ok=True)
rng = np.random.default_rng(7)

L = np.array([-0.45, -0.6, 0.66]); L = L / np.linalg.norm(L)
V = np.array([0.0, 0.0, 1.0])
H = (L + V); H = H / np.linalg.norm(H)


def pnoise(h, w, sy, sx, seed=0):
    """Periodic gaussian-filtered noise, normalized to [-1, 1]."""
    r = np.random.default_rng(seed).standard_normal((h, w))
    fy = np.fft.fftfreq(h)[:, None]
    fx = np.fft.fftfreq(w)[None, :]
    g = np.exp(-((fy * sy) ** 2 + (fx * sx) ** 2) * 2 * np.pi ** 2)
    n = np.real(np.fft.ifft2(np.fft.fft2(r) * g))
    n -= n.mean()
    return n / (np.abs(n).max() + 1e-9)


def normals(h, strength, periodic=True):
    if periodic:
        dx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) * 0.5
        dy = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) * 0.5
    else:
        dy, dx = np.gradient(h)
    n = np.dstack([-dx * strength, -dy * strength, np.ones_like(h)])
    return n / np.linalg.norm(n, axis=2, keepdims=True)


def light(n, base, amb=0.35, kd=0.75, ks=0.4, shin=30):
    d = np.clip((n * L).sum(2), 0, 1)[..., None]
    s = np.clip((n * H).sum(2), 0, 1)[..., None] ** shin
    return np.clip(base * (amb + kd * d) + 255 * ks * s, 0, 255)


def save(arr, name, alpha=None, size=None):
    arr = np.clip(arr, 0, 255).astype(np.uint8)
    if alpha is not None:
        a = np.clip(alpha * 255, 0, 255).astype(np.uint8)
        img = Image.fromarray(np.dstack([arr, a]), 'RGBA')
    else:
        img = Image.fromarray(arr, 'RGB')
    if size:
        img = img.resize(size, Image.LANCZOS)
    img.save(os.path.join(OUT, name), optimize=True)
    print(name, img.size)


# ---------- tolex (leather grain) ----------
T = 512
grain = pnoise(T, T, 2.6, 2.6, 1)
creases = -np.abs(pnoise(T, T, 4.5, 4.5, 2)) ** 0.6
soft = pnoise(T, T, 60, 60, 3)
h = 0.55 * grain + 0.9 * creases + 0.25 * soft
n = normals(h, 2.0)
base = np.full((T, T, 3), [30, 30, 31], float) * (1 + 0.08 * soft[..., None])
save(light(n, base, amb=0.55, kd=0.7, ks=0.22, shin=18), 'tolex.png')

# ---------- diagonal basket-weave grille (oxblood + gold) ----------
T = 96
y, x = np.mgrid[0:T * 4, 0:T * 4] / 4.0
u = x + y
v = x - y
pitch = 6.0
pa = np.cos(np.pi * ((u % pitch) / pitch - 0.5))
pb = np.cos(np.pi * ((v % pitch) / pitch - 0.5))
bu = np.floor(u / 12) % 2
bv = np.floor(v / 12) % 2
topA = (bu == bv)
fib_a = 0.06 * np.sin(v * 3.1) + 0.04 * np.sin(v * 7.3)
fib_b = 0.06 * np.sin(u * 2.9) + 0.04 * np.sin(u * 6.7)
hA = pa * 0.8 + np.where(topA, 0.5, 0.0) + fib_a
hB = pb * 0.8 + np.where(topA, 0.0, 0.5) + fib_b
hh = np.maximum(hA, hB)
useA = hA >= hB
gold_a = (np.floor(u / pitch) % 4 == 0)
gold_b = (np.floor(v / pitch) % 4 == 2)
cA = np.where(gold_a[..., None], [178, 140, 82], [118, 42, 30])
cB = np.where(gold_b[..., None], [165, 128, 74], [96, 33, 24])
col = np.where(useA[..., None], cA, cB).astype(float)
ao = (0.3 + 0.7 * np.clip(hh / 1.3, 0, 1) ** 0.8)[..., None]
n = normals(hh, 1.6, periodic=True)
img = light(n, col * ao, amb=0.5, kd=0.75, ks=0.18, shin=12)
save(img, 'grille.png', size=(T * 2, T * 2))

# ---------- brushed metals ----------
Hh, Ww = 128, 512
br = 0.7 * pnoise(Hh, Ww, 0.6, 60, 4) + 0.3 * pnoise(Hh, Ww, 0.3, 140, 5)
for name, c, amp in [('brushed_silver.png', [204, 207, 211], 26), ('brushed_gold.png', [226, 206, 152], 22), ('brushed_dark.png', [120, 124, 129], 20)]:
    arr = np.array(c, float)[None, None, :] + br[..., None] * amp
    save(arr, name)

# ---------- metal-flake paint overlay ----------
T = 128
f = pnoise(T, T, 0.6, 0.6, 6) * 0.7 + pnoise(T, T, 1.5, 1.5, 8) * 0.3
rgb = np.where(f[..., None] > 0, 255, 0) * np.ones((T, T, 3))
save(rgb, 'flake.png', alpha=np.abs(f) * 0.28)

# ---------- round parts (supersampled) ----------
SS = 4


def grid(n):
    c = (np.arange(n * SS) + 0.5) / (n * SS) * 2 - 1
    yy, xx = np.meshgrid(c, c, indexing='ij')
    return xx, yy, np.hypot(xx, yy), np.arctan2(yy, xx)


def finish(rgb, alpha, n, name):
    img = Image.fromarray(np.dstack([np.clip(rgb, 0, 255), np.clip(alpha * 255, 0, 255)]).astype(np.uint8), 'RGBA')
    img = img.resize((n, n), Image.LANCZOS)
    img.save(os.path.join(OUT, name), optimize=True)
    print(name, img.size)


# black pedal knob: knurled skirt + spun-metal cap
N = 160
xx, yy, r, th = grid(N)
h = np.zeros_like(r)
sk = (r >= 0.6) & (r < 0.96)
cap = r < 0.6
h[sk] = (0.96 - r[sk]) / 0.36 * 0.55 + 0.02 * np.cos(36 * th[sk])
h[cap] = 0.62 + 0.12 * np.sqrt(np.clip(1 - (r[cap] / 0.6) ** 2, 0, 1))
n = normals(h * N * SS / 6, 1.0, periodic=False)
spun = 1 + 0.05 * np.sin(r * 260) + 0.03 * np.sin(r * 610)
bow = np.abs(np.cos(2 * (th + np.pi / 4))) ** 6
col = np.zeros(r.shape + (3,))
col[sk] = [20, 20, 21]
capc = np.array([178, 181, 186])[None, :] * spun[cap][:, None] + (bow[cap] * 70)[:, None]
col[cap] = capc
lit = light(n, col, amb=0.45, kd=0.65, ks=0.35, shin=40)
lit[cap] = np.clip(capc * 0.8 + 40 * np.clip((n[cap] * L).sum(1), 0, 1)[:, None], 0, 255)
alpha = (r < 0.96).astype(float)
finish(lit, alpha, N // 2 * 2, 'knob_black.png')

# cream dome knob (vintage pedal)
xx, yy, r, th = grid(N)
h = np.zeros_like(r)
skirt = (r >= 0.8) & (r < 0.96)
dome = r < 0.8
h[dome] = 0.35 + 0.65 * np.sqrt(np.clip(1 - (r[dome] / 0.8) ** 2, 0, 1)) ** 0.6
h[skirt] = 0.35 * (0.96 - r[skirt]) / 0.16 + 0.03 * np.cos(48 * th[skirt])
n = normals(h * N * SS / 5, 1.0, periodic=False)
col = np.zeros(r.shape + (3,)); col[:] = [238, 228, 202]
lit = light(n, col, amb=0.5, kd=0.6, ks=0.55, shin=45)
finish(lit, (r < 0.96).astype(float), N, 'knob_cream.png')

# silver small knob (reverb)
xx, yy, r, th = grid(N)
h = np.where(r < 0.95, 0.4 + 0.6 * np.sqrt(np.clip(1 - (r / 0.95) ** 2, 0, 1)) ** 0.4, 0) + np.where((r > 0.7) & (r < 0.95), 0.03 * np.cos(40 * th), 0)
n = normals(h * N * SS / 5, 1.0, periodic=False)
col = np.zeros(r.shape + (3,)); col[:] = [205, 208, 212]
col *= (1 + 0.05 * np.sin(r * 300))[..., None]
lit = light(n, col, amb=0.45, kd=0.6, ks=0.7, shin=50)
finish(lit, (r < 0.95).astype(float), N, 'knob_silver.png')

# chickenhead base (amp) - rotation invariant skirt
xx, yy, r, th = grid(N)
h = np.where(r < 0.95, 0.5 * (0.95 - r) / 0.95 + 0.25, 0) + np.where((r > 0.55) & (r < 0.95), 0.03 * np.cos(30 * th), 0)
n = normals(h * N * SS / 4, 1.0, periodic=False)
col = np.zeros(r.shape + (3,)); col[:] = [22, 22, 23]
lit = light(n, col, amb=0.5, kd=0.6, ks=0.45, shin=35)
finish(lit, (r < 0.95).astype(float), N, 'chicken_base.png')

# chickenhead pointer bar (lit mostly from above so rotation stays plausible)
BW, BH = 48, 176
c_u = (np.arange(BW * SS) + 0.5) / (BW * SS) * 2 - 1
c_t = (np.arange(BH * SS) + 0.5) / (BH * SS) * 2 - 1
tt, uu = np.meshgrid(c_t, c_u, indexing='ij')
wdt = 0.55 + 0.4 * (tt + 1) / 2
wdt = np.where(tt < -0.82, wdt * np.sqrt(np.clip(1 - ((tt + 0.82) / 0.18) ** 2, 0, 1)), wdt)
wdt = np.where(tt > 0.86, wdt * np.sqrt(np.clip(1 - ((tt - 0.86) / 0.14) ** 2, 0, 1)), wdt)
inside = np.abs(uu) < wdt
prof = np.sqrt(np.clip(1 - (uu / np.maximum(wdt, 1e-3)) ** 2, 0, 1))
h = np.where(inside, 0.3 + 0.7 * prof, 0)
Lsave = L.copy()
L = np.array([-0.15, -0.25, 0.95]); L = L / np.linalg.norm(L)
H = (L + V); H = H / np.linalg.norm(H)
n = normals(h * 30, 1.0, periodic=False)
col = np.zeros(h.shape + (3,)); col[:] = [16, 16, 17]
line = inside & (np.abs(uu) < 0.12) & (tt < -0.35) & (tt > -0.85)
col[line] = [240, 234, 218]
lit = light(n, col, amb=0.55, kd=0.5, ks=0.6, shin=55)
img = Image.fromarray(np.dstack([lit, inside * 255.0]).astype(np.uint8), 'RGBA').resize((BW // 2 * 1, BH // 2 * 1), Image.LANCZOS)
img = Image.fromarray(np.dstack([lit, inside * 255.0]).astype(np.uint8), 'RGBA').resize((BW, BH), Image.LANCZOS)
img.save(os.path.join(OUT, 'chicken_bar.png'), optimize=True)
print('chicken_bar.png', img.size)
L = Lsave; H = (L + V); H = H / np.linalg.norm(H)

# chrome footswitch with hex nut
N = 160
xx, yy, r, th = grid(N)
hexr = np.max(np.stack([np.abs(xx * np.cos(a) + yy * np.sin(a)) for a in (0, np.pi / 3, 2 * np.pi / 3)]), axis=0)
nut = hexr < 0.88
dome = r < 0.52
collar = (r >= 0.52) & (r < 0.62)
h = np.zeros_like(r)
h[nut] = 0.25 + 0.08 * np.clip((0.88 - hexr[nut]) / 0.06, 0, 1)
h[collar] = 0.36 + 0.02 * np.cos(r[collar] * 220)
h[dome] = 0.38 + 0.55 * np.sqrt(np.clip(1 - (r[dome] / 0.52) ** 2, 0, 1))
n = normals(h * N * SS / 4, 1.0, periodic=False)
R = np.dstack([2 * n[..., 2] * n[..., 0], 2 * n[..., 2] * n[..., 1], 2 * n[..., 2] ** 2 - 1])
up = -R[..., 1] - 0.35 * R[..., 0]
env = np.interp(up, [-1, -0.25, -0.05, 0.08, 0.3, 1], [120, 95, 35, 150, 245, 255])
chrome = np.dstack([env * 0.97, env * 0.99, env * 1.03])
steel = light(n, np.full(r.shape + (3,), [150, 154, 160], float), amb=0.5, kd=0.6, ks=0.5, shin=25)
col = np.where((dome | collar)[..., None], chrome, steel)
alpha = (nut | dome | collar).astype(float)
finish(col, alpha, N, 'footswitch.png')
