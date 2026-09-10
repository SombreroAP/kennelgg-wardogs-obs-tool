# Changelog

All notable changes to Kennel.gg WARDOGS OBS Tools. Release notes on GitHub are taken from here.

## 0.5.4
- **NDI feeds are much steadier.** Three things, all on by default:
  - **What you send is now scaled.** NDI's picture is barely compressed, so sharing a full canvas at 60 is around 200 Mbit - more than a shared or half-duplex network carries steadily, which is what makes a squad mate's feed judder and drop. The share now goes out at **720p 30** by default, about a tenth of that and still plenty to revive from. **Share at** on the Switch tab raises it to 900p, 1080p or the full canvas. Your own stream and recording are untouched: the share is rendered from a view of its own.
  - **Frame sync on what you receive.** Squad mates' NDI feeds are now handed to OBS on OBS's clock instead of whenever the network delivers them, which is the setting that turns a juddering feed into a steady one. It is applied to feeds you already have, once, at start-up.
  - **Receive at** in Edit... per squad mate: full quality, or low bandwidth for a feed that still will not settle - the same thing as setting it by hand on the source, but it sticks.
- If it is still rough: NDI wants wired gigabit. Wi-Fi, powerline and a switch shared with a games console will all drop frames at these rates whatever the settings say.

## 0.5.3
- **A squad mate's Discord share shows their game, not their Discord window.** Their screen share arrives inside Discord's own window - flat grey down the sides, black letterboxing around the picture. The plugin now renders a frame of their feed, walks in from each edge while the whole row or column is still one flat colour, and crops the scene item to what is left. It runs each time their feed goes up, so it follows the window being resized, and never takes more than a third off any side. Turn it off per squad mate with **Borders** in Edit....
- **The POV tag has moved off the map.** It sat bottom-left, over the game's minimap and the score along the bottom. It now sits halfway up the left-hand side, clear of both, and there is an **at** box next to the name tag on the Look tab to put it middle, top left, top centre, bottom left or bottom right instead. Existing setups are moved once.

## 0.5.2
- **Your camera and your alerts stay on top.** A new **Always on top** box on the Switch tab: tick your face cam and your alert overlays and they are lifted back over the top every time the plugin shows a squad mate, brings up the Dual POV window, adds a source or puts the look overlay on - nothing of ours can cover them. The list is the stacking order, first is the topmost, and you can drag it around. Your camera and anything that looks like alerts are ticked for you the first time you open it.
- **A squad mate's Discord feed no longer flickers.** Saving settings re-applied the window-capture settings to a source that already had them, and Windows tears the capture down and starts it again each time. The plugin now writes settings only when something has actually changed. Probing for the window list is also cached for a few seconds - it briefly makes a second capture of the same window, which is the other half of the flicker.
- **Your own placement is left alone.** A squad mate's source was stretched back to the full canvas on every save, so if you had moved or resized it, it snapped back under you. It is only placed when it is first added; after that it is yours.

## 0.5.1
- **OBS no longer crashes when you scan for a squad mate's NDI feed.** To list what is on the network the plugin used to make a hidden NDI source, ask it for its list and throw it away - but DistroAV's finder holds on to whichever source asked and signals it from its own thread, so it was signalling a source that no longer existed and took OBS down with it. The plugin now asks the NDI runtime for the list itself, keeping one finder for the session, and never makes a source to do it. Nothing to set up.
- The NDI picker also lists senders already used elsewhere in your OBS, can be **typed into** for a mate whose PC is not on yet, and fills in on its own a second later as the network answers, instead of holding the window still while it looks.

## 0.5.0
- **A bright sky no longer reads as alive.** The damage-log panel is see-through, so what is behind it changes how the header looks: aim at the sky, drive through smoke or take a muzzle flash and the wording washes out for a moment. The score dipped, and with a single poll enough to end the swap, your own POV came back while you were still on the floor. The match now runs on the picture with its local brightness taken out - the sky's brightness and its gradient go, the letter strokes stay - so the score barely moves when the background changes. Measured on the frame that was failing: a washed-out header that scored 0.75 before (under the threshold, so "alive") now scores 0.94, and a heavily blown-out one 0.83 where it used to score 0.57.
- **And it holds through a washout anyway.** Once you are down the log counts as still there while it scores above the hold level (the threshold less 0.15 by default, on the Detect tab) *and* stays in the place it was found - so nothing elsewhere on screen can pin you down either. The hold is a bridge, not a latch: if the log has not scored a clean match for three seconds it lapses. Coming up now also takes two polls rather than one (existing settings are moved).
- The Detect tab has a **Hold down to** slider showing the score the log is held at.

