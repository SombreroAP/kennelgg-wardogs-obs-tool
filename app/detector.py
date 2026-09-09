"""Track kill-feed rows across frames, decide each row once it has enough reads, then apply
the event rules from config (multi-kill, long range, deaths, vehicles, team kills)."""
import os
import re
import time
from collections import Counter
from dataclasses import dataclass, field

import cv2
import numpy as np

from ocr import read_rows, sig_iou, prof_corr, vote_distance, name_matches
from colors import relation


@dataclass
class FeedEvent:
    killer: str          # OCR text of the killer column
    victim: str          # OCR text of the victim column
    distance_m: int
    dist_conf: int       # number of OCR reads supporting distance_m
    icons: list[str]
    killer_rel: str      # me | squad | team | enemy | neutral | unknown
    victim_rel: str
    ts: float

    @property
    def killer_me(self): return self.killer_rel == "me"
    @property
    def victim_me(self): return self.victim_rel == "me"


@dataclass
class Trigger:
    kind: str
    title: str
    events: list[FeedEvent]
    tags: list[str] = field(default_factory=list)

    def describe(self) -> str:
        """Human/AI-readable summary for filenames: what happened, distances, weapon, outcome.
        e.g. 'Double kill - 68m 61m - rifle - 2 enemies' or 'Sniped from 315m - headshot - sniper - death'"""
        parts = [self.title]
        ds = [f"{e.distance_m}m" for e in self.events if e.distance_m]
        if ds and not any(p.endswith("m") for p in parts[0].split()):
            parts.append(" ".join(ds))
        weapons = [i for e in self.events for i in e.icons if i not in ("skull", "explosion")]
        if weapons:
            parts.append(sorted(set(weapons))[0])
        if any("skull" in e.icons for e in self.events) and "headshot" not in self.title.lower():
            parts.append("headshot")
        if any(e.victim_me for e in self.events):
            parts.append("death")
        elif len(self.events) > 1:
            parts.append(f"{len(self.events)} kills")
        return " - ".join(parts)

    @staticmethod
    def build(kind, title, events, extra=()):
        tags = {kind, *extra}
        for ev in events:
            tags.update(ev.icons)
            if "skull" in ev.icons:
                tags.add("headshot")
            if ev.victim_rel in ("squad", "team"):
                tags.add("teamkill")
            if ev.victim_me:
                tags.add("death")
        return Trigger(kind, title, events, sorted(tags))


@dataclass
class _Row:
    sig: np.ndarray
    prof: np.ndarray
    vprof: np.ndarray
    y: int
    first: float
    last: float
    reads: list = field(default_factory=list)
    done: bool = False
    crop: np.ndarray | None = None


