#include <components/esm3/esmwriter.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/Utils.hpp>
#include <components/esm/util.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/settings/values.hpp>

#include <components/esm3/loadbody.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/journal.hpp"
#include "../mwbase/soundmanager.hpp"

#include "../mwclass/creature.hpp"
#include "../mwclass/npc.hpp"

#include "../mwdialogue/dialoguemanagerimp.hpp"

#include "../mwgui/inventorywindow.hpp"
#include "../mwgui/windowmanagerimp.hpp"

#include "../mwinput/inputmanagerimp.hpp"

#include "../mwmechanics/activespells.hpp"
#include "../mwmechanics/aitravel.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/mechanicsmanagerimp.hpp"
#include "../mwmechanics/spellcasting.hpp"
#include "../mwmechanics/spellutil.hpp"

#include "../mwscript/scriptmanagerimp.hpp"

#include "../mwstate/statemanagerimp.hpp"

#include "../mwworld/cellstore.hpp"
#include "../mwworld/customdata.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwworld/manualref.hpp"
#include "../mwworld/player.hpp"
#include "../mwworld/worldimp.hpp"
#include "../mwworld/worldmodel.hpp"

#include "LocalPlayer.hpp"
#include "Main.hpp"
#include "Networking.hpp"
#include "PlayerList.hpp"
#include "CellController.hpp"
#include "GUIController.hpp"
#include "MechanicsHelper.hpp"

using namespace mwmp;

std::map<std::string, int> storedItemRemovals;

LocalPlayer::LocalPlayer()
{
    deathTime = time(0);
    receivedCharacter = false;

    charGenState.currentStage = 0;
    charGenState.endStage = 1;
    charGenState.isFinished = false;

    ignorePosPacket = false;
    ignoreJailTeleportation = false;
    ignoreJailSkillIncreases = false;
    
    attack.shouldSend = false;
    attack.instant = false;
    attack.pressed = false;

    cast.shouldSend = false;
    cast.instant = false;
    cast.pressed = false;

    killer.isPlayer = false;
    killer.refId = "";
    killer.name = "";

    isChangingRegion = false;

    jailProgressText = "";
    jailEndText = "";

    isUsingBed = false;
    avoidSendingInventoryPackets = false;
    isReceivingQuickKeys = false;
    isPlayingAnimation = false;
    diedSinceArrestAttempt = false;
}

LocalPlayer::~LocalPlayer()
{

}

Networking *LocalPlayer::getNetworking()
{
    return mwmp::Main::get().getNetworking();
}

MWWorld::Ptr LocalPlayer::getPlayerPtr()
{
    return MWBase::Environment::get().getWorld()->getPlayerPtr();
}

void LocalPlayer::update()
{
    static float updateTimer = 0;
    const float timeoutSec = 0.015;

    if ((updateTimer += MWBase::Environment::get().getFrameDuration()) >= timeoutSec)
    {
        updateTimer = 0;
        updateCell();
        updatePosition();
        updateAnimFlags();
        updateAttackOrCast();
        updateEquipment();
        updateStatsDynamic();
        updateAttributes();
        updateSkills();
        updateLevel();
        updateBounty();
        updateReputation();
    }
}

bool LocalPlayer::processCharGen()
{
    MWBase::WindowManager *windowManager = MWBase::Environment::get().getWindowManager();

    // If we haven't finished CharGen and we're in a menu, it must be
    // one of the CharGen menus, so go no further until it's closed
    if (windowManager->isGuiMode() && !charGenState.isFinished)
    {
        return false;
    }

    // If the current stage of CharGen is not the last one,
    // move to the next one
    else if (charGenState.currentStage < charGenState.endStage)
    {
        /*
            For automated tests, skip the menus: set the name and keep the default race, class and birthsign
        */
        if (!Main::getAutoLoginName().empty())
        {
            if (charGenState.currentStage == 0)
                MWBase::Environment::get().getMechanicsManager()->setPlayerName(Main::getAutoLoginName());
            else if (charGenState.currentStage == 1)
            {
                // The default player record has no race, head or hair; use the first playable male Dark Elf parts
                const ESM::RefId race = ESM::RefId::stringRefId("Dark Elf");
                ESM::RefId head, hair;
                for (const ESM::BodyPart& part : MWBase::Environment::get().getESMStore()->get<ESM::BodyPart>())
                {
                    if (part.mRace != race || part.mData.mType != ESM::BodyPart::MT_Skin || part.mData.mVampire
                        || (part.mData.mFlags & (ESM::BodyPart::BPF_Female | ESM::BodyPart::BPF_NotPlayable)))
                        continue;
                    if (part.mData.mPart == ESM::BodyPart::MP_Head && head.empty())
                        head = part.mId;
                    else if (part.mData.mPart == ESM::BodyPart::MP_Hair && hair.empty())
                        hair = part.mId;
                }
                MWBase::Environment::get().getMechanicsManager()->setPlayerRace(race, true, head, hair);
            }

            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Auto-login: completing character generation stage %i",
                charGenState.currentStage);
            charGenState.currentStage++;
            // The last stage is reported below, after ID_PLAYER_BASEINFO, so that the server saves the character
            // with its race, head and hair
            if (charGenState.currentStage < charGenState.endStage)
            {
                getNetworking()->getPlayerPacket(ID_PLAYER_CHARGEN)->setPlayer(this);
                getNetworking()->getPlayerPacket(ID_PLAYER_CHARGEN)->Send();
            }
            return false;
        }

        switch (charGenState.currentStage)

        {
        case 0:
            windowManager->pushGuiMode(MWGui::GM_Name);
            break;
        case 1:
            windowManager->pushGuiMode(MWGui::GM_Race);
            break;
        case 2:
            windowManager->pushGuiMode(MWGui::GM_Class);
            break;
        case 3:
            windowManager->pushGuiMode(MWGui::GM_Birth);
            break;
        default:
            windowManager->pushGuiMode(MWGui::GM_Review);
            break;
        }
        getNetworking()->getPlayerPacket(ID_PLAYER_CHARGEN)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_CHARGEN)->Send();

        return false;
    }

    // If we've reached the last stage of CharGen, send the
    // corresponding packets and mark CharGen as finished
    else if (!charGenState.isFinished)
    {
        MWBase::World *world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr ptrPlayer = world->getPlayerPtr();
        // Read the record from the store: race, head and hair chosen this frame may not have reached mBase yet
        npc = *world->getStore().get<ESM::NPC>().find(ptrPlayer.getCellRef().getRefId());
        birthsign = world->getPlayer().getBirthSign().serializeText();

        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending ID_PLAYER_BASEINFO to server with my CharGen info (race %s, head %s, hair %s)",
            Utils::refIdToCString(npc.mRace), Utils::refIdToCString(npc.mHead), Utils::refIdToCString(npc.mHair));
        getNetworking()->getPlayerPacket(ID_PLAYER_BASEINFO)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_BASEINFO)->Send();

        // Send stats packets if this is the 2nd round of CharGen that
        // only happens for new characters
        if (charGenState.endStage != 1)
        {
            updateStatsDynamic(true);
            updateAttributes(true);
            updateSkills(true);
            updateLevel(true);
            sendClass();
            sendSpellbook();
            getNetworking()->getPlayerPacket(ID_PLAYER_CHARGEN)->setPlayer(this);
            getNetworking()->getPlayerPacket(ID_PLAYER_CHARGEN)->Send();
        }

        // Mark character generation as finished until overridden by a new ID_PLAYER_CHARGEN packet
        charGenState.isFinished = true;
    }

    return true;
}

bool LocalPlayer::isLoggedIn()
{
    if (charGenState.isFinished && (charGenState.endStage > 1 || receivedCharacter))
        return true;

    return false;
}

void LocalPlayer::updateStatsDynamic(bool forceUpdate)
{
    if (statsDynamicIndexChanges.size() > 0)
        statsDynamicIndexChanges.clear();

    MWWorld::Ptr ptrPlayer = getPlayerPtr();

    MWMechanics::CreatureStats *ptrCreatureStats = &ptrPlayer.getClass().getCreatureStats(ptrPlayer);
    MWMechanics::DynamicStat<float> health(ptrCreatureStats->getHealth());
    MWMechanics::DynamicStat<float> magicka(ptrCreatureStats->getMagicka());
    MWMechanics::DynamicStat<float> fatigue(ptrCreatureStats->getFatigue());

    static MWMechanics::DynamicStat<float> oldHealth(ptrCreatureStats->getHealth());
    static MWMechanics::DynamicStat<float> oldMagicka(ptrCreatureStats->getMagicka());
    static MWMechanics::DynamicStat<float> oldFatigue(ptrCreatureStats->getFatigue());


    // Update stats when they become 0 or they have changed enough
    auto needUpdate = [](MWMechanics::DynamicStat<float> &oldVal, MWMechanics::DynamicStat<float> &newVal, int limit) {
        return oldVal != newVal && (newVal.getCurrent() == 0 || oldVal.getCurrent() == 0
                                    || abs(oldVal.getCurrent() - newVal.getCurrent()) >= limit);
    };

    if (forceUpdate || needUpdate(oldHealth, health, 2))
        statsDynamicIndexChanges.push_back(0);

    if (forceUpdate || needUpdate(oldMagicka, magicka, 4))
        statsDynamicIndexChanges.push_back(1);

    if (forceUpdate || needUpdate(oldFatigue, fatigue, 4))
        statsDynamicIndexChanges.push_back(2);

    if (forceUpdate || statsDynamicIndexChanges.size() > 0)
    {
        oldHealth = health;
        oldMagicka = magicka;
        oldFatigue = fatigue;

        health.writeState(creatureStats.mDynamic[0]);
        magicka.writeState(creatureStats.mDynamic[1]);
        fatigue.writeState(creatureStats.mDynamic[2]);

        creatureStats.mDead = ptrCreatureStats->isDead();

        exchangeFullInfo = false;
        getNetworking()->getPlayerPacket(ID_PLAYER_STATS_DYNAMIC)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_STATS_DYNAMIC)->Send();
    }
}

