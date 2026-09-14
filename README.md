# Kennel.gg Wardogs OBS Tool

One OBS plugin (Windows, OBS 30+) for streaming **WARDOGS**, plus an optional companion app.
Made by [The Kennel](https://kennel.gg) — the WARDOGS community: guides, Bootcamp, loadout builder,
leaderboard and [Discord](https://discord.gg/vHqDR9HHcM). The tool is free, and it is one of the ways
people find us.


- **POV swap.** Downed? Your stream shows a squad mate's POV until you are back up. Their feed is
  silent by default, so you keep hearing your own game; one tick box plays theirs instead. Your mic
  is never touched. Detection reads the damage log on your own screen, with its local brightness taken out so a bright sky behind the see-through panel does not change the score; the
  friend's feed is watched for "REVIVING" so the switch back is instant.
- **Clips.** Save OBS's replay buffer on demand and name the file with tags: from a hotkey, the
  dock, automatically when you get downed, or from **ClipHound**, the companion app that OCRs the
  kill feed and asks for a clip on a notable kill.
- **Squad-mate feeds.** Twitch, VDO.Ninja (WebRTC), Discord Go Live, NDI or any OBS source. The
  plugin creates and places the OBS sources for you.
- **The closest one.** With more than one feed, the game's NEARBY list decides which POV comes up:
  the squad mate who is actually next to you, not the one you picked before the match.
- **Dual POV.** In a tank or a Havoc with a crew mate: your own POV stays up and theirs sits in a
  small window placed where that seat's HUD leaves room, with presets for tank driver, tank gunner,
  Havoc pilot and Havoc gunner (CAM view), or drag it anywhere. With ClipHound running it can turn
  itself on when you get in and off when you get out, reading the seat off the vehicle keybind list.
- **The look.** Optional name tag, camcorder frame, film grain and vignette over the friend's feed.
- **No black screen.** Feeds are kept warm, and optionally every squad mate's feed is preloaded and
  playing behind the scenes so a Twitch stream is not starting up at the moment you go down.

## Quick start (testers)

1. Close OBS. Run `kennelgg-<version>-windows-x64-installer.exe`. Windows will warn that the
   file is unsigned: *More info → Run anyway*. Keep **ClipHound** ticked if you want kill-feed clips;
   tick **DistroAV** if you will play at a LAN with other people running this.
2. Start OBS. A short **setup wizard** opens: your name, which source shows WARDOGS (or one click to
   create a Game Capture), a squad mate (type a Twitch channel, or wait for LAN squad mates to appear),
   clips. Finish.
3. Play. **Get downed once**: the Kennel.gg Wardogs dock turns red and your stream shows the squad mate;
   it comes back the instant you are revived.

That is all. Settings live under Tools → Kennel.gg Wardogs OBS Tool..., the dock under View → Docks.

## Install

Close OBS, run `kennelgg-<version>-windows-x64-installer.exe`, start OBS. The installer puts
the plugin in `C:\ProgramData\obs-studio\plugins\kennelgg` and, if you keep the ClipHound
component ticked, the app in `C:\ProgramData\Kennel.gg\ClipHound` (self-contained, Tesseract
included, no Python install). The plugin finds it there and starts it with OBS. Start-menu
shortcut: **ClipHound** (it runs in the background with no window; the plugin starts it with OBS
and all of its settings, including the Twitch login, are on the ClipHound tab in Settings). Two
optional installer tasks download and run the official **NDI 6 Runtime** (Vizrt) and **DistroAV**
installers for LAN squad feeds; neither is bundled, both come from their publishers. An optional
**DistroAV** task downloads the official DistroAV installer (GPL-2, from its GitHub release) and runs
it; DistroAV then asks you to fetch the NDI Runtime from Vizrt, which nobody may redistribute. Settings open by
themselves on first run; later they are under **Tools → Kennel.gg Wardogs OBS Tool...** and the
**Kennel.gg Wardogs** dock is under View → Docks. The zip has the same files for a manual install
(copy its `kennelgg` folder into `C:\ProgramData\obs-studio\plugins\`).

## First run

1. **Switch** tab: pick the source that shows WARDOGS or press **Create Game Capture**. **Add...**
   squad mates, each with their **in-game name**. Nothing of yours is muted by default. The squad
   mate on screen is the one feed with sound (the dock's **POV sound** button turns that off); tick
   anything of yours to mute while they are up if you do not want both game sounds at once.
2. Get downed once with the **Detect** tab open: the bar goes red (~0.9) and the stream cuts to
   the friend. Nothing to calibrate; capture your own template only if it never locks on.
3. **Clips** tab: the replay buffer is started for you. Set the file-name template and, if you use
   ClipHound, its path so OBS starts it.
4. **Look** tab, optional.

## Squad on the LAN (automatic)

At a LAN party nothing needs typing. Every PC running this plugin announces itself on the network
(UDP 47821) and, with **DistroAV** installed, publishes its game feed over NDI as `<PC name> (Kennel
POV)` on audio track 6, with every microphone taken off that track so squad mates hear the game,
not you. The share is rendered from its own view of the program output, so it carries the main
canvas only and leaves other plugins' canvases (Aitum's vertical canvas, for one) alone. Squad mates found this way are added to your list by themselves as NDI feeds, and you to
theirs. Set **Your name** on the Switch tab; untick the options there to opt out.

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

- hotkey **Kennel.gg Wardogs: save a clip now**, or the dock's **Clip now**;
- **clip on downed** (Clips tab);
- **ClipHound** over the local bridge.

**Aitum Backtrack** (or any other plugin with a hotkey): Settings → Clips → tick the OBS hotkeys to
fire on every clip, e.g. Backtrack's *Save* for the source you want. Works alongside the replay
buffer or instead of it (untick "Save OBS's replay buffer").

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
| plugin | `app_config {set: {..., fps, roi:[x,y,w,h], nearby:{enabled, roi, names:[...]}}}` → app `app_config {values}` |
| plugin | `nearby_now` - read the NEARBY panel now (sent the moment the damage log appears) |
| app | `nearby {list:[{name, dist, match}]}` - who the game says is near you, nearest first |

## Settings at a glance

| Tab | What is there |
|---|---|
| Switch | game source and scene; squad mates (Twitch, Kick, YouTube live, VDO.Ninja, Discord or an OBS source; in-game name), the vertical scene for a portrait canvas; **Show whoever is closest** with Wait between swaps and the 50 m range; squad-mate sound and what of yours to mute; LAN discovery and NDI share; Extras: keep warm, preload every feed |
| Dual POV | on/off, crew mate, vehicle and seat preset or a dragged box, opacity |
| Look | name tag, plate, camcorder frame, grain, vignette; preview |
| Detect | live picture with the damage-log match, the header box and the blue **NEARBY box**; **Test read**; the match threshold and **Hold down to**, confirm frames, the 2 s delay before showing and the delay before coming back |
| Clips | replay buffer, file-name template, clip folder, Backtrack hotkeys and folder, clip on downed, ClipHound path and start/close with OBS |
| ClipHound | your kill-feed name, clip library, clip every kill, multi-kill window, the kill-feed box and the reading rate, Twitch login |
| Logs | the plugin's log and ClipHound's, Copy all |
| Help | the version you are running, the check for a newer build, and what everything does |

The dock has the state line, the squad-mate box with the **Closest** tick box, the Nearby line,
Show / Back / **POV sound**, Start ClipHound and Save clip. POV sound is the sound of whoever is on
screen: on (the default) their feed is the one with sound and the unmute moves with the picture;
off, every squad mate is silent. It has a hotkey under OBS Settings → Hotkeys.

## Show whoever is closest

WARDOGS lists the squad mates near you in the bottom-right corner of the HUD, with a distance each,
and that list stays up while you are down. From the moment the damage log appears until you are back
up, ClipHound reads it and tells the plugin, which makes the nearest squad mate the active one just
before your stream cuts to them - so the POV your viewers get is the one running towards you.
Nothing is read while you are up, so it costs nothing between fights.

Tick **Closest** in the dock (or Settings → Switch → **Show whoever is closest**), drag the blue box
round the NEARBY list on the Detect tab, and give each squad mate their **in-game name** in the Edit
dialog. The dock's squad-mate box then follows the closest one by itself; untick Closest to choose a
squad mate yourself. Only names you have configured are ever matched, so a stranger in the list
cannot move your feed. While you are going down every reading picks the nearest one outright; once a
squad mate is on screen, a swap to a nearer one waits for the **Wait between swaps** slider (1-10 s,
4 by default) and only happens for someone within **Swap over only for someone ... m or closer**
(50 m by default) while the one on screen is still in the list, so nothing flaps. Players who are not
set up as a feed are never matched: with five in the squad and two streaming, the nearer streamer is
shown however far they are, and if no streamer is in the list the selected squad mate stays. The list is cleared the moment you are back up and the dock
reads "N/A while you are up". ClipHound must be running: ticking Closest without it asks to start it.

Coming back up is absolute: every squad mate's video and audio is hidden in every scene, so your own
POV is what the stream shows the moment you are revived.

If it reads nobody, press **Test read** next to the box: it shows the crop the plugin is sending and
the rows, names and metres ClipHound got out of it, which says whether the box is in the wrong place,
the text is unreadable, or the in-game names do not match.

## CPU

The plugin's downed search is template matching on an 800 px frame: near zero once locked on, and
while you are alive it runs on every third poll, about 1-2 % of one core. ClipHound's OCR is the
heavier part, and it is one tesseract run per frame for the whole feed however many rows are on
screen (rows are only read until they are decided, 0.75 s after they appear). At the default 10
frames a second that is roughly 10-20 % of one core while kills are being read and a few per cent
between them. The NEARBY panel is only read while you are down, one run every 0.4 s, and
names are only re-read when they change. Drop the rate on the ClipHound tab if the CPU matters
more to you than the second it saves.

## Build

Official [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate). GitHub Actions
builds the plugin, then `app/build_exe.ps1` (PyInstaller + choco Tesseract) and the Inno Setup
installer with both components, on every push: `kennelgg-<version>-windows-x64.zip`
(plugin only) and `kennelgg-<version>-windows-x64-installer.exe` (plugin + app).
