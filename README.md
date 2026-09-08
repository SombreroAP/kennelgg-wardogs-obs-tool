# Kennel.gg WARDOGS OBS Tools

One OBS plugin (Windows, OBS 30+) for streaming **WARDOGS**, plus an optional companion app:

- **POV swap.** Downed? Your stream shows a squad mate's POV (video and game audio) until you are
  back up. Your mic is never touched. Detection reads the damage log on your own screen; the
  friend's feed is watched for "REVIVING" so the switch back is instant.
- **Clips.** Save OBS's replay buffer on demand and name the file with tags: from a hotkey, the
  dock, automatically when you get downed, or from **ClipHound**, the companion app that OCRs the
  kill feed and asks for a clip on a notable kill.
- **Squad-mate feeds.** Twitch, VDO.Ninja (WebRTC), Discord Go Live, NDI or any OBS source. The
  plugin creates and places the OBS sources for you.
- **The look.** Optional name tag, camcorder frame, film grain and vignette over the friend's feed.

## Install

Close OBS, run `kennel-wardogs-<version>-windows-x64-installer.exe`, start OBS. The installer puts
the plugin in `C:\ProgramData\obs-studio\plugins\kennel-wardogs` and, if you keep the ClipHound
component ticked, the app in `C:\ProgramData\Kennel WARDOGS\ClipHound` (self-contained, Tesseract
included, no Python install). The plugin finds it there and starts it with OBS. Start-menu
shortcuts: **ClipHound** and **ClipHound setup** (your in-game name, Twitch login). Settings open by
themselves on first run; later they are under **Tools → Kennel.gg WARDOGS OBS Tools...** and the
**Kennel WARDOGS** dock is under View → Docks. The zip has the same files for a manual install
(copy its `kennel-wardogs` folder into `C:\ProgramData\obs-studio\plugins\`).

## First run

1. **Switch** tab: pick the source that shows WARDOGS or press **Create Game Capture**. **Add...**
   squad mates. Desktop Audio is ticked in the mute list automatically; the mic is labelled.
2. Get downed once with the **Detect** tab open: the bar goes red (~0.9) and the stream cuts to
   the friend. Nothing to calibrate; capture your own template only if it never locks on.
3. **Clips** tab: the replay buffer is started for you. Set the file-name template and, if you use
   ClipHound, its path so OBS starts it.
4. **Look** tab, optional.

## Squad-mate feeds

| | Latency | Friend's setup |
|---|---|---|
| **Twitch stream** | ~2 s with low-latency mode | nothing, they just stream (their mic is in it) |
| **VDO.Ninja** (WebRTC) | ~0.3 s | opens one link in Chrome/Edge, shares the game window with system audio, no mic |
| **Discord Go Live** | ~0.5-1 s | Go Live in the call; you pop their stream out and pick the window; 720p without Nitro |
| **NDI** | ~1 frame | OBS + DistroAV or NDI Screen Capture on the LAN, or over a VPN such as Tailscale |
| **OBS source** | depends | anything already in OBS (capture card, second PC) |

"Keep the friend feed warm" leaves the friend's source enabled but transparent (a colour filter
named `Kennel hide`) and muted between switches, so the switch is instant.

## Clips and ClipHound

The plugin saves OBS's replay buffer and renames the file with `{date} {time} {title} {tags}
{source}`; every clip is logged to `clips.csv` in the plugin's config folder. Sources of clips:

- hotkey **Kennel WARDOGS: save a clip now**, or the dock's **Clip now**;
- **clip on downed** (Clips tab);
- **ClipHound** over the local bridge.

ClipHound (Python, in `app/`, built into `ClipHound.exe` by CI) OCRs the kill feed. It connects to the plugin at
`ws://127.0.0.1:47820`, subscribes to native-resolution crops of the game source, and sends
`{"type":"clip","title":...,"tags":[...]}` on a notable row. The plugin replies with the saved
path and also sends POV events (`downed`, `reviving`, `up`) so those can be tagged. The app can
force `pov` if it ever knows better, but downed detection lives in the plugin: no OCR needed,
no extra latency, and the swap works with the app closed.

### Bridge protocol (v1)

Text frames are JSON with a `type`. Binary frames are crops: 16-byte header `KWF1`, uint16 width,
uint16 height, uint64 timestamp ms (little endian), then JPEG.

| From | Message |
|---|---|
| plugin | `hello {plugin, version, protocol}` on connect; `config {gameSource, povState}` |
| app | `subscribe {frames:true, fps:4, roi:[x,y,w,h], width:0}` (fractions of the game source; width 0 = native) |
| app | `clip {id, title, tags:[...], source}` → plugin `clip_result {id, ok, error}` then `clip_saved {path, title, tags}` |
| app | `status {text}` shown in the dock |
| plugin | `pov {state: downed|reviving|up, friend}` |
| app | `pov {force: downed|up}` |

## CPU

The plugin's downed search is template matching on an 800 px frame: near zero once locked on, and
while you are alive it runs on every third poll, about 1-2 % of one core. ClipHound's OCR is the
heavier part; see its README for the numbers and the knobs (frame rate, only OCR changed rows).

## Build

Official [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate). GitHub Actions
builds the plugin, then `app/build_exe.ps1` (PyInstaller + choco Tesseract) and the Inno Setup
installer with both components, on every push: `kennel-wardogs-<version>-windows-x64.zip`
(plugin only) and `kennel-wardogs-<version>-windows-x64-installer.exe` (plugin + app).
