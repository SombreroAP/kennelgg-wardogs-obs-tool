"""Colour in the kill feed -> relationship. Three teams: red, blue, green. Every player has a
small team-coloured icon in front of (killer) or after (victim) the name; squad mates are
additionally written in orange text with an orange shield. Your own name is white. Measured on the 7 Sep VODs: red emblem hue ~0-5, green emblem 69,
blue emblem 100; a green-team name in the feed reads hue 70 at saturation 120-145.
Colour is measured on the text-core pixels of a column (mask-selected, brightest half), as
median HSV. Hue bands are in config.yaml `colors` (OpenCV hue 0..179)."""
import cv2
import numpy as np

DEFAULT_BANDS = {           # (hue_lo, hue_hi, min_sat) - hue wraps when lo > hi
    "red": (168, 9, 70),
    "orange": (10, 30, 70),
    "green": (45, 85, 50),
    "blue": (86, 130, 45),
}
TEAMS = ("red", "blue", "green")


ICON_SIDE = (16, 80)         # team icon size range in upscaled px (4x): ~6-20 px at 1080p


def measure(hsv_up: np.ndarray, text_mask: np.ndarray | None = None) -> tuple[float, float, float] | None:
    """Colour of the team marker in a column. The marker is a small solid coloured icon
    (square / circle) in front of the killer or after the victim; squad mates also have
    orange text. Background (sunlit bark, grass) is rejected by only accepting compact,
    icon-sized colour blobs - or, for orange, the text pixels themselves."""
    sat = ((hsv_up[:, :, 1] >= 60) & (hsv_up[:, :, 2] >= 110)).astype(np.uint8)
    n, lab, stats, _ = cv2.connectedComponentsWithStats(sat)
    keep = np.zeros_like(sat, dtype=bool)
    for i in range(1, n):
        x, y, w, h, area = stats[i]
        if ICON_SIDE[0] <= w <= ICON_SIDE[1] and ICON_SIDE[0] <= h <= ICON_SIDE[1] and area >= 0.45 * w * h:
            keep[lab == i] = True                       # solid, compact blob = an icon
    px = hsv_up[keep]
    if len(px) < 60 and text_mask is not None:            # no icon: orange squad text?
        tp = hsv_up[(text_mask > 0) & (sat > 0)]
        if len(tp) >= 150:
            px = tp
    if len(px) < 60:
        return None
    px = px[px[:, 2] >= np.percentile(px[:, 2], 50)]
    h = np.median(px[:, 0]); s = np.median(px[:, 1]); v = np.median(px[:, 2])
    return float(h), float(s), float(v)


def classify(hsv: tuple | None, bands: dict | None = None) -> str:
    """-> 'red' | 'orange' | 'green' | 'blue' | 'neutral' | 'unknown'"""
    if hsv is None:
        return "unknown"
    h, s, v = hsv
    for name, (lo, hi, min_s) in (bands or DEFAULT_BANDS).items():
        if s < min_s:
            continue
        inside = lo <= h <= hi if lo <= hi else (h >= lo or h <= hi)
        if inside:
            return name
    return "neutral"


def relation(color: str, my_team: str) -> str:
    """Colour -> 'squad' | 'team' | 'enemy' | 'neutral' | 'unknown' given my team colour."""
    if color in ("neutral", "unknown"):
        return color
    if color == "orange":
        return "squad"
    return "team" if color == my_team else "enemy"


def _count_team_pixels(frame_bgr: np.ndarray, roi: dict, bands: dict | None, min_pixels: int) -> str | None:
    h, w = frame_bgr.shape[:2]
    r = roi
    crop = frame_bgr[int(h * r["y"]):int(h * (r["y"] + r["h"])), int(w * r["x"]):int(w * (r["x"] + r["w"]))]
    hsv = cv2.cvtColor(crop, cv2.COLOR_BGR2HSV)
    sat = hsv[:, :, 1] > 120
    bright = hsv[:, :, 2] > 120
    hue = hsv[:, :, 0]
    b = bands or DEFAULT_BANDS
    def count(name):
        lo, hi, _ = b[name]
        inside = (hue >= lo) & (hue <= hi) if lo <= hi else (hue >= lo) | (hue <= hi)
        return int((inside & sat & bright).sum())
    counts = {t: count(t) for t in TEAMS}
    best = max(counts, key=counts.get)
    return best if counts[best] >= min_pixels else None


def detect_my_team(frame_bgr: np.ndarray, icon_roi: dict | None, minimap_roi: dict | None,
                   bands: dict | None = None) -> str | None:
    """Team colour: first the team emblem in the bottom-right HUD (always on screen, drawn in
    the team colour), then the minimap team-mate pointers as a fallback. 'red' | 'blue' | 'green' | None."""
    if icon_roi:
        t = _count_team_pixels(frame_bgr, icon_roi, bands, min_pixels=15)
        if t:
            return t
    if minimap_roi:
        return _count_team_pixels(frame_bgr, minimap_roi, bands, min_pixels=30)
    return None
