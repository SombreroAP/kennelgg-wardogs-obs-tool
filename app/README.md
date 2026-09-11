# ClipHound

Runs on the **streaming PC**. It pulls the game frame from OBS (the capture-card / NDI
source) over obs-websocket, crops to the kill-feed strip (~4% of the pixels), OCRs it a few
times a second, and when it sees **Sombrero** get a multi-kill or a long-range kill it:

1. creates a Twitch clip on twitch.tv/sombrero as the **InfoKennel** account, and
2. tells OBS to save its Replay Buffer (the "backtrack").

## Setup

```
pip install -r requirements.txt
```
**Windows (streaming PC):** unzip, run `install.bat` once (installs Python and Tesseract via
winget if missing, creates the venv), fill `config.yaml`, run `setup.bat` (OBS sources, feed
region check, Twitch login), then `ClipHound.bat` (`ClipHound.bat --dry-run` to test).
Manual route: install Python 3.11+ with "Add to PATH" ticked and
Install Tesseract: Windows → https://github.com/UB-Mannheim/tesseract/wiki (add to PATH),
macOS → `brew install tesseract`.

### 1. Setup wizard
OBS → Tools → WebSocket Server Settings → Enable, set a password, port 4455. Then run
`setup.bat` (or `python setup.py`). It connects to OBS, lists every video input and asks which
one shows the gameplay: on a two-PC setup that is the **capture card** or NDI input, on one PC
the Game Capture. It also asks your in-game name, the clip library folder and (optionally)
the Twitch app id/secret, writes `config.yaml`, saves `roi.png` and reports whether the replay
buffer is running. `python setup.py --check` re-tests without asking questions.

### 2. Calibrate the region
With the game showing in OBS run `python calibrate.py`. Open `roi.png` – the kill-feed lines
("Kennel.gg - Sombrero [68 m] name") must be fully inside it, and `roi_proc.png` should show
clean black text. Adjust `capture.roi` (fractions of the source frame) until the printed
lines show `KILL ... -> NN m`. You can also test on a screenshot:
`python calibrate.py screenshot.png`. If the capture card adds black bars or scaling, the
fractions differ from a raw screenshot - always calibrate against the OBS source.

### 3. Twitch (clips made by InfoKennel on the sombrero channel)
Logged in as **InfoKennel**, create an app at https://dev.twitch.tv/console/apps (redirect
URL `http://localhost:3000`), put client id/secret in `config.yaml`, run
`python get_token.py` and log in as InfoKennel (private window if the browser is signed in
as sombrero). The script prints which account the token belongs to. Clips are created on
`broadcaster_login` (sombrero) and appear as "clipped by InfoKennel". The sombrero channel
must allow clipping (default on for partners). `clip_delay_s` matters more on a two-PC setup:
the stream is the streaming PC's output, so no extra offset is needed beyond the usual ~3 s.

**Clip titles:** the official Helix API cannot set a clip's title (it uses the stream title).
Every clip is logged to `clips.jsonl` with the intended title, the clip URL and the
`edit_url`. Rename them from the edit URL, or from Creator Dashboard → Clips.

### 4. OBS replay buffer
OBS → Settings → Output → Replay Buffer: enable, 60–120 s. Start it from the Controls panel
(the bot starts it if stopped). `mode: record` starts a normal recording for
`record_seconds` instead. Everything is local to the streaming PC; nothing runs on the
gaming PC.

## Run
```
python main.py --dry-run     # detection only, prints triggers
python main.py               # live
```

## Testing offline against a VOD
```
yt-dlp -f "best[height<=1080]" --download-sections "*23:40-24:40" -o "vod/clip.%(ext)s" https://www.twitch.tv/videos/<id>
python vod_test.py vod/clip.mp4            # add --verbose for every OCR read, --dump for frame crops
```
Every decided feed row is saved to `debug/rows/` for labelling. On the 24:06 slice of the
7 Sep VOD it reads the 68 m and 61 m kills correctly and fires "Double kill (2 players)".

## How detection works
- The feed strip is upscaled 4x and top-hat filtered, which keeps the light HUD text and
  drops the game world behind it. Rows are found from the text projection of the name column.
- A row must be present every frame (the feed blanks briefly between kills) and is only read
  after its half-second fade-in. Ten reads are voted: partial reads like "8" support "68".
- Each row is parsed into killer, distance, victim and icons, then the `rules` list decides.
- Only Sombrero's kills count for multi-kills; team kills and deaths never do.

## Team, squad and enemy by colour
There are three teams: red, blue and green. Names in the feed are coloured:
**orange = squad**, **your team colour = team mate**, the other two colours = enemy, and your
own name is white. The bot measures the
colour of the killer and victim text (`colors` bands in `config.yaml`) and turns it into a
relationship, so `victim: friendly` catches any team kill without a name list. Your team is
read every 30 s (`my_team: auto`) from the team emblem at the bottom right of the HUD
(`team_icon_roi`), falling back to the minimap pointers (`minimap_roi`); set `my_team: red`
to pin it. Calibrated on the 7 Sep VODs: red emblem at 23:40 of the
first stream, blue at 17:27 and green at 1:35:25 of the second.
`python calibrate.py --colors screenshot.png` prints the measured hue/sat/val per row so the
bands can be tuned on frames that show team-mate and squad names.

