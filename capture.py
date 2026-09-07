"""Grab only the kill-feed region of the screen and pre-process it for OCR."""
import mss
import numpy as np
import cv2


class RoiCapture:
    def __init__(self, cfg):
        self.sct = mss.mss()
        self.monitor = cfg["monitor"]
        mon = self.sct.monitors[self.monitor]
        r = cfg["roi"]
        self.box = {
            "left": int(mon["left"] + mon["width"] * r["x"]),
            "top": int(mon["top"] + mon["height"] * r["y"]),
            "width": int(mon["width"] * r["w"]),
            "height": int(mon["height"] * r["h"]),
        }
        self.upscale = float(cfg.get("upscale", 2.0))

    def full_frame(self) -> np.ndarray:
        mon = self.sct.monitors[self.monitor]
        return np.asarray(self.sct.grab(mon))[:, :, :3]

    def grab(self) -> np.ndarray:
        """Return BGR image of the ROI only (cheap: ~1/25th of the screen)."""
        shot = self.sct.grab(self.box)
        return np.asarray(shot)[:, :, :3]

