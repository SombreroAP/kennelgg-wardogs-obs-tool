"""Rebuild templates/ from the crisp catalogue icons.

Catalogue PNGs are 8x feed size with 12 px padding; templates are matched at 4x on a white
mask, so each icon is halved, thresholded and tight-cropped. Catalogue labels map to the
icon names the rules use (heli, tank, car, artillery, rpg, c4, skull, mortar, hammer,
grenade, explosion); gun classes keep their own names. Variants become name_2, name_3...
Old single-frame templates are replaced; name_me.png is kept.
"""
import glob
import os
import re

import cv2

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
CAT, TPL = os.path.join(ROOT, "icons", "catalogue"), os.path.join(ROOT, "templates")
MAP = {"helicopter": "heli", "l2a6": "tank", "humvee": "car", "kodiak_pickup": "car", "sph2_howitzer": "artillery",
       "rpg7": "rpg", "c4_charge": "c4", "headshot": "skull", "mortar_shell": "mortar", "hammer": "hammer",
       "grenade": "grenade", "explosion": "explosion", "galil": "rifle", "rifle_carbine": "rifle",
       "rifle_bullpup": "rifle", "rifle_bolt": "boltgun", "rifle_hunting": "hunting", "dmr_scoped": "sniper",
       "shotgun": "shotgun", "lmg": "lmg", "pistol": "pistol", "skull_small": "skull_small"}

for p in glob.glob(os.path.join(TPL, "*.png")):
    if not os.path.basename(p).startswith("name_"):
        os.remove(p)
counts = {}
for p in sorted(glob.glob(os.path.join(CAT, "*.png"))):
    base = os.path.basename(p)[:-4]
    if base.endswith(("_64", "_dark")) or base == "sheet":
        continue
    label = re.sub(r"_\d+$", "", base)
    name = MAP.get(label)
    if not name:
        continue
    a = cv2.imread(p, cv2.IMREAD_UNCHANGED)[:, :, 3]
    a = cv2.resize(a, None, fx=0.5, fy=0.5, interpolation=cv2.INTER_AREA)
    _, b = cv2.threshold(a, 127, 255, cv2.THRESH_BINARY)
    import numpy as np
    ys, xs = np.where(b > 0)
    b = b[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
    counts[name] = counts.get(name, 0) + 1
    out = name if counts[name] == 1 else f"{name}_{counts[name]}"
    cv2.imwrite(os.path.join(TPL, out + ".png"), b)
    print(f"{base:22} -> {out}  {b.shape[1]}x{b.shape[0]}")
