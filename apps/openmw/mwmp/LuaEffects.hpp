#ifndef OPENMW_MWMP_LUAEFFECTS_HPP
#define OPENMW_MWMP_LUAEFFECTS_HPP

#include <functional>
#include <string>
#include <string_view>

#include <sol/sol.hpp>

#include "../mwlua/context.hpp"
#include "../mwworld/ptr.hpp"

namespace mwmp
{
    /*
        tes3mp addition: sounds, visual effects and animations that Lua scripts play are seen and heard by the other
        players near them. A binding plays the effect here, then calls share(); the players who have the cell loaded
        get an EFFECT message and apply the same effect with the function the binding registered.

        Scripts that react to world events in client mode run on every client, so an effect identical to one this
        client played or received moments ago isn't applied again.
    */
    namespace LuaEffects
    {
        // Applies an effect: `target` is empty for effects at a position (world.vfx.spawn), `args` are the
        // arguments the binding shared
        using Apply = std::function<void(const MWWorld::Ptr& target, const sol::table& args)>;

        void add(const std::string& op, Apply apply);

        // Sends an effect this client just played; `target` may be empty
        void share(const MWLua::Context& context, const MWWorld::Ptr& target, std::string_view op, sol::table args);

        // Applies an effect from another client
        void apply(const std::string& op, const MWWorld::Ptr& target, const sol::table& args, std::string_view data);

        // Builds the arguments table of share()
        template <class... Args>
        sol::table pack(const MWLua::Context& context, Args&&... args)
        {
            sol::table table = context.sol().create_table();
            int count = 0;
            ((table[++count] = std::forward<Args>(args)), ...);
            table["n"] = count;
            return table;
        }
    }
}

#endif // OPENMW_MWMP_LUAEFFECTS_HPP
