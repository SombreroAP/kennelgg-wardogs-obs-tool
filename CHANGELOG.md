# Changelog

All notable changes to Kennel.gg Wardogs OBS Tool. Release notes on GitHub are taken from here.

## 0.8.3
- **The Discord window list shows pop-outs only.** When you add a squad mate by hand, the window picker lists only popped-out streams (Discord titles those with "Stream"), with "Any Discord window" at the bottom for the not-popped-out case. If nothing is popped out it says so instead of offering the wrong windows.
- **Add tells you what it saw.** When Add finds nothing to add, the panel and the log list every Discord window that was open, by title, so a pop-out that is named differently from what the plugin expects can be read straight off the panel.
- The owner of a pop-out is read with whichever apostrophe Discord uses, and for names ending in s.

## 0.8.2
- **Force Dual POV.** The dock button is now a forced toggle: press it and your squad mate is in the small window until you press it again, whatever the vehicle detector thinks. The detector still opens and closes the window by itself when the forced toggle is off, and the button says which of the two is holding it up.
- **The dual window has a frame and their name.** A thin edge round the picture and a small name plate bottom-left, drawn by the same look as the main swap so it matches, and scaled down because the window is. Off and on with the look effects, or by its own tick on the Dual POV tab.
- **A Discord username also finds them in the NEARBY list.** Both their in-game name and their Discord username go to ClipHound, whose matching is already fuzzy, so a slightly different spelling in the game still matches.

## 0.8.1
- **Add asks for the in-game name.** When a popped-out stream becomes a squad mate, the panel asks what they are called in the game, with the Discord username filled in as the guess. That name is what the NEARBY list is matched on, so Closest works from the first match. An **In-game name...** button on the panel changes it later.
- **Dual POV turned on by hand stays on.** Leaving a vehicle only closes a window the vehicle detector opened. Pressing Dual POV with nobody picked uses the active squad mate. The Squad panel has **Show in Dual POV** for whoever is selected.

## 0.8.0
- **Squad button on the dock, and the flow is now: pop it out, press Add.** Open OBS, join Discord voice, watch a squad mate's stream and pop it out. Press **Squad** on the dock, then **Add popped-out Discord streams**: every popped-out stream becomes a squad mate named by their Discord username, bound to that window by exact title, with their in-game name set to match. Already-added people are skipped. The panel also lists the squad with what each one is showing, and has Make active and Remove for mid-broadcast.
- **Window title must match.** A slot made from a pop-out, or from a specific Discord window you picked, is matched on that exact title only. Matching by executable was how a slot ended up on the wrong Discord window; it is now used only for "Any Discord window".
- **Squad mates who go live in Kennel.gg voice can add themselves.** One tick in the Squad panel. The roster address is built in, nothing to paste. Slots are named by Discord username so they line up with pop-outs and in-game names. Put your own Discord username in the panel and only the channel you are sitting in counts, and your own stream is never added.

## 0.7.9
- **Pop-outs are matched on their owner exactly.** A real PC showed a Go Live pop-out is titled "<username>'s Stream". The plugin now reads the username out of that and binds a slot whose Discord username, or slot name, is exactly that person, so a slot called Bryan can never take bryanx's window. A hand-named slot still matches if its name is inside the username.
- When a pop-out belongs to nobody in the squad the log names the Discord user it belongs to, and says when that user is you.

## 0.7.8
- **Your desktop audio was being ticked to mute again on every start.** The "already done that" flag behind the old auto-pick was never saved to disk, so whenever the mute list was empty at start-up the plugin ticked your desktop audio back in, undoing you if you had cleared it. The auto-pick is gone for good: nothing of yours is muted unless you tick it in Settings -> Switch. The list is cleared once more on first start, since anything in it may have been the auto-pick's doing.

## 0.7.7
- **Pop-out binding now covers every Discord squad mate**, including slots where you picked a specific Discord window when you added them. 0.7.6 only watched slots set to "Any Discord window", which is not what the add dialog picks by default when Discord is running, so for most people it watched nothing. The log now says at start how many slots it is watching.
- A slot bound to a pop-out remembers the capture it came from and goes back to exactly that when the pop-out closes.

