# Building a modlist for TES3MP-NG

The process is the same as for TES3MP 0.8.1, with one extra step for Lua mods that change the shared world. OpenMW
Lua mods (`.omwscripts` files, OpenMW 0.48 and later) are content files, like `.esp` plugins, so players install
and order them the same way, and the server lists them in the same file.

## Players

Every player installs the same mods:

1. Unpack each mod into its own folder.
2. Add the folders in the OpenMW launcher (Data Directories), or add `data="..."` lines to `openmw.cfg` in
   `Documents/My Games/OpenMW`.
3. Enable the content files in the launcher's Content Files list, in the server's order, or add `content=` lines to
   `openmw.cfg`. A Lua mod's `.omwscripts` file is enabled like a plugin. Some mods register their scripts through
   an `.omwaddon` or `.esp` plugin instead, which is enabled as usual.

## Server

1. **Required data files:** list every content file the players enable in `data/requiredDataFiles.json`, in the
   same order: plugins, `.omwaddon` files and `.omwscripts` files. OpenMW's own `builtin.omwscripts` is left out.

   ```json
   [
       {"Morrowind.esm": ["0x7B6AF5B9", "0x34282D67"]},
       {"Tribunal.esm": ["0xF481F334", "0x211329EF"]},
       {"Bloodmoon.esm": ["0x43DD2132", "0x9EB62F26"]},
       {"MyQuestMod.esp": []},
       {"MyLuaMod.omwscripts": ["0x1A2B3C4D"]}
   ]
   ```

   An empty list accepts any version of a file. To require one version, copy the checksums from a client's log:
   the client logs a `checksum:` line for each content file when it connects. A `.omwscripts` checksum covers the
   manifest and every file in its scripts' folders, so players also need the same version of the mod's code.

2. **Script policy (Lua mods only):** open each Lua mod's `.omwscripts` file and look for `GLOBAL:` lines. Only
   global scripts need a decision. Leave them in the default `client` mode, or list them as `authority` in
   `data/lua/scriptPolicy.json`:

   ```json
   {
       "default": "client",
       "scripts": {
           "scripts/myspawner/world.lua": "authority",
           "scripts/myeconomy/*": "authority"
       }
   }
   ```

   * `client` (default): every player's game runs its own copy of the script, as in single player. Use it for
     scripts that react to the local player: activation and item use handlers, UI helpers, and anything that
     doesn't change the shared world.
   * `authority`: one player's game runs the script for everyone, and the server keeps its saved state. Use it for
     scripts that change the shared world: spawners, timed world events, economy, shared quest logic. In `client`
     mode, each player's copy would make the same change.

   `PLAYER`, `NPC`, `CREATURE` and other local scripts need no policy: they run on the game that simulates their
   object.

3. **Server scripts:** CoreScripts scripts work as in 0.8.1: copy them into `scripts/custom` and add a
   `require("custom/<name>")` line to `scripts/customScripts.lua`.

## What Lua mods do in multiplayer

* Changes that Lua mods make to the world (placing, moving, removing, scaling, locking objects, containers, the
  player's spells, factions and journal) are sent to the other players, like changes made by mwscript in 0.8.1.
  See [lua-multiplayer.md](lua-multiplayer.md).
* UI, camera, input, sound, post-processing and settings stay on each player's game, so a mod's settings are each
  player's own.
* Records created with `world.createRecord` exist only on the game that created them, so objects made from them
  aren't sent to other players.
* The server owns the game time and weather, so Lua changes to them don't last.

## Limitations

* **Client-only mods are still required mods:** as in 0.8.1, a client whose content list differs from the server's
  is disconnected. A UI-only Lua mod has to be listed on the server, and installed by every player.
* **Scripts registered by plugins:** when a mod registers its scripts through an `.omwaddon` or `.esp`, the
  checksum covers the plugin but not the Lua files. Different versions of those scripts aren't detected.
* **Assets aren't checked:** meshes, textures and sounds aren't compared, as in 0.8.1.
