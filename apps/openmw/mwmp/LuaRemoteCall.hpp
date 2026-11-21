#ifndef OPENMW_MWMP_LUAREMOTECALL_HPP
#define OPENMW_MWMP_LUAREMOTECALL_HPP

#include <string_view>
#include <utility>

#include <components/lua/serialization.hpp>

#include "../mwlua/context.hpp"
#include "../mwworld/ptr.hpp"

#include "LuaSync.hpp"

namespace mwmp
{
    /*
        tes3mp addition: global script calls that change an actor another client simulates (an NPC in a cell this
        client isn't the authority of, or another player). Applied here, the change would be overwritten by that
        client's sync, so the call is sent there instead, and that client makes the same call in its own global
        scripts' context. Its usual sync then shares the result.

        `op` names the Lua function: "Actor.spells:add" is types.Actor.spells(actor):add(...), and "NPC.joinFaction"
        is types.NPC.joinFaction(actor, ...). Returns true when the call was sent, and must not be applied here.
    */
    template <class... Args>
    bool forwardLuaCall(const MWLua::Context& context, const MWWorld::Ptr& target, std::string_view op, Args&&... args)
    {
        if (context.mType != MWLua::Context::Global || !LuaSync::isEnabled() || LuaSync::isLocallySimulated(target))
            return false;

        sol::state_view lua = context.sol();
        sol::table arguments = lua.create_table();
        int count = 0;
        ((arguments[++count] = std::forward<Args>(args)), ...);
        arguments["n"] = count;
        LuaSync::get().onRemoteCall(target, std::string(op), LuaUtil::serialize(arguments, context.mSerializer));
        return true;
    }
}

#endif // OPENMW_MWMP_LUAREMOTECALL_HPP
