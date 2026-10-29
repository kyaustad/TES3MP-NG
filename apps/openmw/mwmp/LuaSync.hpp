#ifndef OPENMW_MWMP_LUASYNC_HPP
#define OPENMW_MWMP_LUASYNC_HPP

#include <mutex>
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
        void onLocalEvent(const MWWorld::Ptr& target, const std::string& eventName, const std::string& data);
        void onStorageValueChanged(std::string_view section, std::string_view key, std::string data, bool persistent);
        void onStorageSectionReplaced(std::string_view section, std::string data);

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
    };
}

#endif // OPENMW_MWMP_LUASYNC_HPP
