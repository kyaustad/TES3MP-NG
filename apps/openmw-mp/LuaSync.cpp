#include "LuaSync.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

#include <components/files/conversion.hpp>
#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/Lua/LuaData.hpp>

#include "Cell.hpp"
#include "CellController.hpp"
#include "Networking.hpp"
#include "Player.hpp"
#include "Script/Script.hpp"

using mwmp::LuaMessage;

std::string LuaSync::sDataDirectory;
std::string LuaSync::sPolicy;
RakNet::RakNetGUID LuaSync::sAuthority;
bool LuaSync::sHasAuthority = false;
std::map<std::string, std::map<std::string, std::string>> LuaSync::sStorage;
std::map<std::string, bool> LuaSync::sPersistentSections;
std::string LuaSync::sGlobalState;
bool LuaSync::sDropCurrentEvent = false;

namespace
{
    constexpr const char* sDefaultPolicy = "{\"default\": \"client\", \"scripts\": {}}";

    std::filesystem::path luaDirectory(const std::string& dataDirectory)
    {
        return Files::pathFromUnicodeString(dataDirectory) / "lua";
    }

    void writeString(std::ostream& out, const std::string& value)
    {
        const uint32_t size = static_cast<uint32_t>(value.size());
        out.write(reinterpret_cast<const char*>(&size), sizeof(size));
        out.write(value.data(), size);
    }

    bool readString(std::istream& in, std::string& value)
    {
        uint32_t size = 0;
        if (!in.read(reinterpret_cast<char*>(&size), sizeof(size)) || size > 64 * 1024 * 1024)
            return false;
        value.resize(size);
        return static_cast<bool>(in.read(value.data(), size));
    }

    std::string readFile(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return {};
        std::ostringstream content;
        content << in.rdbuf();
        return content.str();
    }

    mwmp::NetObjectId playerTarget(const Player& player)
    {
        mwmp::NetObjectId id;
        id.kind = mwmp::NetObjectId::PLAYER;
        id.guid = player.guid.g;
        return id;
    }

    bool isLoaded(Player* player)
    {
        return player != nullptr && player->isHandshaked() && player->getLoadState() == Player::POSTLOADED;
    }
}

void LuaSync::init(const std::string& dataDirectory)
{
    sDataDirectory = dataDirectory;
    std::error_code ec;
    std::filesystem::create_directories(luaDirectory(dataDirectory), ec);

    reloadPolicy();

    // Global storage: sections, each with a persistent flag and its keys
    std::ifstream storage(luaDirectory(dataDirectory) / "globalStorage.bin", std::ios::binary);
    uint32_t sectionCount = 0;
    if (storage && storage.read(reinterpret_cast<char*>(&sectionCount), sizeof(sectionCount)))
    {
        for (uint32_t i = 0; i < sectionCount; ++i)
        {
            std::string section;
            uint32_t keyCount = 0;
            if (!readString(storage, section) || !storage.read(reinterpret_cast<char*>(&keyCount), sizeof(keyCount)))
                break;
            sPersistentSections[section] = true;
            for (uint32_t k = 0; k < keyCount; ++k)
            {
                std::string key, value;
                if (!readString(storage, key) || !readString(storage, value))
                    break;
                sStorage[section][key] = value;
            }
        }
    }

    sGlobalState = readFile(luaDirectory(dataDirectory) / "globalState.bin");

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Lua sync: %zu global storage sections, %zu bytes of global script state",
        sStorage.size(), sGlobalState.size());
}

void LuaSync::reloadPolicy()
{
    sPolicy = readFile(luaDirectory(sDataDirectory) / "scriptPolicy.json");
    if (sPolicy.empty())
        sPolicy = sDefaultPolicy;
}

void LuaSync::send(std::vector<LuaMessage>&& messages, Player* recipient, bool toEveryoneElse)
{
    if (recipient == nullptr || messages.empty())
        return;

    mwmp::BaseWorldstate worldstate;
    worldstate.guid = recipient->guid;
    worldstate.luaMessages = std::move(messages);

    mwmp::WorldstatePacket* packet
        = mwmp::Networking::get().getWorldstatePacketController()->GetPacket(ID_LUA_MESSAGE);
    packet->setWorldstate(&worldstate);
    packet->Send(toEveryoneElse);
}

void LuaSync::broadcast(std::vector<LuaMessage>&& messages, Player* except)
{
    for (auto& [guid, player] : *Players::getPlayers())
    {
        if (player == except || !isLoaded(player))
            continue;
        auto copy = messages;
        send(std::move(copy), player);
    }
}