void LocalPlayer::updateAttributes(bool forceUpdate)
{
    // Only send attributes if we are not a werewolf, or they will be
    // overwritten by the werewolf ones
    if (isWerewolf) return;

    if (attributeIndexChanges.size() > 0)
        attributeIndexChanges.clear();

    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    const MWMechanics::NpcStats &ptrNpcStats = ptrPlayer.getClass().getNpcStats(ptrPlayer);

    for (int i = 0; i < 8; ++i)
    {
        if (ptrNpcStats.getAttribute(ESM::Attribute::indexToRefId(i)).getBase() != creatureStats.mAttributes[i].mBase ||
            ptrNpcStats.getAttribute(ESM::Attribute::indexToRefId(i)).getModifier() != creatureStats.mAttributes[i].mMod ||
            ptrNpcStats.getAttribute(ESM::Attribute::indexToRefId(i)).getDamage() != creatureStats.mAttributes[i].mDamage ||
            ptrNpcStats.getSkillIncreasesForAttribute(Utils::getAttributeId(i)) != npcStats.mSkillIncrease[i] ||
            forceUpdate)
        {
            attributeIndexChanges.push_back(i);
            ptrNpcStats.getAttribute(ESM::Attribute::indexToRefId(i)).writeState(creatureStats.mAttributes[i]);
            npcStats.mSkillIncrease[i] = ptrNpcStats.getSkillIncreasesForAttribute(Utils::getAttributeId(i));
        }
    }

    if (attributeIndexChanges.size() > 0)
    {
        exchangeFullInfo = false;
        getNetworking()->getPlayerPacket(ID_PLAYER_ATTRIBUTE)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_ATTRIBUTE)->Send();
    }
}

void LocalPlayer::updateSkills(bool forceUpdate)
{
    // Only send skills if we are not a werewolf, or they will be
    // overwritten by the werewolf ones
    if (isWerewolf) return;

    if (skillIndexChanges.size() > 0)
        skillIndexChanges.clear();

    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    const MWMechanics::NpcStats &ptrNpcStats = ptrPlayer.getClass().getNpcStats(ptrPlayer);

    for (int i = 0; i < 27; ++i)
    {
        // Update a skill if its base value has changed at all or its progress has changed enough
        if (ptrNpcStats.getSkill(ESM::Skill::indexToRefId(i)).getBase() != npcStats.mSkills[i].mBase ||
            ptrNpcStats.getSkill(ESM::Skill::indexToRefId(i)).getModifier() != npcStats.mSkills[i].mMod ||
            ptrNpcStats.getSkill(ESM::Skill::indexToRefId(i)).getDamage() != npcStats.mSkills[i].mDamage ||
            abs(ptrNpcStats.getSkill(ESM::Skill::indexToRefId(i)).getProgress() - npcStats.mSkills[i].mProgress) > 0.75 ||
            forceUpdate)
        {
            skillIndexChanges.push_back(i);
            ptrNpcStats.getSkill(ESM::Skill::indexToRefId(i)).writeState(npcStats.mSkills[i]);
        }
    }

    if (skillIndexChanges.size() > 0)
    {
        exchangeFullInfo = false;
        getNetworking()->getPlayerPacket(ID_PLAYER_SKILL)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_SKILL)->Send();
    }
}

void LocalPlayer::updateLevel(bool forceUpdate)
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    const MWMechanics::NpcStats &ptrNpcStats = ptrPlayer.getClass().getNpcStats(ptrPlayer);

    if (ptrNpcStats.getLevel() != creatureStats.mLevel ||
        ptrNpcStats.getLevelProgress() != npcStats.mLevelProgress ||
        forceUpdate)
    {
        creatureStats.mLevel = ptrNpcStats.getLevel();
        npcStats.mLevelProgress = ptrNpcStats.getLevelProgress();
        getNetworking()->getPlayerPacket(ID_PLAYER_LEVEL)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_LEVEL)->Send();
    }
}

void LocalPlayer::updateBounty(bool forceUpdate)
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    const MWMechanics::NpcStats &ptrNpcStats = ptrPlayer.getClass().getNpcStats(ptrPlayer);

    if (ptrNpcStats.getBounty() != npcStats.mBounty || forceUpdate)
    {
        npcStats.mBounty = ptrNpcStats.getBounty();
        getNetworking()->getPlayerPacket(ID_PLAYER_BOUNTY)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_BOUNTY)->Send();
    }
}

void LocalPlayer::updateReputation(bool forceUpdate)
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    const MWMechanics::NpcStats &ptrNpcStats = ptrPlayer.getClass().getNpcStats(ptrPlayer);

    if (ptrNpcStats.getReputation() != npcStats.mReputation || forceUpdate)
    {
        npcStats.mReputation = ptrNpcStats.getReputation();
        getNetworking()->getPlayerPacket(ID_PLAYER_REPUTATION)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_REPUTATION)->Send();
    }
}

void LocalPlayer::updatePosition(bool forceUpdate)
{
    MWBase::World *world = MWBase::Environment::get().getWorld();
    MWWorld::Ptr ptrPlayer = world->getPlayerPtr();

    static bool posWasChanged = false;
    static bool isJumping = false;
    static bool sentJumpEnd = true;
    static float oldRot[2] = {0};

    position = ptrPlayer.getRefData().getPosition();

    bool posIsChanging = (direction.pos[0] != 0 || direction.pos[1] != 0 ||
        direction.rot[0] != 0 || direction.rot[1] != 0 || direction.rot[2] != 0);

    // Animations can change a player's position without actually creating directional movement,
    // so update positions accordingly
    if (!posIsChanging && isPlayingAnimation)
    {
        if (MWBase::Environment::get().getMechanicsManager()->checkAnimationPlaying(ptrPlayer, animation.groupname))
            posIsChanging = true;
        else
            isPlayingAnimation = false;
    }

    if (forceUpdate || posIsChanging || posWasChanged)
    {
        oldRot[0] = position.rot[0];
        oldRot[1] = position.rot[2];

        posWasChanged = posIsChanging;

        if (!isJumping && !world->isOnGround(ptrPlayer) && !world->isFlying(ptrPlayer))
            isJumping = true;

        getNetworking()->getPlayerPacket(ID_PLAYER_POSITION)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_POSITION)->Send();
    }
    else if (isJumping && world->isOnGround(ptrPlayer))
    {
        isJumping = false;
        sentJumpEnd = false;
    }
    // Packet with jump end position has to be sent one tick after above check
    else if (!sentJumpEnd)
    {
        sentJumpEnd = true;
        position = ptrPlayer.getRefData().getPosition();
        getNetworking()->getPlayerPacket(ID_PLAYER_POSITION)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_POSITION)->Send();
    }
}

void LocalPlayer::updateCell(bool forceUpdate)
{
    const ESM::Cell *ptrCell = &MWBase::Environment::get().getWorld()->getPlayerPtr().getCell()->getCell()->getEsm3();

    // If the LocalPlayer's Ptr cell is different from the LocalPlayer's packet cell, proceed
    if (forceUpdate || !Main::get().getCellController()->isSameCell(*ptrCell, cell))
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending ID_PLAYER_CELL_CHANGE about LocalPlayer to server");

        LOG_APPEND(TimedLog::LOG_INFO, "- Moved from %s to %s", cell.getShortDescription().c_str(),
                   ptrCell->getShortDescription().c_str());

        if (cell.mRegion != ptrCell->mRegion)
        {
            LOG_APPEND(TimedLog::LOG_INFO, "- Changed region from %s to %s",
                cell.mRegion.empty() ? "none" : Utils::refIdToCString(cell.mRegion),
                ptrCell->mRegion.empty() ? "none" : Utils::refIdToCString(ptrCell->mRegion));

            isChangingRegion = true;
        }

        cell = *ptrCell;
        previousCellPosition = position;

        // Make sure the position is updated before a cell packet is sent, or else
        // cell change events in server scripts will have the wrong player position
        updatePosition(true);

        getNetworking()->getPlayerPacket(ID_PLAYER_CELL_CHANGE)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_CELL_CHANGE)->Send();

        isChangingRegion = false;

        // If this is an interior cell, are there any other players in it? If so,
        // enable their markers
        if (!ptrCell->isExterior())
        {
            mwmp::PlayerList::enableMarkers(*ptrCell);
        }
    }
}

