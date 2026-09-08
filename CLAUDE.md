# kennel-wardogs-obs — session context

**"Kennel.gg WARDOGS OBS Tools"** — one OBS plugin (C++ / Qt, obs-plugintemplate, module id
`kennel-wardogs`) for Windows OBS, renamed from POVBridge on 8 Sep 2026. Two features: the **POV
swap** (downed → show a squad mate's POV; mic untouched) and **clips** (replay-buffer save +
rename, from hotkey/dock/downed/**ClipHound**). ClipHound (`../ClipHound`, Python) stays a separate
app that does the kill-feed OCR and talks to the plugin over a local WebSocket bridge; the split is
deliberate (no Tesseract inside OBS, downed detection stays native). An earlier C# tray app was
deleted; do not resurrect it. Builds go to Google Drive `My Drive/Kennel WARDOGS OBS Tools/`.

| File | What |
|---|---|
| `src/plugin-main.cpp` | module load: Engine + Dock (`obs_frontend_add_dock_by_id`), Tools menu, two frontend hotkeys (saved in `hotkeys.json`) |
| `src/engine.*` | QObject state machine on the UI thread; each poll spawns a worker that captures + matches, posts back via `QMetaObject::invokeMethod` |
| `src/detector.*` | NCC template search: 3x3 blur, integral images, 20 sizes (6 % steps, min height 10 px), band-limited, lock-on fast path |
| `src/capture.*` | render a source to 800 px BGRA: texrender + stagesurface under `obs_enter_graphics` (the obs-websocket screenshot pattern) |
| `src/switcher.*` | scene item show/hide, hide filter (`color_filter_v2` opacity 0) + mute for warm mode, browser sources for Twitch / VDO.Ninja / the look overlay, game-audio mute with restore |
| `src/config.*` | `config.json` via obs_data in the plugin config dir |
| `src/ui/dock.*`, `src/ui/settings-dialog.*` | dock; settings tabs Switch / Look / Detect / Help; FriendDialog; FramePreview with drag box |
| `data/templates/damagelog.png` | "B VIEW DAMAGE LOG" header cut from the user's 1704-wide screenshot (widthFrac 262/1704) |
| `data/templates/reviving.png` | "REVIVING" word cut from the user's 1875-wide revive screenshot (widthFrac 98/1875); ring centre = template top-left + (0.47, 1.31)·tw, radius 0.39·tw, lit if gray > 170 |
| `data/overlay/` | the look page + two brand fonts (from kennel-brand) |

## Measurements that set the constants (do not re-derive)

Python/numpy on the user's three screenshots (`My Drive/screenshots/`), frames resized to 800 wide:
- Damage-log header true match 0.974 / 0.984 (two frames), alive proxy 0.565 / 0.755, with
  blur σ≈0.7, 6 % size steps, min template height 10 px. Threshold default 0.85.
- Without blur the peak is so sharp in size that a 10 % size miss drops the score to ~0.73;
  8 px-tall templates score up to 0.87 on alive frames (hence the minimum height).
- A half-resolution coarse pass loses the header (5 px text); the search runs at full res, stride 2,
  inside x 0.60–1.0, y 0.25–0.85.

## Build status (7 Sep 2026)

- All sources compile clean with `-Werror` under clang (macOS preset). The macOS **link** fails on
  this Mac only because the macOS 26 SDK dropped the AGL framework that obs-deps 2025-07-11 still
  lists; irrelevant for Windows. The Windows build must come from GitHub Actions
  (`.github/workflows/build-project.yaml`, unchanged from the template) — needs the repo pushed to
  GitHub (`SombreroAP/povbridge-obs`, private). **Ask before pushing.**
- Engine throttles the full-frame search to every 3rd poll while unlocked (alive) — CPU question from the user.
- Nothing has run inside OBS yet. First things to check on Windows: `obs_frontend_add_dock_by_id`
  (OBS 30+), browser source setting keys, `color_filter_v2` opacity range 0..1, Twitch embed
  `parent=twitch.tv` autoplaying with sound in CEF, `file:///...?query` for the overlay.
- `.deps/` and `build_macos/` are local build output (gitignored).

## Standing rules

- Ask before pushing to GitHub or posting anywhere. User is on a phone: deliver builds via Drive.
- No emoji-laden prose; short messages, lead with what changed.

| `app/` | ClipHound (Python). `app/bridge.py` = client of the plugin bridge; `app/cliphound.spec` + `app/build_exe.ps1` = PyInstaller bundle with Tesseract copied in (CI, Windows job) |
| `installer/kennel-wardogs.iss` | Inno Setup: component `plugin` → ProgramData\obs-studio\plugins, component `app` → ProgramData\Kennel WARDOGS\ClipHound (users-modify), config.yaml kept on upgrade, Start-menu shortcuts, optional post-install setup run |
