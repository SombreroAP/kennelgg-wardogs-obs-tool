# Kennel.gg Wardogs OBS Tool

One OBS plugin (Windows, OBS 30+) for streaming **WARDOGS**, plus ClipHound, the companion app that
watches the kill feed. Made by [The Kennel](https://kennel.gg), the WARDOGS community: guides, Bootcamp,
loadout builder, leaderboard and [Discord](https://discord.gg/nDyJ7SSM8q). Free, and one of the ways
people find us.

## What it does

- **POV swap.** Downed? Your stream shows a squad mate's POV until you are back up, then cuts back the
  instant you are revived. Detection reads the damage log on your own screen, so it has no latency and
  works with the companion app closed. Your mic is never touched.
- **Squad feeds from Discord.** Your squad plays in a Discord voice channel and Go Lives. You pop their
  streams out into their own windows, press **Add pop-outs**, and each one becomes a squad mate named
  after them, captured by that exact window, tucked to the edge of your screen where Discord keeps
  drawing it. The Kennel Ops bot in the Kennel.gg Discord tells the plugin which channel you are in
  and who is live, so only people with a picture are ever shown, and a slot goes away when its person
  stops.
- **The closest one.** With more than one feed, the game's NEARBY list decides which POV comes up: the
  squad mate who is actually next to you. If nobody near you is live, another live squad mate is shown.
- **Sound that follows the picture** for Twitch, Kick, YouTube and VDO.Ninja squad mates: theirs plays
  and your game's is muted while they are up. Discord's sound is not handled (see Sound below).
- **Dual POV.** In a tank or a Havoc with a crew mate: your own POV stays up and theirs sits in a small
  framed window with their name, placed where that seat's HUD leaves room (presets for tank driver,
  tank gunner, Havoc pilot and Havoc gunner), or wherever you drag it. Forced by hand from its own row
  on the dock, or opened by itself when ClipHound sees you get into a vehicle.
- **Clips.** Save OBS's replay buffer on demand and name the file with tags: from a hotkey, the dock,
  automatically when you get downed, or from **ClipHound** on a notable kill. Clips within 45 s of each
  other are numbered as a run (1 of 3, 2 of 3, 3 of 3). Works with Aitum Backtrack instead of the
  replay buffer.
- **ClipHound.** OCRs the kill feed with the weapon icons matched at any resolution, reads the NEARBY
  list while you are down, reads the vehicle seat, and can post the clip to Twitch.
- **The look.** Optional name tag, camcorder frame, film grain and vignette over the squad mate's feed.
- **No black screen.** Feeds are kept warm, so the swap is instant.

Other squad-mate feeds still work when Discord is not an option: Twitch, Kick, YouTube live, VDO.Ninja
(WebRTC) or any source already in OBS.

## Quick start

1. Close OBS. Run `kennelgg-<version>-windows-x64-installer.exe`. Windows will warn that the file is
   unsigned: *More info, Run anyway*. Keep **ClipHound** ticked for kill-feed clips.
2. Start OBS. **Setup** opens: which source shows WARDOGS (or one click to create a Game Capture), then
   the Discord page. Your Discord username is filled in from the Discord app when it is running.
3. Join the Kennel.gg Discord if you are not in it yet (the dock's **Discord** button). Squad
   automation runs through the Kennel Ops bot there and is for members.
4. Play. Sit in voice with your squad, pop out the streams of the people whose POV you might show,
   press **Add pop-outs**. **Get downed once**: the dock turns red and your stream shows the squad
   mate; it comes back the instant you are revived.

Do not minimise a pop-out. A minimised window stops drawing and its feed freezes. Tucked away is fine,
and **Show pop-outs** brings them back when you want their own volume slider.

Settings live under Tools, Kennel.gg Wardogs OBS Tool; the dock under View, Docks.

## Install

Close OBS, run the installer, start OBS. The plugin goes to `C:\ProgramData\obs-studio\plugins\kennelgg`
and, with the ClipHound component ticked, the app to `C:\ProgramData\Kennel.gg\ClipHound`
(self-contained, Tesseract included, no Python install). The plugin finds it there and starts it with
OBS; all of its settings, including the Twitch login, are on the ClipHound tab. The zip has the same
files for a manual install (copy its `kennelgg` folder into `C:\ProgramData\obs-studio\plugins\`).

## The dock

Top to bottom: the state pill (watching, showing, off), **Add pop-outs** and **Show pop-outs**, one
button per squad mate who is live (press it to force their feed up, again to come back), the squad-mate
box with **Auto switch** and **Closest**, **Show friend's POV** and **Back to me**, the
Dual POV row with its own person and **Force Dual POV**, then Squad, Setup, Settings, Logs and
**Discord**, ClipHound start/stop and Save clip, and the last events.

When the plugin cannot see the voice roster, the live buttons' place shows greyed out with a link to join
the Kennel.gg Discord or to detect your username.

## Squad-mate feeds

| | Latency | Friend's setup |
|---|---|---|
| **Discord Go Live** | ~0.5-1 s | Go Live in the call; you pop their stream out and press Add pop-outs; 720p without Nitro |
| **Twitch / Kick stream** | ~2 s with low-latency mode | nothing, they just stream (their mic is in it) |
| **YouTube live** | ~5 s+ | nothing, the channel has to allow embedding |
| **VDO.Ninja** (WebRTC) | ~0.3 s | opens one link in Chrome/Edge, shares the game window with system audio |
| **OBS source** | depends | anything already in OBS (capture card, second PC) |

"Keep the friend feed warm" leaves the friend's source enabled but transparent (a colour filter named
`Kennel hide`) and muted between switches, so the switch is instant.

## Sound

Two rules, by squad-mate kind.

For **Twitch, Kick, YouTube and VDO.Ninja** squad mates, their sound plays while they are on screen and
your own game's sound is muted: the game source when it carries audio, otherwise Desktop Audio,
chosen once and listed on the Switch tab where it can be changed. Your microphone is never touched,
and everything is put back the moment you are revived. The Switch tab tick box turns their sound off.

For **Discord** squad mates the plugin does not touch sound at all. Discord hands OBS one mix for the
whole call, every stream you watch plus everyone's voice, and nothing outside Discord can pick one
stream out of it. So a Discord feed comes with no audio capture, nothing of yours is muted while it is
shown, and the call's sound reaches your stream through whatever already carries Discord (usually
Desktop Audio). A single stream's level is the slider on its pop-out in Discord.

## Clips and ClipHound

The plugin saves OBS's replay buffer and renames the file with `{date} {time} {title} {tags} {source}`;
every clip is logged to `clips.csv` in the plugin's config folder. Sources of clips:

- hotkey **Kennel.gg Wardogs: save a clip now**, or the dock's **Clip now**;
- **clip on downed** (Clips tab);
- **ClipHound** over the local bridge.

Clips made within the rolling-highlights window (45 s by default) are named as one run, "[1 of 3]" and
so on, and Twitch clips get "part 1" in the title; **Number past clips** on the Clips tab does the same
for clips already on disk.

**Aitum Backtrack** (or any other plugin with a hotkey): Settings, Clips, tick the OBS hotkeys to fire on
every clip. Works alongside the replay buffer or instead of it.

ClipHound (Python, in `app/`, built into `ClipHound.exe` by CI) OCRs the kill feed. It connects to the
plugin at `ws://127.0.0.1:47820`, subscribes to native-resolution crops of the game source, and sends
`{"type":"clip","title":...,"tags":[...]}` on a notable row. The plugin replies with the saved path and
also sends POV events (`downed`, `reviving`, `up`) so those can be tagged. Downed detection lives in the
plugin: no OCR needed, no extra latency, and the swap works with the app closed.

### Bridge protocol (v1)

Text frames are JSON with a `type`. Binary frames are crops: 16-byte header `KWF1`, uint16 width,
uint16 height, uint64 timestamp ms (little endian), then JPEG.

| From | Message |
|---|---|
| plugin | `hello {plugin, version, protocol}` on connect; `config {gameSource, povState}` |
| app | `subscribe {frames:true, fps:4, roi:[x,y,w,h], width:0}` (fractions of the game source; width 0 = native) |
| app | `clip {id, title, tags:[...], source}`, then plugin `clip_result {id, ok, error}` and `clip_saved {path, title, tags}` |
| app | `status {text}` shown in the dock |
| plugin | `pov {state: downed|reviving|up, friend}` |
| app | `pov {force: downed|up}` |
| plugin | `app_config {set: {..., fps, roi:[x,y,w,h], series_window, nearby:{enabled, roi, names:[...]}}}`, then app `app_config {values}` |
| plugin | `nearby_now`: read the NEARBY panel now (sent the moment the damage log appears) |
| app | `nearby {list:[{name, dist, match}]}`: who the game says is near you, nearest first |
| app | `vehicle {seat}`: the seat read off the vehicle keybind list, for Dual POV |

## The Discord roster

Discord gives a third-party program no way to ask who is in a voice call, so the Kennel Ops bot in the
Kennel.gg Discord publishes it: who is in which voice channel, their usernames, and Discord's own
go-live flag for each. The plugin polls that every six seconds, finds your own username in it, follows
whichever channel you are sitting in, and shows only people who are live. Your Discord username is read
from the Discord app on your PC (its local pipe names the logged-in user on a plain handshake; nothing
more is asked of it), or typed in Setup.

Squad automation is for members of the Kennel.gg Discord: the bot publishes a hashed member list and
the plugin checks your username against it. Pop-outs and every manual control work either way. The bot
can also be added to another server you play on; that server then appears in the Squad panel.

## Settings at a glance

| Tab | What is there |
|---|---|
| Switch | game source and scene; squad mates and their in-game names; the vertical scene for a portrait canvas; **Show whoever is closest** with Wait between swaps and the 50 m range; the sound of the squad mate on screen and what of yours to mute; Squad from Discord; Extras: keep warm, preload every feed |
| Dual POV | on/off, crew mate, vehicle and seat preset or a dragged box, opacity, the frame and name size, auto mode |
| Look | name tag, plate, camcorder frame, grain, vignette; preview |
| Detect | live picture with the damage-log match, the header box and the blue **NEARBY box**; **Test read**; thresholds and delays |
| Clips | replay buffer, file-name template, clip folder, Backtrack hotkeys and folder, clip on downed, rolling highlights window, Number past clips, ClipHound path and start/close with OBS |
| ClipHound | your kill-feed name, clip library, clip every kill, multi-kill window, the kill-feed box and the reading rate, Twitch login |
| Logs | the plugin's log and ClipHound's, Copy all |
| Help | the version you are running, the check for a newer build, and what everything does |

The Squad panel (dock, **Squad**) is the mid-broadcast view: Add pop-outs, Make active, Show in Dual POV,
In-game name, Remove, where the pop-outs live (tucked to the edge, or parked on another monitor), the
Discord audio level on stream, and which Discord server counts.

## Show whoever is closest

WARDOGS lists the squad mates near you in the bottom-right corner of the HUD, with a distance each, and
that list stays up while you are down. From the moment the damage log appears until you are back up,
ClipHound reads it and tells the plugin, which makes the nearest live squad mate the active one just
before your stream cuts to them. Nothing is read while you are up.

Tick **Closest** in the dock, drag the blue box round the NEARBY list on the Detect tab, and give each
squad mate their in-game name (asked for when a pop-out is added). Only names you have configured are
ever matched. While you are going down every reading picks the nearest one outright; once a squad mate
is on screen, a swap to a nearer one waits for **Wait between swaps** (4 s by default) and only happens
for someone within 50 m while the one on screen is still in the list, so nothing flaps. Someone the
roster says is not streaming is never picked; if nobody close is live, any live squad mate is shown.
ClipHound must be running: ticking Closest without it asks to start it.

Coming back up is absolute: every squad mate's video and audio is hidden in every scene, so your own
POV is what the stream shows the moment you are revived.

## CPU

The plugin's downed search is template matching on an 800 px frame: near zero once locked on, and
while you are alive it runs on every third poll, about 1-2 % of one core. ClipHound's OCR is the
heavier part: one tesseract run per frame for the whole feed. At the default 10 frames a second that
is roughly 10-20 % of one core while kills are being read and a few per cent between them. The NEARBY
panel is only read while you are down.

## Build

Official [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate). GitHub Actions builds
the plugin, then `app/build_exe.ps1` (PyInstaller + choco Tesseract, and the build fails if Tesseract
is missing) and the Inno Setup installer with both components, on every push:
`kennelgg-<version>-windows-x64.zip` (plugin only) and `kennelgg-<version>-windows-x64-installer.exe`
(plugin + app).
