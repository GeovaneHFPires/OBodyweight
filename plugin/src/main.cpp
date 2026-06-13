#include <SKSE/SKSE.h>
#include <RE/Skyrim.h>
#include "WeightManager.hpp"
#include "ActorLoadHook.hpp"
#include "Config.hpp"

namespace OBW::PapyrusBindings { bool Register(RE::BSScript::IVirtualMachine*); }

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

    // Cell-attach hook: register only after data is loaded — the event source
    // holder isn't reliably ready at plugin-load time.
    auto* messaging = SKSE::GetMessagingInterface();
    if (messaging) {
        messaging->RegisterListener([](SKSE::MessagingInterface::Message* a_msg) {
            if (a_msg && a_msg->type == SKSE::MessagingInterface::kDataLoaded) {
                RE::ScriptEventSourceHolder::GetSingleton()
                    ->AddEventSink(OBW::CellAttachHandler::GetSingleton());
                SKSE::log::info("OBW: cell-attach sink registered (kDataLoaded)");
            }
        });
    }

    SKSE::log::info("OBodyNGWeight loaded");
    return true;
}
