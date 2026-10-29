#ifndef OPENMW_MWMP_LUAWORLDSYNC_HPP
#define OPENMW_MWMP_LUAWORLDSYNC_HPP

#include <functional>
#include <string>
#include <string_view>

#include <components/esm/refid.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/openmw-mp/Base/BaseObject.hpp>

#include "../mwworld/ptr.hpp"

namespace MWWorld
{
    class CellStore;
}

namespace mwmp
{
    /*
        Changes that OpenMW Lua scripts make to the game world, reported with TES3MP's own packets, like 0.8.1 does for
        mwscript (see docs/tes3mp/lua-multiplayer.md). The change is applied on the client that runs the script, as
        in single player, and the server relays it to the other players. The packets have a client script origin, so
        CoreScripts doesn't apply container changes a second time on the sending client.

        Functions taking a Ptr must be called from the main thread, where the Lua bindings apply their delayed
        actions. Bindings that change the world immediately from the Lua thread use post().
    */
    namespace LuaWorldSync
    {
        // In multiplayer, once the local player is logged in
        bool isActive();

        // Runs fn on the main thread, at the next mwmp::Main::frame()
        void post(std::function<void()> fn);
        void flush();

        // Objects in the world
        void objectEnabled(const MWWorld::Ptr& ptr, bool enabled);
        void objectScaled(const MWWorld::Ptr& ptr, float scale);
        void objectRemoved(const MWWorld::Ptr& ptr);
        void objectPlaced(const MWWorld::Ptr& ptr);

        // Where an object was before a teleport
        struct Location
        {
            bool mSynced = false; // the object exists on other clients: from a content file or with an mpNum
            bool mInWorld = false;
            bool mIsActor = false;
            ESM::Cell mCell;
            BaseObject mObject;
        };
        Location capture(const MWWorld::Ptr& ptr);
        void objectMoved(const Location& from, const MWWorld::Ptr& to);

        // Containers and inventories of objects other than players
        void containerChanged(const MWWorld::Ptr& container, const MWWorld::Ptr& item, int count, bool added);

        void lockChanged(const MWWorld::Ptr& ptr, int lockLevel);
        void trapDisarmed(const MWWorld::Ptr& ptr);
        void barterGold(const MWWorld::Ptr& ptr, int gold);

        // The local player's character
        void playerSpell(const MWWorld::Ptr& actor, const ESM::RefId& spellId, bool added);
        void playerFaction(const ESM::RefId& factionId);
        void playerJournalEntry(const ESM::RefId& questId, int index, const MWWorld::Ptr& actor);
        void playerJournalIndex(const ESM::RefId& questId, int index);
        void playerTopic(const ESM::RefId& topicId);

        // mwscript global variables that the server synchronizes (see Main::isValidPacketGlobal)
        void globalVariable(std::string_view globalId);

        // Objects placed by Lua scripts on this client get their mpNum from the server's reply to their placement:
        // the object that was already placed is returned, instead of creating a second one
        MWWorld::Ptr claimPlacement(
            const std::string& refId, const MWWorld::CellStore* cellStore, const ESM::Position& position);
    }
}

#endif // OPENMW_MWMP_LUAWORLDSYNC_HPP
