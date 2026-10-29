#ifndef OPENMW_LUASYNC_HPP
#define OPENMW_LUASYNC_HPP

#include <map>
#include <string>
#include <vector>

#include <RakNetTypes.h>
#include <components/openmw-mp/WinMacroGuard.hpp>

#include <components/openmw-mp/Base/BaseWorldstate.hpp>

class Player;

/*
    Server side of the OpenMW Lua synchronization, see docs/tes3mp/lua-multiplayer.md

    The server never runs OpenMW Lua. It elects the Lua world authority (the client that runs authority mode global
    scripts), relays events between clients, keeps replicated global storage and the saved state of authority mode
    scripts, and lets server scripts take part through callbacks and script functions.
*/
class LuaSync
{
public:
    static void init(const std::string& dataDirectory);

    static void onPlayerLoaded(Player& player);
    static void onPlayerDisconnect(Player& player);

    static void processMessages(Player& sender, const mwmp::BaseWorldstate& worldstate);

    // Server script API
    static void sendGlobalEvent(const std::string& eventName, const std::string& jsonData);
    static void sendPlayerEvent(Player& player, const std::string& eventName, const std::string& jsonData);
    static int getAuthorityPid();
    static void setAuthority(Player* player);
    static void dropCurrentEvent() { sDropCurrentEvent = true; }
    static std::string getStorageValue(const std::string& section, const std::string& key);
    static void setStorageValue(const std::string& section, const std::string& key, const std::string& jsonData);
    static void reloadPolicy();

private:
    static void send(std::vector<mwmp::LuaMessage>&& messages, Player* recipient, bool toEveryoneElse = false);
    static void broadcast(std::vector<mwmp::LuaMessage>&& messages, Player* except);
    static void electAuthority(Player* except);
    static void announceAuthority();
    static void routeLocalEvent(Player& sender, const mwmp::LuaMessage& message);
    static void persistStorage();
    static void persistState();

    static std::string sDataDirectory;
    static std::string sPolicy;
    static RakNet::RakNetGUID sAuthority;
    static bool sHasAuthority;
    static std::map<std::string, std::map<std::string, std::string>> sStorage; // section -> key -> value
    static std::map<std::string, bool> sPersistentSections;
    static std::string sGlobalState;
    static bool sDropCurrentEvent;
};

#endif // OPENMW_LUASYNC_HPP
