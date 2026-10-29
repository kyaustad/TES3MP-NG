#include "Lua.hpp"

#include <string>

#include <components/openmw-mp/TimedLog.hpp>

#include <apps/openmw-mp/LuaSync.hpp>
#include <apps/openmw-mp/Player.hpp>
#include <apps/openmw-mp/Script/ScriptFunctions.hpp>

void LuaFunctions::SendLuaGlobalEvent(const char* eventName, const char* jsonData) noexcept
{
    try
    {
        LuaSync::sendGlobalEvent(eventName, jsonData);
    }
    catch (const std::exception& e)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "SendLuaGlobalEvent(%s): %s", eventName, e.what());
    }
}

void LuaFunctions::SendLuaPlayerEvent(unsigned short pid, const char* eventName, const char* jsonData) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, );

    try
    {
        LuaSync::sendPlayerEvent(*player, eventName, jsonData);
    }
    catch (const std::exception& e)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "SendLuaPlayerEvent(%s): %s", eventName, e.what());
    }
}

void LuaFunctions::DropLuaEvent() noexcept
{
    LuaSync::dropCurrentEvent();
}

int LuaFunctions::GetLuaAuthority() noexcept
{
    return LuaSync::getAuthorityPid();
}

void LuaFunctions::SetLuaAuthority(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, );

    LuaSync::setAuthority(player);
}

const char* LuaFunctions::GetLuaStorageValue(const char* section, const char* key) noexcept
{
    static std::string result;
    try
    {
        result = LuaSync::getStorageValue(section, key);
    }
    catch (const std::exception& e)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "GetLuaStorageValue(%s, %s): %s", section, key, e.what());
        result = "null";
    }
    return result.c_str();
}

void LuaFunctions::SetLuaStorageValue(const char* section, const char* key, const char* jsonData) noexcept
{
    try
    {
        LuaSync::setStorageValue(section, key, jsonData);
    }
    catch (const std::exception& e)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "SetLuaStorageValue(%s, %s): %s", section, key, e.what());
    }
}

void LuaFunctions::ReloadLuaScriptPolicy() noexcept
{
    LuaSync::reloadPolicy();
}
