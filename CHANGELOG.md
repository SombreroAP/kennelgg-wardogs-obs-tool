# Changelog

All notable changes to Kennel.gg WARDOGS OBS Tools. Release notes on GitHub are taken from here.

## 0.4.1
- **NDI share no longer breaks other plugins' canvases.** It was tapped onto OBS's main video mix, which turned Aitum's vertical canvas black and stopped recordings that used it. The share now renders from a view of its own on the program output, so it only ever carries the main canvas and leaves every other canvas alone. It also starts a few seconds after OBS has finished loading, so other plugins set up first.

## 0.4.0
- **Dual POV** (new tab). Two of you in a tank or a Havoc: your own POV stays on screen and your crew mate's feed sits in a small window over it, placed where the game draws nothing. Pick the crew mate, pick the vehicle and the seat you are in - tank driver, tank gunner, Havoc pilot, Havoc gunner in the CAM view - and the window goes where that seat's HUD leaves room (top-left between the team chat and the kill feed; inside the picture frame for the CAM view). Or Custom: drag the box on the live picture, or type left, top and width. Opacity slider. Picture only, no sound. A **Dual POV** button in the dock and a hotkey ("dual POV window on / off") turn it on and off; it comes back on with OBS if it was on. When you go down the window steps aside for the full-screen swap and returns after.

## 0.3.9
- **Closest cannot be turned on without ClipHound running.** The dialog offers to start ClipHound; the tick box stays off until it is connected.
- **Settings window no longer opens squashed.** It came up with every control squeezed to a few pixels until you resized it by hand: the first paint used the geometry from before the window was sized. It now opens at a proper size and lays itself out again the instant it appears. Same for the setup wizard and the Logs window. Fonts sized in pixels by the OBS theme are handled too.

## 0.3.8
- **Only squad mates with a feed count, and the nearest of those wins.** Players near you who are not set up as a feed are never matched, so with five in the squad and two streaming the POV goes to whichever streamer is nearest, however far. The "... m or closer" range rule now only holds the feed on a squad mate who is still in the NEARBY list; if they have left it, any streaming squad mate in the list takes over. If no streaming squad mate is in the list at all, the squad mate already selected stays.

## 0.3.7
- **Closest says so when ClipHound is not running.** Ticking Closest in the dock or in Settings without ClipHound running asks whether to start it (it reads the NEARBY list; nothing works without it). The dock's Nearby line turns red and says the same while it is off, and the Settings tick box is labelled "needs ClipHound running".

## 0.3.6
- **Back up means your own POV, full stop.** On the way back every squad mate's video and audio is hidden in every scene, whatever state it was in. Warm and preloaded feeds now sit invisible rather than transparent (browser sources keep running while hidden), so nothing can be left showing.
- The dock's Nearby line reads **N/A while you are up**.
- The dock's **Closest** tick box and the Settings one are the same switch and stay in step; ticking it in the dock does everything ticking it in Settings does.
- **Swap over only for someone ... m or closer** (Switch tab): once a squad mate is on screen, the feed only moves to a nearer one who is within this distance, 50 m by default; 0 means any distance. The pick when you go down is not limited.

## 0.3.5
- **The closest squad mate is actually the one shown.** While you are going down, every NEARBY reading now picks the nearest squad mate outright. Before, a "15 m closer" rule meant to stop flapping was also blocking the first pick: someone at 4 m was not "15 m closer" than the one at 12 m, so the feed stayed on whoever was already selected. That rule is gone; once a squad mate is on screen, only the **Wait between swaps** slider holds a swap back, and the log says when it does and for how long.
- **The NEARBY list is cleared the moment you are back up.** Who was near you while you were down is not relevant once you are alive, so the dock shows nothing until the next time.

## 0.3.4
- **A distance that cannot be read no longer throws the squad mate away.** The last distance actually read for them is used for up to 12 s, so one bad frame in a burst does not turn them into a question mark and does not change who is closest.
- **Fewer unreadable distances.** The little chip is now accepted when it reads as a bare number (the reader only ever gets digits and an m there), a bright blob that is not a chip is rejected instead of being read as one, and the whole row is read as a last resort when the chip gives nothing.
- **Squad-mate feeds can be preloaded** (Switch tab → Extras, off by default): every squad mate's feed sits in the scene loaded, playing, invisible and silent, so a Twitch feed is not starting up when the swap happens. Each preloaded feed uses its own bandwidth, which is why it is off unless you ask for it.
- **Your own game sound is no longer muted by default.** The squad mate's feed comes in silent instead, so you keep hearing your own game while your stream shows their POV. "Play the squad mate's game sound" turns theirs on, and the list next to it is where you tick anything of yours to mute. Existing setups had your desktop audio ticked automatically by an older version; that is cleared once on upgrade.
- The log names the squad mate, their in-game name and the whole reading it chose from, so a wrong pick is obvious.

