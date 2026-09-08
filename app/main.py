"""ClipHound: OCR the WARDOGS kill feed -> Twitch clip + tagged OBS replay."""
import os
import sys

# Frozen (installer) build: work from the exe's folder so config.yaml, debug/ and the library
# paths resolve, and use the bundled Tesseract.
if getattr(sys, "frozen", False):
    os.chdir(os.path.dirname(sys.executable))
    _tess = os.path.join(os.path.dirname(sys.executable), "tesseract", "tesseract.exe")
    if os.path.exists(_tess):
        os.environ["TESSDATA_PREFIX"] = os.path.join(os.path.dirname(_tess), "tessdata")
        try:
            import pytesseract
            pytesseract.pytesseract.tesseract_cmd = _tess
        except ImportError:
            pass
    if "--setup" in sys.argv or not os.path.exists("config.yaml"):
        import shutil
        if not os.path.exists("config.yaml") and os.path.exists("config.default.yaml"):
            shutil.copy("config.default.yaml", "config.yaml")
        if "--token" in sys.argv:
            import get_token  # noqa: F401  (runs the Twitch login flow on import)
            raise SystemExit(0)
        if "--setup" in sys.argv:
            import setup as _setup
            sys.argv.remove("--setup")
            _setup.main()
            raise SystemExit(0)
import threading
import time

import cv2
import yaml

from capture import RoiCapture
from detector import KillDetector

DRY = "--dry-run" in sys.argv


def main():
    cfg = yaml.safe_load(open("config.yaml", encoding="utf-8"))
    bridge = None
    if cfg["capture"].get("backend", "obs") == "bridge" or cfg["obs"].get("mode") == "bridge":
        from bridge import Bridge
        bridge = Bridge({**(cfg.get("bridge") or {}), "fps": cfg["capture"]["fps"]})
    if cfg["capture"].get("backend", "obs") == "bridge":
        from bridge import BridgeRoiCapture
        cap = BridgeRoiCapture(cfg["capture"], bridge)
    elif cfg["capture"].get("backend", "obs") == "obs":
        from capture_obs import ObsRoiCapture
        cap = ObsRoiCapture(cfg["capture"], cfg["obs"])
    else:
        cap = RoiCapture(cfg["capture"])
    import ocr
    ocr.COLOR_BANDS = {k: tuple(v) for k, v in (cfg["detection"].get("colors") or {}).items()} or None
    det = KillDetector(cfg["detection"], dump_rows="debug/rows" if cfg["capture"].get("debug_dump") else None)
    print(f"[capture] ROI {cap.box} @ {cfg['capture']['fps']} fps   dry-run={DRY}")

    tw = ob = None
    if not DRY and cfg["twitch"]["enabled"]:
        from twitch import Twitch
        tw = Twitch(cfg["twitch"])
    if not DRY and cfg["obs"]["enabled"] and cfg["obs"].get("mode") == "bridge":
        from bridge import BridgeOBS
        ob = BridgeOBS(cfg["obs"], bridge)
    elif not DRY and cfg["obs"]["enabled"]:
        from obs import OBS
        ob = OBS(cfg["obs"])

    def fire(trig):
        print(f"\n*** {trig.kind.upper()}: {trig.title}   tags={trig.tags} ***\n")
        if tw:
            threading.Timer(cfg["twitch"]["clip_delay_s"], lambda: _safe(tw.create_clip, trig.title, trig.tags)).start()
        if ob:
            ev = trig.events[-1] if trig.events else None
            info = {"kind": trig.kind, "description": trig.describe(),
                    "decision_lag_s": 10 / cfg["capture"]["fps"] + 0.5,   # VOTES reads + fade-in
                    "distance_m": ev.distance_m if ev else 0,
                    "killer": ev.killer if ev else "", "victim": ev.victim if ev else "",
                    "icons": ev.icons if ev else [], "kills": len(trig.events)}
            threading.Timer(cfg["obs"]["replay_delay_s"], lambda: _safe(ob.trigger, trig.title, trig.tags, info)).start()

    period = 1.0 / cfg["capture"]["fps"]
    if cfg["capture"].get("debug_dump"):
        os.makedirs("debug", exist_ok=True)
    from colors import detect_my_team
    last_team_check = 0.0
    while True:
        t0 = time.time()
        # team colour: re-check the minimap every 30 s while in auto (it changes each match)
        if cfg["detection"].get("my_team", "auto") == "auto" and t0 - last_team_check > 30 and hasattr(cap, "full_frame"):
            last_team_check = t0
            dc = cfg["detection"]
            team = detect_my_team(cap.full_frame(), dc.get("team_icon_roi"), dc.get("minimap_roi"), ocr.COLOR_BANDS)
            if team and team != det.my_team:
                print(f"[team] you are on the {team} team")
                det.my_team = team
        roi = cap.grab()
        if cfg["capture"].get("debug_dump"):
            cv2.imwrite("debug/roi.png", roi)
        for trig in det.feed_frame(roi):
            fire(trig)
        time.sleep(max(0, period - (time.time() - t0)))


def _safe(fn, *a):
    try:
        fn(*a)
    except Exception as e:
        print(f"[error] {fn.__qualname__}: {e}")


if __name__ == "__main__":
    main()
