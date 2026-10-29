#include "LuaWorldSync.hpp"

#include <chrono>
#include <cmath>
#include <mutex>
#include <vector>

#include <components/misc/strings/algorithm.hpp>
#include <components/openmw-mp/Base/BaseActor.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/Utils.hpp>

#include "../mwbase/dialoguemanager.hpp"
#include "../mwbase/environment.hpp"
#include "../mwbase/journal.hpp"
#include "../mwbase/world.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/npcstats.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/worldmodel.hpp"

#include "ActorList.hpp"
#include "CellController.hpp"
#include "LocalPlayer.hpp"
#include "Main.hpp"
#include "MechanicsHelper.hpp"
#include "Networking.hpp"
#include "ObjectList.hpp"
#include "PlayerList.hpp"
#include "Worldstate.hpp"

using namespace mwmp;

namespace
{
    // Placements waiting for the server to give their object an mpNum
    struct PendingPlacement
    {
        ESM::RefNum mRefNum;
        std::string mRefId;
        std::string mCell;
        osg::Vec3f mPosition;
        std::chrono::steady_clock::time_point mTime;
    };

    std::mutex sMutex;
    std::vector<std::function<void()>> sPosted;
    std::vector<PendingPlacement> sPending;

    constexpr const char* sOrigin = "lua";

    bool inMultiplayerCell(const MWWorld::Ptr& ptr)
    {
        return !ptr.isEmpty() && ptr.isInCell() && !ptr.getCell()->getCell()->isEsm4()
            && ptr.getCell() != &MWBase::Environment::get().getWorldModel()->getDraftCell();
    }

    bool isSynced(const MWWorld::Ptr& ptr)
    {
        return ptr.getCellRef().getMpNum() != 0 || ptr.getCellRef().getRefNum().hasContentFile();
    }

    bool isPlayer(const MWWorld::Ptr& ptr)
    {
        return ptr == MWBase::Environment::get().getWorld()->getPlayerPtr() || PlayerList::isDedicatedPlayer(ptr);
    }

    ObjectList* prepare(const MWWorld::Ptr& ptr)
    {
        ObjectList* objectList = Main::get().getNetworking()->getObjectList();
        objectList->reset();
        objectList->packetOrigin = CLIENT_SCRIPT_GLOBAL;
        objectList->originClientScript = sOrigin;
        if (!ptr.isEmpty() && ptr.isInCell())
            objectList->cell = ptr.getCell()->getCell()->getEsm3();
        return objectList;
    }

    LocalPlayer* localPlayer()
    {
        return Main::get().getLocalPlayer();
    }
}

bool LuaWorldSync::isActive()
{
    return Main::isInitialized() && localPlayer()->isLoggedIn();
}

void LuaWorldSync::post(std::function<void()> fn)
{
    if (!Main::isInitialized())
        return;
    std::lock_guard lock(sMutex);
    sPosted.push_back(std::move(fn));
}

void LuaWorldSync::flush()
{
    std::vector<std::function<void()>> posted;
    {
        std::lock_guard lock(sMutex);
        posted.swap(sPosted);

        // Placements the server never answered, for example because it rejected them
        const auto now = std::chrono::steady_clock::now();
        std::erase_if(sPending, [&](const PendingPlacement& p) { return now - p.mTime > std::chrono::seconds(30); });
    }
    for (const auto& fn : posted)
    {
        try
        {
            fn();
        }
        catch (const std::exception& e)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Failed to send a Lua world change: %s", e.what());
        }
    }
}

void LuaWorldSync::objectEnabled(const MWWorld::Ptr& ptr, bool enabled)
{
    if (!isActive() || !inMultiplayerCell(ptr) || !isSynced(ptr) || isPlayer(ptr))
        return;
    ObjectList* objectList = prepare(ptr);
    objectList->addObjectState(ptr, enabled);
    objectList->sendObjectState();
}

void LuaWorldSync::objectScaled(const MWWorld::Ptr& ptr, float scale)
{
    if (!isActive() || !inMultiplayerCell(ptr) || !isSynced(ptr) || isPlayer(ptr))
        return;
    ObjectList* objectList = prepare(ptr);
    objectList->addObjectScale(ptr, scale);
    objectList->sendObjectScale();
}

void LuaWorldSync::objectRemoved(const MWWorld::Ptr& ptr)
{
    if (!isActive() || !inMultiplayerCell(ptr) || !isSynced(ptr) || isPlayer(ptr))
        return;
    ObjectList* objectList = prepare(ptr);
    objectList->addObjectGeneric(ptr);
    objectList->sendObjectDelete();
}

