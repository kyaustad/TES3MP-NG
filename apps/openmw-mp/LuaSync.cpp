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
std::map<std::string, std::map<std::string, std::pair<mwmp::NetObjectId, std::string>>> LuaSync::sLocalStates;
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

    void writeNetObject(std::ostream& out, const mwmp::NetObjectId& id)
    {
        const uint8_t kind = id.kind;
        out.write(reinterpret_cast<const char*>(&kind), sizeof(kind));
        out.write(reinterpret_cast<const char*>(&id.refNumIndex), sizeof(id.refNumIndex));
        out.write(reinterpret_cast<const char*>(&id.contentFile), sizeof(id.contentFile));
        out.write(reinterpret_cast<const char*>(&id.mpNum), sizeof(id.mpNum));
        out.write(reinterpret_cast<const char*>(&id.guid), sizeof(id.guid));
    }

    bool readNetObject(std::istream& in, mwmp::NetObjectId& id)
    {
        uint8_t kind = 0;
        in.read(reinterpret_cast<char*>(&kind), sizeof(kind));
        in.read(reinterpret_cast<char*>(&id.refNumIndex), sizeof(id.refNumIndex));
        in.read(reinterpret_cast<char*>(&id.contentFile), sizeof(id.contentFile));
        in.read(reinterpret_cast<char*>(&id.mpNum), sizeof(id.mpNum));
        in.read(reinterpret_cast<char*>(&id.guid), sizeof(id.guid));
        id.kind = static_cast<mwmp::NetObjectId::Kind>(kind);
        return static_cast<bool>(in);
    }

    std::string lowerCase(std::string value)
    {
        for (char& c : value)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return value;
    }
}

void LuaSync::init(const std::string& dataDirectory)
{
    sDataDirectory = dataDirectory;
    std::error_code ec;
    std::filesystem::create_directories(luaDirectory(dataDirectory), ec);

    reloadPolicy();

    // Local script states: cell count, then for each cell its description and objects
    {
        std::ifstream in(luaDirectory(dataDirectory) / "localState.bin", std::ios::binary);
        uint32_t cellCount = 0;
        if (in && in.read(reinterpret_cast<char*>(&cellCount), sizeof(cellCount)))
        {
            for (uint32_t i = 0; i < cellCount; ++i)
            {
                std::string cell;
                uint32_t objectCount = 0;
                if (!readString(in, cell) || !in.read(reinterpret_cast<char*>(&objectCount), sizeof(objectCount)))
                    break;
                for (uint32_t o = 0; o < objectCount; ++o)
                {
                    mwmp::NetObjectId id;
                    std::string state;
                    if (!readNetObject(in, id) || !readString(in, state))
                        break;
                    sLocalStates[cell][id.toString()] = { id, state };
                }
            }
        }
    }

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

    // The player's own PLAYER script state; empty if there is none yet, so the client knows it may start saving
    LuaMessage playerState;
    playerState.kind = LuaMessage::PLAYER_STATE;
    playerState.data = readFile(playerStatePath(player));
    messages.push_back(std::move(playerState));

    send(std::move(messages), &player);

    for (const Cell* cell : *player.getCells())
        sendLocalStates(player, cell->getShortDescription());

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

void LuaSync::onCellLoaded(Player& player, const std::string& cellDescription)
{
    if (isLoaded(&player))
        sendLocalStates(player, cellDescription);
}

void LuaSync::sendLocalStates(Player& player, const std::string& cellDescription)
{
    auto it = sLocalStates.find(lowerCase(cellDescription));
    if (it == sLocalStates.end() || it->second.empty())
        return;

    std::vector<LuaMessage> messages;
    for (const auto& [key, entry] : it->second)
    {
        LuaMessage state;
        state.kind = LuaMessage::LOCAL_STATE;
        state.target = entry.first;
        state.cell = cellDescription;
        state.data = entry.second;
        messages.push_back(std::move(state));
    }
    send(std::move(messages), &player);
}

std::filesystem::path LuaSync::playerStatePath(const Player& player)
{
    // Account names are player names, which CoreScripts also uses for its player files
    std::string name = lowerCase(player.npc.mName);
    for (char& c : name)
    {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_' && c != ' ')
            c = '_';
    }
    return luaDirectory(sDataDirectory) / "players" / (name + ".bin");
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
            case LuaMessage::PLAYER_STATE:
            {
                const std::filesystem::path path = playerStatePath(sender);
                std::error_code ec;
                std::filesystem::create_directories(path.parent_path(), ec);
                std::ofstream out(path, std::ios::binary | std::ios::trunc);
                out.write(message.data.data(), static_cast<std::streamsize>(message.data.size()));
                break;
            }
            case LuaMessage::LOCAL_STATE:
            {
                // Only the client that simulates the cell has the current state of its objects
                if (getCellAuthority(message.cell) != &sender)
                {
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE,
                        "Ignoring Lua state of %s in %s from %s, who isn't the cell authority",
                        message.target.toString().c_str(), message.cell.c_str(), sender.npc.mName.c_str());
                    break;
                }
                auto& cellStates = sLocalStates[lowerCase(message.cell)];
                if (message.data.empty())
                    cellStates.erase(message.target.toString());
                else
                    cellStates[message.target.toString()] = { message.target, message.data };
                persistLocalStates();
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
        recipient = getCellAuthority(message.cell);

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

Player* LuaSync::getCellAuthority(const std::string& cellDescription)
{
    // Cells are identified by their short description: "x, y" for exteriors, the name for interiors
    static const std::regex exterior(R"(^\s*(-?\d+)\s*,\s*(-?\d+)\s*$)");
    std::smatch match;
    Cell* cell = std::regex_match(cellDescription, match, exterior)
        ? CellController::get()->getCellByXY(std::stoi(match[1]), std::stoi(match[2]))
        : CellController::get()->getCellByName(cellDescription);

    if (cell == nullptr || cell->getAuthority() == nullptr)
        return nullptr;
    return Players::getPlayer(*cell->getAuthority());
}

void LuaSync::persistLocalStates()
{
    std::ofstream out(luaDirectory(sDataDirectory) / "localState.bin", std::ios::binary | std::ios::trunc);
    if (!out)
        return;
    const uint32_t cellCount = static_cast<uint32_t>(sLocalStates.size());
    out.write(reinterpret_cast<const char*>(&cellCount), sizeof(cellCount));
    for (const auto& [cell, objects] : sLocalStates)
    {
        writeString(out, cell);
        const uint32_t objectCount = static_cast<uint32_t>(objects.size());
        out.write(reinterpret_cast<const char*>(&objectCount), sizeof(objectCount));
        for (const auto& [key, entry] : objects)
        {
            writeNetObject(out, entry.first);
            writeString(out, entry.second);
        }
    }
}

void LuaSync::persistState()
{
    std::ofstream out(luaDirectory(sDataDirectory) / "globalState.bin", std::ios::binary | std::ios::trunc);
    out.write(sGlobalState.data(), static_cast<std::streamsize>(sGlobalState.size()));
}