void LocalPlayer::updateEquipment(bool forceUpdate)
{
    if (equipmentIndexChanges.size() > 0)
        equipmentIndexChanges.clear();

    MWWorld::Ptr ptrPlayer = getPlayerPtr();

    MWWorld::InventoryStore &invStore = ptrPlayer.getClass().getInventoryStore(ptrPlayer);
    for (int slot = 0; slot < MWWorld::InventoryStore::Slots; slot++)
    {
        auto &item = equipmentItems[slot];
        MWWorld::ContainerStoreIterator it = invStore.getSlot(slot);

        if (it != invStore.end())
        {
            MWWorld::CellRef &cellRef = it->getCellRef();

            if ((cellRef.getRefId() == item.refId) == false ||
                cellRef.getCharge() != item.charge ||
                Utils::compareFloats(cellRef.getEnchantmentCharge(), item.enchantmentCharge, 1.0f) == false ||
                it->getCellRef().getCount() != item.count ||
                forceUpdate)
            {
                equipmentIndexChanges.push_back(slot);

                item.refId = it->getCellRef().getRefId().serializeText();
                item.count = it->getCellRef().getCount();
                item.charge = it->getCellRef().getCharge();
                item.enchantmentCharge = it->getCellRef().getEnchantmentCharge();
            }
        }
        else if (!item.refId.empty())
        {
            equipmentIndexChanges.push_back(slot);
            item.refId = "";
            item.count = 0;
            item.charge = -1;
            item.enchantmentCharge = -1;
        }
    }

    if (equipmentIndexChanges.size() > 0)
    {
        exchangeFullInfo = false;
        getNetworking()->getPlayerPacket(ID_PLAYER_EQUIPMENT)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_EQUIPMENT)->Send();
    }
}

void LocalPlayer::updateInventory(bool forceUpdate)
{
    static bool invChanged = false;

    if (forceUpdate)
        invChanged = true;

    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWWorld::InventoryStore &ptrInventory = ptrPlayer.getClass().getInventoryStore(ptrPlayer);
    mwmp::Item item;

    auto setItem = [](Item &item, const MWWorld::Ptr &iter) {
        item.refId = iter.getCellRef().getRefId().serializeText();
        if (Utils::isDynamicRefId(item.refId))
            return true;
        item.count = iter.getCellRef().getCount();
        item.charge = iter.getCellRef().getCharge();
        item.enchantmentCharge = iter.getCellRef().getEnchantmentCharge();
        item.soul = iter.getCellRef().getSoul().serializeText();

        return false;
    };

    if (!invChanged)
    {
        for (const auto &itemOld : inventoryChanges.items)
        {
            auto result = ptrInventory.begin();
            for (; result != ptrInventory.end(); ++result)
            {
                if(setItem(item, *result))
                    continue;

                if (item == itemOld)
                    break;
            }
            if (result == ptrInventory.end())
            {
                invChanged = true;
                break;
            }
        }
    }

    if (!invChanged)
    {
        for (const auto &iter : ptrInventory)
        {
            if(setItem(item, iter))
                continue;

            auto items = inventoryChanges.items;

            if (find(items.begin(), items.end(), item) == items.end())
            {
                invChanged = true;
                break;
            }
        }
    }

    if (!invChanged)
        return;

    invChanged = false;

    sendInventory();
}

void LocalPlayer::updateAttackOrCast()
{
    if (attack.shouldSend)
    {
        getNetworking()->getPlayerPacket(ID_PLAYER_ATTACK)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_ATTACK)->Send();

        attack.shouldSend = false;
    }
    else if (cast.shouldSend)
    {
        getNetworking()->getPlayerPacket(ID_PLAYER_CAST)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_CAST)->Send();

        cast.shouldSend = false;
        cast.hasProjectile = false;
    }
}

void LocalPlayer::updateAnimFlags(bool forceUpdate)
{
    MWBase::World *world = MWBase::Environment::get().getWorld();
    MWWorld::Ptr ptrPlayer = world->getPlayerPtr();

    MWMechanics::NpcStats ptrNpcStats = ptrPlayer.getClass().getNpcStats(ptrPlayer);
    using namespace MWMechanics;

    static bool wasRunning = ptrNpcStats.getMovementFlag(CreatureStats::Flag_Run);
    static bool wasSneaking = ptrNpcStats.getMovementFlag(CreatureStats::Flag_Sneak);
    static bool wasForceJumping = ptrNpcStats.getMovementFlag(CreatureStats::Flag_ForceJump);
    static bool wasForceMoveJumping = ptrNpcStats.getMovementFlag(CreatureStats::Flag_ForceMoveJump);

    bool isRunning = ptrNpcStats.getMovementFlag(CreatureStats::Flag_Run);
    bool isSneaking = ptrNpcStats.getMovementFlag(CreatureStats::Flag_Sneak);
    bool isForceJumping = ptrNpcStats.getMovementFlag(CreatureStats::Flag_ForceJump);
    bool isForceMoveJumping = ptrNpcStats.getMovementFlag(CreatureStats::Flag_ForceMoveJump);
    
    isFlying = world->isFlying(ptrPlayer);
    isJumping = !world->isOnGround(ptrPlayer) && !isFlying;

    // We need to send a new packet at the end of jumping, flying and TCL-ing too,
    // so keep track of what we were doing last frame
    static bool wasJumping = false;
    static bool wasFlying = false;
    static bool hadTcl = false;

    drawState = static_cast<char>(ptrPlayer.getClass().getNpcStats(ptrPlayer).getDrawState());
    static char lastDrawState = static_cast<char>(ptrPlayer.getClass().getNpcStats(ptrPlayer).getDrawState());

    if (wasRunning != isRunning ||
        wasSneaking != isSneaking || wasForceJumping != isForceJumping ||
        wasForceMoveJumping != isForceMoveJumping || lastDrawState != drawState ||
        wasJumping || isJumping || wasFlying != isFlying || hadTcl != hasTcl ||
        forceUpdate)
    {
        wasSneaking = isSneaking;
        wasRunning = isRunning;
        wasForceJumping = isForceJumping;
        wasForceMoveJumping = isForceMoveJumping;
        lastDrawState = drawState;
        
        wasJumping = isJumping;
        wasFlying = isFlying;
        hadTcl = hasTcl;

        movementFlags = 0;

#define __SETFLAG(flag, value) (value) ? (movementFlags | flag) : (movementFlags & ~flag)

        movementFlags = __SETFLAG(CreatureStats::Flag_Sneak, isSneaking);
        movementFlags = __SETFLAG(CreatureStats::Flag_Run, isRunning);
        movementFlags = __SETFLAG(CreatureStats::Flag_ForceJump, isForceJumping);
        movementFlags = __SETFLAG(CreatureStats::Flag_ForceJump, isJumping);
        movementFlags = __SETFLAG(CreatureStats::Flag_ForceMoveJump, isForceMoveJumping);

#undef __SETFLAG

        if (isJumping)
            updatePosition(true); // fix position after jump;

        getNetworking()->getPlayerPacket(ID_PLAYER_ANIM_FLAGS)->setPlayer(this);
        getNetworking()->getPlayerPacket(ID_PLAYER_ANIM_FLAGS)->Send();
    }
}

void LocalPlayer::addItems()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    const MWWorld::ESMStore &esmStore = MWBase::Environment::get().getWorld()->getStore();
    MWWorld::ContainerStore &ptrStore = ptrPlayer.getClass().getContainerStore(ptrPlayer);

    for (const auto &item : inventoryChanges.items)
    {
        // Skip bound items
        if (MWBase::Environment::get().getMechanicsManager()->isBoundItem(Utils::toRefId(item.refId)))
            continue;

        try
        {
            MWWorld::ManualRef itemRef(esmStore, Utils::toRefId(item.refId), item.count);
            MWWorld::Ptr itemPtr = itemRef.getPtr();

            if (item.charge != -1)
                itemPtr.getCellRef().setCharge(item.charge);

            if (item.enchantmentCharge != -1)
                itemPtr.getCellRef().setEnchantmentCharge(item.enchantmentCharge);

            if (!item.soul.empty())
                itemPtr.getCellRef().setSoul(Utils::toRefId(item.soul));

            LOG_APPEND(TimedLog::LOG_INFO, "- Adding inventory item %s with count %i", item.refId.c_str(), item.count);

            ptrStore.add(itemPtr, item.count, false);
        }
        catch (std::exception&)
        {
            LOG_APPEND(TimedLog::LOG_INFO, "- Ignored addition of invalid inventory item %s", item.refId.c_str());
        }
    }

    updateInventoryWindow();
}

void LocalPlayer::addSpells()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWMechanics::Spells &ptrSpells = ptrPlayer.getClass().getCreatureStats(ptrPlayer).getSpells();

    for (const auto &spell : spellbookChanges.spells)
        // Only add spells that are ensured to exist
        if (MWBase::Environment::get().getWorld()->getStore().get<ESM::Spell>().search(spell.mId))
            ptrSpells.add(spell.mId);
        else
            LOG_APPEND(TimedLog::LOG_INFO, "- Ignored addition of invalid spell %s", Utils::refIdToCString(spell.mId));
}

void LocalPlayer::addSpellsActive()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWMechanics::ActiveSpells& activeSpells = ptrPlayer.getClass().getCreatureStats(ptrPlayer).getActiveSpells();

    for (const auto& activeSpell : spellsActiveChanges.activeSpells)
    {
        const ESM::RefNum casterActorId = MechanicsHelper::getActorId(activeSpell.caster);

        // Don't do a check for a spell's existence, because active effects from potions need to be applied here too
        activeSpells.addMultiplayerSpell(Utils::toRefId(activeSpell.id), activeSpell.isStackingSpell,
            activeSpell.params.mEffects, activeSpell.params.mDisplayName, casterActorId);
    }
}

