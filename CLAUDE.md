# ClipHound — session context

Python bot for **twitch.tv/sombrero**. Runs on the **streaming PC** (Windows). Pulls the game
frame from OBS over obs-websocket, OCRs the WARDOGS kill-feed strip, and on a notable feed
row creates a Twitch clip (as the **InfoKennel** account) and saves + renames the OBS replay
buffer with tags. Search clips with `python clips.py <tag>`. One of the Kennel tools by
Sombrero; sibling apps live next door (HotkeyBridge, InputOverlayBridge, povbridge-obs).

| File | What |
|---|---|
| `main.py` | loop: capture ROI → detector → Twitch + OBS triggers; team colour from the HUD emblem every 30 s |
| `capture_obs.py` / `capture.py` | frame source: OBS `GetSourceScreenshot` (default) or a monitor via mss |
| `ocr.py` | tophat mask, row segmentation, per-column OCR, distance regex + voting, icon + own-name templates, colour |
| `colors.py` | team icon / squad text colour → red, blue, green, orange; emblem detection |
| `detector.py` | row tracking across frames, votes, dedupe, rule engine (`config.yaml` rules), multi-kill |
| `twitch.py` / `obs.py` | Helix create clip (title logged, API can't set titles); replay save + rename |
| `vod_test.py`, `calibrate.py` | offline test on a video slice; ROI / colour / source checks |
| `install.bat`, `setup.bat` / `setup.py`, `ClipHound.bat` | Windows install (winget Python + Tesseract, venv), interactive setup wizard (pick the OBS input that shows gameplay: capture card or Game Capture), run |
| `tools/extract_icons.py`, `tools/build_catalogue.py`, `icons/` | harvest feed icons from VOD slices -> labels.json -> reusable icon catalogue |
| `templates/` | white-on-black icon masks at 4x: rifle, boltgun, sniper, heli, explosion, tank, artillery (x2), car, mortar, hammer, rpg, c4, skull, name_me |

## Facts that set the constants (measured on the 7 Sep 2026 VODs, 1080p)
- Kill feed: x 0–24 %, y 42–58 % of the frame; rows 24 px apart, ~9 px text; a row fades in
  ~0.5 s and must be present every frame at 4 fps (feed blanks ~0.5 s between kills).
- Three teams: red (emblem hue ~0-5), green (69), blue (100). Team colour is the small icon
  before a killer / after a victim; squad = orange text + shield; own name is white.
  Distance `[68 m]` only appears on rows involving the streamer.
- Distance OCR: whole-row read, bracket regex, votes; partial reads support longer ones.
- Verified: 24:06 double kill (68 m, 61 m), 24:24 chopper crash, 1:38:52 artillery + headshot
  icons, 15:03 first stream tank, 1:40:35 C4, 1:45:43 RPG + 80 m heli death. Car 19:20, mortar 20:47, hammer 30:52 (first stream); 30:52 also = sniped 315 m headshot.

## Standing rules
- Ask before pushing to GitHub. No emoji-laden prose; lead with what changed.
- Don't put the streaming PC's IP on stream; keep the console off the captured display.
