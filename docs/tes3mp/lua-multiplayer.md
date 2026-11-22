# OpenMW Lua in TES3MP (0.51 port)

This document describes how OpenMW 0.49+ Lua scripts run and synchronize in TES3MP. It is the design that the
`mwmp::LuaSync` client code and the server's Lua relay implement.

## Constraints

* TES3MP's server is lightweight: it has no game world and never runs OpenMW Lua. It relays, arbitrates and
  persists. Its own scripting (CoreScripts) runs on LuaJIT with LuaBridge and can inspect and produce Lua data.
* OpenMW's Lua API was designed for this (see `docs/source/reference/lua-scripting/overview.rst`): local scripts
  only modify their own object, global scripts own the world, player scripts own UI/camera/input, and scripts
  communicate through serializable events and storage.
* In 0.49+, part of the engine's own behavior is written in Lua (`builtin.omwscripts`): activation, item use,
  combat, crimes, settings, console. Those scripts act on behalf of the local player and must run on every client.

## Where scripts run

| Script kind | Runs on | Synchronization |
|---|---|---|
| MENU | every client | none (menus are local) |
| PLAYER (attached to the local player) | the player's own client | none needed for UI/camera/input; world effects go through TES3MP packets |
| PLAYER scripts of other players | nowhere | other players are represented by NPCs whose state comes from the network |
| Local scripts (NPC, CREATURE, CUSTOM...) | the client that simulates the object (the cell authority) | other clients keep the scripts attached but inactive |
| GLOBAL, `client` mode | every client | like TES3MP's mwscript globals: each client runs them for its own context |
| GLOBAL, `authority` mode | only the Lua world authority | state is persisted on the server and handed over when the authority changes |

### Client-only ("no replication") mods

Mods that only use APIs whose effects are local need nothing from multiplayer and work unchanged:
`openmw.ui`, `openmw.camera`, `openmw.input`, `openmw.postprocessing`, `openmw.ambient`, `openmw.debug`,
`openmw.menu`, `storage.playerSection`, the `I.Settings` page registration, and read-only queries through
`openmw.nearby`, `openmw.types` and `openmw.core`. This covers HUD/UI mods, camera mods, input remapping,
settings menus, quality-of-life overlays and shaders. Their MENU and PLAYER scripts run on each client exactly as in
single player, and they don't create network traffic. If such a mod also has GLOBAL scripts, the default `client`
mode runs those locally too.

### Execution modes for GLOBAL scripts

The server sends a policy (CoreScripts `data/lua/scriptPolicy.json`) that maps script paths to a mode:

```json
{
    "default": "client",
    "scripts": {
        "scripts/mymod/worldspawner.lua": "authority",
        "scripts/myothermod/*": "authority"
    }
}
```

* `client` (default) keeps single-player semantics for each player. It is right for activation/use handlers,
  UI helpers and anything that reacts to the local player. Built-in scripts are always `client`.
* `authority` is for scripts that simulate the shared world once (spawners, timed world events, economy, quest
  logic shared by all players). Exactly one client, the Lua world authority, runs them.

The default is `client` because it matches how TES3MP has always treated mwscript global scripts and how most
OpenMW Lua mods are written (as if there were one player). Operators opt world-simulation mods into `authority`.

## Required Lua mods

Lua mods are enforced like plugins, through CoreScripts' `data/requiredDataFiles.json`. A mod's `.omwscripts`
manifest is a content file, and its checksum covers the manifest and every file in the directories of the scripts it
lists, so modules loaded with `require()` count too. Files are resolved across data directories the way the VFS
resolves them: later directories override earlier ones.

```json
[
    {"Morrowind.esm": ["0x7B6AF5B9", "0x34282D67"]},
    {"MyLuaMod.omwscripts": ["0x1A2B3C4D"]},
    {"AnotherLuaMod.omwscripts": []}
]
```

