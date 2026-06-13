#pragma once
#include <RE/Skyrim.h>
#include "WeightManager.hpp"

namespace OBW {

// Hooks TESCellAttachDetachEvent to set weight before OBody processes the actor.
// OBody needs the 3D model loaded to apply morphs — 3D loads after cell-attach,
// so we always run first. By the time OBody blends SKEE morphs, base->weight is
// already our randomized value. No ReapplyActorOBodyMorphs needed → no loop.
struct CellAttachHandler : public RE::BSTEventSink<RE::TESCellAttachDetachEvent> {
    static CellAttachHandler* GetSingleton() {
        static CellAttachHandler instance;
        return &instance;
    }

    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESCellAttachDetachEvent* a_event,
        RE::BSTEventSource<RE::TESCellAttachDetachEvent>*) override
    {
        if (!a_event || !a_event->attached)
            return RE::BSEventNotifyControl::kContinue;

        auto* ref = a_event->reference.get();
        if (!ref) return RE::BSEventNotifyControl::kContinue;

        auto* actor = ref->As<RE::Actor>();
        if (!actor || actor->IsPlayerRef()) return RE::BSEventNotifyControl::kContinue;

        auto* base = actor->GetActorBase();
        if (!base) return RE::BSEventNotifyControl::kContinue;

        // Weight applies to all adults (both sexes); only children are excluded.
        if (actor->IsChild()) return RE::BSEventNotifyControl::kContinue;

        // Male-bodies master toggle: when off, leave male NPCs entirely alone (no weight).
        if (!base->IsFemale() && !WeightManager::GetSingleton().GetMaleBodies())
            return RE::BSEventNotifyControl::kContinue;

        // NECK SEAM PRECAUTION: only set weight BEFORE the 3D is built, so the head
        // facegen and the body mesh are generated together at the new weight. If the
        // 3D already exists, changing base->weight now would move the body neck while
        // the baked head stays put → a geometric neck seam. Cell-attach normally fires
        // before 3D, so this just guards the rare already-loaded case.
        if (actor->Is3DLoaded()) return RE::BSEventNotifyControl::kContinue;

        auto& mgr = WeightManager::GetSingleton();
        const RE::FormID id = actor->GetFormID();
        if (mgr.HasProcessed(id)) return RE::BSEventNotifyControl::kContinue;

        float weight = mgr.GenerateWeight(actor);
        WeightManager::ApplyWeight(actor, weight);
        mgr.MarkProcessed(id);

        return RE::BSEventNotifyControl::kContinue;
    }

private:
    CellAttachHandler() = default;
};

}  // namespace OBW
