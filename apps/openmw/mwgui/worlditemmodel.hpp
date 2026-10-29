#ifndef OPENMW_APPS_OPENMW_MWGUI_WORLDITEMMODEL_H
#define OPENMW_APPS_OPENMW_MWGUI_WORLDITEMMODEL_H

#include "itemmodel.hpp"

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/world.hpp>

#include <components/esm/refid.hpp>

#include <MyGUI_InputManager.h>
#include <MyGUI_RenderManager.h>

#include <stdexcept>

/*
    Start of tes3mp addition

    Include additional headers for multiplayer purposes
*/
#include <apps/openmw/mwmp/Main.hpp>
#include <apps/openmw/mwmp/Networking.hpp>
#include <apps/openmw/mwmp/ObjectList.hpp>
/*
    End of tes3mp addition
*/

namespace MWGui
{
    // Makes it possible to use ItemModel::moveItem to move an item from an inventory to the world.
    class WorldItemModel : public ItemModel
    {
        MWWorld::Ptr dropItemImpl(const ItemStack& item, int count, bool copy)
        {
            MWBase::World& world = *MWBase::Environment::get().getWorld();

            const MWWorld::Ptr player = world.getPlayerPtr();

            world.breakInvisibility(player);

            const MWWorld::Ptr dropped = world.canPlaceObject(mCursorX, mCursorY)
                ? world.placeObject(item.mBase, mCursorX, mCursorY, count, copy)
                : world.dropObjectOnGround(player, item.mBase, count, copy);

            dropped.getCellRef().setOwner(ESM::RefId());

            /*
                Start of tes3mp addition

                Send an ID_OBJECT_PLACE packet every time an object is dropped into the world from the inventory
                screen, then delete it and wait for the server to send it back with a unique mpNum of its own
            */
            if (mwmp::Main::isInitialized())
            {
                mwmp::ObjectList* objectList = mwmp::Main::get().getNetworking()->getObjectList();
                objectList->reset();
                objectList->packetOrigin = mwmp::CLIENT_GAMEPLAY;
                objectList->addObjectPlace(dropped, true);
                objectList->sendObjectPlace();
                world.deleteObject(dropped);
            }
            /*
                End of tes3mp addition
            */

            return dropped;
        }

    public:
        explicit WorldItemModel(float cursorX, float cursorY)
            : mCursorX(cursorX)
            , mCursorY(cursorY)
        {
        }

        ModelIndex getIndex(const ItemStack& /*item*/) override
        {
            throw std::runtime_error("WorldItemModel::getIndex is not implemented");
        }

        void update() override {}

        size_t getItemCount() override { return 0; }

        ItemStack getItem(ModelIndex /*index*/) override
        {
            throw std::runtime_error("WorldItemModel::getItem is not implemented");
        }

        bool usesContainer(const MWWorld::Ptr&) override { return false; }

    protected:
        MWWorld::Ptr addItem(const ItemStack& item, size_t count, bool /*allowAutoEquip*/) override
        {
            const int prevCount = item.mBase.getCellRef().getCount(false);
            const int intCount = static_cast<int>(count);
            item.mBase.getCellRef().setCount(intCount);
            MWWorld::Ptr ptr = dropItemImpl(item, intCount, false);
            item.mBase.getCellRef().setCount(prevCount);
            return ptr;
        }

        MWWorld::Ptr copyItem(const ItemStack& item, size_t count, bool /*allowAutoEquip*/) override
        {
            return dropItemImpl(item, static_cast<int>(count), true);
        }

        void removeItem(const ItemStack& /*item*/, size_t /*count*/) override
        {
            throw std::runtime_error("WorldItemModel::removeItem is not implemented");
        }

    private:
        float mCursorX;
        float mCursorY;
    };
}

#endif
