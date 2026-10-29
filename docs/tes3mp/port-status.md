# TES3MP 0.8.1 → OpenMW 0.51 port status

TES3MP 0.8.1 changed the engine in 718 marked blocks (`Start of tes3mp addition/change`) outside its own `mwmp`
directory. This file tracks how each area was carried over to OpenMW 0.51. Areas where OpenMW 0.51 moved the
behavior into Lua have been adapted rather than copied.

## Ported

| Area | Files | Notes |
|---|---|---|
| Movement and animation | `mwmechanics/character.cpp` | Movement direction for position packets, death states, cast and attack animations, scripted animations of local actors |
| Actors and AI | `mwmechanics/actors.cpp`, `mechanicsmanagerimp.cpp`, `ai*.cpp` | AI is off globally and runs only for local actors. Dedicated actors and players keep their equipment and lights. Bounties are forgiven after death; kill counts come from the server; allied players; NPC activation, attack and arrest rules |
| Combat | `mwclass/npc.cpp`, `creature.cpp`, `mwmechanics/combat.cpp`, `aicombat.cpp`, `mwrender/weaponanimation.cpp` | The attacker's client computes the hit and damage and sends them. The victim's client applies them through OpenMW's Lua combat (`LuaManager::onHit`) |
| Magic | `activespells.cpp`, `spellcasting.cpp`, `spelleffects.cpp`, `spells.cpp`, `summoning.cpp`, `projectilemanager.cpp` | Active spells sync for local players and actors. Cast success comes from the caster's client. Lock/Open spells, Mark, power cooldowns, purges and diseases are sent. Summons are spawned by the server |
| World interaction | `mwworld/player.cpp`, `action*.cpp`, `mwclass/door.cpp`, `container.cpp`, `security.cpp` | Activation goes through the server, whose reply runs OpenMW's Lua activation handlers. Also: locks, traps, harvesting, item pickup, door destination overrides, follower cell changes, action sounds |
| Inventory and containers | `containerstore.cpp`, `inventorystore.cpp`, `mwgui/container*.cpp`, `inventorywindow.cpp`, `draganddrop.*`, `worlditemmodel.hpp`, `hud.cpp` | Player inventory packets are sent. Containers are server-authoritative: drag, drop, Take All and alt-transfer are requests. Item use is approved by the server and runs OpenMW's Lua use handlers. Dropped items are placed by the server |
| Crafting and services | `alchemy.cpp`, `enchanting.cpp`, `repair.cpp`, `recharge.cpp`, trade/training/travel/spell windows | The server creates potion, enchantment and spell records. Merchant gold pools are set by the server. Travel and training don't advance time |
| mwscript | `mwscript/*extensions.cpp`, `globalscripts.cpp` | Enable/Disable, Lock/Unlock, SetDelete, SetScale, PlaceItem/PlaceAt, PositionCell, AddItem/RemoveItem (console), spellbook, factions, journal, topics, werewolf, AI packages, PlayGroup and StreamMusic send packets. Script context and name are tracked |
| GUI and input | `messagebox.*`, `console.cpp`, `quickkeysmenu.cpp`, `jailscreen.cpp`, `waitdialog.cpp`, `mainmenu.cpp`, `bindingsmanager.cpp`, `actionmanager.cpp`, `keyboardnavigation.cpp` | Server message boxes send replies; console commands are reported; quick keys sync; jail and rest rules apply; the chat receives key presses; there's no quicksave/quickload and no new or load game |
| Cells and map | `scene.cpp`, `globalmap.cpp`, `mapwindow.cpp` | Cell load and unload states are sent (they drive cell authority), explored global map tiles are shared, and global map tooltips list the players in each cell |
| Physics | `mwphysics/actor.cpp`, `mwworld/class.cpp` | Collision settings from the server apply to players, actors and placed items |
| Jail and menus | `jailscreen.cpp`, `playerskillhandlers.lua`, `settingswindow.cpp`, `race.cpp` | The server's jail overrides apply, including `ignoreJailSkillIncreases` (passed to the Lua jail handler). The difficulty slider is hidden; the race menu has no back button after login |
| Character creation | `mwgui/charactercreation.cpp`, `mwmp/LocalPlayer.cpp` | The server drives the stages. Automated tests can log in with `--tes3mp-autologin <name> --tes3mp-autologin-password <test password>` |
| Server browser | `apps/browser` | Built with Qt 6 like OpenMW's launcher, and settings read with `mwmp::IniSettings`. Play starts `openmw` with `--connect` |
| Master server | `apps/master` | The HTTP API runs on CrabNet's `TCPInterface` instead of Simple-Web-Server (which needs Boost.Asio APIs that current Boost removed), with the read-only routes only. Ports: `masterserver [master port] [HTTP port]` (25560 and 8080 by default) |

The earlier port work covers `worldimp.cpp`, `weather.cpp`, `cellstore.cpp`, `cellref.cpp`, `dialogue.cpp`,
`windowmanagerimp.cpp`, `mapwindow.cpp`, `interpretercontext.cpp` and `engine.cpp`.

## Differences from 0.8.1

* **Knockdowns:** decided by the victim's client, where OpenMW 0.51's Lua combat scripts handle the hit. In 0.8.1
  the attacker's roll was sent.
* **Lua handlers:** activation and item use go through OpenMW's Lua handlers when the server approves them, so Lua
  mods see them once, on the acting player's client.
* **Lua sync:** see `lua-multiplayer.md`.
* **Chat key:** F8 instead of F2, which OpenMW uses for its post-processing HUD.
* **Master server HTTP API:** the POST and PUT routes for servers older than 0.6 are removed.

## Not ported or not verified

* **Server browser:** the Favorites tab stays hidden, as in 0.8.1, where it was broken. Windows packaging of the
  browser's Qt DLLs isn't set up.
* **Testing:** played by hand so far: movement, NPCs, cell loading, equipping, NPC dialogue, weather, combat, death
  messages and bartering. Other hooks are compile-tested and covered by the automated login, Lua sync, Lua mod,
  persistence, map tile and master server tests; containers, spells, doors, jail and crafting still need hands-on
  testing.
