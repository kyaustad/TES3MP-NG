#ifndef OPENMW_MWMP_LUASYNC_HPP
#define OPENMW_MWMP_LUASYNC_HPP

#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <components/esm3/cellref.hpp>
#include <components/openmw-mp/Base/BaseWorldstate.hpp>

#include "../mwworld/ptr.hpp"

namespace mwmp
{
    /*
        Client side of the OpenMW Lua synchronization, see docs/tes3mp/lua-multiplayer.md

        MWLua::LuaManager asks this class which scripts may run here and hands it events that have to leave this
        client; messages from the server are dispatched back into MWLua::LuaManager.
    */
    class LuaSync
    {
    public:
        enum class Mode
        {
            Client,
            Authority
        };

        static LuaSync& get();

        // Whether multiplayer Lua synchronization applies (false in single player)
        static bool isEnabled();

        // Global script execution modes, from the server's script policy
        Mode getGlobalScriptMode(std::string_view scriptPath) const;
        bool hasAuthorityModeScripts() const { return !mAuthorityPatterns.empty(); }
        bool hasConfig() const { return mHasConfig; }
        bool isAuthority() const { return mIsAuthority; }

        // Whether this client runs the local scripts of an object (the local player, actors and objects in cells
        // this client is the authority of)
        static bool isLocallySimulated(const MWWorld::Ptr& ptr);

        // Object identity across clients
        NetObjectId toNetObject(ESM::RefNum refNum) const;
        ESM::RefNum toLocalObject(const NetObjectId& id) const;

        // Outgoing, called by MWLua; data is in OpenMW's Lua serialization format with local RefNums
        void onGlobalEvent(const std::string& eventName, const std::string& data);
        // An event for the server's scripts only (names start with "MP_"), such as changes to the game time, which
        // the server owns
        void sendServerEvent(const std::string& eventName, const std::string& data);
        void onLocalEvent(const MWWorld::Ptr& target, const std::string& eventName, const std::string& data);
        // A global script call for an actor another client simulates, see LuaRemoteCall.hpp
        void onRemoteCall(const MWWorld::Ptr& target, const std::string& op, const std::string& data);
        // A sound, visual effect or animation played here, see LuaEffects.hpp; `target` may be empty
        void onEffect(const MWWorld::Ptr& target, const std::string& op, const std::string& data);
        // A world item's condition, enchantment charge or soul set by a global script; `data` is the value
        void onItemDataChanged(const MWWorld::Ptr& item, std::string_view property, const std::string& data);
        // An object's owner set by a global script (owner.recordId, owner.factionId as ids, owner.factionRank as a
        // number; empty clears it), shared and kept like item data
        static void onOwnerChanged(const MWWorld::Ptr& object, std::string_view property, std::string value);
        void onStorageValueChanged(std::string_view section, std::string_view key, std::string data, bool persistent);
        void onStorageSectionReplaced(std::string_view section, std::string data);
        // State of an object's local scripts, in network form, saved by MWLua when the object leaves the scene
        void onLocalScriptsSaved(const MWWorld::Ptr& ptr, std::string data);

        // Records created by Lua (world.createRecord) are shared with every client and kept by the server. A record
        // whose content matches one that exists already is that record, so scripts that create their records each
        // time the game starts don't fill the server with copies.
        bool sharesRecords() const;
        std::optional<ESM::RefId> findSharedRecord(const std::string& contentHash) const;
        void onRecordCreated(const ESM::RefId& id, const std::string& contentHash, std::string data);
        // Whether a record id only exists on this client: a generated id that isn't a shared Lua record
        static bool isLocalOnlyRecordId(const ESM::RefId& id);
        static bool isLocalOnlyRecordId(std::string_view id);

        // Lua data with local RefNums <-> network form with NetObjectIds
        std::string dataToNetwork(std::string_view data) const;
        std::string dataToLocal(std::string_view data) const;

        // Incoming, called by the worldstate processor. Messages are only queued here: they are applied by
        // updateLua(), which MWLua::LuaManager calls from the thread that runs Lua.
        void receive(const std::vector<LuaMessage>& messages);
        void updateLua(float dt);

        // Called every frame by mwmp::Main; sends the messages queued since the last frame
        void update(float dt);

        void clear();

    private:
        void applyConfig(const std::string& policyJson);
        void queue(LuaMessage&& message);
        void sendGlobalState();
        void addSharedRecord(const LuaMessage& message);

        bool mHasConfig = false;
        bool mIsAuthority = false;
        bool mStartedGlobalScripts = false;
        Mode mDefaultMode = Mode::Client;
        std::vector<std::pair<std::string, Mode>> mPatterns;
        std::vector<std::string> mAuthorityPatterns;
        std::mutex mMutex; // guards mIncoming and mOutgoing
        std::vector<LuaMessage> mIncoming;
        std::vector<LuaMessage> mOutgoing;
        std::string mPendingAuthorityState;
        bool mHasPendingAuthorityState = false;
        float mStateSaveTimer = 0.f;
        bool mPlayerStateReceived = false; // player script state is saved only after the server sent the stored one
        float mPlayerStateTimer = 0.f;
        float mLocalStateTimer = 0.f;
        std::string mLastPlayerState;
        std::map<std::string, std::size_t> mLastLocalStates; // object -> hash of the last sent state
        // Global storage: section + key -> hash of the value sent or received last, so values that every client's
        // copy of a client mode script writes are sent once
        std::map<std::string, std::size_t> mStorageHashes;
        std::string mPendingClientGlobalState;
        bool mHasPendingClientGlobalState = false;
        std::string mLastClientGlobalState;
        mutable std::mutex mRecordMutex; // guards the shared records, used by Lua and the network
        bool mHasRecordBlock = false;
        std::set<ESM::RefId> mSharedRecords;
        std::map<std::string, ESM::RefId> mSharedRecordsByContent;
    };
}

#endif // OPENMW_MWMP_LUASYNC_HPP
