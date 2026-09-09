# Changelog

All notable changes to Kennel.gg WARDOGS OBS Tools. Release notes on GitHub are taken from here.

## 0.2.18
- Switching back is as fast as switching away. The cause was the "REVIVING" search on the friend's feed running in full on every poll while the friend was on screen, stretching each poll; it now runs in full every 5th poll with a cheap check in between. Confirmation is 2 polls each way and the minimum time on the friend is 0.5 s.

## 0.2.17
- Switch-back also reacts to the score falling away from its steady level; 2 polls, 0.5 s floor. Existing configs migrated.

## 0.2.16
- Squad-mate dialog shows only the fields for the chosen kind (Twitch channel, VDO.Ninja stream ID + quality + link, OBS source, Discord window, NDI source).
- Discord always saves: "Any Discord window" matches by executable and follows the pop-out when it appears; errors show in red.
- VDO.Ninja quality per squad mate: resolution (720/1080/1440), frame rate, bitrate ceiling, codec. Default 1080p60, 12000 kbps, H.264. The friend's link and the plugin's viewer link follow the settings.

## 0.2.15
- POV swap reacts in ~0.2-0.3 s instead of ~1 s: the plugin remembers where the damage log was last found and checks that spot on every poll (10 per second) at almost no CPU cost; the full search still runs only every 600 ms while you are alive. Confirmation is now 2 polls down / 4 polls up. Existing configs are migrated.
- `CHANGELOG.md` added.

## 0.2.14
- VDO.Ninja links now ask for 1080p60 H.264 with an adjustable bitrate ceiling (Switch → Extras). It is a ceiling: WebRTC climbs to it on a LAN or fibre and settles lower on a weak link.
- CI builds Windows only (Actions minutes).

## 0.2.13
- Aitum Backtrack is detected. If OBS's replay buffer is disabled and Backtrack hotkeys are set, clips use Backtrack only and the dock says "Backtrack ready" with its folder instead of nagging about the replay buffer.

## 0.2.12
- Kill feed: a row pushed down a slot by a new kill is followed instead of being counted as a new row, and a row that vanishes is still decided with the reads it had. Every kill in a multi-kill now shows and counts. 5 frames per second, 7 reads to decide.

## 0.2.11
- Discord Go Live and NDI are back in the "Comes in as" list when adding a squad mate (a bad patch had left the list at three entries). Clearer Discord window picker with a hint when no pop-out is open.

## 0.2.10
- ClipHound heals a corrupt `config.yaml` (sets it aside, starts from defaults, the plugin pushes your settings back) and writes its config atomically.
- Dock: Start / Stop ClipHound button with real state (connected, starting, crashed) and a Save clip split button with tagged saves.

## 0.2.9
- "Clip every kill I get" option; multi-kill window 30 s by default and editable in OBS.
- Same kill decided twice (with and without the weapon icon) now counts once.
- Backtrack clips are renamed with the same title and tags as replay-buffer clips.
- Clear warning when OBS's replay buffer cannot start; one ClipHound instance at a time.

## 0.2.8
- ClipHound runs windowless; no console setup anywhere. All of its settings live in OBS.
- Installer can download and run the official NDI 6 Runtime (Vizrt) and DistroAV installers.

## 0.2.7
- Twitch login from inside OBS (device code flow with the Kennel app id).
- Dock shows an event feed (kills, triggers, downed / back up). Backtrack "Save" hotkeys listed first. Logs tab in Settings. One settings window at a time.

## 0.2.6
- ClipHound tab in Settings: in-game name, clip folder, library index, Twitch on/off, channel to clip.
- ClipHound closes when OBS closes.

## 0.2.4
- Readable helper text in dark themes; Settings and wizard are resizable; Logs button on the dock; ClipHound writes `cliphound.log`; look overlay always hidden on the way back; ClipHound launch fixed; Aitum Backtrack / any-hotkey clip triggers.

## 0.2.1
- Fixed a crash when opening Settings from the dock.

## 0.2.0
- First-run setup wizard; DistroAV as an installer option; ClipHound bundled in the installer; quick start.