An empty list accepts any version of the mod. A client logs the checksum of every content file it has when it
connects (`checksum: ...` lines), and a mismatch disconnects it with the usual plugin comparison. OpenMW's own
`builtin.omwscripts` is not listed: it ships with the engine, whose version is enforced separately.

## Object identity

Lua data refers to objects by `ESM::RefNum`. On the network these become `mwmp::NetObjectId`
(`components/openmw-mp/Lua/LuaData.hpp`):

* content file objects: `(refNum index, content file)`; identical on all clients because the server enforces the
  same data files in the same order;
* objects created during the session: their server-assigned `mpNum`;
* players: their RakNet GUID.

Objects without one of these (local-only effects, objects not yet confirmed by the server) become invalid objects
on other clients.

## Events

* `core.sendGlobalEvent` delivers locally to `client` mode handlers, and is forwarded through the server to the
  authority, which delivers it to `authority` mode handlers only. The server calls `OnLuaGlobalEvent` in CoreScripts
  first, which can inspect the data (as JSON) and drop the event.
* `object:sendEvent` delivers locally when this client simulates the target; otherwise it is forwarded: to the
  owning client for players, and to the cell authority for other objects.
* Engine-generated `Hit` events for victims simulated elsewhere are not delivered locally. Attacks travel as
  TES3MP attack packets and the victim's client feeds them into `LuaManager::onHit`, so combat mods run once, where
  the victim lives.
* Server scripts can send events to clients with `tes3mp.SendLuaGlobalEvent` and `tes3mp.SendLuaPlayerEvent`.

## Storage

* `storage.globalSection` is written by global scripts and replicated from every client: the server keeps the
  latest value of every key (the last write wins), persists sections with the `Persistent` lifetime, broadcasts
  changes and sends a full snapshot to joining players. `client` mode scripts, which handle each player's actions
  on that player's client (Ashlander Architect keeps its settlements this way), share their data like `authority`
  mode scripts. A client doesn't send a value it sent or received already, so values that every client's copy of
  a script writes travel once.
  `section:reset()` becomes a reset followed by one write per key.
* Temporary sections stay on each client: they hold per-client runtime data such as settings groups.
* In multiplayer the client neither loads nor saves `global_storage.bin`: the server's snapshot replaces it.
* `storage.playerSection` stays on each client, as in single player.

## Engine handlers of global scripts

Global engine handlers (`onActivate`, `onObjectActive`, `onActorActive`, `onPlayerAdded`, `onNewExterior`...) fire
on each client for what that client's engine sees. A `client` mode script therefore sees its own player's
activations and the objects in its own loaded cells. An `authority` mode script sees what the authority sees, and
learns about other players' actions through events.

## Local script simulation

Each frame the client checks which objects with local scripts it simulates (`mwmp::LuaSync::isLocallySimulated`):

* the local player;
* actors it is the authority of;
* objects in cells it is the authority of;
* items held in inventories.

The local scripts of all other objects are made inactive. They get `onInactive`, and no `onUpdate`, timers or engine
handlers. They become active again when this client takes over the object.

## Saved script state

TES3MP doesn't use OpenMW saves.

* `authority` mode global scripts: their `onSave` data and timers are sent to the server every 30 seconds and when
  the authority changes. The new authority restores them with `onLoad`. Scripts are identified by path, so the
  state survives changes in `.omwscripts` order.
* `client` mode global scripts keep their state across authority changes on the same client, but start fresh each
  session.
* PLAYER scripts: the client sends their `onSave` data to the server every 30 seconds when it changed. The server
  stores it per character (`data/lua/players/<name>.bin`) and sends it back at login, where the scripts restore it
  with `onLoad`. A client only starts saving once it has received the stored state, so a fresh login can't
  overwrite it.
* Local scripts (NPCs, creatures, objects): the client that simulates an object sends its scripts' state when the
  object leaves the scene, and every 60 seconds when it changed. Only the cell's authority is accepted. The server
  keeps it per cell (`data/lua/localState.bin`) and sends it to every player who loads the cell, so the next client
  to simulate the object continues where the last one stopped.
