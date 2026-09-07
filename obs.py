"""OBS on the streaming PC via obs-websocket v5 (TCP 4455 over the LAN).
Saves the replay buffer (backtrack) or runs a timed recording. Reconnects if the link drops."""
import json
import os
import re
import threading
import time
import obsws_python as obs


def _safe_name(s: str) -> str:
    return re.sub(r"[^A-Za-z0-9 _\-\[\]#]+", "", s).strip()


class OBS:
    def __init__(self, cfg):
        self.cfg = cfg
        self.cl = None
        self._lock = threading.Lock()
        self._connect()

    def _connect(self):
        try:
            self.cl = obs.ReqClient(host=self.cfg["host"], port=self.cfg["port"],
                                    password=self.cfg["password"], timeout=3)
            ver = self.cl.get_version()
            print(f"[obs] connected to {self.cfg['host']}:{self.cfg['port']} - OBS {ver.obs_version} "
                  f"(websocket {ver.obs_web_socket_version})")
            if self.cfg["mode"] == "replay" and not self.cl.get_replay_buffer_status().output_active:
                print("[obs] replay buffer is NOT running - starting it")
                self.cl.start_replay_buffer()
        except Exception as e:
            self.cl = None
            print(f"[obs] cannot reach OBS at {self.cfg['host']}:{self.cfg['port']} ({e}); "
                  f"retrying every {self.cfg['reconnect_s']}s")
            threading.Timer(self.cfg["reconnect_s"], self._connect).start()

    def trigger(self, title: str, tags: list[str] | None = None):
        with self._lock:
            if not self.cl:
                print(f"[obs] not connected, backtrack for '{title}' lost")
                return
            try:
                if self.cfg["mode"] == "replay":
                    self.cl.save_replay_buffer()
                    print(f"[obs] replay buffer saved for '{title}'")
                    threading.Timer(3.0, self._rename_last_replay, args=(title, tags or [])).start()
                else:
                    if self.cl.get_record_status().output_active:
                        print("[obs] already recording, skip")
                        return
                    self.cl.start_record()
                    print(f"[obs] recording {self.cfg['record_seconds']}s for '{title}'")
                    threading.Timer(self.cfg["record_seconds"], self._stop).start()
            except Exception as e:
                print(f"[obs] request failed ({e}); reconnecting")
                self.cl = None
                self._connect()

    def _rename_last_replay(self, title: str, tags: list[str]):
        """OBS names replays by timestamp; rename to '<stamp> <title> [tag1 tag2].mkv' so the
        files are searchable, and log them to replays.jsonl next to clips.jsonl."""
        try:
            path = self.cl.get_last_replay_buffer_replay().saved_replay_path
        except Exception as e:
            print(f"[obs] could not get replay path: {e}")
            return
        if not path or not os.path.exists(path):
            print(f"[obs] replay path not found: {path!r} (OBS on another machine? see README)")
            return
        folder, fname = os.path.split(path)
        stem, ext = os.path.splitext(fname)
        new = os.path.join(folder, _safe_name(f"{stem} {title} [{' '.join(tags)}]") + ext)
        try:
            os.rename(path, new)
        except OSError as e:
            print(f"[obs] rename failed ({e}); keeping {path}")
            new = path
        with open("replays.jsonl", "a", encoding="utf-8") as f:
            f.write(json.dumps({"file": new, "title": title, "tags": tags,
                                "created": time.strftime("%Y-%m-%d %H:%M:%S")}, ensure_ascii=False) + "\n")
        print(f"[obs] replay -> {new}")

    def _stop(self):
        try:
            self.cl.stop_record()
            print("[obs] recording stopped")
        except Exception as e:
            print(f"[obs] stop failed: {e}")