void LocalPlayer::addJournalItems()
{
    for (const auto &journalItem : journalChanges)
    {
        MWWorld::Ptr ptrFound;

        if (journalItem.type == JournalItem::ENTRY)
        {
            LOG_APPEND(TimedLog::LOG_VERBOSE, "- type: ENTRY, quest: %s, index: %i, actorRefId: %s",
                journalItem.quest.c_str(), journalItem.index, journalItem.actorRefId.c_str());

            ptrFound = MWBase::Environment::get().getWorld()->searchPtr(Utils::toRefId(journalItem.actorRefId), false);

            if (!ptrFound)
                ptrFound = getPlayerPtr();
        }
        else
        {
            LOG_APPEND(TimedLog::LOG_VERBOSE, "- type: INDEX, quest: %s, index: %i",
                journalItem.quest.c_str(), journalItem.index);
        }

        try
        {
            if (journalItem.type == JournalItem::ENTRY)
            {
                if (journalItem.hasTimestamp)
                {
                    MWBase::Environment::get().getJournal()->addEntry(Utils::toRefId(journalItem.quest), journalItem.index, ptrFound,
                        journalItem.timestamp.daysPassed, journalItem.timestamp.month, journalItem.timestamp.day);
                }
                else
                {
                    MWBase::Environment::get().getJournal()->addEntry(Utils::toRefId(journalItem.quest), journalItem.index, ptrFound);
                }
            }
            else
                MWBase::Environment::get().getJournal()->setJournalIndex(Utils::toRefId(journalItem.quest), journalItem.index);
        }
        catch (std::exception&)
        {
            LOG_APPEND(TimedLog::LOG_INFO, "- Ignored addition of invalid journal quest %s", journalItem.quest.c_str());
        }
    }
}

void LocalPlayer::addTopics()
{
    auto &env = MWBase::Environment::get();
    for (const auto &topic : topicChanges)
    {
        std::string topicId = topic.topicId;

        // If we're using a translated version of Morrowind, translate this topic from English into our language
        if (env.getWindowManager()->getTranslationDataStorage().hasTranslation())
            topicId = env.getWindowManager()->getTranslationDataStorage().getLocalizedTopicId(topicId);

        env.getDialogueManager()->addTopic(Utils::toRefId(topicId));

        if (env.getWindowManager()->containsMode(MWGui::GM_Dialogue))
            env.getDialogueManager()->updateActorKnownTopics();
    }
}

void LocalPlayer::removeItems()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWWorld::ContainerStore &ptrStore = ptrPlayer.getClass().getContainerStore(ptrPlayer);

    for (const auto &item : inventoryChanges.items)
    {
        ptrStore.remove(Utils::toRefId(item.refId), item.count);

        LOG_APPEND(TimedLog::LOG_INFO, "- Removing inventory item %s with count %i", item.refId.c_str(), item.count);
    }
}

void LocalPlayer::removeSpells()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWMechanics::Spells &ptrSpells = ptrPlayer.getClass().getCreatureStats(ptrPlayer).getSpells();

    MWBase::WindowManager *wm = MWBase::Environment::get().getWindowManager();
    for (const auto &spell : spellbookChanges.spells)
    {
        ptrSpells.remove(spell.mId);
        if (spell.mId == wm->getSelectedSpell())
            wm->unsetSelectedSpell();
    }
}

void LocalPlayer::removeSpellsActive()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWMechanics::ActiveSpells& activeSpells = ptrPlayer.getClass().getCreatureStats(ptrPlayer).getActiveSpells();
 
    for (const auto& activeSpell : spellsActiveChanges.activeSpells)
    {
        LOG_APPEND(TimedLog::LOG_INFO, "- removing %sstacking active spell %s", activeSpell.isStackingSpell ? "" : "non-", activeSpell.id.c_str());

        // OpenMW 0.49+ doesn't timestamp active spells, so stacking spells are removed one instance at a time
        if (activeSpell.isStackingSpell)
        {
            if (!activeSpells.removeMultiplayerSpell(ptrPlayer, Utils::toRefId(activeSpell.id)))
                LOG_APPEND(TimedLog::LOG_INFO, "-- spell with this ID could not be found!");
        }
        else
        {
            activeSpells.removeEffectsBySourceSpellId(ptrPlayer, Utils::toRefId(activeSpell.id));
        }
    }
}

void LocalPlayer::die()
{
    creatureStats.mDead = true;

    MWWorld::Ptr playerPtr = MWBase::Environment::get().getWorld()->getPlayerPtr();
    MWMechanics::DynamicStat<float> health = playerPtr.getClass().getCreatureStats(playerPtr).getHealth();
    health.setCurrent(0);
    playerPtr.getClass().getCreatureStats(playerPtr).setHealth(health);

    Main::get().getNetworking()->getPlayerPacket(ID_PLAYER_DEATH)->setPlayer(this);
    Main::get().getNetworking()->getPlayerPacket(ID_PLAYER_DEATH)->Send();
}

void LocalPlayer::resurrect()
{
    creatureStats.mDead = false;

    MWWorld::Ptr ptrPlayer = getPlayerPtr();

    if (resurrectType == mwmp::RESURRECT_TYPE::IMPERIAL_SHRINE)
        MWBase::Environment::get().getWorld()->teleportToClosestMarker(ptrPlayer, Utils::toRefId("divinemarker"));
    else if (resurrectType == mwmp::RESURRECT_TYPE::TRIBUNAL_TEMPLE)
        MWBase::Environment::get().getWorld()->teleportToClosestMarker(ptrPlayer, Utils::toRefId("templemarker"));

    MWBase::Environment::get().getMechanicsManager()->resurrect(ptrPlayer);

    // The player could have died from a hand-to-hand attack, so reset their fatigue
    // as well
    if (creatureStats.mDynamic[2].mMod < 1)
        creatureStats.mDynamic[2].mMod = 1;

    creatureStats.mDynamic[2].mCurrent = creatureStats.mDynamic[2].mMod;
    MWMechanics::DynamicStat<float> fatigue;
    fatigue.readState(creatureStats.mDynamic[2]);
    ptrPlayer.getClass().getCreatureStats(ptrPlayer).setFatigue(fatigue);

    // If this player had a weapon or spell readied when dying, they will still have it
    // readied but be unable to use it unless we clear it here
    ptrPlayer.getClass().getNpcStats(ptrPlayer).setDrawState(MWMechanics::DrawState::Nothing);

    // Record that the player has died since the last attempt was made to arrest them,
    // used to make guards lenient enough to attempt an arrest again
    diedSinceArrestAttempt = true;

    deathTime = time(0);

    LOG_APPEND(TimedLog::LOG_INFO, "- diedSinceArrestAttempt is now true");

    // Record that we are no longer a known werewolf, to avoid being attacked infinitely
    MWBase::Environment::get().getWorld()->setGlobalInt(std::string_view("pcknownwerewolf"), 0);

    // Ensure we unequip any items with constant effects that can put us into an infinite
    // death loop
    static const ESM::RefId damageEffects[5] = { ESM::MagicEffect::DrainHealth, ESM::MagicEffect::FireDamage,
        ESM::MagicEffect::FrostDamage, ESM::MagicEffect::ShockDamage, ESM::MagicEffect::SunDamage };

    for (const auto &damageEffect : damageEffects)
        MechanicsHelper::unequipItemsByEffect(ptrPlayer, ESM::Enchantment::ConstantEffect, damageEffect);

    Main::get().getNetworking()->getPlayerPacket(ID_PLAYER_RESURRECT)->setPlayer(this);
    Main::get().getNetworking()->getPlayerPacket(ID_PLAYER_RESURRECT)->Send();

    updateStatsDynamic(true);
}

void LocalPlayer::closeInventoryWindows()
{
    if (MWBase::Environment::get().getWindowManager()->containsMode(MWGui::GM_Container) ||
        MWBase::Environment::get().getWindowManager()->containsMode(MWGui::GM_Inventory))
        MWBase::Environment::get().getWindowManager()->popGuiMode();

    MWBase::Environment::get().getWindowManager()->finishDragDrop();
}

void LocalPlayer::updateInventoryWindow()
{
    MWBase::Environment::get().getWindowManager()->getInventoryWindow()->updateItemView();
}

void LocalPlayer::setCharacter()
{
    receivedCharacter = true;

    MWBase::World *world = MWBase::Environment::get().getWorld();

    // Ignore invalid races
    if (world->getStore().get<ESM::Race>().search(npc.mRace) != 0)
    {
        MWBase::Environment::get().getWorld()->getPlayer().setBirthSign(Utils::toRefId(birthsign));

        if (resetStats)
        {
            MWBase::Environment::get().getMechanicsManager()->setPlayerRace(npc.mRace, npc.isMale(), npc.mHead, npc.mHair);
            setEquipment();
        }
        else
        {
            ESM::NPC player = *world->getPlayerPtr().get<ESM::NPC>()->mBase;

            player.mRace = npc.mRace;
            player.mHead = npc.mHead;
            player.mHair = npc.mHair;
            player.mModel = npc.mModel;
            player.setIsMale(npc.isMale());
            world->getStore().insert(player);

            MWBase::Environment::get().getMechanicsManager()->playerLoaded();

            // This is needed to update the player's model instantly if they're in 3rd person
            world->reattachPlayerCamera();
        }

        MWBase::Environment::get().getWindowManager()->getInventoryWindow()->rebuildAvatar();
    }
    else
    {
        LOG_APPEND(TimedLog::LOG_INFO, "- Character update was ignored due to invalid race %s", Utils::refIdToCString(npc.mRace));
    }
}

