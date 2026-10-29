#ifndef OPENMW_LUAIO2_HPP
#define OPENMW_LUAIO2_HPP

struct lua_State;

namespace LuaIo2
{
    // Makes require("io2") return the built-in Unicode-aware io module
    void registerPreload(lua_State *L);
}

#endif // OPENMW_LUAIO2_HPP
