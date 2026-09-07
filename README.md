# POVBridge for OBS

When you are **downed in WARDOGS** your screen goes dark except for the damage log. POVBridge is an
OBS plugin (Windows, OBS 30+) that notices that and, for as long as it is on screen, shows a
**squad mate's POV** (video and game audio) on your stream instead of yours. Your microphone keeps
going. The moment you are revived it switches back.

## What you get in OBS

- A **POVBridge dock** (View → Docks): state, active squad mate, "Show friend's POV" / "Back to me",
  Pause, Settings.
- **Tools → POVBridge Settings**: Switch / Look / Detect / Help tabs.
- Two hotkeys under **Settings → Hotkeys**: toggle friend / me, capture template.

## Squad mate feeds

| | Latency | Friend's setup |
|---|---|---|
| **Twitch stream** | ~2 s with low-latency mode | nothing, they just stream (their mic is in it) |
| **VDO.Ninja** (WebRTC) | ~0.3 s | opens one link in Chrome/Edge, shares the game window with system audio, no mic |
| **OBS source** (NDI) | ~1 frame | OBS + DistroAV or NDI Screen Capture on the LAN, or over a VPN such as Tailscale |

Twitch and VDO.Ninja play through one browser source named `POVBridge web` that the plugin creates
in your scene, sized to the canvas, audio routed through OBS so it swaps with the video. Keep
several squad mates on the list and pick who is active from the dock.

## Setup

1. Close OBS, run `povbridge-<version>-windows-x64-installer.exe`, start OBS. (The zip is the same
   files for manual installs: copy its `povbridge` folder into `C:\ProgramData\obs-studio\plugins\`.)
2. **Tools → POVBridge Settings → Switch**: pick your game source and the scene; **Add...** squad
   mates; tick the game-audio inputs to mute (Desktop Audio, or the game / capture-card source if
   that carries the audio). The mic is labelled - leave it unticked.
3. **Detect** tab: nothing to set up. Get downed once and watch the bar go red (~0.9). If it never
   locks on, drag the dotted box tightly around "B VIEW DAMAGE LOG" while downed and press Capture.
4. **Look** tab (optional): name tag, camcorder frame, film grain, vignette. Preview in OBS.
5. Test with the dock buttons or the hotkey.

## How the detection works

WARDOGS shows the damage log ("B  VIEW DAMAGE LOG" with the body silhouette) the whole time you
are downed, with or without the map open, and hides it when you are revived. The plugin renders
your game source to 800 px wide five times a second and looks for that header anywhere on the
right of the frame, at any HUD size, with normalised cross-correlation against a template cut from
a real frame. Measured on real frames: the true header scores 0.97+, an alive frame stays under
0.76. Three matching polls switch to the friend; five non-matching polls (and at least 2 s) switch
back.

**Timing the switch back.** While the friend is on screen the plugin also watches *their* feed for
the word **REVIVING** and the progress ring under it. When it sees it, the switch back fires the
instant the damage log leaves your own game, with no confirmation delay and polls at 10 per
second. Your own feed is the trigger because it has no latency; the friend's feed only arms it.

"Keep the friend feed warm" leaves the friend's source enabled but transparent (a colour filter
named `POVBridge hide`) and muted between switches, so the NDI receiver, WebRTC session or Twitch
player keeps running and the switch is instant.

## Files

Config, hotkeys and a custom template live in OBS's plugin config folder
(`%APPDATA%\obs-studio\plugin_config\povbridge\`).

## Build

Based on the official [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate).
Windows builds come from GitHub Actions (`.github/workflows/build-project.yaml`): every push builds
`povbridge-<version>-windows-x64.zip` and, via `installer/povbridge.iss` (Inno Setup),
`povbridge-<version>-windows-x64-installer.exe`, both as workflow artifacts. Locally on Windows:
`cmake --preset windows-x64 && cmake --build --preset windows-x64`.
