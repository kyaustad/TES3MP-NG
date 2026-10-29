#include "luaevents.hpp"

#include <components/debug/debuglog.hpp>

#include <components/esm/luascripts.hpp>
#include <components/esm3/esmreader.hpp>
#include <components/esm3/esmwriter.hpp>

#include <components/lua/serialization.hpp>

#include "../mwbase/environment.hpp"
#include "../mwworld/worldmodel.hpp"

#include "globalscripts.hpp"
#include "localscripts.hpp"
#include "menuscripts.hpp"

/*
    Start of tes3mp addition

    Include additional headers for multiplayer purposes
*/
#include "../mwmp/LuaSync.hpp"
/*
    End of tes3mp addition
*/

namespace MWLua
{

    void LuaEvents::clear()
    {
        mGlobalEventBatch.clear();
        mLocalEventBatch.clear();
        mNewGlobalEventBatch.clear();
        mNewLocalEventBatch.clear();
        mMenuEvents.clear();
    }

    void LuaEvents::addGlobalEvent(Global event)
    {
        /*
            Start of tes3mp addition

            Global events also reach authority mode scripts on the Lua world authority and server scripts
        */
        if (!event.mFromNetwork && mwmp::LuaSync::isEnabled())
            mwmp::LuaSync::get().onGlobalEvent(event.mEventName, event.mEventData);
        /*
            End of tes3mp addition
        */
        mNewGlobalEventBatch.push_back(std::move(event));
    }

    void LuaEvents::finalizeEventBatch()
    {
        mNewGlobalEventBatch.swap(mGlobalEventBatch);
        mNewLocalEventBatch.swap(mLocalEventBatch);
        mNewGlobalEventBatch.clear();
        mNewLocalEventBatch.clear();
    }

    void LuaEvents::callEventHandlers()
    {
        for (const Global& e : mGlobalEventBatch)
        {
            /*
                Start of tes3mp change (minor)

                Events sent to the Lua world authority by other clients only reach authority mode scripts, because
                client mode scripts already received them on the client that sent them
            */
            if (e.mAuthorityOnly)
            {
                mGlobalScripts.receiveEvent(e.mEventName, e.mEventData, [this](int scriptId) {
                    return mwmp::LuaSync::get().getGlobalScriptMode(mGlobalScripts.getScriptPath(scriptId).value())
                        == mwmp::LuaSync::Mode::Authority;
                });
            }
            else
                mGlobalScripts.receiveEvent(e.mEventName, e.mEventData);
            /*
                End of tes3mp change (minor)
            */
        }
        mGlobalEventBatch.clear();
        for (const Local& e : mLocalEventBatch)
        {
            MWWorld::Ptr ptr = MWBase::Environment::get().getWorldModel()->getPtr(e.mDest);
            /*
                Start of tes3mp addition

                Objects simulated by another client (other players, actors and objects in cells another client is
                the authority of) run their local scripts there, so send the event to that client
            */
            if (!ptr.isEmpty() && !mwmp::LuaSync::isLocallySimulated(ptr))
            {
                if (!e.mFromNetwork)
                    mwmp::LuaSync::get().onLocalEvent(ptr, e.mEventName, e.mEventData);
                else
                    Log(Debug::Verbose) << "Ignored network event " << e.mEventName << " to L" << e.mDest.toString()
                                        << ", which is not simulated by this client";
                continue;
            }
            /*
                End of tes3mp addition
            */
            LocalScripts* scripts = ptr.isEmpty() ? nullptr : ptr.getRefData().getLuaScripts();
            if (scripts)
                scripts->receiveEvent(e.mEventName, e.mEventData);
            else
                Log(Debug::Debug) << "Ignored event " << e.mEventName << " to L" << e.mDest.toString()
                                  << ". Object not found or has no attached scripts";
        }
        mLocalEventBatch.clear();
    }

    void LuaEvents::callMenuEventHandlers()
    {
        for (const Global& e : mMenuEvents)
            mMenuScripts.receiveEvent(e.mEventName, e.mEventData);
        mMenuEvents.clear();
    }

    template <typename Event>
    static void saveEvent(ESM::ESMWriter& esm, ESM::RefNum dest, const Event& event)
    {
        esm.writeHNString("LUAE", event.mEventName);
        esm.writeFormId(dest, true);
        if (!event.mEventData.empty())
            saveLuaBinaryData(esm, event.mEventData);
    }

    void LuaEvents::load(lua_State* lua, ESM::ESMReader& esm, const std::map<int, int>& contentFileMapping,
        const LuaUtil::UserdataSerializer* serializer)
    {
        clear();
        while (esm.isNextSub("LUAE"))
        {
            std::string name = esm.getHString();
            ESM::RefNum dest = esm.getFormId(true);
            std::string data = loadLuaBinaryData(esm);
            try
            {
                data = LuaUtil::serialize(LuaUtil::deserialize(lua, data, serializer), serializer);
            }
            catch (std::exception& e)
            {
                Log(Debug::Error) << "loadEvent: invalid event data: " << e.what();
            }
            if (dest.isSet())
            {
                auto it = contentFileMapping.find(dest.mContentFile);
                if (it != contentFileMapping.end())
                    dest.mContentFile = it->second;
                mLocalEventBatch.push_back({ dest, std::move(name), std::move(data) });
            }
            else
                mGlobalEventBatch.push_back({ std::move(name), std::move(data) });
        }
    }

    void LuaEvents::save(ESM::ESMWriter& esm) const
    {
        // Used as a marker of a global event.
        constexpr ESM::RefNum globalId;

        for (const Global& e : mGlobalEventBatch)
            saveEvent(esm, globalId, e);
        for (const Global& e : mNewGlobalEventBatch)
            saveEvent(esm, globalId, e);
        for (const Local& e : mLocalEventBatch)
            saveEvent(esm, e.mDest, e);
        for (const Local& e : mNewLocalEventBatch)
            saveEvent(esm, e.mDest, e);
    }

}
