#include "WeightManager.hpp"
#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

namespace OBW::PapyrusBindings {

namespace {

constexpr std::string_view kScript{ "OBW_Native" };

// Gera e retorna o peso para o actor (sem aplicar)
float GetWeight(RE::StaticFunctionTag*, RE::Actor* a_actor) {
    return WeightManager::GetSingleton().GenerateWeight(a_actor);
}

// Aplica o peso gerado ao actor e atualiza a geometria
void ApplyGeneratedWeight(RE::StaticFunctionTag*, RE::Actor* a_actor) {
    auto& mgr = WeightManager::GetSingleton();
    float w = mgr.GenerateWeight(a_actor);
    WeightManager::ApplyWeight(a_actor, w);
}

// Modo: 0=Random, 1=Seeded/Deterministic, 2=NpcDefault
std::int32_t GetMode(RE::StaticFunctionTag*) {
    return static_cast<std::int32_t>(WeightManager::GetSingleton().GetMode());
}

void SetMode(RE::StaticFunctionTag*, std::int32_t a_mode) {
    WeightManager::GetSingleton().SetMode(static_cast<WeightMode>(a_mode));
}

float GetBias(RE::StaticFunctionTag*) {
    return WeightManager::GetSingleton().GetBias();
}

void SetBias(RE::StaticFunctionTag*, float a_bias) {
    WeightManager::GetSingleton().SetBias(a_bias);
}

std::int32_t GetSeed(RE::StaticFunctionTag*) {
    return WeightManager::GetSingleton().GetSeed();
}

void RegenerateSeed(RE::StaticFunctionTag*) {
    WeightManager::GetSingleton().RegenerateSeed();
}

std::int32_t GetBodyMode(RE::StaticFunctionTag*) {
    return static_cast<std::int32_t>(WeightManager::GetSingleton().GetBodyMode());
}

void SetBodyMode(RE::StaticFunctionTag*, std::int32_t a_mode) {
    WeightManager::GetSingleton().SetBodyMode(static_cast<BodyMode>(a_mode));
}

float GetMorphScale(RE::StaticFunctionTag*) {
    return WeightManager::GetSingleton().GetMorphScale();
}

void SetMorphScale(RE::StaticFunctionTag*, float a_scale) {
    WeightManager::GetSingleton().SetMorphScale(a_scale);
}

float GetFantasyRatio(RE::StaticFunctionTag*) {
    return WeightManager::GetSingleton().GetFantasyRatio();
}

void SetFantasyRatio(RE::StaticFunctionTag*, float a_ratio) {
    WeightManager::GetSingleton().SetFantasyRatio(a_ratio);
}

float GetUnusualRatio(RE::StaticFunctionTag*) {
    return WeightManager::GetSingleton().GetUnusualRatio();
}

void SetUnusualRatio(RE::StaticFunctionTag*, float a_ratio) {
    WeightManager::GetSingleton().SetUnusualRatio(a_ratio);
}

float GetBreastUnusualRatio(RE::StaticFunctionTag*) {
    return WeightManager::GetSingleton().GetBreastUnusualRatio();
}

void SetBreastUnusualRatio(RE::StaticFunctionTag*, float a_ratio) {
    WeightManager::GetSingleton().SetBreastUnusualRatio(a_ratio);
}

float GetAthleticRatio(RE::StaticFunctionTag*) {
    return WeightManager::GetSingleton().GetAthleticRatio();
}

void SetAthleticRatio(RE::StaticFunctionTag*, float a_ratio) {
    WeightManager::GetSingleton().SetAthleticRatio(a_ratio);
}

std::int32_t GetReRollKey(RE::StaticFunctionTag*) {
    return WeightManager::GetSingleton().GetReRollKey();
}

void SetReRollKey(RE::StaticFunctionTag*, std::int32_t a_key) {
    WeightManager::GetSingleton().SetReRollKey(a_key);
}

float GetActorIntensity(RE::StaticFunctionTag*, RE::Actor* a_actor) {
    return WeightManager::GetSingleton().GetActorIntensity(a_actor);
}

void QueueForMorphs(RE::StaticFunctionTag*, RE::Actor* a_actor) {
    WeightManager::GetSingleton().QueueForMorphs(a_actor);
}

RE::Actor* GetNextMorphActor(RE::StaticFunctionTag*) {
    return WeightManager::GetSingleton().GetNextMorphActor();
}

bool HasMorphsPending(RE::StaticFunctionTag*) {
    return WeightManager::GetSingleton().HasMorphsPending();
}

float GetFrameScore(RE::StaticFunctionTag*, RE::Actor* a_actor) {
    return WeightManager::GetSingleton().GetFrameScore(a_actor);
}

float GetMorphValue(RE::StaticFunctionTag*, RE::Actor* a_actor, float a_frameScore, RE::BSFixedString a_name) {
    const char* raw = a_name.c_str();
    return WeightManager::GetSingleton().GetMorphValue(a_actor, a_frameScore, raw ? raw : "");
}

float GetMaleMorphValue(RE::StaticFunctionTag*, RE::Actor* a_actor, RE::BSFixedString a_name) {
    const char* raw = a_name.c_str();
    return WeightManager::GetSingleton().GetMaleMorphValue(a_actor, raw ? raw : "");
}

float GetMaleIntensity(RE::StaticFunctionTag*, RE::Actor* a_actor) {
    return WeightManager::GetSingleton().GetMaleIntensity(a_actor);
}

void RegenerateActor(RE::StaticFunctionTag*, RE::Actor* a_actor) {
    WeightManager::GetSingleton().RegenerateActor(a_actor);
}

void MarkMorphsApplied(RE::StaticFunctionTag*, RE::Actor* a_actor) {
    if (a_actor) WeightManager::GetSingleton().MarkMorphsApplied(a_actor->GetFormID());
}

bool HasMorphsApplied(RE::StaticFunctionTag*, RE::Actor* a_actor) {
    if (!a_actor) return false;
    return WeightManager::GetSingleton().HasMorphsApplied(a_actor->GetFormID());
}

}  // namespace

bool Register(RE::BSScript::IVirtualMachine* a_vm) {
    a_vm->RegisterFunction("GetWeight",           kScript, GetWeight);
    a_vm->RegisterFunction("ApplyGeneratedWeight",kScript, ApplyGeneratedWeight);
    a_vm->RegisterFunction("GetMode",             kScript, GetMode);
    a_vm->RegisterFunction("SetMode",             kScript, SetMode);
    a_vm->RegisterFunction("GetBias",             kScript, GetBias);
    a_vm->RegisterFunction("SetBias",             kScript, SetBias);
    a_vm->RegisterFunction("GetSeed",             kScript, GetSeed);
    a_vm->RegisterFunction("RegenerateSeed",      kScript, RegenerateSeed);
    a_vm->RegisterFunction("QueueForMorphs",      kScript, QueueForMorphs);
    a_vm->RegisterFunction("GetNextMorphActor",   kScript, GetNextMorphActor);
    a_vm->RegisterFunction("HasMorphsPending",    kScript, HasMorphsPending);
    a_vm->RegisterFunction("GetBodyMode",         kScript, GetBodyMode);
    a_vm->RegisterFunction("SetBodyMode",         kScript, SetBodyMode);
    a_vm->RegisterFunction("GetMorphScale",       kScript, GetMorphScale);
    a_vm->RegisterFunction("SetMorphScale",       kScript, SetMorphScale);
    a_vm->RegisterFunction("GetFantasyRatio",     kScript, GetFantasyRatio);
    a_vm->RegisterFunction("SetFantasyRatio",     kScript, SetFantasyRatio);
    a_vm->RegisterFunction("GetUnusualRatio",     kScript, GetUnusualRatio);
    a_vm->RegisterFunction("SetUnusualRatio",     kScript, SetUnusualRatio);
    a_vm->RegisterFunction("GetBreastUnusualRatio", kScript, GetBreastUnusualRatio);
    a_vm->RegisterFunction("SetBreastUnusualRatio", kScript, SetBreastUnusualRatio);
    a_vm->RegisterFunction("GetAthleticRatio",    kScript, GetAthleticRatio);
    a_vm->RegisterFunction("SetAthleticRatio",    kScript, SetAthleticRatio);
    a_vm->RegisterFunction("GetReRollKey",        kScript, GetReRollKey);
    a_vm->RegisterFunction("SetReRollKey",        kScript, SetReRollKey);
    a_vm->RegisterFunction("GetActorIntensity",   kScript, GetActorIntensity);
    a_vm->RegisterFunction("GetFrameScore",       kScript, GetFrameScore);
    a_vm->RegisterFunction("GetMorphValue",       kScript, GetMorphValue);
    a_vm->RegisterFunction("GetMaleMorphValue",   kScript, GetMaleMorphValue);
    a_vm->RegisterFunction("GetMaleIntensity",    kScript, GetMaleIntensity);
    a_vm->RegisterFunction("RegenerateActor",     kScript, RegenerateActor);
    a_vm->RegisterFunction("MarkMorphsApplied",   kScript, MarkMorphsApplied);
    a_vm->RegisterFunction("HasMorphsApplied",    kScript, HasMorphsApplied);
    SKSE::log::info("OBW: Papyrus bindings registrados");
    return true;
}

}  // namespace OBW::PapyrusBindings