void LocalPlayer::setDynamicStats()
{
    MWBase::World *world = MWBase::Environment::get().getWorld();
    MWWorld::Ptr ptrPlayer = world->getPlayerPtr();

    MWMechanics::CreatureStats *ptrCreatureStats = &ptrPlayer.getClass().getCreatureStats(ptrPlayer);
    MWMechanics::DynamicStat<float> dynamicStat;

    for (int i = 0; i < 3; ++i)
    {
        dynamicStat = ptrCreatureStats->getDynamic(i);
        dynamicStat.setBase(creatureStats.mDynamic[i].mBase);
        dynamicStat.setCurrent(creatureStats.mDynamic[i].mCurrent);
        ptrCreatureStats->setDynamic(i, dynamicStat);
    }
}

void LocalPlayer::setAttributes()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();

    MWMechanics::NpcStats *ptrNpcStats = &ptrPlayer.getClass().getNpcStats(ptrPlayer);
    MWMechanics::AttributeValue attributeValue;

    for (int attributeIndex = 0; attributeIndex < 8; ++attributeIndex)
    {
        // If the server wants to clear our attribute's non-zero modifier, we need to remove
        // the spell effect causing it, to avoid an infinite loop where the effect keeps resetting
        // the modifier
        if (creatureStats.mAttributes[attributeIndex].mMod == 0 && ptrNpcStats->getAttribute(ESM::Attribute::indexToRefId(attributeIndex)).getModifier() > 0)
        {
            ptrNpcStats->getActiveSpells().purgeEffect(ptrPlayer, ESM::MagicEffect::FortifyAttribute,
                ESM::Attribute::indexToRefId(attributeIndex));
            MWBase::Environment::get().getMechanicsManager()->updateMagicEffects(ptrPlayer);

            // Is the modifier for this attribute still higher than 0? If so, unequip items that
            // fortify the attribute
            if (ptrNpcStats->getAttribute(ESM::Attribute::indexToRefId(attributeIndex)).getModifier() > 0)
            {
                MechanicsHelper::unequipItemsByEffect(ptrPlayer, ESM::Enchantment::ConstantEffect, ESM::MagicEffect::FortifyAttribute,
                    ESM::Attribute::indexToRefId(attributeIndex));
                mwmp::Main::get().getGUIController()->refreshGuiMode(MWGui::GM_Inventory);
            }
        }

        attributeValue.readState(creatureStats.mAttributes[attributeIndex]);
        ptrNpcStats->setAttribute(ESM::Attribute::indexToRefId(attributeIndex), attributeValue);

        ptrNpcStats->setSkillIncreasesForAttribute(Utils::getAttributeId(attributeIndex), npcStats.mSkillIncrease[attributeIndex]);
    }
}

void LocalPlayer::setSkills()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();

    MWMechanics::NpcStats *ptrNpcStats = &ptrPlayer.getClass().getNpcStats(ptrPlayer);
    MWMechanics::SkillValue skillValue;

    for (int skillIndex = 0; skillIndex < 27; ++skillIndex)
    {
        // If the server wants to clear our skill's non-zero modifier, we need to remove
        // the spell effect causing it, to avoid an infinite loop where the effect keeps resetting
        // the modifier
        if (npcStats.mSkills[skillIndex].mMod == 0 && ptrNpcStats->getSkill(ESM::Skill::indexToRefId(skillIndex)).getModifier() > 0)
        {
            ptrNpcStats->getActiveSpells().purgeEffect(ptrPlayer, ESM::MagicEffect::FortifySkill,
                ESM::Skill::indexToRefId(skillIndex));
            MWBase::Environment::get().getMechanicsManager()->updateMagicEffects(ptrPlayer);

            // Is the modifier for this skill still higher than 0? If so, unequip items that
            // fortify the skill
            if (ptrNpcStats->getSkill(ESM::Skill::indexToRefId(skillIndex)).getModifier() > 0)
            {
                MechanicsHelper::unequipItemsByEffect(ptrPlayer, ESM::Enchantment::ConstantEffect, ESM::MagicEffect::FortifySkill,
                    ESM::RefId(), ESM::Skill::indexToRefId(skillIndex));
                mwmp::Main::get().getGUIController()->refreshGuiMode(MWGui::GM_Inventory);
            }
        }

        skillValue.readState(npcStats.mSkills[skillIndex]);
        ptrNpcStats->setSkill(ESM::Skill::indexToRefId(skillIndex), skillValue);
    }
}

void LocalPlayer::setLevel()
{
    MWBase::World *world = MWBase::Environment::get().getWorld();
    MWWorld::Ptr ptrPlayer = world->getPlayerPtr();

    MWMechanics::NpcStats *ptrNpcStats = &ptrPlayer.getClass().getNpcStats(ptrPlayer);
    ptrNpcStats->setLevel(creatureStats.mLevel);
    ptrNpcStats->setLevelProgress(npcStats.mLevelProgress);
}

void LocalPlayer::setBounty()
{
    MWBase::World *world = MWBase::Environment::get().getWorld();
    MWWorld::Ptr ptrPlayer = world->getPlayerPtr();

    MWMechanics::NpcStats *ptrNpcStats = &ptrPlayer.getClass().getNpcStats(ptrPlayer);
    ptrNpcStats->setBounty(npcStats.mBounty);
}

void LocalPlayer::setReputation()
{
    MWBase::World *world = MWBase::Environment::get().getWorld();
    MWWorld::Ptr ptrPlayer = world->getPlayerPtr();

    MWMechanics::NpcStats *ptrNpcStats = &ptrPlayer.getClass().getNpcStats(ptrPlayer);
    ptrNpcStats->setReputation(npcStats.mReputation);
}

void LocalPlayer::setPosition()
{
    MWBase::World *world = MWBase::Environment::get().getWorld();
    MWWorld::Ptr ptrPlayer = world->getPlayerPtr();

    // If we're ignoring this position packet because of an invalid cell change,
    // don't make the next one get ignored as well
    if (ignorePosPacket)
        ignorePosPacket = false;
    else
    {
        world->getPlayer().setTeleported(true);

        world->moveObject(ptrPlayer, osg::Vec3f(position.pos[0], position.pos[1], position.pos[2]));
        world->rotateObject(ptrPlayer, osg::Vec3f(position.rot[0], position.rot[1], position.rot[2]));
        world->setInertialForce(ptrPlayer, osg::Vec3f(0.f, 0.f, 0.f));
    }

    updatePosition(true);

    // Make sure we update our draw state, or we'll end up with the wrong one
    updateAnimFlags(true);
}

void LocalPlayer::setMomentum()
{
    MWBase::World *world = MWBase::Environment::get().getWorld();
    MWWorld::Ptr ptrPlayer = world->getPlayerPtr();
    world->setInertialForce(ptrPlayer, momentum.asVec3());
}

void LocalPlayer::setCell()
{
    MWBase::World *world = MWBase::Environment::get().getWorld();
    MWWorld::Ptr ptrPlayer = world->getPlayerPtr();
    ESM::Position pos;

    // To avoid crashes, close container windows this player may be in
    closeInventoryWindows();

    world->getPlayer().setTeleported(true);

    int x = cell.mData.mX;
    int y = cell.mData.mY;

    if (cell.isExterior())
    {
        const osg::Vec2f cellCenter = ESM::indexToPosition(
            ESM::ExteriorCellLocation(x, y, ESM::Cell::sDefaultWorldspaceId), true);
        pos.pos[0] = cellCenter.x();
        pos.pos[1] = cellCenter.y();
        pos.pos[2] = 0;

        pos.rot[0] = pos.rot[1] = pos.rot[2] = 0;

        world->changeToCell(ESM::RefId::esm3ExteriorCell(x, y), pos, true);
        world->fixPosition();
    }
    else if (const ESM::RefId exteriorCellId = world->findExteriorPosition(cell.mName, pos); !exteriorCellId.empty())
    {
        world->changeToCell(exteriorCellId, pos, true);
        world->fixPosition();
    }
    else
    {
        try
        {
            world->findInteriorPosition(cell.mName, pos);
            world->changeToInteriorCell(cell.mName, pos, true);
        }
        // If we've been sent to an invalid interior, ignore the incoming
        // packet about our position in that cell
        catch (std::exception&)
        {
            LOG_APPEND(TimedLog::LOG_INFO, "%s", "- Cell doesn't exist on this client");
            ignorePosPacket = true;
        }
    }

    updateCell(true);
}

void LocalPlayer::setClass()
{
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received ID_PLAYER_CLASS from server");

    if (charClass.mId.empty()) // custom class
    {
        charClass.mData.mIsPlayable = 0x1;
        MWBase::Environment::get().getMechanicsManager()->setPlayerClass(charClass);
    }
    else
    {
        const ESM::Class *existingCharClass = MWBase::Environment::get().getWorld()->getStore().get<ESM::Class>().search(charClass.mId);

        if (existingCharClass)
        {
            MWBase::Environment::get().getMechanicsManager()->setPlayerClass(charClass.mId);
        }
        else
            LOG_APPEND(TimedLog::LOG_INFO, "- Ignored invalid default class %s", Utils::refIdToCString(charClass.mId));
    }
}

