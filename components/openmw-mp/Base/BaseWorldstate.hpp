#ifndef OPENMW_BASEWORLDSTATE_HPP
#define OPENMW_BASEWORLDSTATE_HPP

#include <components/openmw-mp/Lua/LuaData.hpp>
#include <map>
#include <vector>

#include <components/esm3/loadacti.hpp>
#include <components/esm3/loadalch.hpp>
#include <components/esm3/loadappa.hpp>
#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadbody.hpp>
#include <components/esm3/loadbook.hpp>
#include <components/esm3/loadclot.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loaddoor.hpp>
#include <components/esm3/loadench.hpp>
#include <components/esm3/loadgmst.hpp>
#include <components/esm3/loadingr.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/esm3/loadlock.hpp>
#include <components/esm3/loadmisc.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadprob.hpp>
#include <components/esm3/loadrepa.hpp>
#include <components/esm3/loadscpt.hpp>
#include <components/esm3/loadspel.hpp>
#include <components/esm3/loadstat.hpp>
#include <components/esm3/loadweap.hpp>
#include <components/esm3/loadsoun.hpp>

#include <components/openmw-mp/Base/BaseStructs.hpp>

#include <RakNetTypes.h>

#include <components/openmw-mp/WinMacroGuard.hpp>
namespace mwmp
{
    enum RECORD_TYPE
    {
        ACTIVATOR,
        APPARATUS,
        ARMOR,
        BODYPART,
        BOOK,
        CELL,
        CLOTHING,
        CONTAINER,
        CREATURE,
        DOOR,
        ENCHANTMENT,
        GAMESETTING,
        INGREDIENT,
        LIGHT,
        LOCKPICK,
        MISCELLANEOUS,
        NPC,
        POTION,
        PROBE,
        REPAIR,
        SCRIPT,
        SOUND,
        SPELL,
        STATIC,
        WEAPON
    };

    // When using an existing record as a base, this struct tracks which changes
    // need to be made to it
    //
    // Note: These can't be replaced with checks for empty strings or numerical
    //       values of 0 because you want to be able to blank out strings or
    //       set values of 0 through overrides, i.e. if someone is in the
    //       Mages Guild faction, you want to be able to remove them from it
    //       by using a blank faction string as an override
    //
    //       There are, however, a few values that are not allowed to be blanked
    //       out in a record, such as races or classes for NPCs, and those
    //       should rely on checks for empty strings instead of having a
    //       boolean here
    struct BaseOverrides
    {
        bool hasSubtype = false;
        bool hasName = false;
        bool hasModel = false;
        bool hasIcon = false;
        bool hasScript = false;
        bool hasEnchantmentId = false;
        bool hasEnchantmentCharge = false;

        bool hasEffects = false;
        bool hasBodyParts = false;
        bool hasInventory = false;

        bool hasAutoCalc = false;
        bool hasCharge = false;
        bool hasCost = false;
        bool hasFlags = false;
        bool hasValue = false;
        bool hasWeight = false;
        bool hasQuality = false;
        bool hasUses = false;
        bool hasTime = false;
        bool hasRadius = false;
        bool hasColor = false;

        bool hasArmorRating = false;
        bool hasHealth = false;

        bool hasDamageChop = false;
        bool hasDamageSlash = false;
        bool hasDamageThrust = false;
        bool hasReach = false;
        bool hasSpeed = false;

        bool hasKeyState = false;
        bool hasScrollState = false;
        bool hasSkillId = false;
        bool hasText = false;

        bool hasHair = false;
        bool hasHead = false;
        bool hasGender = false;
        bool hasRace = false;
        bool hasFaction = false;

        bool hasScale = false;
        bool hasBloodType = false;
        bool hasBodyPartType = false;
        bool hasVampireState = false;

        bool hasLevel = false;
        bool hasMagicka = false;
        bool hasFatigue = false;

        bool hasSoulValue = false;

        bool hasAiFight = false;
        bool hasAiFlee = false;
        bool hasAiAlarm = false;
        bool hasAiServices = false;

        bool hasSound = false;
        bool hasOpenSound = false;
        bool hasCloseSound = false;

