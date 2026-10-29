#include <components/openmw-mp/Base/BaseStructs.hpp>

#include "../mwscript/interpretercontext.hpp"

/*
    Start of tes3mp addition

    Include additional headers for multiplayer purposes
*/
#include <components/interpreter/runtime.hpp>
#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"
#include "Main.hpp"
#include "Networking.hpp"
#include "LocalPlayer.hpp"
#include "ObjectList.hpp"
/*
    End of tes3mp addition
*/

#include "ScriptController.hpp"

unsigned short ScriptController::getPacketOriginFromContextType(unsigned short contextType)
{
    if (contextType == Interpreter::Context::CONSOLE)
        return mwmp::CLIENT_CONSOLE;
    else if (contextType == Interpreter::Context::DIALOGUE)
        return mwmp::CLIENT_DIALOGUE;
    else if (contextType == Interpreter::Context::SCRIPT_LOCAL)
        return mwmp::CLIENT_SCRIPT_LOCAL;
    else if (contextType == Interpreter::Context::SCRIPT_GLOBAL)
        return mwmp::CLIENT_SCRIPT_GLOBAL;

    return mwmp::CLIENT_GAMEPLAY;
}

bool ScriptController::isSending()
{
    return mwmp::Main::isInitialized() && mwmp::Main::get().getLocalPlayer()->isLoggedIn();
}

unsigned short ScriptController::getPacketOrigin(Interpreter::Runtime& runtime)
{
    return getPacketOriginFromContextType(runtime.getContext().getContextType());
}

mwmp::ObjectList* ScriptController::prepareObjectList(Interpreter::Runtime& runtime)
{
    if (!isSending())
        return nullptr;

    mwmp::ObjectList* objectList = mwmp::Main::get().getNetworking()->getObjectList();
    objectList->reset();
    objectList->packetOrigin = getPacketOrigin(runtime);
    objectList->originClientScript = runtime.getContext().getCurrentScriptName().serializeText();
    return objectList;
}

void ScriptController::sendPlacedObject(Interpreter::Runtime& runtime, const MWWorld::Ptr& placed)
{
    if (!mwmp::Main::isInitialized() || placed.isEmpty())
        return;

    if (mwmp::ObjectList* objectList = prepareObjectList(runtime); objectList && placed.isInCell()
        && !placed.getCell()->getCell()->isEsm4())
    {
        if (placed.getClass().isActor())
        {
            objectList->addObjectSpawn(placed);
            objectList->sendObjectSpawn();
        }
        else
        {
            objectList->addObjectPlace(placed);
            objectList->sendObjectPlace();
        }
    }
    MWBase::Environment::get().getWorld()->deleteObject(placed);
}