* A client that crashes or is killed loses whatever changed since its last save.
* Only mods' scripts are persisted. OpenMW's own scripts (`scripts/omw/`: camera, controls, UI, AI, combat)
  mirror engine state that multiplayer servers don't persist, so they keep the state of the current session.
  Restoring a state restarts only the scripts it contains, and the others keep running.


The saved state (`onSave`) of a player's `client` mode global scripts is kept by the server for that player
(`data/lua/players/<name>.global.bin`), sent with the player state when they log in, and loaded when the global
scripts start, so mods that run per player keep their data across sessions.
## World changes made by Lua scripts

In 0.8.1, changes that mwscript makes to the world are sent with TES3MP's own packets and relayed by CoreScripts.
Lua bindings do the same (`mwmp/LuaWorldSync`): the change is applied on the client that runs the script, as in
single player, and sent with the packet the 0.8.1 server already handles. The origin is `CLIENT_SCRIPT_GLOBAL` with
the client script `lua`, so CoreScripts doesn't apply container changes a second time on the sending client.
Changes are only sent for objects in cells the server tracks, and for objects that also exist on other clients
(from a content file, or with an mpNum).

| Lua API | Packet |
|---|---|
| `world.createObject` + `teleport`, `moveInto` from a container into the world | ObjectPlace, or ObjectSpawn for actors. The server's reply gives the object its mpNum, and the client keeps the object it already placed. Scale, lock and enabled state set before the reply are sent afterwards |
| `object:teleport` | Actors: ActorCellChange. Other objects in the same cell: ObjectMove and ObjectRotate, which CoreScripts keeps (`OnObjectMove`, `OnObjectRotate`). To another cell: ObjectDelete and ObjectPlace. A move before the server answered the object's placement is sent after the answer, so placing an object and adjusting it (as Inventory Extender does when dropping items) makes one object |
| `object:remove` | ObjectDelete, or Container REMOVE for an item in a container |
| `object:moveInto(container)` | Container ADD |
| `object.enabled = ` | ObjectState |
| `object:setScale` | ObjectScale |
| `types.Lockable.lock`, `unlock`, `setTrapSpell(nil)` | ObjectLock, ObjectTrap |
| `types.Actor.setBarterGold` | ObjectMiscellaneous (gold pool) |
| `types.Actor.spells(player):add`, `remove`, `clear` | PlayerSpellbook |
| `types.NPC` faction functions on the player (rank, join, leave, reputation, expel) | PlayerFaction |
| `types.Player.quests(player)[id].stage`, `addJournalEntry`, `types.Player.addTopic` | PlayerJournal, PlayerTopic |
| `world.mwscript.getGlobalVariables()[id] = ` | ClientScriptGlobal, for the globals the server synchronizes (`synchronizedClientGlobalIds`) |

These are already synchronized without Lua hooks, because the client polls the values or the engine sends them:

* the player's stats, skills, level, bounty, position and equipment, and inventory changes;
* the stats, equipment and position of actors this client is the authority of;
* active spells (`mpOnSpellAdded`), doors opened by `types.Door.activateDoor`, and activation and item use
  (approved by the server).

Shared with the other players near the object (`EFFECT` messages, `mwmp/LuaEffects.hpp`):

* `core.sound.playSound3d`, `playSoundFile3d`, `stopSound3d`, `stopSoundFile3d`, `say`, `stopSay`;
* `openmw.animation`: `playBlended`, `playQueued`, `cancel`, `setSpeed` (at most 10 times a second for each
  object), `setLoopingEnabled`, `clearAnimationQueue`, `addVfx`, `removeVfx`, `removeAllVfx`;
* `world.vfx.spawn` and `world.vfx.remove`, for the players in the cell of the player whose script played them.