## 0.4.9
- **Detection works across resolutions.** The gap between the **B** key hint and "VIEW DAMAGE LOG" is a different fraction of the screen at 720p, 1080p, 1440p and 4K, so the old template - which spanned the hint, the gap and the wording - could only ever be a near miss on a resolution other than the one it was cut from. The built-in template is now the wording alone, which is the same shape everywhere and only changes size, and the game source is read at 1000 px across instead of 800 so that small text has enough pixels to match on. On the 1080p frame that was failing this scores 0.86 where everything else on screen scores 0.51; before it was 0.82 against 0.63.
- The match threshold default moves from 0.85 to **0.80** to suit the new template, and existing settings are moved with it (including the ones dragged down below 0.75 to try to make the old template work).

## 0.4.8
- **Learn my HUD** (Detect tab), for a damage log that is never quite matched. Press it while downed: the plugin shows you what it found, and on your say-so cuts that header out of your own screen and uses it from then on, so the score goes near 1 instead of sitting just under the threshold. The built-in template was cut from one particular screen, and the gap between the **B** key hint and the wording is not the same on every HUD - which is exactly the near miss that made one tester drop the threshold to 0.6 and then be shown as downed permanently.

## 0.4.7
- **When the damage log is never found.** The Detect tab has **Save a frame...**, which writes a PNG of your game source exactly as the plugin sees it; do that while downed and send it, and the HUD it cannot match can be looked at directly. The usual search area is also wider than it was (from 45 % across and 15 % down, was 60 % and 25 %), and **Look over the whole frame, at more sizes** tries everything from a third to twice the expected size for a HUD the normal search misses.
- Dragging the match threshold below 0.75 now says, in red, that it will match almost anything - which is what a "downed all the time" reading means, not a fix for a log that is never found.

## 0.4.6
- **The game source can be chosen on the Detect tab too**, right above the picture, instead of only on the Switch tab. It is the same setting in both places.
- **The Help tab shows which version you are running**, and whether a newer one is out.
- **You are told when there is a newer build.** On start-up the plugin asks kennel.gg for a small file saying what the latest build is; if yours is older, the dock shows a line with a download link and the Help tab says the same. Nothing about you is sent, there is no account, and it never installs anything by itself. Turn the check off on the Help tab.

## 0.4.5
- Dual POV tab: **Leave the window up when I get out of the vehicle**, off by default. Off, the window goes by itself when you get out; on, it stays until you turn it off.

## 0.4.4
- **Dual POV goes when you get out of the vehicle, however it was turned on.** While the window is up, ClipHound watches the vehicle keybind corner and three reads with nothing there hide it - from the dock button and the hotkey too, not only when it came on by itself. Turning it on by itself still needs the tick box.

## 0.4.3
- **Crash when closing OBS with Dual POV on, fixed.** The window's private scene could not be found again by name, so every time it was applied a new scene and a new browser page were created and never released; with the window set to come on at start-up that happened on every launch, and OBS then crashed inside the browser engine on the way out. One scene is now kept and released properly before OBS unloads its modules, and the window comes on two and a half seconds after loading, once the browser module is up.
- Detect tab wording: the damage log stays up the whole time you are downed except while the Escape menu is open; with the menu open the plugin comes back to your POV until you close it.

## 0.4.2
- **Dual POV turns itself on in a vehicle** (Dual POV tab, "Turn the window on by itself..."). ClipHound reads the keybind list the game draws bottom-right while you are in a vehicle - CYCLE WEAPON is the tank gunner, DEPLOY SMOKE the tank driver, COLLECTIVE LIFT / DEPLOY FLARES the Havoc pilot, INTERACT and ZOOM alone the Havoc gunner's CAM view - and the window comes up with that seat's placement, then goes when the list goes. A seat is acted on after two readings in a row and "out" after three, so a covered corner does not flap it. One small OCR run a second, only while the option is on. The blue box on the Dual POV picture is where that list is; drag it if your HUD differs.

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
