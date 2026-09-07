"""Scan whole VODs for every kind of kill-feed event.

  python tools/scan_vod.py vod/*.mp4 --fps 2 --out scan/

Per sampled frame: crop the feed strip, segment rows, match icons (templates/) on the white
mask and read the killer/victim colour. Consecutive frames with the same icon set in the same
slot are one event. Writes:
  scan/events.csv        t, video, slot, icons, killer colour, victim colour, duration
  scan/summary.txt       every icon combination with count and example times
  scan/examples/<combo>_<n>.png   a colour crop of the row for each combination
  scan/unknown/          white icon blobs that match no template at >= 0.5 (new event types)
No OCR (too slow for hours of footage); names are not read, only icons and colours.
"""
import argparse
import csv
import glob
import os
import sys
from collections import defaultdict

import cv2
import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from ocr import binarize, segment_rows, SCALE, ICON_COL, NAME_COL, load_templates, _scores, match_icons  # noqa: E402
from colors import measure, classify  # noqa: E402

ROI = dict(x=0.0, y=0.42, w=0.24, h=0.16)


def scan(path, fps_target, out, events, combos, unknown):
    v = cv2.VideoCapture(path)
    fps, n = v.get(cv2.CAP_PROP_FPS), int(v.get(cv2.CAP_PROP_FRAME_COUNT))
    step = max(1, int(round(fps / fps_target)))
    name = os.path.splitext(os.path.basename(path))[0]
    active = {}                         # slot index -> dict(combo, t0, last, kc, vc, crop)
    tmpl = [t for t in load_templates() if not t.startswith("name_")]
    for f in range(0, n, step):
        v.set(cv2.CAP_PROP_POS_FRAMES, f)
        ok, fr = v.read()
        if not ok:
            break
        t = f / fps
        H, W = fr.shape[:2]
        roi = fr[int(H * ROI["y"]):int(H * (ROI["y"] + ROI["h"])), int(W * ROI["x"]):int(W * (ROI["x"] + ROI["w"]))]
        up = cv2.resize(roi, None, fx=SCALE, fy=SCALE, interpolation=cv2.INTER_CUBIC)
        hsv = cv2.cvtColor(up, cv2.COLOR_BGR2HSV)
        white = ((hsv[:, :, 2] > 170) & (hsv[:, :, 1] < 70)).astype(np.uint8) * 255
        _, mask = binarize(roi)
        Wu = up.shape[1]
        seen = set()
        for y0, y1 in segment_rows(mask):
            slot = int(round((y0 - 46) / 24))               # feed rows sit 24 px apart from y~46
            Y0, Y1 = y0 * SCALE, y1 * SCALE
            band = white[Y0:Y1, int(Wu * ICON_COL[0]):int(Wu * ICON_COL[1])]
            icons = match_icons(band)
            if not icons:
                # anything icon-like that no template explains -> unknown pile
                s = _scores(band, tmpl)
                if band.sum() // 255 > 300 and (not s or max(s.values()) < 0.5):
                    k = len(unknown)
                    if k < 400:
                        cv2.imwrite(os.path.join(out, "unknown", f"{name}_{t:07.1f}_{slot}.png"), roi[y0:y1])
                        unknown.append((name, t))
                continue
            combo = "+".join(sorted(icons))
            kc = classify(measure(hsv[Y0:Y1, :int(Wu * NAME_COL[1])], mask[Y0:Y1, :int(Wu * NAME_COL[1])]))
            vc = classify(measure(hsv[Y0:Y1, int(Wu * 0.5):], mask[Y0:Y1, int(Wu * 0.5):]))
            seen.add(slot)
            a = active.get(slot)
            if a and a["combo"] == combo:
                a["last"] = t
                continue
            if a:
                _close(a, name, slot, events, combos, out)
            active[slot] = dict(combo=combo, t0=t, last=t, kc=kc, vc=vc, crop=roi[y0:y1].copy())
        for slot in list(active):
            if slot not in seen and t - active[slot]["last"] > 0.6:
                _close(active.pop(slot), name, slot, events, combos, out)
        if f % (step * 600) == 0:
            print(f"  {name} {t/60:6.1f} min  events={len(events)} combos={len(combos)} unknown={len(unknown)}", flush=True)
    for slot in list(active):
        _close(active.pop(slot), name, slot, events, combos, out)


def _close(a, name, slot, events, combos, out):
    if a["last"] - a["t0"] < 0.75:                      # < 3 frames: noise
        return
    events.append([round(a["t0"], 1), name, slot, a["combo"], a["kc"], a["vc"], round(a["last"] - a["t0"], 1)])
    c = combos[a["combo"]]
    c["n"] += 1
    if len(c["ex"]) < 6:
        c["ex"].append(f"{name}@{int(a['t0']//3600)}h{int(a['t0']%3600//60):02d}m{int(a['t0']%60):02d}s")
        cv2.imwrite(os.path.join(out, "examples", f"{a['combo']}_{c['n']}.png"),
                    cv2.resize(a["crop"], None, fx=3, fy=3, interpolation=cv2.INTER_CUBIC))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("videos", nargs="+")
    ap.add_argument("--fps", type=float, default=2.0)
    ap.add_argument("--out", default="scan")
    a = ap.parse_args()
    for d in ("", "examples", "unknown"):
        os.makedirs(os.path.join(a.out, d), exist_ok=True)
    events, combos, unknown = [], defaultdict(lambda: dict(n=0, ex=[])), []
    for p in sorted(sum((glob.glob(x) for x in a.videos), [])):
        print(p, flush=True)
        scan(p, a.fps, a.out, events, combos, unknown)
    with open(os.path.join(a.out, "events.csv"), "w", newline="") as f:
        w = csv.writer(f); w.writerow(["t_s", "video", "slot", "icons", "killer_colour", "victim_colour", "visible_s"]); w.writerows(events)
    with open(os.path.join(a.out, "summary.txt"), "w") as f:
        for combo, c in sorted(combos.items(), key=lambda kv: -kv[1]["n"]):
            f.write(f"{c['n']:5}  {combo:32}  e.g. {', '.join(c['ex'])}\n")
        f.write(f"\n{len(events)} events, {len(combos)} icon combinations, {len(unknown)} unknown icon rows saved\n")
    print(open(os.path.join(a.out, "summary.txt")).read())


if __name__ == "__main__":
    main()
