#include "aiactivate.hpp"

#include <components/esm3/aisequence.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/luamanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/class.hpp"

#include "creaturestats.hpp"
#include "movement.hpp"
#include "steering.hpp"

/*
    Start of tes3mp addition

    Include additional headers for multiplayer purposes
*/
#include "../mwmp/Main.hpp"
#include "../mwmp/Networking.hpp"
#include "../mwmp/LocalPlayer.hpp"
#include "../mwmp/ObjectList.hpp"
#include "../mwmp/MechanicsHelper.hpp"
/*
    End of tes3mp addition
*/

namespace MWMechanics
{
    AiActivate::AiActivate(const ESM::RefId& objectId, bool repeat)
        : TypedAiPackage<AiActivate>(repeat)
        , mObjectId(objectId)
    {
    }

    bool AiActivate::execute(
        const MWWorld::Ptr& actor, CharacterController& characterController, AiState& state, float duration)
    {
        const MWWorld::Ptr target
            = MWBase::Environment::get().getWorld()->searchPtr(mObjectId, false); // The target to follow

        actor.getClass().getCreatureStats(actor).setDrawState(DrawState::Nothing);

        // Stop if the target doesn't exist
        // Really we should be checking whether the target is currently registered with the MechanicsManager
        if (target == MWWorld::Ptr() || !target.getCellRef().getCount() || !target.getRefData().isEnabled())
            return true;

        // Turn to target and move to it directly, without pathfinding.
        const osg::Vec3f targetDir
            = target.getRefData().getPosition().asVec3() - actor.getRefData().getPosition().asVec3();

        zTurn(actor, std::atan2(targetDir.x(), targetDir.y()), 0.f);
        actor.getClass().getMovementSettings(actor).mPosition[1] = 1;
        actor.getClass().getMovementSettings(actor).mPosition[0] = 0;

        if (MWBase::Environment::get().getWorld()->getMaxActivationDistance() >= targetDir.length())
        {
            // Note: we intentionally do not cancel package after activation here for backward compatibility with
            // original engine.
            /*
                Start of tes3mp change (major)

                Send an ID_OBJECT_ACTIVATE packet instead of activating unilaterally, and cancel the package
                to avoid an infinite activation loop while waiting for the server's reply
            */
            if (mwmp::Main::isInitialized())
            {
                mwmp::ObjectList* objectList = mwmp::Main::get().getNetworking()->getObjectList();
                objectList->reset();
                objectList->packetOrigin = mwmp::CLIENT_GAMEPLAY;
                objectList->addObjectActivate(target, actor);
                objectList->sendObjectActivate();
                return true;
            }
            /*
                End of tes3mp change (major)
            */
            MWBase::Environment::get().getLuaManager()->objectActivated(target, actor);
        }
        return false;
    }

    void AiActivate::writeState(ESM::AiSequence::AiSequence& sequence) const
    {
        auto activate = std::make_unique<ESM::AiSequence::AiActivate>();
        activate->mTargetId = mObjectId;
        activate->mRepeat = getRepeat();

        ESM::AiSequence::AiPackageContainer package;
        package.mType = ESM::AiSequence::Ai_Activate;
        package.mPackage = std::move(activate);
        sequence.mPackages.push_back(std::move(package));
    }

    AiActivate::AiActivate(const ESM::AiSequence::AiActivate* activate)
        : AiActivate(activate->mTargetId, activate->mRepeat)
    {
    }
}
