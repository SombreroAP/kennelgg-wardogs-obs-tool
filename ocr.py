"""Kill-feed OCR. Rows are segmented from a top-hat mask (light UI text on any background),
then each row is read in columns: killer name, distance, victim name. Icons between the
columns are classified by template matching against templates/*.png. Every row also gets a
pixel signature so the detector can tell "same row still on screen" from "new kill",
independent of OCR noise."""
import difflib
from collections import Counter
import glob
import os
import re
from dataclasses import dataclass, field

import cv2
import numpy as np
import pytesseract

from colors import measure, classify

SCALE = 4                     # upscale factor before OCR (feed text is ~9 px tall at 1080p)
NAME_COL = (0.00, 0.40)       # fraction of ROI width holding "Kennel.gg - Sombrero"
VICTIM_COL = (0.50, 1.00)     # victim name (after the distance)
ICON_COL = (0.20, 0.62)       # weapon / kill-type icons live here (vehicle icons start further left)
SIG_H_PX = 12                 # signature window height in ROI px, centred on the text
SIG_SHAPE = (96, 8)           # (w, h) of the downsampled signature
OCR_CFG = "--psm 7 --oem 3"
# Distance token as tesseract tends to see it: "[68 m]", "(6im)", "[8m", "68 m]"... At least one
# bracket, or a word boundary on both sides, so "Sombrero" can never become "50 m".
_D = "[0-9OoIl|!iSBG]"
DIST_RE = re.compile(rf"(?:[\[\({{]\s*({_D}{{1,4}})\s*m\s*[\]\)}}]?|({_D}{{1,4}})\s*m\s*[\]\)}}]|\b({_D}{{1,4}})\s*m\b)")
CONFUSE = str.maketrans({"i": "1", "l": "1", "I": "1", "|": "1", "!": "1", "O": "0", "o": "0",
                         "S": "5", "B": "8", "G": "6"})
TEMPLATE_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "templates")
ICON_MATCH = 0.60             # normalised cross-correlation threshold for an icon template
# Weapon icons are mutually exclusive (one weapon per row): the best-scoring one wins.
# Kill-type icons (skull = headshot, explosion) can appear alongside a weapon.
KILLTYPE_ICONS = ("skull", "explosion")
NAME_MATCH = 0.55             # threshold for templates/name_*.png (your own feed name)
COLOR_BANDS = None            # set from config by main/calibrate; None = colors.DEFAULT_BANDS


@dataclass
class RowRead:
    y: int                    # row top in ROI pixels
    sig: np.ndarray           # bool array SIG_SHAPE[::-1]
    _inv: np.ndarray          # inverted upscaled mask of the whole row (for lazy OCR)
    _mask: np.ndarray         # upscaled mask of the row (for icon matching)
    _bgr: np.ndarray          # original-resolution row crop (for dumping / labelling)
    _hsv: np.ndarray          # upscaled HSV of the row (for name colours)
    name: str = ""
    victim: str = ""
    name_color: str = "unknown"
    victim_color: str = "unknown"
    name_is_me: bool = False     # own-name template matched in the killer column
    victim_is_me: bool = False   # own-name template matched right of the icons
    dists: list = field(default_factory=list)   # distance reads this frame (voting in the detector)
    icons: list = field(default_factory=list)

    def ocr(self):
        """Read the columns. Called only for rows the detector is undecided on."""
        W = self._inv.shape[1]
        col = lambda c: self._inv[:, int(W * c[0]):int(W * c[1])]
        self.name = _ocr(col(NAME_COL))
        self.victim = _ocr(col(VICTIM_COL))
        W = self._mask.shape[1]
        mcol = lambda c: self._mask[:, int(W * c[0]):int(W * c[1])]
        hcol = lambda c: self._hsv[:, int(W * c[0]):int(W * c[1])]
        # distance: read the whole row (context helps tesseract keep the brackets), then also
        # re-read just the bracketed word box at 2x. Both votes go to the detector.
        row = _prep_row(self._inv)
        text, data = _ocr_data(row)
        self.dists = [d for d in (_digits(text), _digits_from_box(row, data)) if d]
        # colour comes from the team icon in front of the killer / after the victim (and orange
        # squad text), so measure the whole column on saturated pixels, not just text pixels
        self.name_color = classify(measure(hcol(NAME_COL), mcol(NAME_COL)), COLOR_BANDS)
        vx = _victim_span(data, W)                     # exact x-span of the victim's name, if found
        vsel = slice(vx[0], W) if vx else slice(int(W * 0.5), W)
        self.victim_color = classify(measure(self._hsv[:, vsel], self._mask[:, vsel]), COLOR_BANDS)
        # icons are pure white; match on a white-pixel mask, which stays clean on busy backgrounds
        white = ((self._hsv[:, :, 2] > 150) & (self._hsv[:, :, 1] < 70)).astype(np.uint8) * 255
        self.icons = match_icons(white[:, int(W * ICON_COL[0]):int(W * ICON_COL[1])])
        # own-name template: robust where OCR fails (rock, sky, wood backgrounds)
        self.name_is_me = match_name(white[:, :int(W * NAME_COL[1])])
        self.victim_is_me = match_name(white[:, int(W * 0.45):])
        return self


