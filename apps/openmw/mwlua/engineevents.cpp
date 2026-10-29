#include "engineevents.hpp"

#include <components/debug/debuglog.hpp>
#include <components/settings/values.hpp>

#include "../mwbase/environment.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/worldmodel.hpp"

#include "globalscripts.hpp"
#include "localscripts.hpp"
#include "object.hpp"

#include "../mwmp/LuaSync.hpp" // tes3mp addition

namespace MWLua
{

    class EngineEvents::Visitor
    {
    public:
        explicit Visitor(GlobalScripts& globalScripts)
            : mGlobalScripts(globalScripts)
        {
        }

        void operator()(const OnActive& event) const
        {
            MWWorld::Ptr ptr = getPtr(event.mObject);
            if (ptr.isEmpty())
                return;
            if (ptr.getCellRef().getRefId() == "player")
                mGlobalScripts.playerAdded(GObject(ptr));
            else
            {
                mGlobalScripts.objectActive(GObject(ptr));
                const MWWorld::Class& objClass = ptr.getClass();
                if (objClass.isActor())
                    mGlobalScripts.actorActive(GObject(ptr));
                if (objClass.isItem(ptr))
                    mGlobalScripts.itemActive(GObject(ptr));
            }
            if (auto* scripts = getLocalScripts(ptr))
                scripts->setActive(true);
        }

        void operator()(const OnInactive& event) const
        {
            // tes3mp change (minor): also deactivate scripts of objects that another client simulates
            MWWorld::Ptr ptr = getPtr(event.mObject);
            if (auto* scripts = ptr.isEmpty() ? nullptr : ptr.getRefData().getLuaScripts())
                scripts->setActive(false);
        }

        void operator()(const OnTeleported& event) const
        {
            if (auto* scripts = getLocalScripts(event.mObject))
                scripts->onTeleported();
        }

        void operator()(const OnActivate& event) const
        {
            MWWorld::Ptr obj = getPtr(event.mObject);
            MWWorld::Ptr actor = getPtr(event.mActor);
            if (actor.isEmpty() || obj.isEmpty())
                return;
            mGlobalScripts.onActivate(GObject(obj), GObject(actor));
            if (auto* scripts = getLocalScripts(obj))
                scripts->onActivated(LObject(actor));
        }

        void operator()(const OnUseItem& event) const
        {
            MWWorld::Ptr obj = getPtr(event.mObject);
            MWWorld::Ptr actor = getPtr(event.mActor);
            if (actor.isEmpty() || obj.isEmpty())
                return;
            mGlobalScripts.onUseItem(GObject(obj), GObject(actor), event.mForce);
        }

        void operator()(const OnConsume& event) const
        {
            MWWorld::Ptr actor = getPtr(event.mActor);
            MWWorld::Ptr consumable = getPtr(event.mConsumable);
            if (actor.isEmpty() || consumable.isEmpty())
                return;
            if (auto* scripts = getLocalScripts(actor))
                scripts->onConsume(LObject(consumable));
        }

        void operator()(const OnNewExterior& event) const { mGlobalScripts.onNewExterior(GCell{ &event.mCell }); }

        void operator()(const OnAnimationTextKey& event) const
        {
            MWWorld::Ptr actor = getPtr(event.mActor);
            if (actor.isEmpty())
                return;
            if (auto* scripts = getLocalScripts(actor))
                scripts->onAnimationTextKey(event.mGroupname, event.mKey);
        }

        void operator()(const OnAnimationEnded& event) const
        {
            MWWorld::Ptr actor = getPtr(event.mActor);
            if (actor.isEmpty())
                return;
            if (auto* scripts = getLocalScripts(actor))
                scripts->onAnimationEnded(
                    event.mGroupname, event.mStartKey, event.mStopKey, event.mTime, event.mCompletion);
        }

        void operator()(const OnSkillUse& event) const
        {
            MWWorld::Ptr actor = getPtr(event.mActor);
            if (actor.isEmpty())
                return;
            if (auto* scripts = getLocalScripts(actor))
                scripts->onSkillUse(event.mSkill, event.useType, event.scale);
        }

        void operator()(const OnSkillLevelUp& event) const
        {
            MWWorld::Ptr actor = getPtr(event.mActor);
            if (actor.isEmpty())
                return;
            if (auto* scripts = getLocalScripts(actor))
                scripts->onSkillLevelUp(event.mSkill, event.mSource);
        }

        void operator()(const OnJailTimeServed& event) const
        {
            MWWorld::Ptr actor = getPtr(event.mActor);
            if (actor.isEmpty())
                return;
            if (auto* scripts = getLocalScripts(actor))
                scripts->onJailTimeServed(event.mDays);
        }

    private:
        MWWorld::Ptr getPtr(ESM::RefNum id) const
        {
            MWWorld::Ptr res = mWorldModel->getPtr(id);
            if (res.isEmpty() && Settings::lua().mLuaDebug)
                Log(Debug::Verbose) << "Can not find object" << id.toString() << " when calling engine hanglers";
            return res;
        }

        LocalScripts* getLocalScripts(const MWWorld::Ptr& ptr) const
        {
            /*
                Start of tes3mp change (minor)

                Local scripts of objects that another client simulates don't run here
            */
            if (ptr.isEmpty() || !mwmp::LuaSync::isLocallySimulated(ptr))
                return nullptr;
            /*
                End of tes3mp change (minor)
            */
            else
                return ptr.getRefData().getLuaScripts();
        }

        LocalScripts* getLocalScripts(ESM::RefNum id) const { return getLocalScripts(getPtr(id)); }

        GlobalScripts& mGlobalScripts;
        MWWorld::WorldModel* mWorldModel = MWBase::Environment::get().getWorldModel();
    };

    void EngineEvents::callEngineHandlers()
    {
        Visitor vis(mGlobalScripts);
        for (const Event& event : mQueue)
            std::visit(vis, event);
        mQueue.clear();
    }

}