void LocalPlayer::setEquipment()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();

    MWWorld::InventoryStore &ptrInventory = ptrPlayer.getClass().getInventoryStore(ptrPlayer);

    for (int slot = 0; slot < MWWorld::InventoryStore::Slots; slot++)
    {
        mwmp::Item &currentItem = equipmentItems[slot];

        if (!currentItem.refId.empty())
        {
            auto it = std::find_if(ptrInventory.begin(), ptrInventory.end(), [&currentItem](const MWWorld::Ptr &itemPtr) {
                return (itemPtr.getCellRef().getRefId() == currentItem.refId);
            });

            // If the item is not in our inventory, add it as long as it's not a bound item
            if (it == ptrInventory.end())
            {
                if (!MWBase::Environment::get().getMechanicsManager()->isBoundItem(Utils::toRefId(currentItem.refId)))
                {
                    try
                    {
                        auto addIter = ptrInventory.ContainerStore::add(Utils::toRefId(currentItem.refId), currentItem.count, false);

                        ptrInventory.equip(slot, addIter);
                    }
                    catch (std::exception&)
                    {
                        LOG_APPEND(TimedLog::LOG_INFO, "- Ignored addition of invalid equipment item %s", currentItem.refId.c_str());
                    }
                }
            }
            else
            {
                // Don't try to equip an item that is already equipped
                if (ptrInventory.getSlot(slot) != it)
                    ptrInventory.equip(slot, it);
            }
        }
        else
            ptrInventory.unequipSlot(slot);
    }

    MWBase::Environment::get().getWindowManager()->getInventoryWindow()->updatePlayer();
}

void LocalPlayer::setInventory()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWWorld::ContainerStore &ptrStore = ptrPlayer.getClass().getContainerStore(ptrPlayer);

    // Ensure no item is being drag and dropped
    MWBase::Environment::get().getWindowManager()->finishDragDrop();

    // Clear items in inventory
    ptrStore.clear();

    // Proceed by adding items
    addItems();

    // Don't automatically setEquipment() here, or the player could end
    // up getting a new set of their starting clothes, or other items
    // supposed to no longer exist
    //
    // Instead, expect server scripts to do that manually
}

void LocalPlayer::setSpellbook()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWMechanics::Spells &ptrSpells = ptrPlayer.getClass().getCreatureStats(ptrPlayer).getSpells();

    // Clear spells in spellbook, while ignoring abilities, powers, etc.
    std::vector<ESM::RefId> spellsToRemove;
    for (const ESM::Spell* spell : ptrSpells)
    {
        if (spell->mData.mType == ESM::Spell::ST_Spell)
            spellsToRemove.push_back(spell->mId);
    }
    for (const ESM::RefId& spellId : spellsToRemove)
        ptrSpells.remove(spellId);

    // Proceed by adding spells
    addSpells();
}

void LocalPlayer::setSpellsActive()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWMechanics::ActiveSpells& activeSpells = ptrPlayer.getClass().getCreatureStats(ptrPlayer).getActiveSpells();
    activeSpells.clear(ptrPlayer);

    // Proceed by adding spells active
    addSpellsActive();
}

void LocalPlayer::setCooldowns()
{
    MWBase::World* world = MWBase::Environment::get().getWorld();
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWMechanics::Spells& ptrSpells = ptrPlayer.getClass().getCreatureStats(ptrPlayer).getSpells();

    for (const auto& cooldown : cooldownChanges)
    {
        if (const ESM::Spell* spell = world->getStore().get<ESM::Spell>().search(Utils::toRefId(cooldown.id)))
        {

            ptrSpells.setPowerUseTimestamp(spell, cooldown.startTimestampDay, cooldown.startTimestampHour);
        }
    }
}

void LocalPlayer::setQuickKeys()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received ID_PLAYER_QUICKKEYS from server");

    for (const auto &quickKey : quickKeyChanges)
    {
        LOG_APPEND(TimedLog::LOG_INFO, "- slot: %i, type: %i, itemId: %s", quickKey.slot, quickKey.type, quickKey.itemId.c_str());

        if (quickKey.type == QuickKey::ITEM || quickKey.type == QuickKey::ITEM_MAGIC)
        {
            MWWorld::InventoryStore &ptrInventory = ptrPlayer.getClass().getInventoryStore(ptrPlayer);

            auto it = std::find_if(ptrInventory.begin(), ptrInventory.end(), [&quickKey](const MWWorld::Ptr &inventoryItem) {
                return (inventoryItem.getCellRef().getRefId() == quickKey.itemId);
            });

            if (it != ptrInventory.end())
                MWBase::Environment::get().getWindowManager()->setQuickKey(quickKey.slot, quickKey.type, *it);
        }
        else if (quickKey.type == QuickKey::MAGIC)
        {
            MWMechanics::Spells &ptrSpells = ptrPlayer.getClass().getCreatureStats(ptrPlayer).getSpells();
            const ESM::RefId spellId = Utils::toRefId(quickKey.itemId);

            if (ptrSpells.hasSpell(spellId))
                MWBase::Environment::get().getWindowManager()->setQuickKey(quickKey.slot, quickKey.type, MWWorld::Ptr(), spellId);
        }
        else
            MWBase::Environment::get().getWindowManager()->setQuickKey(quickKey.slot, quickKey.type, MWWorld::Ptr());
    }
}

void LocalPlayer::setFactions()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWMechanics::NpcStats &ptrNpcStats = ptrPlayer.getClass().getNpcStats(ptrPlayer);

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received ID_PLAYER_FACTION from server - action: %i", factionChanges.action);

    for (const auto &faction : factionChanges.factions)
    {
        LOG_APPEND(TimedLog::LOG_VERBOSE, " - processing faction: %s", faction.factionId.c_str());
        const ESM::RefId factionId = Utils::toRefId(faction.factionId);
        const ESM::Faction *esmFaction = MWBase::Environment::get().getWorld()->getStore().get<ESM::Faction>().search(factionId);

        if (!esmFaction)
        {
            LOG_APPEND(TimedLog::LOG_INFO, "- Ignored invalid faction %s", faction.factionId.c_str());
            continue;
        }

        if (factionChanges.action == mwmp::FactionChanges::RANK)
        {

            if (!ptrNpcStats.isInFaction(factionId))
            {
                // If the player isn't in this faction, make them join it
                ptrNpcStats.joinFaction(factionId);
                LOG_APPEND(TimedLog::LOG_VERBOSE, "\t>JOINED FACTION: %s on rank change to: %d.",
                    faction.factionId.c_str(), faction.rank);
            }

            // While the faction rank is different in the packet than in the NpcStats,
            // adjust the NpcStats accordingly
            if (faction.rank != ptrNpcStats.getFactionRank(factionId))
                ptrNpcStats.setFactionRank(factionId, faction.rank);
        }
        else if (factionChanges.action == mwmp::FactionChanges::EXPULSION)
        {
            // If the expelled state is different in the packet than in the NpcStats,
            // adjust the NpcStats accordingly
            if (faction.isExpelled != ptrNpcStats.getExpelled(factionId))
            {
                if (faction.isExpelled)
                    ptrNpcStats.expell(factionId, false);
                else
                    ptrNpcStats.clearExpelled(factionId);
            }
        }

        else if (factionChanges.action == mwmp::FactionChanges::REPUTATION)
            ptrNpcStats.setFactionReputation(factionId, faction.reputation);
    }
}

void LocalPlayer::setBooks()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWMechanics::NpcStats &ptrNpcStats = ptrPlayer.getClass().getNpcStats(ptrPlayer);

    for (const auto &book : bookChanges)
        ptrNpcStats.flagAsUsed(Utils::toRefId(book.bookId));
}

void LocalPlayer::setShapeshift()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();

    MWBase::Environment::get().getWorld()->scaleObject(ptrPlayer, scale);
    MWBase::Environment::get().getMechanicsManager()->setWerewolf(ptrPlayer, isWerewolf);
}

void LocalPlayer::setMarkLocation()
{
    MWWorld::CellStore *ptrCellStore = Main::get().getCellController()->getCellStore(markCell);

    if (ptrCellStore)
        MWBase::Environment::get().getWorld()->getPlayer().markPosition(ptrCellStore, markPosition);
}

void LocalPlayer::setSelectedSpell()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();

    MWMechanics::CreatureStats& stats = ptrPlayer.getClass().getCreatureStats(ptrPlayer);
    MWMechanics::Spells& spells = stats.getSpells();

    const ESM::RefId spellId = Utils::toRefId(selectedSpellId);

    // Spells::hasSpell() looks the id up in the store, which throws for an empty id
    if (spellId.empty() || !MWBase::Environment::get().getESMStore()->get<ESM::Spell>().search(spellId)
        || !spells.hasSpell(spellId))
        return;

    MWBase::Environment::get().getWindowManager()->setSelectedSpell(spellId,
        int(MWMechanics::getSpellSuccessChance(spellId, ptrPlayer)));
}

