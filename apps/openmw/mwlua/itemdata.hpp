#ifndef MWLUA_ITEMDATA_H
#define MWLUA_ITEMDATA_H

#include <sol/forward.hpp>

#include <string_view>

namespace MWWorld
{
    class Ptr;
}

namespace MWLua
{
    struct Context;

    void addItemDataBindings(sol::table& item, const Context& context);

    // tes3mp addition: sets an item's condition, enchantment charge or soul like types.Item.itemData does
    void mpApplyItemData(const MWWorld::Ptr& ptr, std::string_view property, const sol::object& value);

}
#endif // MWLUA_ITEMDATA_H