        bool hasScriptText = false;

        bool hasVolume = false;
        bool hasMinRange = false;
        bool hasMaxRange = false;
    };

    struct ActivatorRecord
    {
        ESM::Activator data;
        std::string baseId;
        BaseOverrides baseOverrides;
    };

    struct ApparatusRecord
    {
        ESM::Apparatus data;
        std::string baseId;
        BaseOverrides baseOverrides;
    };

    struct ArmorRecord
    {
        ESM::Armor data;
        std::string baseId;
        BaseOverrides baseOverrides;
    };

    struct BodyPartRecord
    {
        ESM::BodyPart data;
        std::string baseId;
        BaseOverrides baseOverrides;
    };

    struct BookRecord
    {
        ESM::Book data;
        std::string baseId;
        BaseOverrides baseOverrides;
    };

    struct CellRecord
    {
        ESM::Cell data;
        std::string baseId;
        BaseOverrides baseOverrides;
    };

    struct ClothingRecord
    {
        ESM::Clothing data;
        std::string baseId;
        BaseOverrides baseOverrides;
    };

    struct ContainerRecord
    {
        ESM::Container data;
        std::string baseId;
        std::vector<mwmp::Item> inventory;
        BaseOverrides baseOverrides;
    };

    struct CreatureRecord
    {
        ESM::Creature data;
        std::string baseId;
        std::string inventoryBaseId;
        std::vector<mwmp::Item> inventory;
        BaseOverrides baseOverrides;
    };

    struct DoorRecord
    {
        ESM::Door data;
        std::string baseId;
        BaseOverrides baseOverrides;
    };

    struct EnchantmentRecord
    {
        ESM::Enchantment data;
        std::string baseId;
        BaseOverrides baseOverrides;
    };

    struct GameSettingRecord
    {
        ESM::GameSetting data;
        std::string baseId;
        BaseOverrides baseOverrides;
        ClientVariable variable;
    };

    struct IngredientRecord
    {
        ESM::Ingredient data;
        std::string baseId;
        BaseOverrides baseOverrides;
    };

    struct LightRecord
    {
        ESM::Light data;
        std::string baseId;
        BaseOverrides baseOverrides;
    };

    struct LockpickRecord
    {
        ESM::Lockpick data;
        std::string baseId;
        BaseOverrides baseOverrides;
    };

    struct MiscellaneousRecord
    {
        ESM::Miscellaneous data;
        std::string baseId;
        BaseOverrides baseOverrides;
    };

    struct NpcRecord
    {
        ESM::NPC data;
        std::string baseId;
        std::string inventoryBaseId;
        std::vector<mwmp::Item> inventory;
        BaseOverrides baseOverrides;
    };

    struct PotionRecord
    {
        ESM::Potion data;
        unsigned int quantity = 1;
        std::string baseId;
        BaseOverrides baseOverrides;
    };

    struct ProbeRecord
    {
        ESM::Probe data;
        std::string baseId;
        BaseOverrides baseOverrides;
    };

    struct RepairRecord
    {
        ESM::Repair data;
        std::string baseId;
        BaseOverrides baseOverrides;
    };

    struct ScriptRecord
    {
        ESM::Script data;
        std::string baseId;
        BaseOverrides baseOverrides;
    };

    struct SoundRecord
    {
        ESM::Sound data;
        std::string baseId;
        BaseOverrides baseOverrides;
    };

    struct SpellRecord
    {
        ESM::Spell data;
        std::string baseId;
        BaseOverrides baseOverrides;
    };

    struct StaticRecord
    {
        ESM::Static data;
        std::string baseId;
        BaseOverrides baseOverrides;
    };

    struct WeaponRecord
    {
        ESM::Weapon data;
        unsigned int quantity = 1;
        std::string baseId;
        BaseOverrides baseOverrides;
    };

    static const int maxImageDataSize = 1800;

    struct MapTile
    {
        int x;
        int y;
        std::vector<char> imageData;
    };

    struct Weather
    {
        std::string region;
        int currentWeather;
        int nextWeather;
        int queuedWeather;
        float transitionFactor;
    };

