#ifndef OPENMW_PROCESSORLUAMESSAGE_HPP
#define OPENMW_PROCESSORLUAMESSAGE_HPP

#include "../WorldstateProcessor.hpp"
#include "../../LuaSync.hpp"

namespace mwmp
{
    class ProcessorLuaMessage final: public WorldstateProcessor
    {
    public:
        ProcessorLuaMessage()
        {
            BPP_INIT(ID_LUA_MESSAGE)
        }

        virtual void Do(WorldstatePacket &packet, Worldstate &worldstate)
        {
            LuaSync::get().receive(worldstate.luaMessages);
        }
    };
}

#endif //OPENMW_PROCESSORLUAMESSAGE_HPP
