"""Turn labelled harvested icons into a clean, reusable icon catalogue.

  python tools/build_catalogue.py            (reads icons/labels.json + icons/unlabelled/)

For each labelled icon the sharp averaged mask is upsampled, thresholded into a crisp
silhouette, cleaned with a small open/close, and exported as:
  icons/catalogue/<label>.png        white on transparent, 8x feed size, anti-aliased edges
  icons/catalogue/<label>_64.png     64 px tall
  icons/catalogue/<label>_dark.png   dark on transparent
  icons/catalogue/<label>.svg        traced outline, scales to any size
  icons/catalogue/sheet.png          overview
Ids sharing a label are kept as separate files (label, label_2, ...): they are usually
different weapons of one class, not the same drawing.
"""
import json
import os

import cv2
import numpy as np

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "icons")
SRC, OUT = os.path.join(ROOT, "unlabelled"), os.path.join(ROOT, "catalogue")
os.makedirs(OUT, exist_ok=True)
labels = json.load(open(os.path.join(ROOT, "labels.json")))
groups = {}
for icon_id, label in labels.items():
    if icon_id.startswith("_") or label in ("?", "text", "fragment", ""):
        continue
    p = os.path.join(SRC, icon_id + ".png")
    if os.path.exists(p):
        groups.setdefault(label, []).append(cv2.imread(p, cv2.IMREAD_GRAYSCALE))


def crisp(avg: np.ndarray, up: int = 2) -> np.ndarray:
    """Sharp average (4x) -> clean binary silhouette at 8x."""
    a = cv2.resize(avg, None, fx=up, fy=up, interpolation=cv2.INTER_CUBIC)
    a = cv2.GaussianBlur(a, (3, 3), 0)
    _, b = cv2.threshold(a, 110, 255, cv2.THRESH_BINARY)
    k = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (3, 3))
    b = cv2.morphologyEx(b, cv2.MORPH_OPEN, k)
    b = cv2.morphologyEx(b, cv2.MORPH_CLOSE, k)
    n, lab, stats, _ = cv2.connectedComponentsWithStats(b)
    for i in range(1, n):                                  # drop specks smaller than 1.5 % of the icon
        if stats[i, cv2.CC_STAT_AREA] < 0.015 * (b > 0).sum():
            b[lab == i] = 0
    ys, xs = np.where(b > 0)
    return b[ys.min():ys.max() + 1, xs.min():xs.max() + 1] if len(ys) else b


def to_svg(b: np.ndarray, path: str):
    h, w = b.shape
    contours, hier = cv2.findContours(b, cv2.RETR_CCOMP, cv2.CHAIN_APPROX_SIMPLE)
    d = []
    for c in contours:
        c = cv2.approxPolyDP(c, 0.6, True).reshape(-1, 2)
        if len(c) < 3:
            continue
        d.append("M" + " L".join(f"{x},{y}" for x, y in c) + " Z")
    with open(path, "w") as f:
        f.write(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" width="{w}" height="{h}">'
                f'<path fill="#fff" fill-rule="evenodd" d="{" ".join(d)}"/></svg>')


sheet = []
items = [(label if i == 0 else f"{label}_{i + 1}", m) for label, masks in sorted(groups.items()) for i, m in enumerate(masks)]
for label, avg in items:
    b = crisp(avg)
    pad = 12
    b = cv2.copyMakeBorder(b, pad, pad, pad, pad, cv2.BORDER_CONSTANT, value=0)
    aa = cv2.GaussianBlur(b, (3, 3), 0)                                # 1 px anti-aliased edge
    cv2.imwrite(os.path.join(OUT, f"{label}.png"), np.dstack([np.full_like(aa, 255)] * 3 + [aa]))
    cv2.imwrite(os.path.join(OUT, f"{label}_dark.png"), np.dstack([np.full_like(aa, 20)] * 3 + [aa]))
    s = 64 / aa.shape[0]
    small = cv2.resize(aa, (max(1, int(aa.shape[1] * s)), 64), interpolation=cv2.INTER_AREA)
    cv2.imwrite(os.path.join(OUT, f"{label}_64.png"), np.dstack([np.full_like(small, 255)] * 3 + [small]))
    to_svg(b, os.path.join(OUT, f"{label}.svg"))
    tile = np.zeros((110, 220), np.uint8)
    s = min(1.0, 80 / aa.shape[0], 210 / aa.shape[1]); r = cv2.resize(aa, (int(aa.shape[1] * s), int(aa.shape[0] * s)), interpolation=cv2.INTER_AREA)
    tile[(90 - r.shape[0]) // 2:(90 - r.shape[0]) // 2 + r.shape[0], (220 - r.shape[1]) // 2:(220 - r.shape[1]) // 2 + r.shape[1]] = r
    tile = cv2.cvtColor(tile, cv2.COLOR_GRAY2BGR)
    cv2.putText(tile, label, (4, 104), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (0, 255, 255), 1)
    sheet.append(tile)
while len(sheet) % 5:
    sheet.append(np.zeros((110, 220, 3), np.uint8))
cv2.imwrite(os.path.join(OUT, "sheet.png"), np.vstack([np.hstack(sheet[i:i + 5]) for i in range(0, len(sheet), 5)]))
print(f"{len(items)} icons -> {OUT}")
