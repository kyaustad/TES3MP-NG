#include "LuaSync.hpp"

#include <yaml-cpp/yaml.h>

#include <components/misc/strings/algorithm.hpp>
#include <components/misc/strings/lower.hpp>
#include <components/openmw-mp/Lua/LuaData.hpp>
#include <components/openmw-mp/TimedLog.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwlua/luamanagerimp.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/worldmodel.hpp"

#include "CellController.hpp"
#include "DedicatedPlayer.hpp"
#include "LocalPlayer.hpp"
#include "Main.hpp"
#include "Networking.hpp"
#include "PlayerList.hpp"
#include "Worldstate.hpp"

using namespace mwmp;

namespace
{
    // The authority sends the state of authority mode scripts to the server this often, so a new authority loses little after a crash
    constexpr float sStateSaveInterval = 30.f;

    MWLua::LuaManager* luaManager()
    {
        MWBase::LuaManager* manager = MWBase::Environment::get().getLuaManager();
        return static_cast<MWLua::LuaManager*>(manager);
    }

    bool globMatch(std::string_view pattern, std::string_view text)
    {
        // '*' matches any sequence of characters; comparison is case-insensitive like VFS paths
        size_t p = 0, t = 0, star = std::string_view::npos, mark = 0;
        while (t < text.size())
        {
            if (p < pattern.size() && pattern[p] == '*')
            {
                star = p++;
                mark = t;
            }
            else if (p < pattern.size() && Misc::StringUtils::toLower(pattern[p]) == Misc::StringUtils::toLower(text[t]))
            {
                ++p;
                ++t;
            }
            else if (star != std::string_view::npos)
            {
                p = star + 1;
                t = ++mark;
            }
            else
                return false;
        }
        while (p < pattern.size() && pattern[p] == '*')
            ++p;
        return p == pattern.size();
    }

    LuaSync::Mode parseMode(const std::string& mode)
    {
        return Misc::StringUtils::ciEqual(mode, "authority") ? LuaSync::Mode::Authority : LuaSync::Mode::Client;
    }

    uint64_t localGuid()
    {
        return Main::get().getLocalPlayer()->guid.g;
    }
}

LuaSync& LuaSync::get()
{
    static LuaSync instance;
    return instance;
}

bool LuaSync::isEnabled()
{
    return Main::isInitialized();
}

void LuaSync::clear()
{
    mHasConfig = false;
    mIsAuthority = false;
    mStartedGlobalScripts = false;
    mPatterns.clear();
    mAuthorityPatterns.clear();
    {
        std::lock_guard lock(mMutex);
        mIncoming.clear();
        mOutgoing.clear();
    }
    mPendingAuthorityState.clear();
    mHasPendingAuthorityState = false;
}

LuaSync::Mode LuaSync::getGlobalScriptMode(std::string_view scriptPath) const
{
    // OpenMW's own scripts implement engine behavior for the local player and always run on every client
    if (Misc::StringUtils::ciStartsWith(scriptPath, "scripts/omw/"))
        return Mode::Client;

    for (const auto& [pattern, mode] : mPatterns)
    {
        if (globMatch(pattern, scriptPath))
            return mode;
    }
    return mDefaultMode;
}

void LuaSync::applyConfig(const std::string& policyJson)
{
    mPatterns.clear();
    mAuthorityPatterns.clear();
    mDefaultMode = Mode::Client;

    try
    {
        // JSON is a subset of YAML
        const YAML::Node root = YAML::Load(policyJson);
        if (root["default"])
            mDefaultMode = parseMode(root["default"].as<std::string>());
        if (root["scripts"] && root["scripts"].IsMap())
        {
            for (const auto& entry : root["scripts"])
            {
                const std::string pattern = Misc::StringUtils::lowerCase(entry.first.as<std::string>());
                const Mode mode = parseMode(entry.second.as<std::string>());
                mPatterns.emplace_back(pattern, mode);
                if (mode == Mode::Authority)
                    mAuthorityPatterns.push_back(pattern);
            }
        }
    }
    catch (const std::exception& e)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Invalid Lua script policy from server: %s", e.what());
    }

    if (mDefaultMode == Mode::Authority)
        mAuthorityPatterns.push_back("*");

    mHasConfig = true;
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Lua script policy: default mode %s, %zu script rules",
        mDefaultMode == Mode::Authority ? "authority" : "client", mPatterns.size());
}

bool LuaSync::isLocallySimulated(const MWWorld::Ptr& ptr)
{
    if (!isEnabled() || ptr.isEmpty())
        return true;

    if (ptr == MWBase::Environment::get().getWorld()->getPlayerPtr())
        return true;

    if (PlayerList::isDedicatedPlayer(ptr))
        return false;

    CellController* cellController = Main::get().getCellController();
    if (cellController->isLocalActor(ptr))
        return true;
    if (cellController->isDedicatedActor(ptr))
        return false;

    // Items in inventories and objects that aren't in a cell follow whoever holds them, which is this client
    if (!ptr.isInCell() || ptr.getCell()->getCell()->isEsm4())
        return true;

    return cellController->hasLocalAuthority(ptr.getCell()->getCell()->getEsm3());
}

