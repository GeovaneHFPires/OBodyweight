#include <SKSE/SKSE.h>
#include <RE/Skyrim.h>
#include "WeightManager.hpp"
#include "Config.hpp"
#include "MorphInterface.hpp"

namespace OBW::PapyrusBindings { bool Register(RE::BSScript::IVirtualMachine*); }

namespace {
// Independent distribution sink: when an NPC's 3D loads in a PROCEDURAL body mode, watch it so the
// procedural fallback can self-distribute if OBody never fires for it (empty preset library, or an
// NPC OBody's own distribution skips). In OBody-preset mode we stay off (that mode relies on OBody).
class ActorLoadSink final : public RE::BSTEventSink<RE::TESObjectLoadedEvent> {
public:
    static ActorLoadSink* GetSingleton() {
        static ActorLoadSink s;
        return &s;
    }
    RE::BSEventNotifyControl ProcessEvent(const RE::TESObjectLoadedEvent* a_event,
                                          RE::BSTEventSource<RE::TESObjectLoadedEvent>*) override {
        if (!a_event || !a_event->loaded) return RE::BSEventNotifyControl::kContinue;
        auto& wm = OBW::WeightManager::GetSingleton();
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_event->formID);
        if (!actor || actor == RE::PlayerCharacter::GetSingleton()) return RE::BSEventNotifyControl::kContinue;
        if (!actor->GetActorBase() || !actor->HasKeywordString("ActorTypeNPC"))
            return RE::BSEventNotifyControl::kContinue;   // humanoid NPCs only (skip creatures/objects)
        // Neck-seam color fix for EVERY humanoid NPC, in ALL body modes (the seam is a pre-existing facegen/RSV
        // issue, not OBW-specific). The deferred re-applies hold it past RSV's late head re-apply.
        wm.ScheduleNeckColor(actor->GetFormID());
        if (wm.GetBodyMode() == OBW::BodyMode::kOBodyPreset) return RE::BSEventNotifyControl::kContinue;
        wm.WatchForFallback(actor->GetFormID());
        return RE::BSEventNotifyControl::kContinue;
    }
};
}  // namespace

SKSEPluginLoad(const SKSE::LoadInterface* a_skse) {
    SKSE::Init(a_skse);

    // Log
    auto path = SKSE::log::log_directory();
    if (path) {
        *path /= "OBodyNGWeight.log";
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
        auto logger = std::make_shared<spdlog::logger>("OBW", std::move(sink));
        logger->set_level(spdlog::level::debug);
        logger->flush_on(spdlog::level::debug);
        spdlog::set_default_logger(std::move(logger));
    }

    // Read INI defaults (FOMOD-installed preset) before WeightManager is constructed.
    OBW::Config::Load();
    // Plugin exclusion list (NPCs from these .esp/.esl/.esm are left untouched).
    OBW::Config::LoadExclusions();

    // Papyrus
    auto* papyrus = SKSE::GetPapyrusInterface();
    if (!papyrus) return false;
    papyrus->Register(OBW::PapyrusBindings::Register);

    // Cosave (UID unique across all SKSE plugins)
    auto* serial = SKSE::GetSerializationInterface();
    if (serial) {
        serial->SetUniqueID(OBW::WeightManager::kRecordUID);
        serial->SetSaveCallback(OBW::WeightManager::SaveCallback);
        serial->SetLoadCallback(OBW::WeightManager::LoadCallback);
        serial->SetRevertCallback(OBW::WeightManager::RevertCallback);
    }

    // NOTE: OBW no longer changes the actor's real weight (base->weight). Changing it
    // caused neck seams (the baked head facegen is at the editor weight and can't follow)
    // and outfit/body weight mismatches. Body size now comes purely from morphs. The
    // per-NPC weight value is kept as a "mock weight" that drives those morphs.

    // SKEE (RaceMenu) BodyMorph interface — acquired at kPostPostLoad, when SKEE is ready. Lets the
    // OBody-preset path apply all morphs from C++ (no Papyrus per-slider calls, no 128-array cap).
    if (auto* messaging = SKSE::GetMessagingInterface()) {
        messaging->RegisterListener([](SKSE::MessagingInterface::Message* a_msg) {
            if (a_msg->type == SKSE::MessagingInterface::kPostPostLoad) {
                SKEE::InterfaceExchangeMessage msg;
                SKSE::GetMessagingInterface()->Dispatch(
                    SKEE::InterfaceExchangeMessage::kExchangeInterface, &msg,
                    sizeof(SKEE::InterfaceExchangeMessage*), "skee");
                if (msg.interfaceMap)
                    OBW::g_morph = static_cast<SKEE::IBodyMorphInterface*>(
                        msg.interfaceMap->QueryInterface("BodyMorph"));
                SKSE::log::info("SKEE BodyMorph interface: {}", OBW::g_morph ? "acquired" : "NOT FOUND");
            } else if (a_msg->type == SKSE::MessagingInterface::kDataLoaded) {
                // Independent distribution: hook actor 3D-load so procedural mode works without OBody.
                if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
                    holder->AddEventSink<RE::TESObjectLoadedEvent>(ActorLoadSink::GetSingleton());
                    SKSE::log::info("OBW: actor-load sink installed (procedural self-distribution)");
                }
            }
        });
    }

    SKSE::log::info("OBodyNGWeight loaded");
    return true;
}
