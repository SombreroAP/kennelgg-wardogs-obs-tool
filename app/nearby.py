"""Read the game's NEARBY panel (bottom right of the HUD): which squad mates are near you and
how far away they are, in metres.

The panel is a short right-aligned list - one row per player, the name then a small chip with
the distance - and it stays on screen while you are down, which is exactly when the OBS plugin
wants it: it shows the POV of whoever is closest, because that is who is coming to revive you.

Only rows whose name matches one of the squad mates configured in the plugin are reported, so
OCR noise cannot make the plugin switch to somebody who is not there. Kept deliberately cheap:
one tesseract call per row, a second small one only when the distance chip did not come out.
"""
import re

import cv2
import numpy as np
import pytesseract

from ocr import name_matches

SCALE = 3                     # upscale before OCR; the panel's text is ~14 px tall at 1080p
NAME_CFG = "--psm 6 --oem 3"                                   # the names, stacked, one line each
DIST_CFG = "--psm 11 --oem 3 -c tessedit_char_whitelist=0123456789m"   # the distance chips, stacked
ONE_CFG = "--psm 8 --oem 3 -c tessedit_char_whitelist=0123456789m"     # one chip on its own (fallback)
DIST_RE = re.compile(r"([0-9OoIl|!iSBG]{1,3})\s*[mM]")
CONFUSE = str.maketrans({"i": "1", "l": "1", "I": "1", "|": "1", "!": "1", "O": "0", "o": "0",
                         "S": "5", "B": "8", "G": "6"})
MAX_M = 999
GAP = 24                      # white space between stacked rows so tesseract keeps them apart
JUNK = ("nearby", "squad")    # the panel's own header


