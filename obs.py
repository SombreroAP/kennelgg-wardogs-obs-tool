"""OBS on the streaming PC via obs-websocket v5 (TCP 4455 over the LAN).
Saves the replay buffer (backtrack) or runs a timed recording. Reconnects if the link drops."""
import json
import os
import re
import threading
import time
import obsws_python as obs


def _update_library_index(lib: str, rec: dict):
    """index.csv (everything) and resolve_metadata.csv (DaVinci Resolve import format:
    File Name, Description, Keywords, Comments) in the library root."""
    import csv
    idx = os.path.join(lib, "index.csv")
    new = not os.path.exists(idx)
    with open(idx, "a", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        if new:
            w.writerow(["created", "file", "title", "tags", "kind", "distance_m", "killer", "victim", "icons"])
        w.writerow([rec.get("created"), rec.get("file"), rec.get("title"), " ".join(rec.get("tags", [])),
                    rec.get("kind"), rec.get("distance_m"), rec.get("killer"), rec.get("victim"), " ".join(rec.get("icons", []))])
    rm = os.path.join(lib, "resolve_metadata.csv")
    new = not os.path.exists(rm)
    with open(rm, "a", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        if new:
            w.writerow(["File Name", "Description", "Keywords", "Comments", "Scene", "Shot"])
        w.writerow([os.path.basename(rec.get("file", "")), rec.get("description", rec.get("title")), ",".join(rec.get("tags", [])),
                    f"{rec.get('kind','')} {rec.get('distance_m','')}m killer={rec.get('killer','')} victim={rec.get('victim','')}".strip(),
                    rec.get("created", "")[:10], rec.get("kind", "")])


def _safe_name(s: str) -> str:
    s = re.sub(r"[^A-Za-z0-9 _\-\[\]#@.]+", "", s)
    return re.sub(r"\s+", " ", s).strip()[:180]        # Windows path budget


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

    def trigger(self, title: str, tags: list[str] | None = None, info: dict | None = None):
        """title: short clip title. info['description'] (if given) is the long filename summary."""
        self._info = info or {}
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
        lib = self.cfg.get("library") or ""
        if lib:
            folder = os.path.join(lib, time.strftime("%Y-%m-%d"))
            os.makedirs(folder, exist_ok=True)
        desc = self._info.get("description") or title
        moment = self.cfg.get("replay_delay_s", 4.0) + self._info.get("decision_lag_s", 3.0)
        # e.g. "Replay 2026-09-08 20-14-33 - Double kill - 68m 61m - rifle - 2 kills [multikill rifle] @-7s.mkv"
        new = os.path.join(folder, _safe_name(f"{stem} - {desc} [{' '.join(tags)}] @-{moment:.0f}s") + ext)
        try:
            os.rename(path, new)
        except OSError as e:
            print(f"[obs] rename failed ({e}); keeping {path}")
            new = path
        rec = {"file": new, "title": title, "description": desc, "tags": tags,
               "created": time.strftime("%Y-%m-%d %H:%M:%S"),
               "moment_s_from_end": round(moment, 1),      # the kill is about this far before the clip ends
               **{k: v for k, v in getattr(self, "_info", {}).items() if k != "description"}}
        with open("replays.jsonl", "a", encoding="utf-8") as f:
            f.write(json.dumps(rec, ensure_ascii=False) + "\n")
        with open(os.path.splitext(new)[0] + ".json", "w", encoding="utf-8") as f:
            json.dump(rec, f, ensure_ascii=False, indent=1)          # sidecar next to the clip
        if lib:
            _update_library_index(lib, rec)
        print(f"[obs] replay -> {new}")

    def _stop(self):
        try:
            self.cl.stop_record()
            print("[obs] recording stopped")
        except Exception as e:
            print(f"[obs] stop failed: {e}")