## 0.7.6
- **Pop-outs bind themselves.** Pop a squad mate's share out of Discord (right-click their tile, Pop Out) and within two seconds their slot is showing that window and nothing else. Close it and the slot goes back to the Discord window. Two or three pop-outs give you two or three separate feeds. The log says whose window it found and what it was called.
  - Discord titles a popped-out tile with the person's username, so the roster now carries usernames too. If a pop-out shows up that matches nobody, the log names it so you can see why.
  - A pop-out that is minimised freezes; the log tells you to restore it. It can sit behind the game, just not minimised.
- **One capture of the Discord call, not one per squad mate.** 0.7.5 made every squad mate their own capture and audio capture of the same Discord window. Five squad mates meant five captures of one window, all showing the same picture. They share one now. Existing slots are moved over on first start.

## 0.7.5
- **Squad slots fill themselves in from Discord.** Somebody goes live in your voice channel and a slot appears with their Discord name on it, their capture already made. They stop sharing and the slot goes away again. Slots you added yourself are never touched.
  - Turn it on in Settings -> Squad, under **Squad from Discord**, and paste the roster address from the server. It carries a key, so treat it like a password and keep it off stream.
  - Discord will not tell a plugin who is in a call - the client's own interface for that is gated behind a permission Discord grants application by application, by hand. The Kennel.gg Discord bot publishes the roster instead, which is why this only works for our server.
  - Only people actually sharing get a slot. Discord puts every share inside the one window, so a slot for somebody who is not live could never show anything.
  - Popping a share out is still a click in Discord. Nothing here moves your mouse for you.

## 0.7.4
- **ClipHound stuck on "starting" with no clips and no NEARBY, explained and fixed.** From a user's OBS log: the bridge was on `47820` and everything worked, then two hours later `bridge: listening on ws://127.0.0.1:47821` and ClipHound never connected again. Nothing was pressed - a mouse wheel over the settings window had rolled the **Bridge port** spin box by one. ClipHound reads that port from its own config.yaml, and the plugin never told it, so it went on knocking at the old number for ever.
  - **A scroll no longer changes a setting.** Spin boxes, sliders and drop-downs ignore the wheel until you click into them; scrolling moves the page, as it should.
  - **ClipHound is told the new port** and restarted, so the two can never disagree. If its config cannot be written the log says so and what to do.
  - If you are on an older build and see this: set **Bridge port** back to **47820** (Settings -> ClipHound).
- **Twitch clips stopped being made after a while, and now they will not.** Twitch hands back a *new* refresh token every time the old one is used, and invalidates the old one - ClipHound updated it in memory and printed "update config.yaml yourself", so the next start sent a dead token and got `400 Bad Request` for ever after. The new pair is written to config.yaml the moment it is refreshed, and a 400 now says in plain words that the login has expired and where to fix it, in the plugin's own log rather than only ClipHound's.
- **ClipHound sitting at "starting" said nothing about why.** A failed connection to the plugin was swallowed - the app retried silently every 3 seconds for ever. It now says what the connection error was and what to check, once, and the plugin prints the tail of ClipHound's log if 25 seconds pass with no connection.
- **Setting the clip length could switch the replay buffer off.** 0.7.2 stopped the buffer and started it again on the next line, but OBS's stop has not finished when the call returns, so the start silently failed - leaving the buffer off and no clips at all until OBS was restarted. The restart now waits for the stop, checks it came back, and says so if it did not.
- **ClipHound stuck on "starting", and no clips: now it tells you why.** If two copies of the plugin are installed, both load and the second one cannot open ClipHound's bridge port - ClipHound then connects to the wrong one and sits at "starting" for ever, with no clips. That failure only ever reached OBS's own log, so from the dock it looked like nothing at all. It now says so in the plugin's log, names the old folder to delete, and the installer removes `plugins\kennel-wardogs` itself - or says plainly that it could not, which almost always means OBS was still open.
- **And if ClipHound starts but never connects**, after 25 seconds the plugin prints the last lines of ClipHound's own log into its own, instead of leaving "starting" on screen with no explanation.

- **Replay files are named the same as the Twitch clip.** ClipHound now hands the plugin the same plain-English headline it gives Twitch, and the default file name is that headline followed by the date and time - `Double kill at 68m and 61m with a rifle - 2026-09-11 21-04-33.mkv`. The tag list has gone from the default name (it is still in the clip index, and `{tags}` still works in a template of your own). A template you typed yourself is left alone.

