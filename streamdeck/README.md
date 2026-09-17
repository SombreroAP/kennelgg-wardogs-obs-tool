# Kennel.gg Wardogs - Stream Deck plugin

Keys for the OBS plugin: squad mate POV (a chosen one, or whoever is live in Kennel.gg voice),
next squad mate, clip, instant replay, clip + replay, voice control on/off, Dual POV on/off, my POV.

It talks to the OBS plugin over the same local bridge ClipHound uses (ws://127.0.0.1:47820) and
follows the plugin's state, so keys show who is live and what is on. Nothing leaves the PC.

Build: `npm install && npm run pack` -> `dist/com.kennelgg.wardogs.streamDeckPlugin` (double-click
to install; Stream Deck 6.4 or newer). `src/plugin.js` is bundled by esbuild into the plugin folder.
