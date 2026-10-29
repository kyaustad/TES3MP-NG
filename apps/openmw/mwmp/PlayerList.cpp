#include <components/openmw-mp/TimedLog.hpp>
#include <apps/openmw/mwclass/creature.hpp>

#include "../mwbase/environment.hpp"

#include "../mwclass/npc.hpp"

#include "../mwmechanics/creaturestats.hpp"

#include "../mwworld/cellstore.hpp"
#include "../mwworld/player.hpp"
#include "../mwworld/worldimp.hpp"

#include "PlayerList.hpp"
#include "Main.hpp"
#include "DedicatedPlayer.hpp"
#include "CellController.hpp"
#include "GUIController.hpp"


using namespace mwmp;

std::map <RakNet::RakNetGUID, DedicatedPlayer *> PlayerList::playerList;

void PlayerList::update(float dt)
{
    for (auto &playerEntry : playerList)
    {
        DedicatedPlayer *player = playerEntry.second;
        if (player == nullptr) continue;

        player->update(dt);
    }
}

DedicatedPlayer *PlayerList::newPlayer(RakNet::RakNetGUID guid)
{
    LOG_APPEND(TimedLog::LOG_INFO, "- Creating new DedicatedPlayer with guid %s", guid.ToString());

    playerList[guid] = new DedicatedPlayer(guid);

    LOG_APPEND(TimedLog::LOG_INFO, "- There are now %i DedicatedPlayers", playerList.size());

    return playerList[guid];
}

void PlayerList::deletePlayer(RakNet::RakNetGUID guid)
{
    if (playerList[guid]->reference)
        playerList[guid]->deleteReference();

    delete playerList[guid];
    playerList.erase(guid);
}

void PlayerList::cleanUp()
{
    for (auto &playerEntry : playerList)
        delete playerEntry.second;
}

DedicatedPlayer *PlayerList::getPlayer(RakNet::RakNetGUID guid)
{
    return playerList[guid];
}

DedicatedPlayer *PlayerList::getPlayer(const MWWorld::Ptr &ptr)
{
    for (auto &playerEntry : playerList)
    {
        if (playerEntry.second == nullptr || playerEntry.second->getPtr().mRef == nullptr)
            continue;
        
        if (playerEntry.second->getPtr().getCellRef().getRefId() == ptr.getCellRef().getRefId())
            return playerEntry.second;
    }

    return nullptr;
}

DedicatedPlayer* PlayerList::getPlayer(ESM::RefNum actorId)
{
    // OpenMW 0.49+ identifies actors by RefNum, which every object in the scene has
    for (auto& playerEntry : playerList)
    {
        if (playerEntry.second == nullptr || playerEntry.second->getPtr().mRef == nullptr)
            continue;

        if (playerEntry.second->getPtr().getCellRef().getRefNum() == actorId)
            return playerEntry.second;
    }

    return nullptr;
}

std::vector<RakNet::RakNetGUID> PlayerList::getPlayersInCell(const ESM::Cell& cell)
{
    std::vector<RakNet::RakNetGUID> playersInCell;

    for (auto& playerEntry : playerList)
    {
        if (playerEntry.first != RakNet::UNASSIGNED_CRABNET_GUID)
        {
            if (Main::get().getCellController()->isSameCell(cell, playerEntry.second->cell))
            {
                playersInCell.push_back(playerEntry.first);
            }
        }
    }

    return playersInCell;
}

bool PlayerList::isDedicatedPlayer(const MWWorld::Ptr &ptr)
{
    if (ptr.mRef == nullptr)
        return false;

    // Players are never from content files and never have an mpNum
    if (ptr.getCellRef().hasContentFile() || ptr.getCellRef().getMpNum() != 0)
        return false;

    return (getPlayer(ptr) != nullptr);
}

void PlayerList::enableMarkers(const ESM::Cell& cell)
{
    for (auto &playerEntry : playerList)
    {
        if (playerEntry.second == nullptr || playerEntry.second->getPtr().mRef == nullptr)
            continue;

        if (Main::get().getCellController()->isSameCell(cell, playerEntry.second->cell))
        {
            playerEntry.second->enableMarker();
        }
    }
}

/*
    Go through all DedicatedPlayers checking if their mHitAttemptActorId matches this one
    and set it to -1 if it does

    This resets the combat target for a DedicatedPlayer's followers in Actors::update()
*/
void PlayerList::clearHitAttemptActorId(ESM::RefNum actorId)
{
    for (auto &playerEntry : playerList)
    {
        if (playerEntry.second == nullptr || playerEntry.second->getPtr().mRef == nullptr)
            continue;

        MWMechanics::CreatureStats &playerCreatureStats = playerEntry.second->getPtr().getClass().getCreatureStats(playerEntry.second->getPtr());

        if (playerCreatureStats.getHitAttemptActor() == actorId)
            playerCreatureStats.setHitAttemptActor(ESM::RefNum());
    }
}
