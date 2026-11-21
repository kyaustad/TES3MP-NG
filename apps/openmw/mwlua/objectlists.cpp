#include "objectlists.hpp"

#include <components/misc/resourcehelpers.hpp>

#include "../mwbase/environment.hpp"

#include "../mwclass/container.hpp"

#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/scene.hpp"
#include "../mwworld/worldmodel.hpp"

namespace MWLua
{

    void ObjectLists::update()
    {
        mActivatorsInScene.updateList();
        mActorsInScene.updateList();
        mContainersInScene.updateList();
        mDoorsInScene.updateList();
        mItemsInScene.updateList();
    }

    void ObjectLists::clear()
    {
        mActivatorsInScene.clear();
        mActorsInScene.clear();
        mContainersInScene.clear();
        mDoorsInScene.clear();
        mItemsInScene.clear();
    }

    ObjectLists::ObjectGroup* ObjectLists::chooseGroup(const MWWorld::Ptr& ptr)
    {
        // It is important to check `isMarker` first.
        // For example "prisonmarker" has class "Door" despite that it is only an invisible marker.
        if (Misc::ResourceHelpers::isHiddenMarker(ptr.getCellRef().getRefId()))
            return nullptr;
        const MWWorld::Class& cls = ptr.getClass();
        if (cls.isActivator())
            return &mActivatorsInScene;
        if (cls.isActor())
            return &mActorsInScene;
        if (ptr.mRef->getType() == ESM::REC_DOOR || ptr.mRef->getType() == ESM::REC_DOOR4)
            return &mDoorsInScene;
        if (typeid(cls) == typeid(MWClass::Container))
            return &mContainersInScene;
        if (cls.isItem(ptr) || ptr.mRef->getType() == ESM::REC_LIGH)
            return &mItemsInScene;
        return nullptr;
    }

    void ObjectLists::mpSetPlayers(const MWWorld::Ptr& localPlayer, const std::vector<MWWorld::Ptr>& remotePlayers)
    {
        MWWorld::WorldModel* worldModel = MWBase::Environment::get().getWorldModel();
        MWWorld::Scene* scene = MWBase::Environment::get().getWorldScene();
        std::vector<ObjectId> all{ getId(localPlayer) };
        std::vector<ObjectId> nearby{ getId(localPlayer) };
        for (const MWWorld::Ptr& ptr : remotePlayers)
        {
            // Remote players move between cells, active or not
            worldModel->registerPtr(ptr);
            all.push_back(getId(ptr));
            if (scene->isCellActive(*ptr.getCell()))
                nearby.push_back(getId(ptr));
        }
        if (all != *mPlayers)
            *mPlayers = std::move(all);
        if (nearby != *mNearbyPlayers)
            *mNearbyPlayers = std::move(nearby);
    }

    void ObjectLists::objectAddedToScene(const MWWorld::Ptr& ptr)
    {
        MWBase::Environment::get().getWorldModel()->registerPtr(ptr);
        ObjectGroup* group = chooseGroup(ptr);
        if (group)
            addToGroup(*group, ptr);
    }

    void ObjectLists::objectRemovedFromScene(const MWWorld::Ptr& ptr)
    {
        ObjectGroup* group = chooseGroup(ptr);
        if (group)
            removeFromGroup(*group, ptr);
    }

    void ObjectLists::ObjectGroup::updateList()
    {
        if (mChanged)
        {
            mList->clear();
            for (ObjectId id : mSet)
                mList->push_back(id);
            mChanged = false;
        }
    }

    void ObjectLists::ObjectGroup::clear()
    {
        mChanged = false;
        mList->clear();
        mSet.clear();
    }

    void ObjectLists::addToGroup(ObjectGroup& group, const MWWorld::Ptr& ptr)
    {
        group.mSet.insert(getId(ptr));
        group.mChanged = true;
    }

    void ObjectLists::removeFromGroup(ObjectGroup& group, const MWWorld::Ptr& ptr)
    {
        group.mSet.erase(getId(ptr));
        group.mChanged = true;
    }
}