## 0.7.2
- **Twitch clips are titled with what happened.** They used to carry the stream's title; the moment that set them off was only written to ClipHound's own index. The clip now goes to Twitch with a plain-English title - *Double kill at 68m and 61m with a rifle*, *Died to a headshot at 120m*, *Crashed my chopper* - and the same wording names the OBS replay file.
- **Clip length is a setting: 45 seconds by default.** Clips tab -> *Clip length*. It is written straight into OBS's own replay-buffer setting for both output modes, so the plugin and OBS never disagree about how far back a clip reaches, and the buffer is restarted if it was running.
- One honest limit: the 45 seconds is for the clips OBS saves. A Twitch clip's length is Twitch's - the API takes the seconds leading up to the request and its edit page trims afterwards; nothing the plugin sends can make one 45 s long.

## 0.7.1
- **The 0.7.0 installer put the plugin in the wrong folder, so OBS never loaded it** - no dock, no Tools entry, no error. It kept the old AppId so Windows would see an upgrade, and Inno Setup then reused the previous install folder: `kennelgg.dll` ended up under `plugins\kennel-wardogs`, and OBS only loads a DLL named after its folder. 0.7.1 always installs to `plugins\kennelgg` and removes the stray folder 0.7.0 left. Nothing else changed.

## 0.7.0
- **Renamed to Kennel.gg Wardogs OBS Tool, all the way down.** The window, the dock, the hotkey labels, the installer, the repo - and now the plugin itself: the module is `kennelgg`, it installs to `plugins\kennelgg`, ClipHound lives in `ProgramData\Kennel.gg\ClipHound`, and every source the plugin makes is named "Kennel.gg ..." ("Kennel.gg · Pup", "Kennel.gg web", "Kennel.gg look", "Kennel.gg dual"). The Help tab has an **About The Kennel** section with what kennel.gg is and links to the site, Discord, Twitch and X, and the installer's welcome page says who made it.
- **Nothing is lost in the move.** The installer removes the old `plugins\kennel-wardogs` folder (two copies would both load), carries ClipHound's config over and removes its old folder; the plugin picks up its old settings file the first time it starts under the new id; sources made by earlier builds are renamed in place rather than made again, so scenes do not fill with duplicates; and the hotkey ids are unchanged, so bindings survive. Close OBS, run the installer, start OBS: that is all.
- **Nothing of yours is muted when the POV changes, and no sound is taken from the squad mate's feed** - for everyone, including setups that had either ticked. Both are still there on the Switch tab to turn on. A Discord squad mate's audio capture follows the same rule now in every mode.
- **NDI is shelved.** LAN discovery, the NDI share and auto-adding are off and out of sight; the code is kept, and a squad mate already set up as NDI keeps working, but NDI is not offered for new ones. It is parked until it behaves reliably.
- **Kick and YouTube live streams** as squad-mate kinds, alongside Twitch. Kick takes the channel name; YouTube takes a channel link, @handle, channel ID or a live video link - a handle is looked up once on Save for the channel ID the player needs. With a channel, whatever they are streaming right now is shown.
- **The swap on a vertical canvas too.** Switch tab -> *Vertical scene*: pick the scene your portrait stream (Aitum Vertical) shows, and the squad mate's feed is shown there as well - full height, sides cropped - with the look overlay in a portrait form: everything sized to the narrow canvas and the POV tag across the top, where the cropped feed has no HUD. Same source in both scenes, so nothing is decoded twice; the *at* box on the Look tab still moves the tag.

## 0.6.6
- **A squad mate's NDI feed is no longer streaming the whole time you are alive.** Keeping a feed warm leaves the source running so a swap is instant - which for NDI means their full stream crossing the network and being decoded on both PCs continuously, for something you need only when you go down. At 1440p that is around 240 Mbit running permanently, and it is enough on its own to make everything judder. NDI feeds are now connected only while they are shown; there is a tick box on the Switch tab (Extras) to keep one connected if you would rather have the instant swap and can spare the bandwidth. Browser and Discord feeds are unchanged.
- If the picture still judders, the size being sent is the next thing: **Share at** on the *sending* PC. A full 1440p canvas is about 240 Mbit of nearly-raw video; 1080p is about a third of that and 720p a tenth.

