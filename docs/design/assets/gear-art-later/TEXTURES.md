# Encore — texture & render library

Every texture and pre-rendered part used in the Encore UI design. All are PNG, rendered procedurally with Python (numpy + Pillow + scipy) by the two scripts stored alongside this doc (`render_assets.py`, `render_amp.py`). Re-run them to regenerate or tweak:

```
python3 -I render_assets.py OUTDIR   # tiles, knobs, footswitch
python3 -I render_amp.py OUTDIR      # amp body + cab body
```

Lighting convention for everything: one key light from the top-left (−0.42, −0.62, 0.66), viewer straight on. Keep new renders on the same light so parts match.

Rule: textures are always kept — never delete or overwrite one without saving the previous version.

## Tileable textures (seamless)

| File | Size | Show at (CSS) | Use | Asset URL in the design canvas |
|---|---|---|---|---|
| tolex.png | 512×512 | 192 px tile | Black leather tolex: pedalboard surface, amp/cab covering | /_blob/3d6f9110dbe6da1bfe549597457b1148 |
| grille.png | 192×192 | 96 px tile | Diagonal oxblood/gold basket-weave grille cloth | /_blob/89e911aa9d6c259e340364da8e57fb5b |
| brushed_silver.png | 512×128 | 256×64 tile | Brushed aluminium (Compressor, Tape Echo frame) | /_blob/1503142138f2ba8bf42c6907b26dab50 |
| brushed_gold.png | 512×128 | 256×64 tile | Brushed gold control panels | /_blob/d464f7a8894845a18991d1f153de44c7 |
| brushed_dark.png | 512×128 | 256×64 tile | Dark brushed steel (Graphic EQ, jack plates) | /_blob/3300ebb17d9250b0fb9da48236c8955a |
| flake.png | 128×128 (RGBA) | 96 px tile, blend: overlay | Metal-flake speckle over painted pedals | /_blob/a71b9321d213c34f162508c0002c0987 |

## Pre-rendered parts (transparent PNG, 2× resolution)

| File | Size | Show at (CSS) | Use | Asset URL |
|---|---|---|---|---|
| amp_body.png | 1200×860 | 600×430 | Combo amp body: tolex with rounded edges, gold piping, recessed brushed-gold panel (x22–578, y44–182), grille (x22–578, y194–408), chrome corners, handle. Knobs/logo are HTML overlays. | /_blob/25ebf133be833b5b5ad3c6370435ba6b |
| cab_body.png | 460×600 | 230×300 | 2×12 cab: tolex, piping, grille, chrome corners | /_blob/cfd4bdf72f9b6b40b69b3a73ae74e4d2 |
| knob_black.png | 160×160 | 32–46 px | Black knurled knob with spun-metal cap (pointer drawn in CSS) | /_blob/7bd2416c143ab3d7528e080820ae3a09 |
| knob_cream.png | 160×160 | 44 px | Cream vintage dome knob | /_blob/b30d42bb78b0962c58562d03e5c128aa |
| knob_silver.png | 160×160 | 32 px | Small silver knurled knob | /_blob/bef9d65d89507fd501ebf2cdc52be7e7 |
| chicken_base.png | 160×160 | 48–60 px | Chickenhead knob skirt (static) | /_blob/d063a5ea76bbc07282c40fdb188ccc4b |
| chicken_bar.png | 48×176 | 16×58 px | Chickenhead pointer bar (rotate this layer) | /_blob/01d0890fd28fd5ff2b3d5628e5f31827 |
| footswitch.png | 160×160 | 44–50 px | Chrome footswitch with hex nut | /_blob/922a56e9d1212bf6411d51e587efbf21 |

Rotation: knob images are rotation-invariant; rotate only the CSS pointer (or the chickenhead bar) so the lighting stays correct.

Asset URLs belong to the design canvas "Encore — Live & Touch UI Redesign" (https://claude.ai/artifact/VJEpVteR9LgtESUkfghhNC). For the real app, copy the PNGs into the repo (e.g. `resources/skins/default/`) and load them as JUCE `Image`s.
