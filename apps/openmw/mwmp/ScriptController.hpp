#ifndef OPENMW_SCRIPTCONTROLLER_HPP
#define OPENMW_SCRIPTCONTROLLER_HPP

namespace Interpreter
{
    class Runtime;
}

namespace MWWorld
{
    class Ptr;
}

namespace mwmp
{
    class ObjectList;
}

namespace ScriptController
{
    unsigned short getPacketOriginFromContextType(unsigned short contextType);

    // Whether script changes are sent to the server: in multiplayer, once the local player is logged in
    bool isSending();

    // The packet origin of the script that is running
    unsigned short getPacketOrigin(Interpreter::Runtime& runtime);

    // An ObjectList reset for a packet about the running script, or nullptr if nothing should be sent
    mwmp::ObjectList* prepareObjectList(Interpreter::Runtime& runtime);

    // Sends an ID_OBJECT_PLACE or ID_OBJECT_SPAWN packet for an object a script placed, then deletes the object:
    // the server sends it back with a unique mpNum of its own
    void sendPlacedObject(Interpreter::Runtime& runtime, const MWWorld::Ptr& placed);
}


#endif //OPENMW_SCRIPTCONTROLLER_HPP
