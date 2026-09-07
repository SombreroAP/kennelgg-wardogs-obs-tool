"""Check the ROI: `python calibrate.py --sources` lists OBS inputs; `--list` shows monitors;
`--colors` prints the measured name colours per row (and the team from the minimap for a file); `python calibrate.py` saves
roi.png / roi_proc.png and prints what tesseract reads. `python calibrate.py image.png` tests a file."""
import sys
import cv2
import mss
import yaml
from capture import RoiCapture
from ocr import read_rows, binarize

cfg = yaml.safe_load(open("config.yaml", encoding="utf-8"))

if "--sources" in sys.argv:
    import obsws_python as obs
    o = cfg["obs"]
    cl = obs.ReqClient(host=o["host"], port=o["port"], password=o["password"], timeout=3)
    for s in cl.get_input_list().inputs:
        print(f"{s['inputName']!r}  ({s['inputKind']})")
    sys.exit()
if "--list" in sys.argv:
    for i, m in enumerate(mss.mss().monitors):
        print(i, m)
    sys.exit()

if cfg["capture"].get("backend", "obs") == "obs":
    from capture_obs import ObsRoiCapture
    cap = ObsRoiCapture(cfg["capture"], cfg["obs"])
else:
    cap = RoiCapture(cfg["capture"])
if len(sys.argv) > 1:                       # test against a screenshot file
    img = cv2.imread(sys.argv[1])
    h, w = img.shape[:2]
    r = cfg["capture"]["roi"]
    roi = img[int(h*r["y"]):int(h*(r["y"]+r["h"])), int(w*r["x"]):int(w*(r["x"]+r["w"]))]
else:
    roi = cap.grab()
cv2.imwrite("roi.png", roi)
cv2.imwrite("roi_proc.png", binarize(roi)[1])
print("saved roi.png / roi_proc.png")
pn = cfg["detection"]["player_name"].lower()
for rd in read_rows(roi):
    rd.ocr()
    mine = pn in rd.name.lower()
    print(f"{'MINE ' if mine else '     '}y={rd.y:3}  name={rd.name!r} [{rd.name_color}]  dist={rd.dists}  "
          f"victim={rd.victim!r} [{rd.victim_color}]  icons={rd.icons}")
if "--colors" in sys.argv:
    from colors import measure, detect_my_team
    from ocr import binarize, NAME_COL, VICTIM_COL, SCALE
    import numpy as np
    g, m = binarize(roi); hsv = cv2.cvtColor(binarize.last_up, cv2.COLOR_BGR2HSV); W = m.shape[1]
    for rd in read_rows(roi):
        for label, c in (("killer", NAME_COL), ("victim", VICTIM_COL)):
            Y0, Y1 = rd.y * SCALE, rd.y * SCALE + rd._mask.shape[0]
            v = measure(hsv[Y0:Y1, int(W*c[0]):int(W*c[1])], m[Y0:Y1, int(W*c[0]):int(W*c[1])])
            print(f"y={rd.y:3} {label}: hue/sat/val = {tuple(round(x) for x in v) if v else None}")
    if len(sys.argv) > 1 and not sys.argv[1].startswith("--"):
        dc = cfg["detection"]
        print("team (emblem, then minimap):", detect_my_team(img, dc.get("team_icon_roi"), dc.get("minimap_roi")))