NetObjectId LuaSync::toNetObject(ESM::RefNum refNum) const
{
    NetObjectId id;
    const MWWorld::Ptr ptr = MWBase::Environment::get().getWorldModel()->getPtr(refNum);

    if (!ptr.isEmpty())
    {
        if (ptr == MWBase::Environment::get().getWorld()->getPlayerPtr())
        {
            id.kind = NetObjectId::PLAYER;
            id.guid = localGuid();
            return id;
        }
        if (DedicatedPlayer* player = PlayerList::isDedicatedPlayer(ptr) ? PlayerList::getPlayer(ptr) : nullptr)
        {
            id.kind = NetObjectId::PLAYER;
            id.guid = player->guid.g;
            return id;
        }
        if (ptr.getCellRef().getMpNum() != 0)
        {
            id.kind = NetObjectId::MP;
            id.mpNum = ptr.getCellRef().getMpNum();
            return id;
        }
    }

    if (refNum.hasContentFile())
    {
        id.kind = NetObjectId::CONTENT;
        id.refNumIndex = refNum.mIndex;
        id.contentFile = refNum.mContentFile;
    }
    return id;
}

ESM::RefNum LuaSync::toLocalObject(const NetObjectId& id) const
{
    switch (id.kind)
    {
        case NetObjectId::CONTENT:
            return ESM::RefNum{ id.refNumIndex, id.contentFile };
        case NetObjectId::MP:
        {
            const MWWorld::Ptr ptr = MWBase::Environment::get().getWorld()->searchPtrViaUniqueIndex(0, id.mpNum);
            return ptr.isEmpty() ? ESM::RefNum() : ptr.getCellRef().getRefNum();
        }
        case NetObjectId::PLAYER:
        {
            if (id.guid == localGuid())
                return MWBase::Environment::get().getWorld()->getPlayerPtr().getCellRef().getRefNum();
            RakNet::RakNetGUID guid;
            guid.g = id.guid;
            DedicatedPlayer* player = PlayerList::getPlayer(guid);
            if (player != nullptr && !player->getPtr().isEmpty())
                return player->getPtr().getCellRef().getRefNum();
            return ESM::RefNum();
        }
        default:
            return ESM::RefNum();
    }
}

std::string LuaSync::dataToNetwork(std::string_view data) const
{
    if (data.empty())
        return {};
    return LuaData::toNetwork(data, [this](LuaData::LocalRefNum r) {
        return toNetObject(ESM::RefNum{ r.index, r.contentFile });
    });
}

std::string LuaSync::dataToLocal(std::string_view data) const
{
    if (data.empty())
        return {};
    return LuaData::toLocal(data, [this](const NetObjectId& id) {
        const ESM::RefNum refNum = toLocalObject(id);
        return LuaData::LocalRefNum{ refNum.mIndex, refNum.mContentFile };
    });
}

void LuaSync::queue(LuaMessage&& message)
{
    std::lock_guard lock(mMutex);
    mOutgoing.push_back(std::move(message));
}

void LuaSync::onGlobalEvent(const std::string& eventName, const std::string& data)
{
    // Client mode scripts already received the event here; only authority mode scripts and server scripts can
    // be interested in it. Without authority mode scripts, client-only mods create no network traffic at all.
    if (!hasAuthorityModeScripts())
        return;

    LuaMessage message;
    message.kind = LuaMessage::GLOBAL_EVENT;
    message.name = eventName;
    try
    {
        message.data = dataToNetwork(data);
    }
    catch (const std::exception& e)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Not sending Lua global event %s: %s", eventName.c_str(), e.what());
        return;
    }
    queue(std::move(message));
}

void LuaSync::onLocalEvent(const MWWorld::Ptr& target, const std::string& eventName, const std::string& data)
{
    // Attacks on objects simulated elsewhere travel as TES3MP attack packets, and the victim's client feeds them
    // into its own Lua combat scripts
    if (eventName == "Hit")
        return;

    LuaMessage message;
    message.kind = LuaMessage::LOCAL_EVENT;
    message.name = eventName;
    message.target = toNetObject(target.getCellRef().getRefNum());

    if (message.target.kind == NetObjectId::NONE)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE, "Not sending Lua event %s to %s, which only exists on this client",
            eventName.c_str(), target.getCellRef().getRefId().toDebugString().c_str());
        return;
    }

    if (target.isInCell() && !target.getCell()->getCell()->isEsm4())
        message.cell = target.getCell()->getCell()->getEsm3().getShortDescription();

    try
    {
        message.data = dataToNetwork(data);
    }
    catch (const std::exception& e)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Not sending Lua event %s: %s", eventName.c_str(), e.what());
        return;
    }
    queue(std::move(message));
}