## Tags, search and local clips
Every trigger carries tags: the rule's own tags plus its icons (skull = headshot, tank, heli,
rpg, c4, rifle, sniper...) and whether it was a death, team kill or multi-kill. They are written
to `clips.jsonl` (Twitch clip URL + edit link) and `replays.jsonl` (the OBS replay file, which
the bot renames to `<timestamp> <title> [tags].mkv`). Search either with:
```
python clips.py headshot          # all headshots
python clips.py tank car          # tank or car kills
python clips.py --tags            # tag counts
```
Every trigger saves the replay buffer locally, so compilations can be cut from the renamed
files without touching Twitch. Set the buffer to 90-120 s in OBS so the lead-up is included.

**Clip library for editing.** Set `obs.library` to a folder (e.g. `D:\ClipHound`) and every
replay is moved to `<library>/<date>/` and renamed to say what is in it, e.g.
`Replay 2026-09-08 20-14-33 - Double kill - 68m 61m - rifle - 2 kills [multikill rifle] @-7s.mkv`
(`@-7s` = the moment is about 7 s before the end of the file). Each has a `.json` sidecar
holding title, tags, kind, distance, killer, victim and icons. Two indexes are kept in the
library root: `index.csv` (everything) and `resolve_metadata.csv`. In DaVinci Resolve, import
the clips, then Media Pool > right-click > Metadata > Import and pick that CSV: titles become
descriptions and tags become keywords, so smart bins like "headshot" or "multikill" fill
themselves. An editing agent (e.g. a DaVinci Resolve MCP server) gets everything from the filename
alone; the sidecar adds the structured version (kind, distance, killer, victim, icons,
`moment_s_from_end`) and `index.csv` is the whole library in one table.

## Icon catalogue
`tools/extract_icons.py vod/*.mp4` harvests every feed icon from video slices, averages the
sightings of each distinct shape into a clean mask and writes `icons/unlabelled/` plus a
contact sheet. Label them in `icons/labels.json` (same label on several ids = variants), then
`tools/build_catalogue.py` writes `icons/catalogue/<label>.png` (white on transparent, 4x),
`<label>_64.png` and `<label>_dark.png` for overlays, the website or Discord. `?` entries in
labels.json still need a name.

## Icons (tank / chopper / vehicle rules)
Vehicle rules need a template of the feed icon. Included templates, cut from the 7 Sep VODs:
`rifle`, `boltgun`, `sniper`, `heli`, `explosion`, `tank`, `artillery`, `car`, `mortar`, `hammer`, `rpg`, `c4`, `skull` (headshot),
and `name_me` (the streamer's own feed name; matched as pixels so identity survives
backgrounds where OCR fails). Weapon icons are exclusive per row (best score wins);
`skull` and `explosion` stack on top.
The feed uses a weapon icon for guns and a helicopter / car / tank icon for vehicles; a
crash shows the vehicle icon plus the explosion icon and no distance, made from the 24:06 rows. To add a variant (e.g. `car_2.png` for a different vehicle icon):
1. Run `vod_test.py` (or the live bot with `debug_dump: true`) over footage containing the
   event; the decided rows land in `debug/rows/`.
2. Upscale the row 4x, threshold to the pure-white pixels, and tight-crop the icon:
   ```
   python -c "import cv2,numpy as np; im=cv2.resize(cv2.imread('debug/rows/row_003_0m.png'),None,fx=4,fy=4,interpolation=cv2.INTER_CUBIC); h=cv2.cvtColor(im,cv2.COLOR_BGR2HSV); w=((h[:,:,2]>170)&(h[:,:,1]<70)).astype(np.uint8)*255; cv2.imwrite('white.png',w)"
   ```
   then crop the icon out of `white.png` in any image editor and save it as
   `templates/<name>.png`. Rule `icon: tank` matches any template whose name starts
   with "tank", so `tank_a.png`, `tank_b.png` can cover variants.
Until those templates exist the vehicle rules simply never fire.

## Tuning (`config.yaml`)
- `multikill_window_s` – how close kills must be to count as one multi-kill.
- `rules[].min_dist` / `min_conf` – metres, and how many OCR reads must agree first.
- `teammates` – squad names so `victim: teammate` rules (chopper team kill) can match.
- `cooldown_s` – gap between clips; a double becoming a triple always re-fires.


## Running with the Kennel.gg Wardogs OBS plugin

The plugin (`../kennel-wardogs-obs`) can be ClipHound's eyes and hands: set `capture.backend: bridge`
and `obs.mode: bridge` in `config.yaml`, and ClipHound connects to `ws://127.0.0.1:47820`, receives
the game frames from inside OBS (no obs-websocket, no password) and asks the plugin to save and name
the replay clips. Clips fired while you are downed get a `downed` tag. The plugin's Clips tab can
start `ClipHound.bat` when OBS starts. Twitch clips still come from this app.

CPU: the OCR is the heaviest part of the pair. Tesseract runs per feed row on a crop of the frame
(the feed is 24 % x 16 % of 1080p) at `capture.fps`, and only until a row is decided - about 0.75 s
of reading whatever the rate. At the default 10 fps that is roughly 10-20 % of one core while a kill
is being read and a few per cent between kills. Reading the NEARBY panel (when the plugin asks for
it) is one tesseract call a second, or one every 0.4 s while you are down; names are only re-read
when they change. Lower `fps` on the plugin's ClipHound tab if the CPU matters more than the second
it saves.
