#include "actiontalk.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"

#include "../mwmechanics/actorutil.hpp"

namespace MWWorld
{
    ActionTalk::ActionTalk(const Ptr& actor)
        : Action(false, actor)
    {
    }

    void ActionTalk::executeImp(const Ptr& actor)
    {
        /*
            Start of tes3mp change (major)

            We need to be able to make actors start conversations with players, so don't require the
            local player to be the one activating
        */
        MWBase::Environment::get().getWindowManager()->pushGuiMode(MWGui::GM_Dialogue, getTarget());
        /*
            End of tes3mp change (major)
        */
    }
}
