"""Talk to the Kennel.gg WARDOGS OBS plugin over its local bridge (ws://127.0.0.1:47820).

Replaces both obs-websocket uses when `capture.backend: bridge` / `obs.mode: bridge`:
- frames: the plugin pushes JPEG crops of the game source at native resolution (binary frames,
  16-byte header "KWF1" + uint16 w + uint16 h + uint64 ts_ms, little endian);
- clips: we send {"type":"clip", ...}; the plugin saves OBS's replay buffer, renames the file with
  the tags and answers with clip_saved {path}. The plugin also sends pov events (downed/reviving/up).
Protocol v1 is documented in the plugin's README. Needs `pip install websocket-client`.
"""
import json
import os
import struct
import threading
import time

import cv2
import numpy as np

try:
    import websocket  # websocket-client
except ImportError as e:  # pragma: no cover
    raise SystemExit("pip install websocket-client") from e


class Bridge:
    """One connection shared by the frame source and the clip trigger. Reconnects on its own."""

    def __init__(self, cfg: dict):
        self.url = f"ws://127.0.0.1:{int(cfg.get('port', 47820))}"
        self.fps = float(cfg.get("fps", 3))
        self.ws = None
        self.connected = False
        self._frame = None            # latest full frame (BGR)
        self._frame_ts = 0.0
        self._lock = threading.Lock()
        self._pending = {}            # clip id -> callback(path)
        self._seq = 0
        self.pov_state = "up"
        self.on_pov = None            # callback(state, friend)
        self.game_source = ""
        self.cfg = None              # full config dict (set by main) for app_config / twitch messages
        self.save_cfg = None         # callable(cfg) that writes config.yaml
        self.on_config = None        # callable(cfg) after the plugin changed settings
        threading.Thread(target=self._run, daemon=True).start()

    # ---- connection ----
    def _run(self):
        self._lost_at = None
        while True:
            if self._lost_at and time.time() - self._lost_at > 20 and os.environ.get("KENNEL_FROM_OBS"):
                print("[bridge] plugin gone for 20 s and we were started by OBS - exiting")
                os._exit(0)
            try:
                self.ws = websocket.WebSocketApp(self.url, on_open=self._on_open, on_message=self._on_message,
                                                 on_close=self._on_close, on_error=lambda *_: None)
                self.ws.run_forever(ping_interval=20, ping_timeout=10)
            except Exception as e:
                print(f"[bridge] {e}")
            self.connected = False
            time.sleep(3)

    def _send_app_state(self):
        if self.cfg is None:
            return
        c = self.cfg
        self.send({"type": "app_config", "values": {
            "player_name": c["detection"].get("player_name", ""),
            "library": (c.get("obs") or {}).get("library", ""),
            "broadcaster": (c.get("twitch") or {}).get("broadcaster_login", ""),
            "twitch_enabled": bool((c.get("twitch") or {}).get("enabled")),
            "fps": (c.get("capture") or {}).get("fps", 3),
            "clip_every_kill": bool(c["detection"].get("clip_every_kill")),
            "multikill_window": float(c["detection"].get("multikill_window_s", 30)),
        }})
        from twitch_device import status
        self.send(status(c))

    def _on_open(self, ws):
        self.connected = True
        print(f"[bridge] connected to the plugin at {self.url}")
        self._send_app_state()
        # full frame at native size; we crop the kill feed ourselves so the minimap check still works
        self.send({"type": "subscribe", "frames": True, "fps": self.fps, "roi": [0, 0, 1, 1], "width": 0})
        self.status("ClipHound watching the kill feed")

    def _on_close(self, ws, *_):
        if self.connected:
            print("[bridge] plugin went away; reconnecting")
            self._lost_at = time.time()
        self.connected = False

    def _on_message(self, ws, msg):
        if isinstance(msg, (bytes, bytearray)):
            if len(msg) < 16 or msg[:4] != b"KWF1":
                return
            w, h, ts = struct.unpack_from("<HHQ", msg, 4)
            frame = cv2.imdecode(np.frombuffer(msg[16:], np.uint8), cv2.IMREAD_COLOR)
            if frame is not None:
                with self._lock:
                    self._frame, self._frame_ts = frame, ts / 1000.0
            return
        try:
            o = json.loads(msg)
        except ValueError:
            return
        t = o.get("type")
        if t == "hello":
            print(f"[bridge] plugin {o.get('plugin')} {o.get('version')} (protocol {o.get('protocol')})")
        elif t == "app_config" and self.cfg is not None and "set" in o:
            v = o["set"]
            c = self.cfg
            if "player_name" in v:
                c["detection"]["player_name"] = v["player_name"]
            if "library" in v:
                c.setdefault("obs", {})["library"] = v["library"]
            if "broadcaster" in v:
                c.setdefault("twitch", {})["broadcaster_login"] = v["broadcaster"]
                c["twitch"]["broadcaster_id"] = ""
            if "twitch_enabled" in v:
                c.setdefault("twitch", {})["enabled"] = bool(v["twitch_enabled"])
            if "fps" in v:
                c.setdefault("capture", {})["fps"] = float(v["fps"])
            if "clip_every_kill" in v:
                c["detection"]["clip_every_kill"] = bool(v["clip_every_kill"])
            if "multikill_window" in v:
                c["detection"]["multikill_window_s"] = float(v["multikill_window"])
            if self.save_cfg:
                self.save_cfg(c)
            print(f"[bridge] settings from the plugin: {v}")
            if self.on_config:
                self.on_config(c)
            self._send_app_state()
        elif t == "twitch_login" and self.cfg is not None:
            from twitch_device import start_login
            start_login(self.cfg, lambda st: self.send({"type": "twitch_status", **st}), lambda c: (self.save_cfg(c) if self.save_cfg else None, self.on_config(c) if self.on_config else None, self._send_app_state()))
        elif t == "twitch_logout" and self.cfg is not None:
            from twitch_device import logout
            logout(self.cfg, lambda c: (self.save_cfg(c) if self.save_cfg else None, self.on_config(c) if self.on_config else None))
            self._send_app_state()
        elif t == "app_state":
            self._send_app_state()
        elif t == "shutdown":
            print("[bridge] OBS is closing - ClipHound exiting")
            os._exit(0)
        elif t == "config":
            self.game_source = o.get("gameSource", "")
            self.pov_state = o.get("povState", "up")
        elif t == "pov":
            self.pov_state = o.get("state", "up")
            print(f"[bridge] pov: {self.pov_state} ({o.get('friend', '')})")
            if self.on_pov:
                self.on_pov(self.pov_state, o.get("friend", ""))
        elif t == "clip_result":
            if not o.get("ok"):
                print(f"[bridge] clip refused: {o.get('error')}")
                self._pending.pop(o.get("id"), None)
        elif t == "clip_saved":
            print(f"[bridge] clip saved: {o.get('path')}")
            # the plugin does not echo our id on clip_saved; pair with the oldest pending request
            if self._pending:
                cid = next(iter(self._pending))
                cb = self._pending.pop(cid)
                if cb:
                    cb(o.get("path", ""), o)

    def send(self, o: dict):
        try:
            if self.ws and self.connected:
                self.ws.send(json.dumps(o))
                return True
        except Exception as e:
            print(f"[bridge] send failed: {e}")
        return False

    def status(self, text: str):
        self.send({"type": "status", "text": text})

    def event(self, text: str, kind: str = "kill"):
        """Something happened in the kill feed (shown in the plugin's dock)."""
        self.send({"type": "event", "kind": kind, "text": text})

    # ---- frames ----
    def latest(self):
        with self._lock:
            return self._frame, self._frame_ts

    # ---- clips ----
    def clip(self, title: str, tags: list, source: str = "cliphound", on_saved=None) -> bool:
        self._seq += 1
        cid = f"c{self._seq}"
        self._pending[cid] = on_saved
        ok = self.send({"type": "clip", "id": cid, "title": title, "tags": list(tags), "source": source})
        if not ok:
            self._pending.pop(cid, None)
            print(f"[bridge] not connected, clip '{title}' lost")
        return ok