void LuaSync::onPlayerLoaded(Player& player)
{
    std::vector<LuaMessage> messages;

    LuaMessage config;
    config.kind = LuaMessage::CONFIG;
    config.name = sPolicy;
    messages.push_back(config);

    for (const auto& [section, values] : sStorage)
    {
        for (const auto& [key, value] : values)
        {
            LuaMessage set;
            set.kind = LuaMessage::STORAGE_SET;
            set.name = section;
            set.key = key;
            set.data = value;
            messages.push_back(std::move(set));
        }
    }

    send(std::move(messages), &player);

    if (!sHasAuthority || !isLoaded(Players::getPlayer(sAuthority)))
        electAuthority(nullptr);
    else
    {
        LuaMessage authority;
        authority.kind = LuaMessage::AUTHORITY;
        authority.target = playerTarget(*Players::getPlayer(sAuthority));
        send({ authority }, &player);
    }
}

void LuaSync::onPlayerDisconnect(Player& player)
{
    if (sHasAuthority && player.guid == sAuthority)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Lua world authority %s disconnected", player.npc.mName.c_str());
        sHasAuthority = false;
        electAuthority(&player);
    }
}

void LuaSync::electAuthority(Player* except)
{
    for (auto& [guid, candidate] : *Players::getPlayers())
    {
        if (candidate == except || !isLoaded(candidate))
            continue;
        setAuthority(candidate);
        return;
    }

    sHasAuthority = false;
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "No player can be the Lua world authority right now");
}

void LuaSync::setAuthority(Player* player)
{
    if (!isLoaded(player))
        return;

    sAuthority = player->guid;
    sHasAuthority = true;

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "%s (%u) is now the Lua world authority", player->npc.mName.c_str(),
        player->getId());

    // The new authority starts authority mode scripts from the last state we have
    LuaMessage state;
    state.kind = LuaMessage::GLOBAL_STATE;
    state.data = sGlobalState;
    send({ state }, player);

    announceAuthority();

    Script::Call<Script::CallbackIdentity("OnLuaAuthorityChange")>(player->getId());
}

void LuaSync::announceAuthority()
{
    Player* authority = Players::getPlayer(sAuthority);
    if (authority == nullptr)
        return;

    LuaMessage message;
    message.kind = LuaMessage::AUTHORITY;
    message.target = playerTarget(*authority);
    broadcast({ message }, nullptr);
}

int LuaSync::getAuthorityPid()
{
    if (!sHasAuthority)
        return -1;
    Player* authority = Players::getPlayer(sAuthority);
    return authority != nullptr ? authority->getId() : -1;
}

void LuaSync::processMessages(Player& sender, const mwmp::BaseWorldstate& worldstate)
{
    const bool fromAuthority = sHasAuthority && sender.guid == sAuthority;

    for (const LuaMessage& message : worldstate.luaMessages)
    {
        switch (message.kind)
        {
            case LuaMessage::GLOBAL_EVENT:
            {
                sDropCurrentEvent = false;

                std::string json;
                try
                {
                    json = mwmp::LuaData::toJson(message.data);
                }
                catch (const std::exception& e)
                {
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Dropping Lua global event %s from %s: %s",
                        message.name.c_str(), sender.npc.mName.c_str(), e.what());
                    break;
                }

                Script::Call<Script::CallbackIdentity("OnLuaGlobalEvent")>(
                    sender.getId(), message.name.c_str(), json.c_str());

                if (sDropCurrentEvent || fromAuthority || !sHasAuthority)
                    break;

                // The sender's client mode scripts already handled it; authority mode scripts are on the authority
                LuaMessage forward = message;
                forward.flags |= LuaMessage::FLAG_AUTHORITY_ONLY;
                send({ forward }, Players::getPlayer(sAuthority));
                break;
            }
            case LuaMessage::LOCAL_EVENT:
                routeLocalEvent(sender, message);
                break;
            case LuaMessage::STORAGE_SET:
            {
                if (!fromAuthority)
                {
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                        "Ignoring global storage change %s.%s from %s, who isn't the Lua world authority",
                        message.name.c_str(), message.key.c_str(), sender.npc.mName.c_str());
                    break;
                }

                if (message.data.empty())
                    sStorage[message.name].erase(message.key);
                else
                    sStorage[message.name][message.key] = message.data;
                sPersistentSections[message.name] = (message.flags & LuaMessage::FLAG_PERSISTENT) != 0;

                broadcast({ message }, &sender);
                persistStorage();
                break;
            }
            case LuaMessage::STORAGE_RESET:
            {
                if (!fromAuthority)
                    break;
                sStorage.erase(message.name);
                broadcast({ message }, &sender);
                persistStorage();
                break;
            }
            case LuaMessage::GLOBAL_STATE:
            {
                if (!fromAuthority)
                    break;
                sGlobalState = message.data;
                persistState();
                break;
            }
            default:
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Unexpected Lua message kind %u from %s", message.kind,
                    sender.npc.mName.c_str());
                break;
        }
    }
}

