"""Harvest every kill-feed icon from video slices into an icon library.

  python tools/extract_icons.py vod/*.mp4                       -> icons_unlabelled/
  python tools/extract_icons.py vod/*.mp4 --fps 2 --min-sightings 5

For each sampled frame: crop the feed strip, segment rows, take the pure-white pixels in the
icon column, merge them into icon blobs, and cluster the blobs by normalised cross-correlation.
Every cluster's sightings are aligned and *averaged*, which turns the blocky single-frame
masks into a clean anti-aliased icon. Blobs tesseract can read as text (name fragments,
brackets, digits) are dropped. Output per icon: icon_NNN.png (averaged 4x mask),
icon_NNN_colour.png (one colour sighting), and index.json + contact_sheet.png for labelling.
Label by adding {"icon_012": "skull"} to labels.json, then run tools/build_catalogue.py.
"""
import argparse
import glob
import json
import os
import sys

import cv2
import numpy as np
import pytesseract

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from ocr import binarize, segment_rows, SCALE, ICON_COL  # noqa: E402

ROI = dict(x=0.0, y=0.42, w=0.24, h=0.16)
MIN_AREA, MIN_W, MIN_H, MAX_W, MAX_H = 150, 12, 12, 260, 80   # 4x px
SAME = 0.72                                    # NCC above this = same icon (AA variants merge)
CANVAS = (96, 280)                             # h, w: aligned accumulation canvas (4x px)


def icon_blobs(roi_bgr):
    up = cv2.resize(roi_bgr, None, fx=SCALE, fy=SCALE, interpolation=cv2.INTER_CUBIC)
    hsv = cv2.cvtColor(up, cv2.COLOR_BGR2HSV)
    white = ((hsv[:, :, 2] > 170) & (hsv[:, :, 1] < 70)).astype(np.uint8) * 255
    soft = hsv[:, :, 2].astype(np.float32) / 255.0 * (hsv[:, :, 1] < 90)   # graded brightness for averaging
    _, mask = binarize(roi_bgr)
    W = up.shape[1]
    x0, x1 = int(W * ICON_COL[0]), int(W * ICON_COL[1])
    out = []
    for y0, y1 in segment_rows(mask):
        band = white[y0 * SCALE:y1 * SCALE, x0:x1]
        merged = cv2.dilate(band, np.ones((13, 13), np.uint8))
        n, lab, stats, _ = cv2.connectedComponentsWithStats(merged)
        for i in range(1, n):
            x, y, w, h, _ = stats[i]
            crop = band[y:y + h, x:x + w]
            if crop.sum() // 255 < MIN_AREA or w < MIN_W or h < MIN_H or w > MAX_W or h > MAX_H:
                continue
            ys, xs = np.where(crop > 0)
            cy0, cy1, cx0, cx1 = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
            crop = crop[cy0:cy1, cx0:cx1]
            Y, X = y0 * SCALE + y + cy0, x0 + x + cx0
            out.append(dict(mask=crop, soft=soft[Y:Y + crop.shape[0], X:X + crop.shape[1]],
                            col=up[Y:Y + crop.shape[0], X:X + crop.shape[1]], x=X / W))
    return out


def ncc(a, b):
    if abs(a.shape[0] - b.shape[0]) > 12 or abs(a.shape[1] - b.shape[1]) > 24:
        return 0.0
    h, w = max(a.shape[0], b.shape[0]), max(a.shape[1], b.shape[1])
    pad = lambda m: cv2.copyMakeBorder(m, 0, h - m.shape[0], 0, w - m.shape[1], cv2.BORDER_CONSTANT, value=0)
    small, big = (a, b) if a.size <= b.size else (b, a)
    return float(cv2.matchTemplate(pad(big), small, cv2.TM_CCOEFF_NORMED).max())


def accumulate(c, b):
    """Align blob b to the cluster's reference by centroid and add its graded brightness."""
    m = b["mask"]
    ys, xs = np.where(m > 0)
    cy, cx = ys.mean(), xs.mean()
    oy, ox = int(CANVAS[0] / 2 - cy), int(CANVAS[1] / 2 - cx)
    y0, x0 = max(0, oy), max(0, ox)
    y1, x1 = min(CANVAS[0], oy + m.shape[0]), min(CANVAS[1], ox + m.shape[1])
    if y1 <= y0 or x1 <= x0:
        return
    c["acc"][y0:y1, x0:x1] += b["soft"][y0 - oy:y1 - oy, x0 - ox:x1 - ox]
    c["n"] += 1


