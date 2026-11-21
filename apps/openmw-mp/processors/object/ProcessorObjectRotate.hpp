#ifndef OPENMW_PROCESSOROBJECTROTATE_HPP
#define OPENMW_PROCESSOROBJECTROTATE_HPP

#include "../ObjectProcessor.hpp"

namespace mwmp
{
    class ProcessorObjectRotate : public ObjectProcessor
    {
    public:
        ProcessorObjectRotate()
        {
            BPP_INIT(ID_OBJECT_ROTATE)
        }

        // tes3mp-ng change (major): scripts save and relay moved objects, so players who load the cell later see
        // them where they are, instead of the packet only being relayed to the players online
        void Do(ObjectPacket &packet, Player &player, BaseObjectList &objectList) override
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received %s from %s", strPacketID.c_str(), player.npc.mName.c_str());

            Script::Call<Script::CallbackIdentity("OnObjectRotate")>(player.getId(), objectList.cell.getShortDescription().c_str());
        }
    };
}

#endif //OPENMW_PROCESSOROBJECTROTATE_HPP