## 0.6.5
- **Why NDI only worked with OBS run as administrator, explained in the plugin.** Windows hands whole ranges of TCP ports to Hyper-V, WSL, Docker and the like, and a program running as a normal user cannot bind anything inside them. NDI's ports (5960-5970) land inside one of those ranges on a lot of machines - so NDI works when OBS is elevated and not otherwise, no firewall rule will fix it, and nothing on screen ever hints at it. **Check NDI** now says whether OBS is elevated, whether NDI's ports are inside a reserved range, and the exact commands that free them; it also says so once in the log at start-up, unprompted.
- **Deleting a squad mate offers to delete the sources made for them.** *Remove and delete the sources* / *Remove, keep the sources* / *Cancel*, with the sources listed so you can see what will go. Only ever ones the plugin made: a squad mate set up as an OBS source you already had keeps it, and the shared browser source is never touched.
- **Preloading no longer keeps every NDI feed decoding.** A warm NDI feed is a receiver running a full stream the whole time, and several at 1440p judder for no gain - an NDI receiver is back in well under a second. Browser feeds, which take seconds to load, are still all kept warm; NDI and Discord feeds are kept warm only for the squad mate you would actually show.

## 0.6.4
- **Neither PC being able to see the other's feed at all was the plugin's fault, and this fixes it.** To list NDI feeds for the squad-mate picker, the plugin loaded its own copy of the NDI runtime into OBS - and if DistroAV had not already loaded one, that was a *second* NDI stack in the same process, initialised separately and holding a finder open for the life of OBS. Two NDI stacks contending for the same discovery sockets stops that PC seeing feeds and stops its own feed being seen. Both ends had the plugin, so both went dark, and it looked like a network fault.
- The plugin now uses **only the runtime DistroAV has already loaded**, never loads or initialises one itself, and never holds a finder open: it asks once when you press **Check NDI** or add a squad mate, and lets go immediately. Nothing asks NDI anything on a timer any more.
- **Install this on both PCs and restart both** (a fully restarted OBS on each is enough; a reboot does no harm). Feeds should appear in DistroAV's source list again as they did before.

## 0.6.3
- **Check NDI reads NDI's own machine settings.** Two settings there switch discovery off completely, and are the usual reason a PC sees no feeds at all - its own included: a discovery server that is set but not answering, and a receive or send group that is not the one everybody else uses. NDI Access Manager writes them, and the report now says when either is set, with the file it read.
- **Fix discovery**, next to Check NDI. On a network where NDI cannot discover anything, this writes your squad's addresses into NDI's own settings for that PC so it looks at them directly rather than waiting to find them - which is NDI's own documented answer to exactly this. It changes a setting outside OBS, so it asks first, and it says plainly when it cannot write the file.

## 0.6.2
- **Check NDI**, next to the NDI share line on the Switch tab, and in the log whenever the share starts. It reports what is actually true on that PC rather than guessing: whether your feed is running, whether the NDI runtime could be loaded at all, every feed NDI can see on the network, and whether your own is among them.
- **When your feed is running but nothing can see it, it names the likely reason.** On a gaming PC that is usually not the firewall but a second network adapter: NDI advertises on one interface, and Hyper-V, WSL, Docker, VirtualBox and VPN clients all add adapters that can win that choice while ordinary traffic still routes perfectly. The report lists every active adapter with its address so you can see which ones are in the way. With only one adapter it points at Windows Firewall instead.
- 0.6.1 said "your feed is running but discovery cannot see it" even when the NDI runtime had not loaded and it could not actually tell. It now says which of the two it is.
- The log says **"NDI share has STOPPED"** with the reason when the output dies, instead of only mentioning it while restarting.

## 0.6.1
- **A ticked box is no longer taken for a working share.** The plugin told the squad it was sharing over NDI whenever the box was ticked, whether or not the output had actually started - so a share that failed looked exactly like a working one from the other end, and the squad mate's feed was simply blank. Your beacon now only advertises a feed while the output is really running, and the Switch tab has an **NDI share** line that says what it is actually doing, in red when it is not.
- **It checks that anything can find your feed.** A few seconds after the share starts, the plugin asks the NDI runtime whether it can see your own feed. If it cannot, the log says so and names the usual causes - Windows Firewall blocking OBS on a private network, or the two PCs being on different subnets - because a feed nobody can discover is, to a squad mate, the same as no feed at all.
- **It restarts a share that has died**, every 15 seconds, and says so in the log.
- **The link test warns about different subnets.** NDI finds feeds by multicast, which does not cross a subnet: a squad mate whose link measures perfectly can still never appear in your source list. The plugin also now hands NDI the addresses of the squad mates its own beacon already found, so its list still fills in where discovery alone would not.

