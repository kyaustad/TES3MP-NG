#ifndef OPENMW_LUAAPI_HPP
#define OPENMW_LUAAPI_HPP

#include "../Types.hpp"

/*
    Server script functions for the OpenMW Lua synchronization, see docs/tes3mp/lua-multiplayer.md

    Lua data is exchanged as JSON: tables become objects or arrays, vectors and colors become
    {"__type": "vec3", "v": [x, y, z]} and game objects {"__type": "object", ...}.
*/
#define LUAAPI \
    {"SendLuaGlobalEvent",        LuaFunctions::SendLuaGlobalEvent},\
    {"SendLuaPlayerEvent",        LuaFunctions::SendLuaPlayerEvent},\
    {"DropLuaEvent",              LuaFunctions::DropLuaEvent},\
    {"GetLuaAuthority",           LuaFunctions::GetLuaAuthority},\
    {"SetLuaAuthority",           LuaFunctions::SetLuaAuthority},\
    {"GetLuaStorageValue",        LuaFunctions::GetLuaStorageValue},\
    {"SetLuaStorageValue",        LuaFunctions::SetLuaStorageValue},\
    {"ReloadLuaScriptPolicy",     LuaFunctions::ReloadLuaScriptPolicy}

class LuaFunctions
{
public:

    /**
    * \brief Send an OpenMW Lua global event to every player, as if a script had called core.sendGlobalEvent.
    *
    * \param eventName The name of the event.
    * \param jsonData The event's data as JSON, or "null".
    */
    static void SendLuaGlobalEvent(const char* eventName, const char* jsonData) noexcept;

    /**
    * \brief Send an OpenMW Lua event to a player's own scripts, as if a script had called player:sendEvent.
    */
    static void SendLuaPlayerEvent(unsigned short pid, const char* eventName, const char* jsonData) noexcept;

    /**
    * \brief Stop the Lua global event being handled by OnLuaGlobalEvent from reaching the Lua world authority.
    */
    static void DropLuaEvent() noexcept;

    /**
    * \brief Get the pid of the player who runs authority mode global scripts, or -1.
    */
    static int GetLuaAuthority() noexcept;

    /**
    * \brief Make a player run authority mode global scripts.
    */
    static void SetLuaAuthority(unsigned short pid) noexcept;

    /**
    * \brief Get a value of OpenMW Lua global storage as JSON, or "null".
    */
    static const char* GetLuaStorageValue(const char* section, const char* key) noexcept;

    /**
    * \brief Set a value of OpenMW Lua global storage from JSON ("null" removes it) and send it to every player.
    */
    static void SetLuaStorageValue(const char* section, const char* key, const char* jsonData) noexcept;

    /**
    * \brief Reload data/lua/scriptPolicy.json; it applies to players who connect afterwards.
    */
    static void ReloadLuaScriptPolicy() noexcept;
};

#endif //OPENMW_LUAAPI_HPP
