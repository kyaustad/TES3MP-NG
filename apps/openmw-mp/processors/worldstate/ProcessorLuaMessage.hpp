#ifndef OPENMW_PROCESSORLUAMESSAGE_HPP
#define OPENMW_PROCESSORLUAMESSAGE_HPP

#include "../WorldstateProcessor.hpp"
#include "../../LuaSync.hpp"

namespace mwmp
{
    class ProcessorLuaMessage : public WorldstateProcessor
    {
    public:
        ProcessorLuaMessage()
        {
            BPP_INIT(ID_LUA_MESSAGE)
        }

        void Do(WorldstatePacket &packet, Player &player, BaseWorldstate &worldstate) override
        {
            LuaSync::processMessages(player, worldstate);
        }
    };
}

#endif //OPENMW_PROCESSORLUAMESSAGE_HPP
