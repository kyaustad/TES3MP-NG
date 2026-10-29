#include "difficultyscaling.hpp"

#include <components/settings/values.hpp>

#include "../mwbase/environment.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/ptr.hpp"

#include "actorutil.hpp"

/*
    Start of tes3mp addition

    Include additional headers for multiplayer purposes
*/
#include "../mwmp/Main.hpp"
#include "../mwmp/LocalPlayer.hpp"
/*
    End of tes3mp addition
*/

float scaleDamage(float damage, const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim)
{
    const MWWorld::Ptr& player = MWMechanics::getPlayer();

    static const float fDifficultyMult
        = MWBase::Environment::get().getESMStore()->get<ESM::GameSetting>().find("fDifficultyMult")->mValue.getFloat();

    // tes3mp change (major): use the difficulty setting received from the server instead of the client's settings
    const float difficultyTerm = 0.01f
        * (mwmp::Main::isInitialized() ? mwmp::Main::get().getLocalPlayer()->difficulty : Settings::game().mDifficulty);

    float x = 0;
    if (victim == player)
    {
        if (difficultyTerm > 0)
            x = fDifficultyMult * difficultyTerm;
        else
            x = difficultyTerm / fDifficultyMult;
    }
    else if (attacker == player)
    {
        if (difficultyTerm > 0)
            x = -difficultyTerm / fDifficultyMult;
        else
            x = fDifficultyMult * (-difficultyTerm);
    }

    damage *= 1 + x;
    return damage;
}