void LuaWorldSync::objectPlaced(const MWWorld::Ptr& ptr)
{
    if (!isActive() || !inMultiplayerCell(ptr) || isPlayer(ptr))
        return;

    const std::string refId = ptr.getCellRef().getRefId().serializeText();
    if (Utils::isDynamicRefId(refId))
    {
        // Records created by Lua scripts only exist on the client that created them
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Not sending the placement of %s, a record that only exists on this "
            "client", refId.c_str());
        return;
    }

    {
        std::lock_guard lock(sMutex);
        sPending.push_back({ ptr.getCellRef().getRefNum(), refId,
            ptr.getCell()->getCell()->getEsm3().getShortDescription(), ptr.getRefData().getPosition().asVec3(),
            std::chrono::steady_clock::now() });
    }

    ObjectList* objectList = prepare(ptr);
    if (ptr.getClass().isActor())
    {
        objectList->addObjectSpawn(ptr);
        objectList->sendObjectSpawn();
    }
    else
    {
        objectList->addObjectPlace(ptr);
        objectList->sendObjectPlace();
    }
}

LuaWorldSync::Location LuaWorldSync::capture(const MWWorld::Ptr& ptr)
{
    Location location;
    if (!isActive() || ptr.isEmpty())
        return location;
    location.mInWorld = inMultiplayerCell(ptr);
    location.mSynced = location.mInWorld && isSynced(ptr);
    location.mIsActor = ptr.getClass().isActor();
    if (location.mInWorld)
    {
        location.mCell = ptr.getCell()->getCell()->getEsm3();
        location.mObject = Main::get().getNetworking()->getObjectList()->getBaseObjectFromPtr(ptr);
    }
    return location;
}

void LuaWorldSync::objectMoved(const Location& from, const MWWorld::Ptr& to)
{
    if (!isActive() || isPlayer(to))
        return;

    if (from.mIsActor)
    {
        // Actors keep their identity: the server moves them to the new cell
        if (!from.mSynced || !inMultiplayerCell(to)
            || CellController::isSameCell(from.mCell, to.getCell()->getCell()->getEsm3()))
        {
            if (!from.mSynced)
                objectPlaced(to);
            return;
        }

        BaseActor baseActor;
        baseActor.refNum = to.getCellRef().getMpRefNumIndex();
        baseActor.mpNum = to.getCellRef().getMpNum();
        baseActor.cell = to.getCell()->getCell()->getEsm3();
        baseActor.position = to.getRefData().getPosition();
        baseActor.isFollowerCellChange = true;

        ActorList* actorList = Main::get().getNetworking()->getActorList();
        actorList->reset();
        actorList->cell = from.mCell;
        actorList->addCellChangeActor(baseActor);
        actorList->sendCellChangeActors();
        return;
    }

    // Other objects are removed from where they were and placed again where they are now
    if (from.mSynced)
    {
        ObjectList* objectList = prepare(MWWorld::Ptr());
        objectList->cell = from.mCell;
        objectList->addBaseObject(from.mObject);
        objectList->sendObjectDelete();
    }
    objectPlaced(to);
}

void LuaWorldSync::containerChanged(const MWWorld::Ptr& container, const MWWorld::Ptr& item, int count, bool added)
{
    // Players' inventories are sent by ContainerStore itself
    if (!isActive() || count <= 0 || !inMultiplayerCell(container) || !isSynced(container) || isPlayer(container))
        return;

    ObjectList* objectList = prepare(container);
    objectList->action = added ? BaseObjectList::ADD : BaseObjectList::REMOVE;
    objectList->containerSubAction = BaseObjectList::NONE;
    BaseObject baseObject = objectList->getBaseObjectFromPtr(container);
    objectList->addContainerItem(baseObject, item, added ? count : 0, added ? 0 : count);
    objectList->addBaseObject(baseObject);
    objectList->sendContainer();
}

void LuaWorldSync::lockChanged(const MWWorld::Ptr& ptr, int lockLevel)
{
    if (!isActive() || !inMultiplayerCell(ptr) || !isSynced(ptr))
        return;
    ObjectList* objectList = prepare(ptr);
    objectList->addObjectLock(ptr, lockLevel);
    objectList->sendObjectLock();
}

void LuaWorldSync::trapDisarmed(const MWWorld::Ptr& ptr)
{
    if (!isActive() || !inMultiplayerCell(ptr) || !isSynced(ptr))
        return;
    ObjectList* objectList = prepare(ptr);
    objectList->addObjectTrap(ptr, ptr.getRefData().getPosition(), true);
    objectList->sendObjectTrap();
}

