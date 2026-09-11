"""Twitch Helix: create a clip. Title is recorded locally (see README on titles)."""
import json
import time
import requests

HELIX = "https://api.twitch.tv/helix"


class Twitch:
    def __init__(self, cfg):
        self.cfg = cfg
        self.h = {"Client-Id": cfg["client_id"], "Authorization": f"Bearer {cfg['access_token']}"}
        self.broadcaster_id = cfg.get("broadcaster_id") or self._user_id(cfg["broadcaster_login"])

    def _get(self, path, **params):
        r = requests.get(f"{HELIX}/{path}", headers=self.h, params=params, timeout=10)
        if r.status_code == 401 and self.cfg.get("refresh_token"):
            self._refresh()
            r = requests.get(f"{HELIX}/{path}", headers=self.h, params=params, timeout=10)
        r.raise_for_status()
        return r.json()

    def _refresh(self):
        data = {"grant_type": "refresh_token", "refresh_token": self.cfg["refresh_token"], "client_id": self.cfg["client_id"]}
        if self.cfg.get("client_secret"):
            data["client_secret"] = self.cfg["client_secret"]
        r = requests.post("https://id.twitch.tv/oauth2/token", data=data, timeout=10)
        r.raise_for_status()
        tok = r.json()
        self.cfg["access_token"] = tok["access_token"]
        self.cfg["refresh_token"] = tok.get("refresh_token", self.cfg["refresh_token"])
        self.h["Authorization"] = f"Bearer {tok['access_token']}"
        print("[twitch] token refreshed (update config.yaml with the new refresh_token if you restart)")

    def _user_id(self, login):
        d = self._get("users", login=login)
        return d["data"][0]["id"]

    def create_clip(self, title: str, tags: list[str] | None = None, _with_title: bool = True) -> dict | None:
        # Twitch names the clip after the stream unless told otherwise; the title goes with the
        # request (Helix accepts one of up to 100 characters). Length is Twitch's to decide: the API
        # takes the seconds leading up to the request, and its edit page trims after the fact.
        params = {
            "broadcaster_id": self.broadcaster_id,
            "has_delay": str(self.cfg.get("has_delay", False)).lower(),
        }
        if _with_title and title:
            params["title"] = title[:100]
        r = requests.post(f"{HELIX}/clips", headers=self.h, params=params, timeout=10)
        if r.status_code == 401 and self.cfg.get("refresh_token"):
            self._refresh()
            return self.create_clip(title, tags, _with_title)
        if r.status_code == 400 and _with_title:
            print("[twitch] Twitch refused the title; making the clip without one")
            return self.create_clip(title, tags, _with_title=False)
        if r.status_code == 404:
            print("[twitch] 404: channel is not live, no clip made")
            return None
        r.raise_for_status()
        clip = r.json()["data"][0]
        clip["title"] = title
        clip["tags"] = sorted(tags or [])
        clip["url"] = f"https://clips.twitch.tv/{clip['id']}"
        clip["created"] = time.strftime("%Y-%m-%d %H:%M:%S")
        with open("clips.jsonl", "a", encoding="utf-8") as f:
            f.write(json.dumps(clip, ensure_ascii=False) + "\n")
        print(f"[twitch] clip created: {clip['url']}  title='{title}'  tags={clip['tags']}  edit: {clip['edit_url']}")
        return clip