The clients that have the cell loaded play the effect too. Scripts that react to world events in `client` mode run
on every client, so a client skips an effect identical to one it played or received in the last 0.3 seconds.

Item data that global scripts set on world items (`types.Item.itemData(item).condition`, `enchantmentCharge`,
`soul`), and the owners that scripts set on world objects (`object.owner.recordId`, `factionId`, `factionRank`,
which decide what counts as theft), are sent to the players in the cell (`ITEM_DATA` messages); the server keeps
them in `data/lua/itemData.bin` and sends them with the cell. Items in inventories follow the inventory sync, which compares these values.

The server owns the game time: `world.setGameTimeScale` and `world.advanceTime` send `MP_SetGameTimeScale` and
`MP_AdvanceTime` events to the server, where CoreScripts changes the time for every player. Server scripts can
refuse them with `OnLuaGlobalEvent` validators.

Local to each client, by design:

* UI, camera, input, post-processing, ambient (non-3D) sound and music APIs;
* the weather, which the server owns: Lua changes to it are overwritten by the server's;

Global scripts that change the world should run in `authority` mode. In `client` mode, every client runs its own
copy, so each one makes and sends the same change.

## Changes to actors another client simulates

A global script call that changes an actor another client simulates (an NPC in a cell this client isn't the
authority of, or another player) is not applied here, where that client's sync would overwrite it. It is sent to
that client as a `REMOTE_CALL` message, routed like a local event, and that client makes the same call in its own
global scripts' context; its usual sync shares the result. Forwarded calls:

* `types.Actor.spells(actor)`: `add`, `remove`, `clear`
* `types.Actor.activeSpells(actor)`: `add`, `remove`
* `types.Actor.activeEffects(actor)`: `remove`, `set`, `modify`
* on other players: `types.NPC.setFactionRank`, `modifyFactionRank`, `joinFaction`, `leaveFaction`,
  `setFactionReputation`, `modifyFactionReputation`, `expel`, `clearExpelled`

Stats, equipment and AI stats can only be changed by the actor's own local scripts (`openmw.self`), which run on
the client that simulates it, so they need no forwarding. NPC disposition and the factions of NPCs aren't
synchronized by TES3MP at all; they stay local to the client that changes them.

## Other players

`world.players` holds every player, the local one first, and `nearby.players` those in the active cells. Other
players are TES3MP DedicatedPlayers, which the engine treats as NPCs (they get NPC scripts, not PLAYER scripts), but
scripts see them as players: `object.type` is `types.Player` and `types.Player.objectIsInstance` is true, so mods can
tell players from NPCs, send them events (delivered to their own client) and target them with AI packages.
`types.Player` functions that only make sense for the local player (quests, controls, UI modes, crime level)
still refuse other players.

## Records created by Lua

`world.createRecord` records are shared: the creating client sends the record to the server as an ESM file (a
`RECORD` message, like a saved game's records), the server keeps it in `data/lua/records.bin` and relays it, and
sends all of them to each client right after the handshake, before the player's data that may use them. Objects
and items of these records are then synchronized like any other.

* Ids can't clash: with the handshake, the server gives each connection its own block of 2^32 generated ids
  (`Generated:0x100000000` and up for the first one), and keeps counting blocks across restarts.
* A record with the same type and content as a shared one is that record: `createRecord` returns it instead of
  making another. Mods that create their records each time the game starts, on every client, get the same ids
  everywhere and don't add copies on the server.
* Generated ids that aren't shared (records made before the server's id block arrived) stay local to the client,
  as before: objects and items of them aren't sent.

## Wire format

A single `ID_LUA_MESSAGE` worldstate packet carries a list of messages: `kind`, `flags`, `name`, `key`, `target`,
`cell` and a binary payload. Events and storage values use OpenMW's Lua serialization format with network object ids.
The authority's saved state is a list of `(path, data, timers)` entries, and the server stores it as an opaque blob.