def binarize(roi_bgr: np.ndarray):
    """Return (upscaled gray, tophat mask). Tophat keeps small bright features = HUD text."""
    up = cv2.resize(roi_bgr, None, fx=SCALE, fy=SCALE, interpolation=cv2.INTER_CUBIC)
    gray = cv2.cvtColor(up, cv2.COLOR_BGR2GRAY)
    binarize.last_up = up
    k = cv2.getStructuringElement(cv2.MORPH_RECT, (5 * SCALE, 5 * SCALE))
    th = cv2.morphologyEx(gray, cv2.MORPH_TOPHAT, k)
    _, mask = cv2.threshold(th, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
    return gray, mask


def segment_rows(mask: np.ndarray, min_h_px: int = 6, pad_px: int = 3) -> list[tuple[int, int]]:
    """Row spans (y0, y1) in *ROI* pixels, from the horizontal projection of the name column."""
    h, w = mask.shape
    col = mask[:, int(w * NAME_COL[0]):int(w * NAME_COL[1])] > 0
    proj = col.sum(1)
    thr = col.shape[1] * 0.06
    rows, on, y0 = [], False, 0
    for y, v in enumerate(proj):
        if v > thr and not on:
            on, y0 = True, y
        elif v <= thr and on:
            on = False
            rows.append((y0, y))
    if on:
        rows.append((y0, h))
    out = []
    for y0, y1 in rows:
        if (y1 - y0) >= min_h_px * SCALE:
            out.append((max(0, y0 // SCALE - pad_px), min(h // SCALE, y1 // SCALE + pad_px)))
    return out


def _ocr(img) -> str:
    return pytesseract.image_to_string(img, config=OCR_CFG).strip()


def _digits(text: str) -> str | None:
    m = DIST_RE.search(text)
    if not m:
        return None
    g = next(g for g in m.groups() if g).translate(CONFUSE)
    return g if g.isdigit() else None


def _prep_row(inv: np.ndarray) -> np.ndarray:
    """Thicken strokes slightly and pad so brackets at the crop edge survive."""
    img = cv2.erode(inv, np.ones((2, 2), np.uint8))
    return cv2.copyMakeBorder(img, 8, 8, 16, 16, cv2.BORDER_CONSTANT, value=255)


def _ocr_data(img):
    d = pytesseract.image_to_data(img, config=OCR_CFG, output_type=pytesseract.Output.DICT)
    return " ".join(w for w in d["text"] if w), d


PAD_X, PAD_Y = 16, 8          # padding added by _prep_row (word boxes are in padded coords)


def _victim_span(data, W) -> tuple[int, int] | None:
    """x-range (row coords) of the word boxes after the distance bracket, i.e. the victim's
    name, skipping the trailing platform icon (a short box at the end)."""
    idx = [i for i, w in enumerate(data["text"]) if w and re.search(r"[\]\)}]", w)]
    if idx:
        i0 = idx[-1]
        boxes = [(data["left"][i], data["width"][i]) for i in range(i0 + 1, len(data["text"]))
                 if data["text"][i].strip() and data["width"][i] > 8]
    else:   # no distance (vehicle crash rows): the victim is whatever text sits right of centre
        boxes = [(data["left"][i], data["width"][i]) for i in range(len(data["text"]))
                 if data["text"][i].strip() and data["width"][i] > 8 and data["left"][i] - PAD_X > W * 0.5]
    if not boxes:
        return None
    if len(boxes) > 1 and boxes[-1][1] < 12 * SCALE:   # trailing icon box
        boxes = boxes[:-1]
    x0 = max(0, boxes[0][0] - PAD_X)
    x1 = min(W, boxes[-1][0] + boxes[-1][1] - PAD_X)
    return (x0, x1) if x1 - x0 > 8 else None


def _digits_from_box(img, data) -> str | None:
    for i, w in enumerate(data["text"]):
        if re.search(r"[\[\(\]\)]", w) or re.fullmatch(rf"{_D}+m?", w):
            x, y, ww, hh = data["left"][i], data["top"][i], data["width"][i], data["height"][i]
            crop = img[max(0, y - 6):y + hh + 6, max(0, x - 10):x + ww + 10]
            crop = cv2.resize(crop, None, fx=2, fy=2, interpolation=cv2.INTER_CUBIC)
            d = _digits(_ocr(crop))
            if d:
                return d
    return None


def read_rows(roi_bgr: np.ndarray) -> list[RowRead]:
    """Segment rows and compute signatures. No OCR yet - call RowRead.ocr() on the ones you need."""
    gray, mask = binarize(roi_bgr)
    hsv = cv2.cvtColor(binarize.last_up, cv2.COLOR_BGR2HSV)
    H, W = mask.shape
    inv = cv2.bitwise_not(mask)                     # dark text on white for tesseract
    out = []
    for y0, y1 in segment_rows(mask):
        Y0, Y1 = y0 * SCALE, y1 * SCALE
        # full-width signature, vertically centred on the name text so a 1-2 px row jitter
        # doesn't change it; compared frame-to-frame by the detector
        band = mask[Y0:Y1, :int(W * NAME_COL[1])]
        ys = np.where(band.sum(1) > 0)[0]
        cy = (ys.mean() if len(ys) else band.shape[0] / 2) + Y0
        h = SIG_H_PX * SCALE
        S0 = int(max(0, min(H - h, cy - h / 2)))
        sig = cv2.resize(mask[S0:S0 + h], SIG_SHAPE, interpolation=cv2.INTER_AREA) > 64
        out.append(RowRead(y=y0, sig=sig, _inv=inv[Y0:Y1], _mask=mask[Y0:Y1], _bgr=roi_bgr[y0:y1],
                           _hsv=hsv[Y0:Y1]))
    return out


def sig_iou(a: np.ndarray, b: np.ndarray, cols: tuple[float, float] = (0.0, 1.0)) -> float:
    """IoU of two signatures, optionally restricted to a horizontal fraction."""
    w = a.shape[1]
    a, b = a[:, int(w * cols[0]):int(w * cols[1])], b[:, int(w * cols[0]):int(w * cols[1])]
    inter = np.logical_and(a, b).sum()
    union = np.logical_or(a, b).sum()
    return inter / union if union else 0.0


MAX_DIST_M = 999


def vote_distance(reads: list[str]) -> tuple[int | None, int]:
    """Pick the distance from noisy reads -> (metres, support). A partial read ('8' for '68')
    supports any longer candidate it is a prefix/suffix of, so '68','8','8','6' -> (68, 4).
    Multi-digit candidates need at least two supporting reads; otherwise fall back to the
    plain majority. Support is the number of reads behind the answer - use it as confidence."""
    reads = [r for r in reads if r and int(r) <= MAX_DIST_M]
    if not reads:
        return None, 0
    support = lambda c: sum(r == c or c.startswith(r) or c.endswith(r) for r in reads)
    cands = [c for c in set(reads) if len(c) >= 2 and support(c) >= 2]
    if cands:
        best = max(cands, key=lambda c: (support(c), len(c)))
        return int(best), support(best)
    best, n = Counter(reads).most_common(1)[0]
    return int(best), n


def name_matches(text: str, name: str, min_ratio: float = 0.72) -> bool:
    """Fuzzy: does `name` appear in OCR `text`? ('Kennel.gg-Sambrezo' still matches 'Sombrero').
    Also accepts a clean fragment of at least 5 characters ('pPOOH' for 'wOnderPOOH') at a
    stricter ratio, for rows where the background eats half the name."""
    t, n = text.lower(), name.lower()
    if n in t:
        return True
    L = len(n)
    if any(difflib.SequenceMatcher(None, t[i:i + L], n).ratio() >= min_ratio
           for i in range(max(1, len(t) - L + 1))):
        return True
    words = [w for w in re.split(r"[^a-z0-9]+", t) if len(w) >= 5]
    for w in words:
        for j in range(0, L - len(w) + 1):
            if difflib.SequenceMatcher(None, w, n[j:j + len(w)]).ratio() >= 0.8:
                return True
    return False


# ---- icons ---------------------------------------------------------------------------------
_templates: dict[str, np.ndarray] | None = None


def load_templates() -> dict[str, np.ndarray]:
    """templates/<icon>.png : binary (white-on-black) icon masks at ROI resolution x SCALE,
    cut from the white pixels of a row (see README "Icons")."""
    global _templates
    if _templates is None:
        _templates = {}
        for p in glob.glob(os.path.join(TEMPLATE_DIR, "*.png")):
            t = cv2.imread(p, cv2.IMREAD_GRAYSCALE)
            if t is not None:
                _templates[os.path.splitext(os.path.basename(p))[0]] = t
    return _templates


def _scores(mask_band: np.ndarray, names) -> dict[str, float]:
    out = {}
    for name in names:
        t = load_templates()[name]
        if t.shape[0] > mask_band.shape[0] or t.shape[1] > mask_band.shape[1]:
            continue
        out[name] = float(cv2.matchTemplate(mask_band, t, cv2.TM_CCOEFF_NORMED).max())
    return out


def match_icons(mask_band: np.ndarray) -> list[str]:
    names = [n for n in load_templates() if not n.startswith("name_")]
    s = _scores(mask_band, names)
    hits = [n for n in KILLTYPE_ICONS if s.get(n, 0) >= ICON_MATCH]
    weapons = {n: v for n, v in s.items() if n not in KILLTYPE_ICONS}
    if weapons:
        best = max(weapons, key=weapons.get)
        if weapons[best] >= ICON_MATCH:
            hits.append(re.sub(r"_\d+$", "", best))   # tank_2.png -> "tank" (variants of one icon)
    return hits


def match_name(mask_band: np.ndarray) -> bool:
    """Does one of templates/name_*.png (your own feed name) appear in this band?"""
    names = [n for n in load_templates() if n.startswith("name_")]
    return any(v >= NAME_MATCH for v in _scores(mask_band, names).values())