    struct Kill
    {
        std::string refId;
        int number;
    };

    /*
        A message of the OpenMW Lua synchronization, see docs/tes3mp/lua-multiplayer.md
    */
    struct LuaMessage
    {
        enum Kind : uint8_t
        {
            CONFIG = 0,        // server -> client: name is the script policy JSON
            AUTHORITY = 1,     // server -> client: target is the Lua world authority's player
            GLOBAL_EVENT = 2,  // name is the event, data its payload
            LOCAL_EVENT = 3,   // name is the event, target the receiving object, cell the object's cell
            STORAGE_SET = 4,   // name is the global storage section, key the key, data the value (empty to remove)
            STORAGE_RESET = 5, // name is the global storage section, data its whole content as a table
            GLOBAL_STATE = 6,  // data is the saved state of authority mode global scripts
            REQUEST_STATE = 7  // server -> authority: send GLOBAL_STATE now
        };

        enum Flags : uint8_t
        {
            FLAG_NONE = 0,
            FLAG_AUTHORITY_ONLY = 1, // GLOBAL_EVENT: only authority mode scripts should receive it
            FLAG_FROM_SERVER = 2,    // sent by server scripts
            FLAG_PERSISTENT = 4      // STORAGE_SET: the section survives server restarts
        };

        uint8_t kind = GLOBAL_EVENT;
        uint8_t flags = FLAG_NONE;
        std::string name;
        std::string key;
        NetObjectId target;
        std::string cell;
        std::string data;
    };

    class BaseWorldstate
    {
    public:

        BaseWorldstate()
        {
            time.year = -1;
            time.month = -1;
            time.day = -1;
            time.hour = -1;

            time.daysPassed = -1;
            time.timeScale = -1;
        }

        RakNet::RakNetGUID guid;

        mwmp::Time time;
        std::vector<std::string> synchronizedClientScriptIds;
        std::vector<std::string> synchronizedClientGlobalIds;

        std::vector<ClientVariable> clientGlobals;

        bool hasPlayerCollision;
        bool hasActorCollision;
        bool hasPlacedObjectCollision;
        bool useActorCollisionForPlacedObjects;

        std::string authorityRegion;

        std::vector<Kill> killChanges;
        std::vector<LuaMessage> luaMessages;
        std::vector<std::string> enforcedCollisionRefIds;
        std::map<std::string, std::string> destinationOverrides;

        std::vector<MapTile> mapTiles;

        bool forceWeather;
        Weather weather;

        unsigned short recordsType;
        unsigned int recordsCount;

        std::vector<ActivatorRecord> activatorRecords;
        std::vector<ApparatusRecord> apparatusRecords;
        std::vector<ArmorRecord> armorRecords;
        std::vector<BodyPartRecord> bodyPartRecords;
        std::vector<BookRecord> bookRecords;
        std::vector<CellRecord> cellRecords;
        std::vector<ClothingRecord> clothingRecords;
        std::vector<ContainerRecord> containerRecords;
        std::vector<CreatureRecord> creatureRecords;
        std::vector<DoorRecord> doorRecords;
        std::vector<EnchantmentRecord> enchantmentRecords;
        std::vector<GameSettingRecord> gameSettingRecords;
        std::vector<IngredientRecord> ingredientRecords;
        std::vector<LightRecord> lightRecords;
        std::vector<LockpickRecord> lockpickRecords;
        std::vector<MiscellaneousRecord> miscellaneousRecords;
        std::vector<NpcRecord> npcRecords;
        std::vector<PotionRecord> potionRecords;
        std::vector<ProbeRecord> probeRecords;
        std::vector<RepairRecord> repairRecords;
        std::vector<ScriptRecord> scriptRecords;
        std::vector<SoundRecord> soundRecords;
        std::vector<SpellRecord> spellRecords;
        std::vector<StaticRecord> staticRecords;
        std::vector<WeaponRecord> weaponRecords;

        std::vector<ESM::Cell> cellsToReset;

        bool isValid;
    };
}

#endif //OPENMW_BASEWORLDSTATE_HPP