void LocalPlayer::sendDeath(char newDeathState)
{
    if (MechanicsHelper::isEmptyTarget(killer))
        killer = MechanicsHelper::getTarget(getPlayerPtr());

    deathState = newDeathState;

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending ID_PLAYER_DEATH about myself to server\n- deathState: %d", deathState);
    getNetworking()->getPlayerPacket(ID_PLAYER_DEATH)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_DEATH)->Send();

    MechanicsHelper::clearTarget(killer);
}

void LocalPlayer::sendClass()
{
    MWBase::World *world = MWBase::Environment::get().getWorld();
    const ESM::NPC *npcBase = world->getPlayerPtr().get<ESM::NPC>()->mBase;
    const ESM::Class *esmClass = world->getStore().get<ESM::Class>().find(npcBase->mClass);

    if (npcBase->mClass.is<ESM::GeneratedRefId>() || npcBase->mClass.contains("$dynamic")) // custom class
    {
        charClass.mId = Utils::toRefId("");
        charClass.mName = esmClass->mName;
        charClass.mDescription = esmClass->mDescription;
        charClass.mData = esmClass->mData;
    }
    else
        charClass.mId = esmClass->mId;

    getNetworking()->getPlayerPacket(ID_PLAYER_CHARCLASS)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_CHARCLASS)->Send();
}

void LocalPlayer::sendInventory()
{
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending entire inventory to server");

    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWWorld::InventoryStore &ptrInventory = ptrPlayer.getClass().getInventoryStore(ptrPlayer);
    mwmp::Item item;

    inventoryChanges.items.clear();

    for (const auto &iter : ptrInventory)
    {
        item.refId = iter.getCellRef().getRefId().serializeText();

        // Skip any items that somehow have clientside-only dynamic IDs
        if (Utils::isDynamicRefId(item.refId))
            continue;

        // Skip bound items
        if (MWBase::Environment::get().getMechanicsManager()->isBoundItem(Utils::toRefId(item.refId)))
            continue;

        item.count = iter.getCellRef().getCount();
        item.charge = iter.getCellRef().getCharge();
        item.enchantmentCharge = iter.getCellRef().getEnchantmentCharge();
        item.soul = iter.getCellRef().getSoul().serializeText();

        inventoryChanges.items.push_back(item);
    }

    inventoryChanges.action = InventoryChanges::SET;
    getNetworking()->getPlayerPacket(ID_PLAYER_INVENTORY)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_INVENTORY)->Send();
}

void LocalPlayer::sendItemChange(const mwmp::Item& item, unsigned int action)
{
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending item change for %s with action %i, count %i",
        item.refId.c_str(), action, item.count);

    inventoryChanges.items.clear();
    inventoryChanges.items.push_back(item);
    inventoryChanges.action = action;

    getNetworking()->getPlayerPacket(ID_PLAYER_INVENTORY)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_INVENTORY)->Send();
}

void LocalPlayer::sendItemChange(const MWWorld::Ptr& itemPtr, int count, unsigned int action)
{
    mwmp::Item item = MechanicsHelper::getItem(itemPtr, count);
    sendItemChange(item, action);
}

void LocalPlayer::sendItemChange(const std::string& refId, int count, unsigned int action)
{
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending item change for %s with action %i, count %i",
        refId.c_str(), action, count);

    inventoryChanges.items.clear();
    
    mwmp::Item item;
    item.refId = refId;
    item.count = count;
    item.charge = -1;
    item.enchantmentCharge = -1;
    item.soul = "";

    inventoryChanges.items.push_back(item);

    inventoryChanges.action = action;
    getNetworking()->getPlayerPacket(ID_PLAYER_INVENTORY)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_INVENTORY)->Send();
}

void LocalPlayer::sendStoredItemRemovals()
{
    inventoryChanges.items.clear();

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending stored item removals for LocalPlayer:");

    for (auto storedItemRemoval : storedItemRemovals)
    {
        mwmp::Item item;
        item.refId = storedItemRemoval.first;
        item.count = storedItemRemoval.second;
        item.charge = -1;
        item.enchantmentCharge = -1;
        item.soul = "";
        inventoryChanges.items.push_back(item);

        LOG_APPEND(TimedLog::LOG_INFO, "- %s with count %i", item.refId.c_str(), item.count);
    }

    inventoryChanges.action = mwmp::InventoryChanges::ACTION_TYPE::REMOVE;
    getNetworking()->getPlayerPacket(ID_PLAYER_INVENTORY)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_INVENTORY)->Send();

    storedItemRemovals.clear();
}

void LocalPlayer::sendSpellbook()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWMechanics::Spells &ptrSpells = ptrPlayer.getClass().getCreatureStats(ptrPlayer).getSpells();

    spellbookChanges.spells.clear();

    // Send spells in spellbook, while ignoring abilities, powers, etc.
    for (const auto &spell : ptrSpells)
    {
        if (spell->mData.mType == ESM::Spell::ST_Spell)
            spellbookChanges.spells.push_back(*spell);
    }

    spellbookChanges.action = SpellbookChanges::SET;
    getNetworking()->getPlayerPacket(ID_PLAYER_SPELLBOOK)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_SPELLBOOK)->Send();
}

void LocalPlayer::sendSpellChange(std::string id, unsigned int action)
{
    // Skip any bugged spells that somehow have clientside-only dynamic IDs
    if (Utils::isDynamicRefId(id))
        return;

    spellbookChanges.spells.clear();

    ESM::Spell spell;
    spell.mId = Utils::toRefId(id);
    spellbookChanges.spells.push_back(spell);

    spellbookChanges.action = action;
    getNetworking()->getPlayerPacket(ID_PLAYER_SPELLBOOK)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_SPELLBOOK)->Send();
}

void LocalPlayer::sendSpellsActive()
{
    MWWorld::Ptr ptrPlayer = getPlayerPtr();
    MWMechanics::ActiveSpells& activeSpells = ptrPlayer.getClass().getCreatureStats(ptrPlayer).getActiveSpells();

    spellsActiveChanges.activeSpells.clear();

    // Send spells in spellbook, while ignoring abilities, powers, etc.
    for (const auto& ptrSpell : activeSpells)
    {
        mwmp::ActiveSpell packetSpell;
        packetSpell.id = ptrSpell.getSourceSpellId().serializeText();
        packetSpell.isStackingSpell = ptrSpell.hasFlag(ESM::ActiveSpells::Flag_Stackable);
        packetSpell.caster = MechanicsHelper::getTarget(MWBase::Environment::get().getWorldModel()->getPtr(ptrSpell.getCaster()));
        packetSpell.params.mSourceSpellId = ptrSpell.getSourceSpellId();
        packetSpell.params.mDisplayName = ptrSpell.getDisplayName();
        packetSpell.params.mEffects = ptrSpell.getEffects();
        spellsActiveChanges.activeSpells.push_back(packetSpell);
    }

    spellsActiveChanges.action = mwmp::SpellsActiveChanges::SET;
    getNetworking()->getPlayerPacket(ID_PLAYER_SPELLS_ACTIVE)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_SPELLS_ACTIVE)->Send();
}

void LocalPlayer::sendSpellsActiveAddition(const std::string id, bool isStackingSpell, const MWMechanics::ActiveSpells::ActiveSpellParams& params)
{
    // Skip any bugged spells that somehow have clientside-only dynamic IDs
    if (Utils::isDynamicRefId(id))
        return;

    spellsActiveChanges.activeSpells.clear();


    const MWWorld::Ptr caster = MWBase::Environment::get().getWorldModel()->getPtr(params.getCaster());
    const MWWorld::TimeStamp timeStamp = MWBase::Environment::get().getWorld()->getTimeStamp();

    mwmp::ActiveSpell spell;
    spell.id = id;
    spell.isStackingSpell = isStackingSpell;
    spell.caster = MechanicsHelper::getTarget(caster);
    spell.timestampDay = timeStamp.getDay();
    spell.timestampHour = timeStamp.getHour();
    spell.params.mEffects = params.getEffects();
    spell.params.mDisplayName = params.getDisplayName();
    spell.params.mSourceSpellId = params.getSourceSpellId();
    spellsActiveChanges.activeSpells.push_back(spell);

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending active spell addition with stacking %s, timestamp %i %f",
        spell.isStackingSpell ? "true" : "false", spell.timestampDay, spell.timestampHour);

    spellsActiveChanges.action = mwmp::SpellsActiveChanges::ADD;
    getNetworking()->getPlayerPacket(ID_PLAYER_SPELLS_ACTIVE)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_SPELLS_ACTIVE)->Send();
}

void LocalPlayer::sendSpellsActiveRemoval(const std::string id, bool isStackingSpell, MWWorld::TimeStamp timestamp)
{
    // Skip any bugged spells that somehow have clientside-only dynamic IDs
    if (Utils::isDynamicRefId(id))
        return;

    spellsActiveChanges.activeSpells.clear();

    mwmp::ActiveSpell spell;
    spell.id = id;
    spell.isStackingSpell = isStackingSpell;
    spell.timestampDay = timestamp.getDay();
    spell.timestampHour = timestamp.getHour();
    spellsActiveChanges.activeSpells.push_back(spell);

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending active spell removal with stacking %s, timestamp %i %f",
        spell.isStackingSpell ? "true" : "false", spell.timestampDay, spell.timestampHour);

    spellsActiveChanges.action = mwmp::SpellsActiveChanges::REMOVE;
    getNetworking()->getPlayerPacket(ID_PLAYER_SPELLS_ACTIVE)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_SPELLS_ACTIVE)->Send();
}