void LuaSync::onStorageValueChanged(std::string_view section, std::string_view key, std::string data, bool persistent)
{
    if (!mIsAuthority)
        return;

    LuaMessage message;
    message.kind = LuaMessage::STORAGE_SET;
    message.flags = persistent ? LuaMessage::FLAG_PERSISTENT : LuaMessage::FLAG_NONE;
    message.name = std::string(section);
    message.key = std::string(key);
    message.data = dataToNetwork(data);
    queue(std::move(message));
}

void LuaSync::onStorageSectionReplaced(std::string_view section, std::string data)
{
    if (!mIsAuthority)
        return;

    LuaMessage message;
    message.kind = LuaMessage::STORAGE_RESET;
    message.name = std::string(section);
    message.data = dataToNetwork(data);
    queue(std::move(message));
}

void LuaSync::receive(const std::vector<LuaMessage>& messages)
{
    std::lock_guard lock(mMutex);
    mIncoming.insert(mIncoming.end(), messages.begin(), messages.end());
}

void LuaSync::updateLua(float dt)
{
    std::vector<LuaMessage> messages;
    {
        std::lock_guard lock(mMutex);
        messages.swap(mIncoming);
    }

    MWLua::LuaManager* lua = luaManager();
    const bool wasAuthority = mIsAuthority;
    auto toLocal = [this](const std::string& data) { return dataToLocal(data); };

    for (const LuaMessage& message : messages)
    {
        try
        {
            switch (message.kind)
            {
                case LuaMessage::CONFIG:
                    // The server's storage snapshot follows the policy
                    if (!mHasConfig)
                        lua->mpClearGlobalStorage();
                    applyConfig(message.name);
                    break;
                case LuaMessage::AUTHORITY:
                    mIsAuthority = message.target.kind == NetObjectId::PLAYER && message.target.guid == localGuid();
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Lua world authority: %s",
                        mIsAuthority ? "this client" : "another player");
                    break;
                case LuaMessage::GLOBAL_STATE:
                    mPendingAuthorityState = message.data;
                    mHasPendingAuthorityState = true;
                    break;
                case LuaMessage::REQUEST_STATE:
                    sendGlobalState();
                    break;
                case LuaMessage::GLOBAL_EVENT:
                    lua->mpDeliverGlobalEvent(
                        message.name, toLocal(message.data), (message.flags & LuaMessage::FLAG_AUTHORITY_ONLY) != 0);
                    break;
                case LuaMessage::LOCAL_EVENT:
                {
                    const ESM::RefNum target = toLocalObject(message.target);
                    if (target.isSet())
                        lua->mpDeliverLocalEvent(target, message.name, toLocal(message.data));
                    break;
                }
                case LuaMessage::STORAGE_SET:
                    lua->mpSetStorageValue(message.name, message.key, toLocal(message.data));
                    break;
                case LuaMessage::STORAGE_RESET:
                    lua->mpResetStorageSection(message.name, toLocal(message.data));
                    break;
                default:
                    break;
            }
        }
        catch (const std::exception& e)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Failed to apply Lua message %s: %s", message.name.c_str(),
                e.what());
        }
    }

    if (!mHasConfig)
        return;

    if (mIsAuthority && hasAuthorityModeScripts())
    {
        mStateSaveTimer += dt;
        if (mStateSaveTimer >= sStateSaveInterval)
        {
            mStateSaveTimer = 0.f;
            sendGlobalState();
        }
    }

    // Start global scripts once the policy is known, and restart them when this client gains or loses authority
    if (!mStartedGlobalScripts || wasAuthority != mIsAuthority)
    {
        if (wasAuthority && !mIsAuthority)
            sendGlobalState();

        const std::string* state = (mIsAuthority && mHasPendingAuthorityState) ? &mPendingAuthorityState : nullptr;
        lua->mpStartGlobalScripts(mIsAuthority, state);
        mStartedGlobalScripts = true;
        mHasPendingAuthorityState = false;
        mPendingAuthorityState.clear();
        mStateSaveTimer = 0.f;
    }
}

void LuaSync::sendGlobalState()
{
    if (!hasAuthorityModeScripts())
        return;

    LuaMessage message;
    message.kind = LuaMessage::GLOBAL_STATE;
    message.data = luaManager()->mpSaveAuthorityScripts();
    mStateSaveTimer = 0.f;
    queue(std::move(message));
}

void LuaSync::update(float /*dt*/)
{
    std::vector<LuaMessage> outgoing;
    {
        std::lock_guard lock(mMutex);
        outgoing.swap(mOutgoing);
    }
    if (outgoing.empty())
        return;

    Worldstate* worldstate = Main::get().getNetworking()->getWorldstate();
    worldstate->luaMessages = std::move(outgoing);

    WorldstatePacket* packet = Main::get().getNetworking()->getWorldstatePacket(ID_LUA_MESSAGE);
    packet->setWorldstate(worldstate);
    packet->Send();
    worldstate->luaMessages.clear();
}
