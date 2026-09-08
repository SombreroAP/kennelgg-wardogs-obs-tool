"""Offline test: run the pipeline over a video file at capture.fps.
`python vod_test.py vod/clip.mp4 [--dump]`  (--dump writes debug/roi_<t>.png for each sampled frame)"""
import sys, time, os
import cv2, yaml
from capture import RoiCapture
from detector import KillDetector
from ocr import read_rows

cfg = yaml.safe_load(open("config.yaml", encoding="utf-8"))
cc, dc = cfg["capture"], cfg["detection"]
dump = "--dump" in sys.argv
os.makedirs("debug", exist_ok=True)

class FileCap(RoiCapture):
    def __init__(self, w, h):
        r = cc["roi"]; self.upscale = float(cc.get("upscale", 2.0))
        self.box = (int(w*r["x"]), int(h*r["y"]), int(w*r["w"]), int(h*r["h"]))

vid = cv2.VideoCapture(sys.argv[1])
fps = vid.get(cv2.CAP_PROP_FPS); n = int(vid.get(cv2.CAP_PROP_FRAME_COUNT))
w, h = int(vid.get(3)), int(vid.get(4))
cap = FileCap(w, h); det = KillDetector(dc, dump_rows="debug/rows")
step = max(1, int(round(fps / cc["fps"])))
print(f"{w}x{h} @ {fps:.0f}fps, {n} frames, sampling every {step} frames; ROI {cap.box}")

# detector uses wall-clock; fake it with video time
import detector, ocr
vt = [0.0]
detector.time = ocr.time = type("T", (), {"time": staticmethod(lambda: vt[0])})

t0 = time.time(); ocr_ms = []
for i in range(0, n, step):
    vid.set(cv2.CAP_PROP_POS_FRAMES, i); ok, frame = vid.read()
    if not ok: break
    vt[0] = i / fps
    x, y, bw, bh = cap.box
    roi = frame[y:y+bh, x:x+bw]
    if dump: cv2.imwrite(f"debug/roi_{vt[0]:05.1f}.png", roi)
    s = time.time()
    if "--verbose" in sys.argv:
        for rd in read_rows(roi):
            rd.ocr(); print(f"  t={vt[0]:5.1f}s y={rd.y:3} name={rd.name!r}[{rd.name_color}] dists={rd.dists} victim={rd.victim!r}[{rd.victim_color}]")
    trigs = det.feed_frame(roi); ocr_ms.append((time.time()-s)*1000)
    for trig in trigs:
        print(f"\n*** t={vt[0]:5.1f}s  {trig.kind.upper()}: {trig.title}   tags={trig.tags} ***\n")
print(f"done in {time.time()-t0:.1f}s; OCR avg {sum(ocr_ms)/len(ocr_ms):.0f} ms/frame")