void LuaWorldSync::barterGold(const MWWorld::Ptr& ptr, int gold)
{
    if (!isActive() || !inMultiplayerCell(ptr) || !isSynced(ptr) || isPlayer(ptr))
        return;
    MechanicsHelper::sendGoldPool(ptr, gold);
}

void LuaWorldSync::playerSpell(const MWWorld::Ptr& actor, const ESM::RefId& spellId, bool added)
{
    if (!isActive() || actor != MWBase::Environment::get().getWorld()->getPlayerPtr())
        return;
    localPlayer()->sendSpellChange(spellId.serializeText(), added ? SpellbookChanges::ADD : SpellbookChanges::REMOVE);
}

void LuaWorldSync::playerFaction(const ESM::RefId& factionId)
{
    if (!isActive() || factionId.empty())
        return;
    const MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
    const MWMechanics::NpcStats& stats = player.getClass().getNpcStats(player);
    const std::string id = factionId.serializeText();

    const auto& ranks = stats.getFactionRanks();
    if (auto it = ranks.find(factionId); it != ranks.end())
        localPlayer()->sendFactionRank(id, it->second);
    localPlayer()->sendFactionReputation(id, stats.getFactionReputation(factionId));
    localPlayer()->sendFactionExpulsionState(id, stats.getExpelled(factionId));
}

void LuaWorldSync::playerJournalEntry(const ESM::RefId& questId, int index, const MWWorld::Ptr& actor)
{
    if (isActive())
        localPlayer()->sendJournalEntry(questId.serializeText(), index, actor);
}

void LuaWorldSync::playerJournalIndex(const ESM::RefId& questId, int index)
{
    if (isActive())
        localPlayer()->sendJournalIndex(questId.serializeText(), index);
}

void LuaWorldSync::playerTopic(const ESM::RefId& topicId)
{
    if (isActive())
        localPlayer()->sendTopic(topicId.serializeText());
}

void LuaWorldSync::globalVariable(std::string_view globalId)
{
    if (!isActive() || !Main::isValidPacketGlobal(globalId))
        return;

    MWBase::World* world = MWBase::Environment::get().getWorld();
    Worldstate* worldstate = Main::get().getNetworking()->getWorldstate();
    switch (world->getGlobalVariableType(globalId))
    {
        case 'f':
            worldstate->sendClientGlobal(std::string(globalId), world->getGlobalFloat(globalId));
            break;
        case 's':
            worldstate->sendClientGlobal(std::string(globalId), world->getGlobalInt(globalId), VARIABLE_TYPE::SHORT);
            break;
        case 'l':
            worldstate->sendClientGlobal(std::string(globalId), world->getGlobalInt(globalId), VARIABLE_TYPE::LONG);
            break;
        default:
            break;
    }
}

MWWorld::Ptr LuaWorldSync::claimPlacement(
    const std::string& refId, const MWWorld::CellStore* cellStore, const ESM::Position& position)
{
    if (cellStore == nullptr || cellStore->getCell()->isEsm4())
        return {};
    const std::string cell = cellStore->getCell()->getEsm3().getShortDescription();

    std::lock_guard lock(sMutex);
    for (auto it = sPending.begin(); it != sPending.end(); ++it)
    {
        if (!Misc::StringUtils::ciEqual(it->mRefId, refId) || !Misc::StringUtils::ciEqual(it->mCell, cell)
            || (it->mPosition - position.asVec3()).length() > 1.f)
            continue;

        const ESM::RefNum refNum = it->mRefNum;
        const MWWorld::Ptr ptr = MWBase::Environment::get().getWorldModel()->getPtr(refNum);
        sPending.erase(it);
        if (ptr.isEmpty() || ptr.getCellRef().getMpNum() != 0)
            return {};

        // Changes that scripts made before the object had its mpNum are sent once it has one
        sPosted.push_back([refNum] {
            const MWWorld::Ptr placed = MWBase::Environment::get().getWorldModel()->getPtr(refNum);
            if (placed.isEmpty() || placed.getCellRef().getMpNum() == 0)
                return;
            if (placed.getCellRef().getScale() != 1.f)
                objectScaled(placed, placed.getCellRef().getScale());
            if (placed.getCellRef().getLockLevel() > 0)
                lockChanged(placed, placed.getCellRef().getLockLevel());
            if (!placed.getRefData().isEnabled())
                objectEnabled(placed, false);
        });
        return ptr;
    }
    return {};
}
