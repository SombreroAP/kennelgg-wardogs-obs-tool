"""ClipHound: OCR the WARDOGS kill feed -> Twitch clip + tagged OBS replay."""
import os
import sys

# Frozen (installer) build: work from the exe's folder so config.yaml, debug/ and the library
# paths resolve, and use the bundled Tesseract.
class _Tee:
    """stdout/stderr to the console and to cliphound.log with timestamps (the plugin's Logs button reads it)."""
    def __init__(self, stream, path):
        self.stream, self.f, self.at_line_start = stream, open(path, "a", encoding="utf-8", buffering=1), True
    def write(self, s):
        try:
            if self.stream is not None:
                self.stream.write(s)
        except Exception:
            pass
        try:
            import time as _t
            for part in s.splitlines(True):
                if self.at_line_start and part.strip():
                    self.f.write(_t.strftime("%H:%M:%S ") + part)
                else:
                    self.f.write(part)
                self.at_line_start = part.endswith("\n")
        except Exception:
            pass
    def flush(self):
        try:
            self.stream.flush(); self.f.flush()
        except Exception:
            pass
    def __getattr__(self, n):
        if self.stream is None:
            raise AttributeError(n)
        return getattr(self.stream, n)


if getattr(sys, "frozen", False):
    os.chdir(os.path.dirname(sys.executable))
    # one ClipHound at a time (the plugin may try to start it again)
    try:
        import ctypes
        _mutex = ctypes.windll.kernel32.CreateMutexW(None, False, "Local\\KennelClipHound")
        if ctypes.windll.kernel32.GetLastError() == 183:
            raise SystemExit(0)
    except SystemExit:
        raise
    except Exception:
        pass
    try:
        _lp = "cliphound.log"
        if os.path.exists(_lp) and os.path.getsize(_lp) > 2_000_000:
            os.replace(_lp, "cliphound.log.old")
        sys.stdout = _Tee(sys.stdout, _lp)
        sys.stderr = _Tee(sys.stderr, _lp)
        print(f"=== ClipHound started {__import__('time').strftime('%Y-%m-%d %H:%M:%S')} ===")
    except Exception as _e:
        print(f"[log] could not open cliphound.log: {_e}")
    _tess = os.path.join(os.path.dirname(sys.executable), "tesseract", "tesseract.exe")
    if os.path.exists(_tess):
        os.environ["TESSDATA_PREFIX"] = os.path.join(os.path.dirname(_tess), "tessdata")
        try:
            import pytesseract
            pytesseract.pytesseract.tesseract_cmd = _tess
        except ImportError:
            pass
    if not os.path.exists("config.yaml") and os.path.exists("config.default.yaml"):
        import shutil
        shutil.copy("config.default.yaml", "config.yaml")   # settings come from the OBS plugin's ClipHound tab
    if True:   # developer flags only; the installer and the plugin never pass these
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
    if bridge is not None:
        bridge.cfg = cfg
        bridge.save_cfg = lambda c: yaml.safe_dump(c, open("config.yaml", "w", encoding="utf-8"), sort_keys=False, allow_unicode=True)
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
    if not DRY and cfg["twitch"]["enabled"] and cfg["twitch"].get("access_token"):
        try:
            from twitch import Twitch
            tw = Twitch(cfg["twitch"])
        except Exception as e:
            print(f"[twitch] not ready ({e}); log in from the OBS plugin (ClipHound tab)")
    if not DRY and cfg["obs"]["enabled"] and cfg["obs"].get("mode") == "bridge":
        from bridge import BridgeOBS
        ob = BridgeOBS(cfg["obs"], bridge)
    elif not DRY and cfg["obs"]["enabled"]:
        from obs import OBS
        ob = OBS(cfg["obs"])

    state = {"tw": tw, "ob": ob}

    def apply_live(c):
        # settings changed from the OBS plugin: name, library, Twitch, clip rules
        det.me = c["detection"].get("player_name", det.me)
        det.cfg["clip_every_kill"] = bool(c["detection"].get("clip_every_kill"))
        det.cfg["multikill_window_s"] = float(c["detection"].get("multikill_window_s", 12))
        try:
            if not DRY and c["twitch"].get("enabled") and c["twitch"].get("access_token"):
                from twitch import Twitch
                state["tw"] = Twitch(c["twitch"])
                print(f"[twitch] ready as {c['twitch'].get('clipper_login', '?')} for channel {c['twitch'].get('broadcaster_login', '?')}")
            else:
                state["tw"] = None
        except Exception as e:
            print(f"[twitch] not ready: {e}")
            state["tw"] = None
    if bridge is not None:
        bridge.on_config = apply_live

    def fire(trig):
        tw, ob = state["tw"], state["ob"]
        print(f"\n*** {trig.kind.upper()}: {trig.title}   tags={trig.tags} ***\n")
        if bridge is not None:
            bridge.event(f"{trig.title}  [{', '.join(trig.tags)}]", "trigger")
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
        if bridge is not None and hasattr(det, "last_new_events"):
            for ev in det.last_new_events:
                try:
                    who = f"{ev.killer} > {ev.victim}" + (f" {ev.distance_m}m" if getattr(ev, "distance_m", 0) else "")
                    bridge.event(who, "kill")
                except Exception:
                    pass
        time.sleep(max(0, period - (time.time() - t0)))


def _safe(fn, *a):
    try:
        fn(*a)
    except Exception as e:
        print(f"[error] {fn.__qualname__}: {e}")


if __name__ == "__main__":
    try:
        main()
    except SystemExit:
        raise
    except Exception:
        import traceback
        traceback.print_exc()
        raise
