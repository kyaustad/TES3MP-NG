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

* `storage.globalSection` is written by global scripts. Writes on the authority are replicated: the server keeps the
  latest value of every key, persists sections with the `Persistent` lifetime, broadcasts changes and sends a full
  snapshot to joining players.
  `section:reset()` becomes a reset followed by one write per key.
* Writes by `client` mode global scripts on other clients stay on that client and are overwritten by the next
  replicated value. Mods that share state between players need an `authority` mode script to own it.
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
* Player and local script state is not persisted between sessions yet.

## Wire format

A single `ID_LUA_MESSAGE` worldstate packet carries a list of messages: `kind`, `flags`, `name`, `key`, `target`,
`cell` and a binary payload. Events and storage values use OpenMW's Lua serialization format with network object ids.
The authority's saved state is a list of `(path, data, timers)` entries, and the server stores it as an opaque blob.
