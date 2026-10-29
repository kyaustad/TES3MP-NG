#ifndef OPENMW_PACKETLUAMESSAGE_HPP
#define OPENMW_PACKETLUAMESSAGE_HPP

#include <components/openmw-mp/Packets/Worldstate/WorldstatePacket.hpp>
#include <components/openmw-mp/NetworkMessages.hpp>

namespace mwmp
{
    // Carries OpenMW Lua events, storage changes and script state, see docs/tes3mp/lua-multiplayer.md
    class PacketLuaMessage : public WorldstatePacket
    {
    public:
        PacketLuaMessage(RakNet::RakPeerInterface *peer);

        virtual void Packet(RakNet::BitStream *newBitstream, bool send);
    };
}

#endif //OPENMW_PACKETLUAMESSAGE_HPP