void LocalPlayer::sendCooldownChange(std::string id, int startTimestampDay, float startTimestampHour)
{
    // Skip any bugged spells that somehow have clientside-only dynamic IDs
    if (Utils::isDynamicRefId(id))
        return;

    cooldownChanges.clear();

    SpellCooldown spellCooldown;
    spellCooldown.id = id;
    spellCooldown.startTimestampDay = startTimestampDay;
    spellCooldown.startTimestampHour = startTimestampHour;

    cooldownChanges.push_back(spellCooldown);
;
    getNetworking()->getPlayerPacket(ID_PLAYER_COOLDOWNS)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_COOLDOWNS)->Send();
}

void LocalPlayer::sendQuickKey(unsigned short slot, int type, const std::string& itemId)
{
    quickKeyChanges.clear();

    mwmp::QuickKey quickKey;
    quickKey.slot = slot;
    quickKey.type = type;
    quickKey.itemId = itemId;

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending ID_PLAYER_QUICKKEYS", itemId.c_str());
    LOG_APPEND(TimedLog::LOG_INFO, "- slot: %i, type: %i, itemId: %s", quickKey.slot, quickKey.type, quickKey.itemId.c_str());

    quickKeyChanges.push_back(quickKey);

    getNetworking()->getPlayerPacket(ID_PLAYER_QUICKKEYS)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_QUICKKEYS)->Send();
}

void LocalPlayer::sendJournalEntry(const std::string& quest, int index, const MWWorld::Ptr& actor)
{
    journalChanges.clear();

    mwmp::JournalItem journalItem;
    journalItem.type = JournalItem::ENTRY;
    journalItem.quest = quest;
    journalItem.index = index;
    journalItem.actorRefId = actor.getCellRef().getRefId().serializeText();
    journalItem.hasTimestamp = false;

    journalChanges.push_back(journalItem);

    getNetworking()->getPlayerPacket(ID_PLAYER_JOURNAL)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_JOURNAL)->Send();
}

void LocalPlayer::sendJournalIndex(const std::string& quest, int index)
{
    journalChanges.clear();

    mwmp::JournalItem journalItem;
    journalItem.type = JournalItem::INDEX;
    journalItem.quest = quest;
    journalItem.index = index;

    journalChanges.push_back(journalItem);

    getNetworking()->getPlayerPacket(ID_PLAYER_JOURNAL)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_JOURNAL)->Send();
}

void LocalPlayer::sendFactionRank(const std::string& factionId, int rank)
{
    factionChanges.factions.clear();
    factionChanges.action = FactionChanges::RANK;

    mwmp::Faction faction;
    faction.factionId = factionId;
    faction.rank = rank;

    factionChanges.factions.push_back(faction);

    getNetworking()->getPlayerPacket(ID_PLAYER_FACTION)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_FACTION)->Send();
}

void LocalPlayer::sendFactionExpulsionState(const std::string& factionId, bool isExpelled)
{
    factionChanges.factions.clear();
    factionChanges.action = FactionChanges::EXPULSION;

    mwmp::Faction faction;
    faction.factionId = factionId;
    faction.isExpelled = isExpelled;

    factionChanges.factions.push_back(faction);

    getNetworking()->getPlayerPacket(ID_PLAYER_FACTION)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_FACTION)->Send();
}

void LocalPlayer::sendFactionReputation(const std::string& factionId, int reputation)
{
    factionChanges.factions.clear();
    factionChanges.action = FactionChanges::REPUTATION;

    mwmp::Faction faction;
    faction.factionId = factionId;
    faction.reputation = reputation;

    factionChanges.factions.push_back(faction);

    getNetworking()->getPlayerPacket(ID_PLAYER_FACTION)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_FACTION)->Send();
}

void LocalPlayer::sendTopic(const std::string& topicId)
{
    topicChanges.clear();

    mwmp::Topic topic;

    // For translated versions of the game, make sure we translate the topic back into English first
    if (MWBase::Environment::get().getWindowManager()->getTranslationDataStorage().hasTranslation())
        topic.topicId = std::string(MWBase::Environment::get().getWindowManager()->getTranslationDataStorage().topicKeyword(topicId));
    else
        topic.topicId = topicId;

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending ID_PLAYER_TOPIC with topic %s", topic.topicId.c_str());

    topicChanges.push_back(topic);

    getNetworking()->getPlayerPacket(ID_PLAYER_TOPIC)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_TOPIC)->Send();
}

void LocalPlayer::sendBook(const std::string& bookId)
{
    bookChanges.clear();

    mwmp::Book book;
    book.bookId = bookId;

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending ID_PLAYER_BOOK with book %s", book.bookId.c_str());

    bookChanges.push_back(book);

    getNetworking()->getPlayerPacket(ID_PLAYER_BOOK)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_BOOK)->Send();
}

void LocalPlayer::sendWerewolfState(bool werewolfState)
{
    isWerewolf = werewolfState;

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending ID_PLAYER_SHAPESHIFT with isWerewolf of %s", isWerewolf ? "true" : "false");

    getNetworking()->getPlayerPacket(ID_PLAYER_SHAPESHIFT)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_SHAPESHIFT)->Send();
}

void LocalPlayer::sendMarkLocation(const ESM::Cell& newMarkCell, const ESM::Position& newMarkPosition)
{
    miscellaneousChangeType = mwmp::MISCELLANEOUS_CHANGE_TYPE::MARK_LOCATION;
    markCell = newMarkCell;
    markPosition = newMarkPosition;

    getNetworking()->getPlayerPacket(ID_PLAYER_MISCELLANEOUS)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_MISCELLANEOUS)->Send();
}

void LocalPlayer::sendSelectedSpell(const std::string& newSelectedSpellId)
{
    miscellaneousChangeType = mwmp::MISCELLANEOUS_CHANGE_TYPE::SELECTED_SPELL;
    selectedSpellId = newSelectedSpellId;

    getNetworking()->getPlayerPacket(ID_PLAYER_MISCELLANEOUS)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_MISCELLANEOUS)->Send();
}

void LocalPlayer::sendItemUse(const MWWorld::Ptr& itemPtr, bool itemMagicState, char currentDrawState)
{
    usedItem.refId = itemPtr.getCellRef().getRefId().serializeText();
    usedItem.count = itemPtr.getCellRef().getCount();
    usedItem.charge = itemPtr.getCellRef().getCharge();
    usedItem.enchantmentCharge = itemPtr.getCellRef().getEnchantmentCharge();
    usedItem.soul = itemPtr.getCellRef().getSoul().serializeText();

    usingItemMagic = itemMagicState;
    itemUseDrawState = currentDrawState;

    getNetworking()->getPlayerPacket(ID_PLAYER_ITEM_USE)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_ITEM_USE)->Send();
}

void LocalPlayer::sendCellStates()
{
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending ID_PLAYER_CELL_STATE to server");
    getNetworking()->getPlayerPacket(ID_PLAYER_CELL_STATE)->setPlayer(this);
    getNetworking()->getPlayerPacket(ID_PLAYER_CELL_STATE)->Send();
}

void LocalPlayer::clearCellStates()
{
    cellStateChanges.clear();
}

void LocalPlayer::clearCurrentContainer()
{
    currentContainer.refId = "";
    currentContainer.refNum = 0;
    currentContainer.mpNum = 0;
}

void LocalPlayer::storeCellState(const ESM::Cell& storedCell, int stateType)
{
    std::vector<CellState>::iterator iter;

    for (iter = cellStateChanges.begin(); iter != cellStateChanges.end(); )
    {
        // If there's already a cell state recorded for this particular cell,
        // remove it
        if (storedCell.getShortDescription() == (*iter).cell.getShortDescription())
            iter = cellStateChanges.erase(iter);
        else
            ++iter;
    }

    CellState cellState;
    cellState.cell = storedCell;
    cellState.type = stateType;

    cellStateChanges.push_back(cellState);
}

void LocalPlayer::storeCurrentContainer(const MWWorld::Ptr &container)
{
    currentContainer.refId = container.getCellRef().getRefId().serializeText();
    currentContainer.refNum = container.getCellRef().getMpRefNumIndex();
    currentContainer.mpNum = container.getCellRef().getMpNum();
}

void LocalPlayer::storeItemRemoval(const std::string& refId, int count)
{
    storedItemRemovals[refId] = storedItemRemovals[refId] + count;
}

void LocalPlayer::storeLastEnchantmentQuantity(unsigned int quantity)
{
    lastEnchantmentQuantity = quantity;
}

void LocalPlayer::playAnimation()
{
    MWBase::Environment::get().getMechanicsManager()->playAnimationGroup(getPlayerPtr(),
        animation.groupname, animation.mode, animation.count, animation.persist);

    isPlayingAnimation = true;
}

void LocalPlayer::playSpeech()
{
    MWBase::Environment::get().getSoundManager()->say(getPlayerPtr(),
        Misc::ResourceHelpers::correctSoundPath(VFS::Path::Normalized(sound)));

    MWBase::WindowManager *winMgr = MWBase::Environment::get().getWindowManager();
    if (Settings::gui().mSubtitles)

        winMgr->messageBox(MWBase::Environment::get().getDialogueManager()->getVoiceCaption(sound), MWGui::ShowInDialogueMode_Never);
}
