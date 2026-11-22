#include "LuaEffects.hpp"

#include <chrono>
#include <map>
#include <mutex>
#include <set>

#include <components/debug/debuglog.hpp>
#include <components/lua/serialization.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwworld/cellstore.hpp"

#include "LuaSync.hpp"

namespace mwmp::LuaEffects
{
    namespace
    {
        using Clock = std::chrono::steady_clock;
        constexpr auto sDuplicateWindow = std::chrono::milliseconds(300);

        std::mutex sMutex;
        std::map<std::string, Apply> sEffects;
        std::map<std::string, Clock::time_point> sRecent; // effect key -> when it was last played here
        std::map<std::string, Clock::time_point> sLastSent; // frequent effects -> when they were last sent
        std::set<std::string> sLogged; // effects of other clients played here at least once
        // Effects that scripts may change every frame are sent at most this often for each object
        constexpr auto sFrequentInterval = std::chrono::milliseconds(100);

        bool isFrequent(std::string_view op)
        {
            return op == "anim.setSpeed";
        }

        std::string effectKey(std::string_view op, const MWWorld::Ptr& target, std::string_view data)
        {
            std::string key(op);
            key += '\x1f';
            if (!target.isEmpty())
            {
                const ESM::RefNum refNum = target.getCellRef().getRefNum();
                key += std::to_string(refNum.mIndex) + ':' + std::to_string(refNum.mContentFile);
            }
            key += '\x1f';
            key += data;
            return key;
        }

        // Whether the same effect was played here moments ago; records it otherwise
        bool isRecent(const std::string& key)
        {
            const Clock::time_point now = Clock::now();
            std::lock_guard lock(sMutex);
            std::erase_if(sRecent, [&](const auto& entry) { return now - entry.second > sDuplicateWindow; });
            if (sRecent.contains(key))
                return true;
            sRecent[key] = now;
            return false;
        }
    }

    void add(const std::string& op, Apply apply)
    {
        std::lock_guard lock(sMutex);
        sEffects[op] = std::move(apply);
    }

    void share(const MWLua::Context& context, const MWWorld::Ptr& target, std::string_view op, sol::table args)
    {
        if (!LuaSync::isEnabled() || !LuaSync::get().hasConfig())
            return;
        std::string data = LuaUtil::serialize(args, context.mSerializer);
        if (isRecent(effectKey(op, target, data)))
            return; // another client's copy of the same script played it already
        if (isFrequent(op))
        {
            const std::string key = effectKey(op, target, {});
            const Clock::time_point now = Clock::now();
            std::lock_guard lock(sMutex);
            auto last = sLastSent.find(key);
            if (last != sLastSent.end() && now - last->second < sFrequentInterval)
                return;
            sLastSent[key] = now;
        }
        LuaSync::get().onEffect(target, std::string(op), data);
    }

    void apply(const std::string& op, const MWWorld::Ptr& target, const sol::table& args, std::string_view data)
    {
        if (isRecent(effectKey(op, target, data)))
            return;
        Apply effect;
        {
            std::lock_guard lock(sMutex);
            auto found = sEffects.find(op);
            if (found == sEffects.end())
                return;
            effect = found->second;
            if (sLogged.insert(op).second)
                Log(Debug::Info) << "Playing " << op << " effects of other players";
        }
        try
        {
            effect(target, args);
        }
        catch (const std::exception& e)
        {
            Log(Debug::Warning) << "Failed to play " << op << " from another player: " << e.what();
        }
    }
}