void LuaSync::routeLocalEvent(Player& sender, const LuaMessage& message)
{
    Player* recipient = nullptr;

    if (message.target.kind == mwmp::NetObjectId::PLAYER)
    {
        RakNet::RakNetGUID guid;
        guid.g = message.target.guid;
        recipient = Players::getPlayer(guid);
    }
    else if (!message.cell.empty())
    {
        // Cells are identified by their short description: "x, y" for exteriors, the name for interiors
        static const std::regex exterior(R"(^\s*(-?\d+)\s*,\s*(-?\d+)\s*$)");
        std::smatch match;
        Cell* cell = std::regex_match(message.cell, match, exterior)
            ? CellController::get()->getCellByXY(std::stoi(match[1]), std::stoi(match[2]))
            : CellController::get()->getCellByName(message.cell);

        if (cell != nullptr && cell->getAuthority() != nullptr)
            recipient = Players::getPlayer(*cell->getAuthority());
    }

    if (!isLoaded(recipient))
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE, "Dropping Lua event %s for %s: nobody simulates it",
            message.name.c_str(), message.target.toString().c_str());
        return;
    }

    send({ message }, recipient);
}

void LuaSync::sendGlobalEvent(const std::string& eventName, const std::string& jsonData)
{
    LuaMessage message;
    message.kind = LuaMessage::GLOBAL_EVENT;
    message.flags = LuaMessage::FLAG_FROM_SERVER;
    message.name = eventName;
    message.data = mwmp::LuaData::fromJson(jsonData);
    broadcast({ message }, nullptr);
}

void LuaSync::sendPlayerEvent(Player& player, const std::string& eventName, const std::string& jsonData)
{
    LuaMessage message;
    message.kind = LuaMessage::LOCAL_EVENT;
    message.flags = LuaMessage::FLAG_FROM_SERVER;
    message.name = eventName;
    message.target = playerTarget(player);
    message.data = mwmp::LuaData::fromJson(jsonData);
    send({ message }, &player);
}

std::string LuaSync::getStorageValue(const std::string& section, const std::string& key)
{
    auto sectionIt = sStorage.find(section);
    if (sectionIt == sStorage.end())
        return "null";
    auto keyIt = sectionIt->second.find(key);
    if (keyIt == sectionIt->second.end())
        return "null";
    return mwmp::LuaData::toJson(keyIt->second);
}

void LuaSync::setStorageValue(const std::string& section, const std::string& key, const std::string& jsonData)
{
    LuaMessage message;
    message.kind = LuaMessage::STORAGE_SET;
    message.flags = LuaMessage::FLAG_FROM_SERVER | LuaMessage::FLAG_PERSISTENT;
    message.name = section;
    message.key = key;
    message.data = mwmp::LuaData::fromJson(jsonData);

    if (message.data.empty())
        sStorage[section].erase(key);
    else
        sStorage[section][key] = message.data;
    sPersistentSections[section] = true;

    broadcast({ message }, nullptr);
    persistStorage();
}

void LuaSync::persistStorage()
{
    const std::filesystem::path path = luaDirectory(sDataDirectory) / "globalStorage.bin";
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return;

    uint32_t sectionCount = 0;
    for (const auto& [section, values] : sStorage)
        sectionCount += sPersistentSections[section] ? 1 : 0;
    out.write(reinterpret_cast<const char*>(&sectionCount), sizeof(sectionCount));

    for (const auto& [section, values] : sStorage)
    {
        if (!sPersistentSections[section])
            continue;
        writeString(out, section);
        const uint32_t keyCount = static_cast<uint32_t>(values.size());
        out.write(reinterpret_cast<const char*>(&keyCount), sizeof(keyCount));
        for (const auto& [key, value] : values)
        {
            writeString(out, key);
            writeString(out, value);
        }
    }
}

void LuaSync::persistState()
{
    std::ofstream out(luaDirectory(sDataDirectory) / "globalState.bin", std::ios::binary | std::ios::trunc);
    out.write(sGlobalState.data(), static_cast<std::streamsize>(sGlobalState.size()));
}
