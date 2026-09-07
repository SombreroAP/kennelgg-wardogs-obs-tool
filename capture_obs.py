"""Pull the game frame from OBS on this (streaming) PC via obs-websocket GetSourceScreenshot,
then crop to the kill-feed ROI. No screen grab, no projector window needed."""
import base64
import numpy as np
import cv2
import obsws_python as obs


class ObsRoiCapture:
    def __init__(self, cap_cfg, obs_cfg):
        self.source = cap_cfg["obs_source"]
        self.r = cap_cfg["roi"]
        self.upscale = float(cap_cfg.get("upscale", 2.0))
        self.cl = obs.ReqClient(host=obs_cfg["host"], port=obs_cfg["port"],
                                password=obs_cfg["password"], timeout=3)
        names = [s["inputName"] for s in self.cl.get_input_list().inputs]
        if self.source not in names:
            raise SystemExit(f"[capture] OBS source '{self.source}' not found. Sources: {names}")
        frame = self._frame()
        h, w = frame.shape[:2]
        self.box = (int(w * self.r["x"]), int(h * self.r["y"]), int(w * self.r["w"]), int(h * self.r["h"]))
        print(f"[capture] OBS source '{self.source}' is {w}x{h}; ROI x,y,w,h = {self.box}")

    def _frame(self) -> np.ndarray:
        # 'png' is lossless (better for OCR); a 1080p decode is a few ms.
        res = self.cl.get_source_screenshot(self.source, "png", None, None, -1)
        b64 = res.image_data.split(",", 1)[1]
        buf = np.frombuffer(base64.b64decode(b64), np.uint8)
        return cv2.imdecode(buf, cv2.IMREAD_COLOR)

    def grab(self) -> np.ndarray:
        x, y, w, h = self.box
        self._last = self._frame()
        return self._last[y:y + h, x:x + w]

    def full_frame(self) -> np.ndarray:
        """Most recent full source frame (used for the minimap team-colour check)."""
        return self._last if getattr(self, "_last", None) is not None else self._frame()

    def preprocess(self, bgr: np.ndarray) -> np.ndarray:
        if self.upscale != 1.0:
            bgr = cv2.resize(bgr, None, fx=self.upscale, fy=self.upscale, interpolation=cv2.INTER_CUBIC)
        gray = cv2.cvtColor(bgr, cv2.COLOR_BGR2GRAY)
        _, th = cv2.threshold(gray, 170, 255, cv2.THRESH_BINARY)
        th = cv2.bitwise_not(th)
        return cv2.medianBlur(th, 3)