class BridgeRoiCapture:
    """Frame source with the same interface as ObsRoiCapture / RoiCapture, fed by the plugin."""

    def __init__(self, cap_cfg: dict, bridge: Bridge):
        self.b = bridge
        self.r = cap_cfg["roi"]
        self.upscale = float(cap_cfg.get("upscale", 2.0))
        print("[capture] waiting for the first frame from the plugin (is OBS running with the plugin?)")
        frame = self._wait_frame()
        h, w = frame.shape[:2]
        self.box = (int(w * self.r["x"]), int(h * self.r["y"]), int(w * self.r["w"]), int(h * self.r["h"]))
        print(f"[capture] plugin source '{self.b.game_source}' is {w}x{h}; ROI x,y,w,h = {self.box}")

    def _wait_frame(self):
        while True:
            f, _ = self.b.latest()
            if f is not None:
                return f
            time.sleep(0.5)

    def grab(self) -> np.ndarray:
        f, ts = self.b.latest()
        if f is None or time.time() - ts > 5:
            f = self._wait_frame()
        self._last = f
        x, y, w, h = self.box
        return f[y:y + h, x:x + w]

    def full_frame(self) -> np.ndarray:
        return self._last if getattr(self, "_last", None) is not None else self._wait_frame()

    def preprocess(self, bgr: np.ndarray) -> np.ndarray:
        if self.upscale != 1.0:
            bgr = cv2.resize(bgr, None, fx=self.upscale, fy=self.upscale, interpolation=cv2.INTER_CUBIC)
        gray = cv2.cvtColor(bgr, cv2.COLOR_BGR2GRAY)
        _, th = cv2.threshold(gray, 170, 255, cv2.THRESH_BINARY)
        th = cv2.bitwise_not(th)
        return cv2.medianBlur(th, 3)


class BridgeOBS:
    """Clip trigger with the same .trigger() as obs.OBS, but the plugin saves and names the file."""

    def __init__(self, obs_cfg: dict, bridge: Bridge):
        self.cfg = obs_cfg
        self.b = bridge

    def trigger(self, title: str, tags=None, info=None):
        info = info or {}
        tags = list(tags or [])
        if info.get("kind") and info["kind"] not in tags:
            tags.insert(0, info["kind"])
        if info.get("distance_m"):
            tags.append(f"{int(info['distance_m'])}m")
        if self.b.pov_state != "up":
            tags.append("downed")
        lib = self.cfg.get("library")

        def saved(path, _o):
            if lib and path:
                from obs import _update_library_index
                rec = dict(info)
                rec.update({"created": time.strftime("%Y-%m-%dT%H:%M:%S"), "file": path, "title": title, "tags": tags})
                try:
                    _update_library_index(lib, rec)
                except Exception as e:
                    print(f"[bridge] library index: {e}")

        self.b.clip(title, tags, "cliphound", on_saved=saved)