## 0.6.0
- **A squad mate's NDI feed that connects to nothing, fixed.** The plugin composed their feed's name from their beacon as "<their computer> (Kennel POV)", but NDI advertises the machine name in its own form - upper case, the DNS name, whatever the runtime settled on - and DistroAV matches that string exactly. One letter's difference and the source connects to nothing, shows nothing, and reports no error anywhere. The plugin now asks the NDI runtime what is actually being published and matches on the part in brackets, which is the half we control; if a name has drifted it is corrected and the log says so.
- **And it says so when nobody is publishing.** If no feed on the network matches, adding the squad mate now fails with a real message, and the log lists every NDI name it can actually see - so "their OBS is not sharing" and "we are asking for the wrong name" stop looking identical.
- **Every settings tab scrolls.** At 125 % Windows scaling, on a laptop screen or with a large font the contents were squeezed into whatever height was left instead of keeping their own. The window can also be made genuinely small now.

## 0.5.9
- **A squad mate's NDI feed showing nothing at all, fixed.** 0.5.4 turned DistroAV's frame sync on for every NDI feed to smooth out judder. On some setups the picture then never arrives, which is worse than the judder it was meant to fix. The plugin no longer touches how a feed is timed: **Timing** in Edit... now starts at *leave DistroAV's own setting alone*, and 0.5.9 puts frame sync back off on feeds 0.5.4 to 0.5.8 turned it on for. Frame sync, timestamps, the sender's timecode and none are all still there to try by hand - which is where a setting like that belongs.
- The forced "normal latency" write is gone with it. The only receive setting the plugin sets by itself is the one you choose in **Receive at**.

## 0.5.8
- **Test the link to this squad mate**, next to the squad list on the Switch tab. It sends flat out to their OBS for four seconds, and their end reports what actually landed - so you get a real number for the path between the two PCs, not what the adapters claim. It then lists what each NDI size needs at your frame rate and tells you which one to set. "It is gigabit" is not a measurement: one port negotiated at 100 Mbit, a powerline adapter or a single Wi-Fi hop all look the same from the desk, and none of them carry a full-canvas NDI stream.
- Both PCs need 0.5.8 with **Find squad mates on the LAN** ticked. The listening side uses the LAN port + 1 (47846 by default) and does nothing but count what it is sent and throw it away.

## 0.5.7
- **The NDI share no longer blacks out another plugin's extra canvas.** 0.5.4 gave the share's own view its own frame rate as well as its own size, and a video mix running on a clock of its own is what Aitum's vertical canvas went black on - the same symptom 0.4.3 fixed, caused again by a different line. The share now only ever changes the output size: the mix keeps the canvas size and OBS's own frame rate.
- **Share at is sizes only** - 720p, 900p, 1080p or the full canvas, always at your OBS frame rate. That also removes the frame-rate halving that made a smaller share look choppy: 720p is now 720p at 60 if that is what you run.
- If a canvas is ever black again, set **Share at** to *the full canvas* (that makes the share an exact copy of what OBS renders) or untick the NDI share, and tell me which of the two it was.

## 0.5.6
- **Share at now covers both halves of the trade-off.** 0.5.4 only offered 30 fps, so choosing a size that a network could carry also halved the frame rate - steady, but soft and visibly half the frames. Every size now has a 30 and a 60: 720p, 900p, 1080p and the full canvas. The default moves to **1080p 30**, about three times the picture of 720p 30 for a third of a gigabit link; pick 720p 60 instead if motion matters more to you than sharpness.
- The downscale uses **Lanczos** rather than bicubic - noticeably sharper at 720p and 1080p, and it costs the network nothing.
- **Timing**, per squad mate in Edit...: frame sync (the default), network timestamps, the sender's timecode, or none. If a feed still judders, these are worth trying in turn; senders differ.
- **Test feed** button on the Switch tab. It watches the selected squad mate's feed for two seconds and tells you how many new pictures a second are actually arriving - which is the only way to tell a feed that is not being delivered from one that is arriving fine and being drawn badly.

## 0.5.5
- **The judder in the first seconds after a swap is gone.** Keeping a squad mate's feed warm hid its scene item, which is right for a browser feed - it goes on playing while hidden - but wrong for NDI: OBS stops a hidden source, DistroAV drops the connection, and the swap was then spent reconnecting and catching up. Non-browser feeds now stay in the scene fully transparent instead, so the receiver is connected and in step before you ever go down. Turn **Keep warm** on (Switch tab, Extras) if you had it off.

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