## 0.3.3
- **Wait between swaps** slider (Switch tab, "Show whoever is closest"): how long the feed stays on one squad mate before it may swap to a closer one while you are down. 1 to 10 seconds, 4 by default. Low values follow whoever is nearest as they run to you, high values pick one and leave it. The swap the moment you go down never waits, whatever this is set to.

## 0.3.2
- **Test read** button on the Detect tab, next to the NEARBY box: it shows the box as the plugin sees it and exactly what ClipHound reads there - the rows it found, the names, the metres, and whether any of it matched a squad mate. It also says what to change when nothing matched. ClipHound saves the picture it looked at as `nearby_debug.png` in its own folder.
- **Several reasons the NEARBY list read nobody, fixed.** When the distance chip was not found the row was cut short before the metres, so the fallback could never find them; the whole row is read now. The chip is found as the rightmost solid box with much looser limits. A second way of picking out the text is tried when the scene behind the panel is bright, where the first one turns the whole crop into one blob. Names are matched more loosely, on letters and digits alone. A squad mate whose name is read but whose distance is not still counts as nearby.
- **The NEARBY list is only read while you are down.** Nothing is read between fights: the plugin asks the moment the damage log appears and keeps asking until you are back up, so it costs nothing while you play.

## 0.3.1
- **The kill feed keeps up now.** Each row used to cost four separate tesseract runs per frame, so with three rows on screen ClipHound managed under two frames a second and a kill took four or five seconds to come out. All the rows in a frame are now read in one run, and a row is decided 0.75 s after it is first read whatever the frame rate. A kill reaches the dock about a second after it happens. (The clip file still lands about four seconds later on purpose, so the moment is inside it: Clips tab.)
- **Closest actually drives the feed.** The squad-mate box in the dock now follows the closest one by itself while the new **Closest** tick box next to it is on, and only that box is used when it is off. Nothing changes mid-swap unless someone is clearly closer (15 m) and not more than once every four seconds; the moment you go down that guard is dropped so you always get the nearest one.
- **The NEARBY reading stops flickering.** A single bad frame no longer wipes the list: ClipHound only reports an empty list after three misses in a row, the plugin keeps the last good reading, and the dock says what it is waiting for instead of "nothing read yet".
- The blue **NEARBY box moved to the Detect tab**, next to the damage-log picture, since it drives the POV switch and not the clipping. The kill-feed box stays on the ClipHound tab.
- The log says why a nearby squad mate was not switched to: not in your list, or their feed source is missing.

## 0.3.0
- **Show whoever is closest.** The game's NEARBY list (bottom right of the HUD) is read while you play, so when you go down the POV that comes up is the squad mate who can actually reach you. Switch tab: turn it on, choose whether to keep following the nearest one while you are down, and how much closer someone must be (default 15 m) before the feed swaps over mid-swap. Each squad mate needs the name the game shows for them: Switch → Edit... → In-game name. Needs ClipHound running; without a reading the squad mate you picked is used exactly as before.
- **The kill-feed and NEARBY areas are picked by dragging on the live picture** (ClipHound tab): choose which of the two the drag sets, drag a box, done. It goes to ClipHound straight away, no restart. This is the picker that 0.2.24 announced but did not actually ship.
- **The kill feed is read twice as fast.** The default rate is now 10 times a second (was 5) and is on the ClipHound tab. A kill is decided after about 0.75 s of reading whatever the rate, so a kill now becomes a clip in about a second instead of two.
- **A kill you cover up still clips.** A feed row that disappears early - you opened the inventory or the map, or a multi-kill pushed it off - is decided on what was read, and a row with your own name in it is decided on two reads.
- Dock shows the NEARBY reading while the feature is on.

## 0.2.24
- Kill-feed area sent to ClipHound live (the picker for it arrived in 0.3.0).

## 0.2.23
- Switch delays, on the Detect tab: the squad mate is shown 2 s after you go down by default (a revive inside that window never switches), and you come back instantly on revive (0 ms). Both editable.

## 0.2.22
- Clip file names put what happened first, then the date and time: `{title}_{tags}_{date}_{time}`. Existing configs on the old default are migrated; the template stays editable on the Clips tab.

## 0.2.21
- Settings, setup wizard and Logs windows always open fully on screen (their title bar could sit above the screen edge and be impossible to grab).

## 0.2.20
- Backtrack file naming: the output folder is found from any Backtrack/Aitum source or filter setting that looks like a path, subfolders are scanned, the watch lasts 90 s, and the log says which folders are watched (or that none is known - set it under Settings → Clips).

## 0.2.19
- Dock: the damage-log match bar is replaced by "Downed state detector: Alive / Downed".

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