def looks_like_text(mask):
    t = pytesseract.image_to_string(cv2.bitwise_not(cv2.copyMakeBorder(mask, 10, 10, 10, 10, cv2.BORDER_CONSTANT, value=0)),
                                    config="--psm 7 --oem 3").strip()
    letters = sum(ch.isalnum() for ch in t)
    return letters >= 3 or any(ch in t for ch in "[]()") and letters >= 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("videos", nargs="+")
    ap.add_argument("--fps", type=float, default=2.0)
    ap.add_argument("--out", default="icons_unlabelled")
    ap.add_argument("--min-sightings", type=int, default=5)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    clusters = []
    for path in sorted(sum((glob.glob(p) for p in a.videos), [])):
        v = cv2.VideoCapture(path)
        fps, n = v.get(cv2.CAP_PROP_FPS), int(v.get(cv2.CAP_PROP_FRAME_COUNT))
        for f in range(0, n, max(1, int(fps / a.fps))):
            v.set(cv2.CAP_PROP_POS_FRAMES, f)
            ok, fr = v.read()
            if not ok:
                break
            H, W = fr.shape[:2]
            roi = fr[int(H * ROI["y"]):int(H * (ROI["y"] + ROI["h"])), int(W * ROI["x"]):int(W * (ROI["x"] + ROI["w"]))]
            for b in icon_blobs(roi):
                for c in clusters:
                    if ncc(b["mask"], c["ref"]) >= SAME:
                        accumulate(c, b)
                        c["xs"].append(b["x"])
                        if len(c["seen"]) < 4:
                            c["seen"].append([os.path.basename(path), round(f / fps, 1)])
                        break
                else:
                    c = dict(ref=b["mask"], col=b["col"], acc=np.zeros(CANVAS, np.float32), n=0, xs=[],
                             seen=[[os.path.basename(path), round(f / fps, 1)]])
                    accumulate(c, b)
                    c["xs"].append(b["x"])
                    clusters.append(c)
        print(f"{os.path.basename(path)}: {len(clusters)} clusters so far")

    kept = []
    for c in clusters:
        if c["n"] < a.min_sightings:
            continue
        avg = c["acc"] / c["n"]
        avg = (np.clip(avg / max(avg.max(), 1e-6), 0, 1) * 255).astype(np.uint8)
        ys, xs = np.where(avg > 60)
        if not len(ys):
            continue
        c["avg"] = avg[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
        if looks_like_text(c["ref"]):
            continue
        kept.append(c)
    kept.sort(key=lambda c: -c["n"])

    index, tiles = [], []
    for i, c in enumerate(kept):
        name = f"icon_{i:03d}"
        cv2.imwrite(os.path.join(a.out, name + ".png"), c["avg"])
        cv2.imwrite(os.path.join(a.out, name + "_colour.png"), c["col"])
        index.append(dict(id=name, sightings=c["n"], size_4x=list(c["avg"].shape[::-1]),
                          x_frac=round(float(np.median(c["xs"])), 2), examples=c["seen"]))
        tile = np.zeros((96, 200), np.uint8)
        h, w = c["avg"].shape
        s = min(1.0, 80 / h, 190 / w)
        r = cv2.resize(c["avg"], (max(1, int(w * s)), max(1, int(h * s))), interpolation=cv2.INTER_AREA)
        tile[(88 - r.shape[0]) // 2:(88 - r.shape[0]) // 2 + r.shape[0], (200 - r.shape[1]) // 2:(200 - r.shape[1]) // 2 + r.shape[1]] = r
        tile = cv2.cvtColor(tile, cv2.COLOR_GRAY2BGR)
        cv2.putText(tile, f"{i:03d}  x{c['n']}", (4, 92), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (0, 255, 255), 1)
        tiles.append(tile)
    while len(tiles) % 6:
        tiles.append(np.zeros((96, 200, 3), np.uint8))
    cv2.imwrite(os.path.join(a.out, "contact_sheet.png"), np.vstack([np.hstack(tiles[i:i + 6]) for i in range(0, len(tiles), 6)]))
    json.dump(index, open(os.path.join(a.out, "index.json"), "w"), indent=1)
    print(f"{len(kept)} icons -> {a.out}/contact_sheet.png (label them in labels.json)")


if __name__ == "__main__":
    main()