def _mask(gray_up: np.ndarray) -> np.ndarray:
    """Small bright features (HUD text) on any background."""
    k = cv2.getStructuringElement(cv2.MORPH_RECT, (5 * SCALE, 5 * SCALE))
    th = cv2.morphologyEx(gray_up, cv2.MORPH_TOPHAT, k)
    _, m = cv2.threshold(th, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
    return m


def _row_spans(mask: np.ndarray, min_h: int = 5, pad: int = 2) -> list[tuple[int, int]]:
    """(y0, y1) of each text row in upscaled pixels."""
    proj = (mask > 0).sum(1)
    thr = max(3.0, mask.shape[1] * 0.015)
    out, on, y0 = [], False, 0
    for y, v in enumerate(proj):
        if v > thr and not on:
            on, y0 = True, y
        elif v <= thr and on:
            on = False
            if y - y0 >= min_h * SCALE:
                out.append((max(0, y0 - pad * SCALE), min(mask.shape[0], y + pad * SCALE)))
    if on and mask.shape[0] - y0 >= min_h * SCALE:
        out.append((max(0, y0 - pad * SCALE), mask.shape[0]))
    return out


def _sheet(crops: list[np.ndarray]) -> tuple[np.ndarray, list[tuple[int, int]]]:
    """Stack row crops into one image with white gaps, so all of them are read in a single
    tesseract call (starting tesseract costs far more than the pixels do). Returns the sheet
    and each row's (top, bottom) inside it."""
    w = max(c.shape[1] for c in crops) + 24
    parts, bands, y = [], [], GAP
    parts.append(np.full((GAP, w), 255, np.uint8))
    for c in crops:
        pad = np.full((c.shape[0], w), 255, np.uint8)
        pad[:, 12:12 + c.shape[1]] = c
        parts.append(pad)
        bands.append((y, y + c.shape[0]))
        y += c.shape[0] + GAP
        parts.append(np.full((GAP, w), 255, np.uint8))
    return np.vstack(parts), bands


def _read_sheet(crops: list[np.ndarray], cfg: str) -> list[str]:
    """One OCR call for all the crops; the text of each comes back in the same order."""
    if not crops:
        return []
    sheet, bands = _sheet(crops)
    d = pytesseract.image_to_data(sheet, config=cfg, output_type=pytesseract.Output.DICT)
    out = [""] * len(bands)
    for i, word in enumerate(d["text"]):
        if not word.strip():
            continue
        mid = d["top"][i] + d["height"][i] / 2
        for j, (y0, y1) in enumerate(bands):
            if y0 - GAP / 2 <= mid <= y1 + GAP / 2:
                out[j] = (out[j] + " " + word).strip()
                break
    return out


def _ocr_one(crop: np.ndarray) -> str:
    img = cv2.copyMakeBorder(crop, 12, 12, 18, 18, cv2.BORDER_CONSTANT, value=255)
    return pytesseract.image_to_string(img, config=ONE_CFG).strip()


def _metres(text: str) -> int | None:
    m = DIST_RE.search(text)
    if not m:
        return None
    g = m.group(1).translate(CONFUSE)
    return int(g) if g.isdigit() and int(g) <= MAX_M else None


def _chip(gray_row: np.ndarray):
    """The distance sits in a small solid chip at the right end of the row: dark text on a light
    box, the opposite way round from the name. Eroding the bright pixels rubs out the thin glyphs
    and leaves the chip, which is then the biggest thing left. Returns (x, y, w, h) or None."""
    _, th = cv2.threshold(gray_row, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
    k = max(3, (SCALE * 3) // 2)
    er = cv2.erode(th, np.ones((k, k), np.uint8))
    n, _lab, stats, _c = cv2.connectedComponentsWithStats(er, 8)
    best = None
    for i in range(1, n):
        x, y, w, h, area = stats[i]
        if w >= 6 * SCALE and h >= 4 * SCALE and (best is None or area > best[4]):
            best = (x, y, w, h, area)
    return best[:4] if best else None


def _sig(crop: np.ndarray) -> np.ndarray:
    """Cheap fingerprint of a name crop, so an unchanged name is not read again next frame."""
    return cv2.resize(crop, (128, 16), interpolation=cv2.INTER_AREA) < 128


def _cached(prev: list, sig: np.ndarray) -> str:
    """The text read for a name that looked like this last frame. Compared on the glyph pixels
    only (overlap, not equality): the background behind the panel moves, and most of the crop is
    background, so plain pixel equality calls two different names the same."""
    for s, t in prev:
        if s.shape != sig.shape:
            continue
        union = np.logical_or(s, sig).sum()
        if union and np.logical_and(s, sig).sum() / union >= 0.75:
            return t
    return ""


def read(roi_bgr: np.ndarray, names: list[str], cache: dict | None = None) -> list[dict]:
    """[{name, dist, match}] for every row that matched one of `names`, nearest first.

    Two tesseract calls for the whole panel: one for the names, one for the distance chips.
    Pass a dict as `cache` and names that have not changed since the last frame are not read
    again, which leaves one call per frame in the steady state."""
    if roi_bgr is None or roi_bgr.size == 0 or not names:
        return []
    up = cv2.resize(roi_bgr, None, fx=SCALE, fy=SCALE, interpolation=cv2.INTER_CUBIC)
    gray = cv2.cvtColor(up, cv2.COLOR_BGR2GRAY)
    mask = _mask(gray)
    nameCrops, chipCrops = [], []
    for y0, y1 in _row_spans(mask):
        g, m = gray[y0:y1], mask[y0:y1]
        box = _chip(g)
        if box is None:
            continue                                  # no distance chip: the NEARBY header, or noise
        cx, cy, cw, ch = box
        if cx < 8:
            continue                                  # nothing left of the chip to read a name from
        crop = g[max(0, cy - 2):cy + ch + 2, max(0, cx - 3):cx + cw + 3]
        crop = cv2.resize(crop, None, fx=2, fy=2, interpolation=cv2.INTER_CUBIC)
        _, bw = cv2.threshold(crop, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
        chipCrops.append(bw)
        nameCrops.append(cv2.bitwise_not(m[:, :cx - 2]))
    if not chipCrops:
        return []

    sigs = [_sig(c) for c in nameCrops]
    prev = (cache or {}).get("rows", [])
    texts: list[str] = [""] * len(sigs)
    todo = []
    for i, sig in enumerate(sigs):
        hit = _cached(prev, sig) if cache is not None else ""
        if hit:
            texts[i] = hit
        else:
            todo.append(i)
    for i, t in zip(todo, _read_sheet([nameCrops[i] for i in todo], NAME_CFG)):
        texts[i] = t
    if cache is not None:                             # only ever holds the rows on screen now
        cache["rows"] = [(sig, t) for sig, t in zip(sigs, texts) if t]

    dists = _read_sheet(chipCrops, DIST_CFG)
    seen: dict[str, dict] = {}
    for i, (text, dtext) in enumerate(zip(texts, dists)):
        dist = _metres(dtext)
        if dist is None:                              # the stacked read missed this one: try it alone
            dist = _metres(_ocr_one(chipCrops[i]))
        if dist is None or not text or any(j in text.lower() for j in JUNK):
            continue
        match = next((n for n in names if n and name_matches(text, n)), "")
        if not match:
            continue
        who = re.sub(r"[^A-Za-z0-9 ._-]", "", text).strip()
        old = seen.get(match.lower())
        if old is None or dist < old["dist"]:
            seen[match.lower()] = {"name": who or match, "dist": dist, "match": match}
    return sorted(seen.values(), key=lambda e: e["dist"])


class Watcher:
    """Reads the panel from the plugin's frames as often as the plugin asked for, and sends the
    list back over the bridge whenever it changes."""

    def __init__(self, bridge):
        self.b = bridge
        self.last = 0.0
        self.sent: list[dict] = []
        self.sent_at = 0.0
        self.warned = False
        self.cache: dict = {}

    def maybe_read(self, frame, now: float):
        c = self.b.nearby_cfg
        if frame is None or not c.get("enabled") or not c.get("names"):
            return
        # read often while it matters (going down, or a squad mate's POV already on screen),
        # once a second otherwise: one tesseract call a second is a few per cent of one core
        busy = now < self.b.nearby_burst or self.b.pov_state != "up"
        if now - self.last < (0.4 if busy else float(c.get("interval", 1.0))):
            return
        self.last = now
        h, w = frame.shape[:2]
        r = c.get("roi") or [0.80, 0.79, 0.19, 0.14]
        x0, y0 = int(w * r[0]), int(h * r[1])
        crop = frame[y0:y0 + int(h * r[3]), x0:x0 + int(w * r[2])]
        try:
            found = read(crop, list(c["names"]), self.cache)
        except Exception as e:
            if not self.warned:
                self.warned = True
                print(f"[nearby] cannot read the panel: {e}")
            return
        if found != self.sent or now - self.sent_at > 5:
            if found != self.sent:
                print("[nearby] " + (", ".join(f"{e['match']} {e['dist']}m" for e in found) or "nobody"))
            self.sent, self.sent_at = found, now
            self.b.send({"type": "nearby", "list": found})
