#ifndef OPENMW_MECHANICSHELPER_HPP
#define OPENMW_MECHANICSHELPER_HPP

#include <components/openmw-mp/Base/BaseStructs.hpp>

#include "../mwworld/containerstore.hpp"

#include <osg/Vec3f>


namespace MechanicsHelper
{
    osg::Vec3f getLinearInterpolation(osg::Vec3f start, osg::Vec3f end, float percent);
    ESM::Position getPositionFromVector(osg::Vec3f vector);

    void spawnLeveledCreatures(MWWorld::CellStore* cellStore);

    bool isUsingRangedWeapon(const MWWorld::Ptr& ptr);

    mwmp::Attack *getLocalAttack(const MWWorld::Ptr& ptr);
    mwmp::Attack *getDedicatedAttack(const MWWorld::Ptr& ptr);

    mwmp::Cast *getLocalCast(const MWWorld::Ptr& ptr);
    mwmp::Cast *getDedicatedCast(const MWWorld::Ptr& ptr);

    MWWorld::Ptr getPlayerPtr(const mwmp::Target& target);
    ESM::RefNum getActorId(const mwmp::Target& target);

    mwmp::Item getItem(const MWWorld::Ptr& itemPtr, int count);
    mwmp::Target getTarget(const MWWorld::Ptr& ptr);
    void clearTarget(mwmp::Target& target);
    bool isEmptyTarget(const mwmp::Target& target);

    void assignAttackTarget(mwmp::Attack* attack, const MWWorld::Ptr& target);
    void resetAttack(mwmp::Attack* attack);
    void resetCast(mwmp::Cast* cast);

    // See whether playerChecked belongs to playerWithTeam's team
    // Note: This is not supposed to also check if playerWithTeam is on playerChecked's
    //       team, because it should technically be possible to be allied to someone
    //       who isn't mutually allied to you
    bool isTeamMember(const MWWorld::Ptr& playerChecked, const MWWorld::Ptr& playerWithTeam);

    // Whether this actor is simulated by another client: a DedicatedPlayer or a DedicatedActor
    bool isNetworkDriven(const MWWorld::Ptr& ptr);

    bool getSpellSuccess(const ESM::RefId& spellId, const MWWorld::Ptr& caster);

    void processAttack(mwmp::Attack attack, const MWWorld::Ptr& attacker);
    void processCast(mwmp::Cast cast, const MWWorld::Ptr& caster);

    void createSpellGfx(const MWWorld::Ptr& targetPtr, const std::vector<ESM::ActiveEffect>& mEffects);

    bool isStackingSpell(const ESM::RefId& id);
    bool isStackingSpell(const std::string& id);
    bool doesEffectListContainEffect(const ESM::EffectList& effectList, const ESM::RefId& effectId,
        const ESM::RefId& attributeId = {}, const ESM::RefId& skillId = {});
    void unequipItemsByEffect(const MWWorld::Ptr& ptr, short enchantmentType, const ESM::RefId& effectId,
        const ESM::RefId& attributeId = {}, const ESM::RefId& skillId = {});


    MWWorld::Ptr getItemPtrFromStore(const mwmp::Item& item, MWWorld::ContainerStore& store);
}


#endif //OPENMW_MECHANICSHELPER_HPP
