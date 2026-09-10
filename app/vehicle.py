"""Which vehicle seat you are in, read off the keybind list WARDOGS draws bottom-right while you
are in a vehicle (and not on foot). Its lines name the seat:

  COLLECTIVE LIFT / DEPLOY FLARES -> Havoc pilot        DEPLOY SMOKE   -> tank driver
  CYCLE WEAPON                    -> tank gunner        INTERACT+ZOOM only -> Havoc gunner (CAM)

The plugin turns its Dual POV window on with the matching preset and off when the list goes.
One tesseract run a second on a small crop; nothing while it is turned off."""
import re

import cv2
import numpy as np
import pytesseract

SCALE = 2
CFG = "--psm 6 --oem 3"


def classify(text: str) -> str:
    t = re.sub(r"[^A-Z ]", " ", text.upper())
    if "COLLECTIVE" in t or "FLARE" in t:
        return "havoc-pilot"
    if "SMOKE" in t:
        return "tank-driver"
    if "CYCLE" in t or "WEAPON" in t:
        return "tank-gunner"
    if "ZOOM" in t and "INTERACT" in t and "CAMERA" not in t and "SEAT" not in t:
        return "havoc-gunner"
    if "SEAT" in t or "SQUAD ONLY" in t or "FREE LOOK" in t:
        return "vehicle"          # in something, seat unclear: keep whatever preset is set
    return "none"


def read(roi_bgr: np.ndarray) -> tuple[str, str]:
    """(seat, text read)."""
    if roi_bgr is None or roi_bgr.size == 0:
        return "none", ""
    up = cv2.resize(roi_bgr, None, fx=SCALE, fy=SCALE, interpolation=cv2.INTER_CUBIC)
    gray = cv2.cvtColor(up, cv2.COLOR_BGR2GRAY)
    # white capitals on the game: keep what is brighter than its surroundings, dark on white for OCR
    m = cv2.adaptiveThreshold(cv2.GaussianBlur(gray, (0, 0), 1.0), 255,
                              cv2.ADAPTIVE_THRESH_GAUSSIAN_C, cv2.THRESH_BINARY, 31, -14)
    inv = cv2.bitwise_not(m)
    inv = cv2.copyMakeBorder(inv, 12, 12, 12, 12, cv2.BORDER_CONSTANT, value=255)
    text = pytesseract.image_to_string(inv, config=CFG).strip()
    return classify(text), text


class Watcher:
    """Reads the corner once a second while the plugin has automatic Dual POV on, and reports a
    seat only after it has seen it twice in a row (and 'none' three times), so a covered corner
    does not flap the window."""

    def __init__(self, bridge):
        self.b = bridge
        self.last = 0.0
        self.seen = []
        self.sent = ""

    def maybe_read(self, frame, now: float):
        c = self.b.vehicle_cfg
        if frame is None or not c.get("enabled") or now - self.last < 1.0:
            return
        self.last = now
        h, w = frame.shape[:2]
        r = c.get("roi") or [0.86, 0.60, 0.14, 0.25]
        x0, y0 = int(w * r[0]), int(h * r[1])
        try:
            seat, text = read(frame[y0:y0 + int(h * r[3]), x0:x0 + int(w * r[2])])
        except Exception as e:
            print(f"[vehicle] cannot read: {e}")
            return
        self.seen = (self.seen + [seat])[-3:]
        need = 3 if seat == "none" else 2
        if len(self.seen) >= need and all(s == seat for s in self.seen[-need:]) and seat != self.sent:
            self.sent = seat
            print(f"[vehicle] {seat}  ({' | '.join(text.splitlines())})")
            self.b.send({"type": "vehicle", "seat": seat, "text": text})
