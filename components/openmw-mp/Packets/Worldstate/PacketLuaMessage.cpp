#include "PacketLuaMessage.hpp"

#include <components/openmw-mp/NetworkMessages.hpp>

using namespace mwmp;

namespace
{
    constexpr uint32_t maxLuaMessages = 1000;
}

PacketLuaMessage::PacketLuaMessage(RakNet::RakPeerInterface *peer) : WorldstatePacket(peer)
{
    packetID = ID_LUA_MESSAGE;
    orderChannel = CHANNEL_WORLDSTATE;
}

void PacketLuaMessage::Packet(RakNet::BitStream *newBitstream, bool send)
{
    WorldstatePacket::Packet(newBitstream, send);

    uint32_t count = 0;

    if (send)
        count = static_cast<uint32_t>(worldstate->luaMessages.size());

    RW(count, send);

    if (count > maxLuaMessages)
    {
        packetValid = false;
        return;
    }

    if (!send)
    {
        worldstate->luaMessages.clear();
        worldstate->luaMessages.resize(count);
    }

    for (auto &&message : worldstate->luaMessages)
    {
        RW(message.kind, send);
        RW(message.flags, send);
        RW(message.name, send, true);

        if (message.kind == LuaMessage::STORAGE_SET)
            RW(message.key, send, true);

        if (message.kind == LuaMessage::LOCAL_EVENT || message.kind == LuaMessage::AUTHORITY
            || message.kind == LuaMessage::LOCAL_STATE)
        {
            uint8_t kind = message.target.kind;
            RW(kind, send);
            message.target.kind = static_cast<NetObjectId::Kind>(kind);
            switch (message.target.kind)
            {
                case NetObjectId::CONTENT:
                    RW(message.target.refNumIndex, send);
                    RW(message.target.contentFile, send);
                    break;
                case NetObjectId::MP:
                    RW(message.target.mpNum, send);
                    break;
                case NetObjectId::PLAYER:
                    RW(message.target.guid, send);
                    break;
                default:
                    break;
            }
            RW(message.cell, send, true);
        }

        if (!RWBinary(message.data, send))
        {
            packetValid = false;
            return;
        }
    }
}
