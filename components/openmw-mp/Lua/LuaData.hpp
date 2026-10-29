#ifndef OPENMW_MP_LUADATA_HPP
#define OPENMW_MP_LUADATA_HPP

#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>

/*
    Helpers for moving OpenMW Lua data (LuaUtil::serialize binary format, see components/lua/serialization.cpp)
    between multiplayer peers.

    OpenMW serializes game objects inside event data and saved script state as local ESM::RefNums. Those are only
    meaningful on the client that produced them: objects from content files share RefNums between clients that use
    the same load order, but objects created at runtime get a locally generated RefNum. Before data leaves a client
    every object reference is rewritten into a NetObjectId, and rewritten back into a local RefNum on arrival.

    The server never runs OpenMW Lua, but its own scripts can inspect and produce Lua data through a JSON mapping.
*/

namespace mwmp
{
    struct NetObjectId
    {
        enum Kind : uint8_t
        {
            NONE = 0,    // Unresolvable object; becomes an invalid GameObject on the receiving side
            CONTENT = 1, // Object from a content file: refNumIndex + contentFile are identical on all clients
            MP = 2,      // Object created during the multiplayer session: identified by its server-assigned mpNum
            PLAYER = 3   // A player character: identified by the RakNet GUID of its client
        };

        Kind kind = NONE;
        uint32_t refNumIndex = 0;
        int32_t contentFile = -1;
        uint32_t mpNum = 0;
        uint64_t guid = 0;

        bool operator==(const NetObjectId& other) const
        {
            if (kind != other.kind)
                return false;
            switch (kind)
            {
                case CONTENT:
                    return refNumIndex == other.refNumIndex && contentFile == other.contentFile;
                case MP:
                    return mpNum == other.mpNum;
                case PLAYER:
                    return guid == other.guid;
                default:
                    return true;
            }
        }

        std::string toString() const;
    };

    namespace LuaData
    {
        class FormatError : public std::runtime_error
        {
        public:
            using std::runtime_error::runtime_error;
        };

        struct LocalRefNum
        {
            uint32_t index = 0;
            int32_t contentFile = -1;
            bool isSet() const { return index != 0 || contentFile != -1; }
        };

        using ToNetObject = std::function<NetObjectId(LocalRefNum)>;
        using ToLocalObject = std::function<LocalRefNum(const NetObjectId&)>;

        // Type names used for objects in network-form data. Local form uses "o" and "objlist".
        inline constexpr std::string_view sNetObjectTypeName = "mpo";
        inline constexpr std::string_view sNetObjectListTypeName = "mpolist";

        // Rewrites every local object reference in OpenMW Lua binary data into network form.
        std::string toNetwork(std::string_view localData, const ToNetObject& toNet);

        // Rewrites every network object reference into a local RefNum.
        std::string toLocal(std::string_view netData, const ToLocalObject& toLocal);

        // Network-form binary data <-> JSON, used by the server's scripts.
        // Tables whose keys are 1..n become arrays, tables with string keys become objects, anything else is
        // encoded as {"__pairs": [[key, value], ...]}. Vectors, colors, transforms and objects become objects with
        // a "__type" field.
        std::string toJson(std::string_view netData);
        std::string fromJson(std::string_view json);

        // Throws FormatError if the data is not valid OpenMW Lua binary data
        void validate(std::string_view data);
    }
}

#endif // OPENMW_MP_LUADATA_HPP