class KillDetector:
    SIG_MATCH = 0.40           # IoU vs the previous frame above this = same row still on screen
    PROF_MATCH = 0.45          # ...and the killer-name profile must correlate (catches slot swaps)
    Y_MATCH = 8                # ...and within this many ROI px vertically (same slot)
    Y_SHIFT = 120              # ...or up to this far down: a new kill pushes older rows down a slot
    ROW_TTL_S = 0.7            # a row must be seen every frame or two; the feed blanks ~0.5 s
    MIN_READS = 4              # a row that vanishes (pushed off by a multi-kill) is still decided with this many reads
                               # between kills, and pixels can't tell two kills in one slot apart
    FADE_IN_S = 0.5            # rows fade in; OCR is garbage before this
    VOTES = 7                  # OCR samples before a row is decided (~1.5 s at 5 fps)
    DUP_S = 15.0               # same slot + same distance + same roles within this = same kill

    def __init__(self, cfg, dump_rows: str | None = None):
        self.cfg = cfg
        self.me = cfg["player_name"]
        self.my_team = cfg.get("my_team", "auto")      # 'red' | 'blue' | 'green' | 'auto' (set by main from the HUD)
        self.rules = cfg.get("rules") or []
        self.dump_rows = dump_rows
        self._rows: list[_Row] = []
        self._decided: list[tuple[float, int, int, tuple]] = []   # (ts, y, dist, roles)
        self._my_kills: list[FeedEvent] = []
        self._last_trigger = float('-inf')
        self._multikill_fired_for = 0
        self._n = 0

    # ---- frame in ---------------------------------------------------------------
    def feed_frame(self, roi_bgr) -> list[Trigger]:
        now = time.time()
        events = []
        for rd in read_rows(roi_bgr):
            best, best_iou = None, 0.0
            for r in self._rows:
                dy = rd.y - r.y
                if r.last == now or dy < -self.Y_MATCH or dy > self.Y_SHIFT:
                    continue
                iou = sig_iou(r.sig, rd.sig)
                # a row that moved down a slot must match its name profile strongly, not just its shape
                need_prof = self.PROF_MATCH if abs(dy) <= self.Y_MATCH else max(self.PROF_MATCH, 0.7)
                if iou > best_iou and prof_corr(r.prof, rd.prof) >= need_prof:
                    best, best_iou = r, iou
            if best is None or best_iou < self.SIG_MATCH:
                best = _Row(sig=rd.sig, prof=rd.prof, vprof=rd.vprof, y=rd.y, first=now, last=now)
                self._rows.append(best)
            best.last, best.sig, best.prof, best.y = now, rd.sig, rd.prof, rd.y
            if best.done or now - best.first < self.FADE_IN_S:
                continue
            best.reads.append(rd.ocr())              # only undecided rows cost tesseract time
            if best.crop is None:
                best.crop = rd._bgr
            if len(best.reads) >= self.VOTES:
                best.done = True
                ev = self._decide(best)
                if ev:
                    events.append(ev)
        # rows that vanish: HUD noise if they had almost no reads, otherwise (pushed off the feed by
        # a multi-kill, or faded) decide them with what we have
        keep = []
        for r in self._rows:
            if now - r.last < self.ROW_TTL_S:
                keep.append(r)
            elif not r.done and len(r.reads) >= self.MIN_READS:
                r.done = True
                ev = self._decide(r)
                if ev:
                    events.append(ev)
        self._rows = keep
        self.last_new_events = events   # every decided feed row (the plugin's dock shows them)
        return self._apply_rules(events, now)

    def _decide(self, row: _Row) -> FeedEvent | None:
        names = [r.name for r in row.reads]
        victims = [r.victim for r in row.reads]
        # majority of reads normally; crash rows (vehicle + explosion icons) sit on whatever the
        # crash site looks like, so there two agreeing reads are enough
        icon_reads = sum((r.icons for r in row.reads), [])
        crash = "explosion" in icon_reads
        need = 2 if crash else (len(names) // 2 + 1)
        killer_me = (sum(name_matches(n, self.me) for n in names) >= need
                     or sum(r.name_is_me for r in row.reads) >= 2)
        victim_me = (sum(name_matches(v, self.me) for v in victims) >= need
                     or sum(r.victim_is_me for r in row.reads) >= 2)
        top = Counter(names).most_common(1)[0][0]
        if len(re.sub(r"[^A-Za-z0-9]", "", top)) < 4 and not crash and not (killer_me or victim_me):
            return None                                # no readable killer name: HUD/texture noise
        kcol = Counter(r.name_color for r in row.reads).most_common(1)[0][0]
        vcol = Counter(r.victim_color for r in row.reads).most_common(1)[0][0]
        team = self.my_team if self.my_team in ("red", "blue", "green") else "unknown"
        killer_rel = "me" if killer_me else relation(kcol, team)
        victim_rel = "me" if victim_me else relation(vcol, team)
        if not (killer_me or victim_me or "squad" in (killer_rel, victim_rel)):
            return None                                # someone else's kill
        dist, conf = vote_distance(sum((r.dists for r in row.reads), []))
        dist = dist or 0
        # weapon icon: majority of reads; skull / explosion are small and flicker on busy
        # backgrounds, so 30 % of reads (at least 2) is enough
        cnt = Counter(sum((r.icons for r in row.reads), []))
        icons = [i for i, c in cnt.items()
                 if (c >= max(2, 0.3 * len(row.reads)) if i in ("skull", "explosion") else c * 2 > len(row.reads))]
        roles = (killer_rel, victim_rel)
        # the feed shifts rows down as new ones arrive, so identity is (roles, distance, icons)
        # inside a window, not the y position
        self._decided = [x for x in self._decided if row.first - x[0] < self.DUP_S]
        key = (roles, dist, tuple(sorted(icons)))
        if any(x[1] == key and prof_corr(x[2], row.vprof) >= 0.8 for x in self._decided):
            return None                                # same kill re-acquired after a shift / dropped frame
        self._decided.append((row.first, key, row.vprof))
        ev = FeedEvent(killer=Counter(names).most_common(1)[0][0], victim=Counter(victims).most_common(1)[0][0],
                       distance_m=dist, dist_conf=conf, icons=icons,
                       killer_rel=killer_rel, victim_rel=victim_rel, ts=row.first)
        print(f"[feed] {killer_rel}({kcol}) {ev.killer!r} -> {victim_rel}({vcol}) {ev.victim!r}  "
              f"{dist} m (x{conf}) icons={icons} reads={sum((r.dists for r in row.reads), [])}")
        if self.dump_rows is not None and row.crop is not None:
            os.makedirs(self.dump_rows, exist_ok=True)
            self._n += 1
            cv2.imwrite(os.path.join(self.dump_rows, f"row_{self._n:03d}_{dist}m.png"), row.crop)
        return ev

    # ---- events -> triggers ----------------------------------------------------------
    def _apply_rules(self, events: list[FeedEvent], now) -> list[Trigger]:
        out = []
        win = self.cfg["multikill_window_s"]
        self._my_kills = [k for k in self._my_kills if now - k.ts <= win]
        if not self._my_kills:
            self._multikill_fired_for = 0
        last = getattr(self, "_last_kill_key", None)
        for ev in events:
            my_kill = ev.killer_me and ev.victim_rel not in ("me", "squad", "team")
            if my_kill:
                # the same feed row is sometimes decided twice (with and without the weapon icon);
                # one kill = one entry
                key = (ev.victim.strip().lower()[:12], ev.distance_m)
                if last and last[0] == key and now - last[1] < 4:
                    continue
                self._last_kill_key = last = (key, now)
                self._my_kills.append(ev)
            matched = False
            for rule in self.rules:
                if self._rule_hits(rule, ev):
                    title = rule["title"].format(dist=ev.distance_m, killer=ev.killer, victim=ev.victim)
                    out.append(Trigger.build(rule.get("kind", "rule"), title, [ev], rule.get("tags", ())))
                    matched = True
                    break                                  # first matching rule wins
            if not matched and my_kill and self.cfg.get("clip_every_kill"):
                title = f"Kill {ev.distance_m}m" if ev.distance_m else "Kill"
                out.append(Trigger.build("kill", title, [ev], ("kill",)))
        n = len(self._my_kills)
        if n >= self.cfg["multikill_min"] and n > self._multikill_fired_for:
            names = {2: "Double", 3: "Triple", 4: "Quad", 5: "Penta"}.get(n, f"{n}x")
            out.append(Trigger.build("multikill", f"{names} kill ({n} players)", list(self._my_kills), ("multikill",)))
            self._multikill_fired_for = n
        # cooldown: one rule trigger per cooldown_s (a multikill upgrade is always allowed);
        # several rows decided in the same frame share one clip
        rule_ok = now - self._last_trigger >= self.cfg["cooldown_s"]
        keep = []
        for t in out:
            if t.kind == "multikill":
                keep.append(t)
            elif rule_ok:
                keep.append(t)
                rule_ok = False
        if keep:
            self._last_trigger = now
        return keep

    @staticmethod
    def _rule_hits(rule: dict, ev: FeedEvent) -> bool:
        # killer / victim in a rule: me | squad | team | friendly (squad or team) | enemy | other (not me) | any
        def ok(want, rel):
            return {"any": True, "me": rel == "me", "squad": rel == "squad", "team": rel == "team",
                    "friendly": rel in ("squad", "team"), "enemy": rel == "enemy",
                    "other": rel != "me"}[want]
        if not ok(rule.get("killer", "any"), ev.killer_rel) or not ok(rule.get("victim", "any"), ev.victim_rel):
            return False
        if ev.distance_m < rule.get("min_dist", 0):
            return False
        if "min_dist" in rule and ev.dist_conf < rule.get("min_conf", 3):
            return False                               # distance too uncertain for a range rule
        need = rule.get("icon")
        if need and not any(i.startswith(need) for i in ev.icons):
            return False
        return True
