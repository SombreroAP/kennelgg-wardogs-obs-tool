"""Turn labelled harvested icons into a reusable icon catalogue.

  python tools/build_catalogue.py            (reads icons/labels.json + icons/unlabelled/)

Writes icons/catalogue/<label>.png        white icon on transparent, padded, 4x feed size
       icons/catalogue/<label>_64.png     64 px tall version for overlays / web
       icons/catalogue/<label>_dark.png   dark-on-light version
       icons/catalogue/sheet.png          overview
Several ids with the same label are kept as separate files (label.png, label_2.png, ...):
they are usually different weapons that share a class, not the same drawing.
"""
import json
import os
import sys

import cv2
import numpy as np

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "icons")
SRC, OUT = os.path.join(ROOT, "unlabelled"), os.path.join(ROOT, "catalogue")
os.makedirs(OUT, exist_ok=True)
labels = json.load(open(os.path.join(ROOT, "labels.json")))
groups = {}
for icon_id, label in labels.items():
    if icon_id.startswith("_") or label in ("?", "text", ""):
        continue
    p = os.path.join(SRC, icon_id + ".png")
    if os.path.exists(p):
        groups.setdefault(label, []).append(cv2.imread(p, cv2.IMREAD_GRAYSCALE))

sheet = []
items = [(label if i == 0 else f"{label}_{i + 1}", m) for label, masks in sorted(groups.items()) for i, m in enumerate(masks)]
for label, a in items:
    masks = [a]
    a = cv2.GaussianBlur(a, (3, 3), 0)
    pad = 8
    a = cv2.copyMakeBorder(a, pad, pad, pad, pad, cv2.BORDER_CONSTANT, value=0)
    rgba = np.dstack([np.full_like(a, 255)] * 3 + [a])               # white, alpha = mask
    cv2.imwrite(os.path.join(OUT, f"{label}.png"), rgba)
    dark = np.dstack([np.full_like(a, 20)] * 3 + [a])
    cv2.imwrite(os.path.join(OUT, f"{label}_dark.png"), dark)
    s = 64 / a.shape[0]
    cv2.imwrite(os.path.join(OUT, f"{label}_64.png"), cv2.resize(rgba, (max(1, int(a.shape[1] * s)), 64), interpolation=cv2.INTER_AREA))
    tile = np.zeros((110, 220), np.uint8)
    s = min(1.0, 80 / a.shape[0], 210 / a.shape[1]); r = cv2.resize(a, (int(a.shape[1] * s), int(a.shape[0] * s)))
    tile[(90 - r.shape[0]) // 2:(90 - r.shape[0]) // 2 + r.shape[0], (220 - r.shape[1]) // 2:(220 - r.shape[1]) // 2 + r.shape[1]] = r
    tile = cv2.cvtColor(tile, cv2.COLOR_GRAY2BGR)
    cv2.putText(tile, label, (4, 104), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (0, 255, 255), 1)
    sheet.append(tile)
    print(label)
while len(sheet) % 5:
    sheet.append(np.zeros((110, 220, 3), np.uint8))
cv2.imwrite(os.path.join(OUT, "sheet.png"), np.vstack([np.hstack(sheet[i:i + 5]) for i in range(0, len(sheet), 5)]))
print(f"{len(items)} icons -> {OUT}")
