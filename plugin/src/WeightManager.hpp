#pragma once
#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <mutex>
#include <random>
#include <string>
#include <unordered_set>
#include "Debug.hpp"

namespace OBW {

enum class WeightMode : std::uint8_t {
    kRandom     = 0,
    kSeeded     = 1,
    kNpcDefault = 2,
};

enum class BodyMode : std::uint8_t {
    kProcedural         = 0,  // generate per-part NiOverride morphs — no preset files needed
    kOBodyPreset        = 1,  // let OBody pick the preset; we only set weight
    kProceduralOriented = 2,  // procedural, but each slider blended toward the OBody preset (strength = _presetOrient)
};

class WeightManager {
public:
    static WeightManager& GetSingleton() noexcept;

    // Generate the per-NPC "mock weight" (0-100) according to the configured mode. This
    // value drives the body-size morphs; it is NOT written to the actor's real weight
    // (that would cause neck seams + outfit mismatches — see main.cpp).
    float GenerateWeight(RE::Actor* a_actor);

    // Per-INSTANCE mock weight (0-100) for body mode 1 (OBody preset interpolation): seeded by
    // the ACTOR formID (so generic NPCs sharing a base still vary) + Bias, honoring Random vs
    // Seeded via the session seed. NpcDefault returns the actor's real weight (+Bias). This is
    // what drives PresetManager's lerp between each preset slider's small/big values.
    float GetPresetWeight(RE::Actor* a_actor);

    // Procedural morph generation — no preset files required.
    // GetFrameScore: one call per actor; returns 0-100, bimodal (pushed toward extremes).
    // GetMorphValue: pass the score from GetFrameScore to keep all parts correlated.
    float GetFrameScore(RE::Actor* a_actor);
    float GetMorphValue(RE::Actor* a_actor, float a_frameScore, std::string_view morphName);
    // Volume slider value already combined with per-NPC intensity and soft-capped to the
    // sculpted vertex range (no spikes). Use for kVol sliders; kDef ones use GetMorphValue.
    float GetVolumeMorph(RE::Actor* a_actor, float a_frameScore, std::string_view morphName);

    // Male procedural morphs (HIMBO sliders). Triggered by OBody's OnActorGenerated
    // (OBody has a male preset DB too), drained by OBW_Quest's sex-branched ApplyMorphs.
    // Mirrors the female system: build (muscle+fat) + fantasy intensity + traits +
    // unusual + tone, minus breast sag/perk.
    float GetMaleMorphValue(RE::Actor* a_actor, std::string_view morphName);
    // Per-NPC male intensity: realistic (~1.0) or fantasy (1.3-2.0), or unusual extreme.
    float GetMaleIntensity(RE::Actor* a_actor);

    // Configuration
    WeightMode    GetMode() const noexcept      { return _mode; }
    void          SetMode(WeightMode m)         { _mode = m; }
    float         GetBias() const noexcept      { return _bias; }
    void          SetBias(float b)              { _bias = b; }
    std::int32_t  GetSeed() const noexcept      { return static_cast<std::int32_t>(_seed); }
    void          RegenerateSeed();
    BodyMode      GetBodyMode() const noexcept  { return _bodyMode; }
    void          SetBodyMode(BodyMode m)       { _bodyMode = m; }
    // Global morph intensity multiplier (1.0 = default). Set from the MCM.
    float         GetMorphScale() const noexcept { return _morphScale; }
    void          SetMorphScale(float s)         { _morphScale = s; }
    // Preset orientation strength (0.0-1.0) for body mode 2 (Procedural Oriented): how much the OBody
    // preset pulls the procedural blend per slider (0 = pure procedural, 1 = pure preset). MCM-tunable.
    float         GetPresetOrient() const noexcept { return _presetOrient; }
    void          SetPresetOrient(float o)         { _presetOrient = o; }
    // Fraction of NPCs that are "fantasy" (exaggerated). 0.0-1.0. Set from the MCM.
    float         GetFantasyRatio() const noexcept { return _fantasyRatio; }
    void          SetFantasyRatio(float r)         { _fantasyRatio = r; }
    // Fraction of NPCs with an "unusual body" (out-of-distribution: ultra-petite +
    // disproportionate). 0.0-1.0. Set from the MCM.
    float         GetUnusualRatio() const noexcept { return _unusualRatio; }
    void          SetUnusualRatio(float r)         { _unusualRatio = r; }
    // Fraction of NPCs with "unusual breasts" (extreme sag or extreme perk). 0.0-1.0.
    float         GetBreastUnusualRatio() const noexcept { return _breastUnusualRatio; }
    void          SetBreastUnusualRatio(float r)         { _breastUnusualRatio = r; }
    // Fraction of FEMALES that are "athletic" (visible muscle tone/definition). 0.0-1.0.
    float         GetAthleticRatio() const noexcept { return _athleticRatio; }
    void          SetAthleticRatio(float r)         { _athleticRatio = r; }
    // Re-roll hotkey (DirectInput scancode). Default 26 = the [ / { key. MCM-bindable.
    std::int32_t  GetReRollKey() const noexcept { return _reRollKey; }
    void          SetReRollKey(std::int32_t k)  { _reRollKey = k; }
    // Master toggle for the whole female-body feature (morphs). When false, OBW ignores
    // female NPCs completely (OBody/vanilla handle them). MCM-toggleable, persisted in the cosave.
    bool          GetFemaleBodies() const noexcept { return _femaleBodies; }
    void          SetFemaleBodies(bool b)          { _femaleBodies = b; }
    // Master toggle for the whole male-body feature (weight + morphs). When false, OBW
    // ignores male NPCs completely. MCM-toggleable, persisted in the cosave.
    bool          GetMaleBodies() const noexcept { return _maleBodies; }
    void          SetMaleBodies(bool b)          { _maleBodies = b; }
    // Player-tunable male build multiplier (1.0 = default). Scales the whole male body
    // uniformly, so proportions hold at any value. MCM "Male build".
    float         GetMaleBuild() const noexcept { return _maleBuild; }
    void          SetMaleBuild(float b)         { _maleBuild = b; }
    // Debug logging master switch (MCM "Debug logging", OFF by default). Mirrors to the cheap
    // global g_debugLog that the loggers check. Persisted in the cosave.
    bool          GetDebugLog() const noexcept  { return _debugLog; }
    void          SetDebugLog(bool b)           { _debugLog = b; g_debugLog = b; }

    // Female muscle-tone score 0-100 (athletic roll + snu snu, belly-suppressed) — the
    // same value that drives the muscle morph sliders. Exposed for other mods.
    int GetToneScore(RE::Actor* a_actor);

    // Per-NPC effective intensity: realistic (~1.0) or fantasy (~1.3-2.2),
    // times the global scale. Papyrus calls this once per NPC and applies it to
    // every slider. This is what produces a realistic-majority + fantasy-minority mix.
    float GetActorIntensity(RE::Actor* a_actor);

    // CBPC physics tier for this NPC's archetype: 0 = default, 1 = firm (toned/lean),
    // 2 = soft (heavy/jiggly). Papyrus puts the NPC in the matching faction so CBPC's
    // IsInFaction condition selects the right physics config, then RefreshActorBounceSettings.
    int GetPhysicsTier(RE::Actor* a_actor);

    // CONTINUOUS physics percentage (0-100) for CBPC's ApplyBounce/CollisionInterpolation, so the
    // physics CORRELATES with the actual body: it follows frame size + the archetype's softness
    // (muscle firms, fat softens). kind 0 = bounce (size + softness), 1 = collision (size-driven).
    // ~32 is neutral (amplitude ~1.0 / base collider).
    int GetPhysicsPercent(RE::Actor* a_actor, int a_kind);

    // Public body-type API (for other mods): the NPC's full archetype.
    // Id 0-14 (-1 if none); name e.g. "Pear","BBW","Hourglass". 15 archetypes, unlike the 5
    // physics tiers. Deterministic per NPC + seed (same as the morphs they got).
    int         GetArchetypeId(RE::Actor* a_actor);
    std::string GetArchetypeName(RE::Actor* a_actor);

    // Per-session processed set — prevents re-applying weight on every cell crossing.
    // Locked: cell-attach (loading thread) and Papyrus VM both touch these containers.
    bool HasProcessed(RE::FormID id) const  { std::scoped_lock l(_mutex); return _processed.contains(id); }
    void MarkProcessed(RE::FormID id)       { std::scoped_lock l(_mutex); _processed.insert(id); }
    void ClearProcessed()                   { std::scoped_lock l(_mutex); _processed.clear(); _morphQueue.clear(); _fallbackWatch.clear(); }

    // One-shot flag: set before UpdateModelWeight(true), consumed by next OnActorGenerated.
    // Prevents the OBody re-fire loop without blocking future legitimate events.
    void MarkMorphsApplied(RE::FormID id)  { std::scoped_lock l(_mutex); _morphsApplied.insert(id); }
    bool HasMorphsApplied(RE::FormID id) {
        std::scoped_lock l(_mutex);
        auto it = _morphsApplied.find(id);
        if (it == _morphsApplied.end()) return false;
        _morphsApplied.erase(it);  // auto-clear: one-shot per UpdateModelWeight call
        return true;
    }

    // Morph queue — owned by C++; Papyrus only asks for next actor to process.
    void          QueueForMorphs(RE::Actor* a_actor);
    RE::Actor*    GetNextMorphActor();        // returns nullptr when queue is empty
    bool          HasMorphsPending() const   { return !_morphQueue.empty(); }

    // Independent distribution (procedural fallback) — so procedural bodies work even with an EMPTY
    // preset library (when OBody never fires OnActorGenerated), and for NPCs OBody's own distribution
    // skips. The actor-load sink WATCHES each loaded NPC; SweepFallback (polled from Papyrus) gives
    // OBody a short grace, then self-distributes any watched NPC OBody never handled. In OBody-preset
    // mode this stays off (that mode relies on OBody's distribution). Returns how many were enqueued.
    void          WatchForFallback(RE::FormID a_id);
    int           SweepFallback();

    // Force re-generation for a specific actor (used by the re-generate hotkey).
    // Assigns a new random seed to this actor so the result differs from the default.
    void RegenerateActor(RE::Actor* a_actor);

    // Re-apply to EVERY loaded NPC: clears their processed flag and re-queues them, so the current
    // generation logic + CBPC physics are re-applied without waiting for cell reloads (MCM button).
    // Keeps each NPC's body (same seed); returns how many were queued.
    int ReprocessAllLoaded();

    // SKSE cosave callbacks
    static void SaveCallback(SKSE::SerializationInterface* a_intf);
    static void LoadCallback(SKSE::SerializationInterface* a_intf);
    static void RevertCallback(SKSE::SerializationInterface* a_intf);

private:
    WeightManager();

    // Returns the effective seed for an actor: override if present, else formID ^ _seed.
    std::uint32_t GetActorSeed(RE::FormID id) const noexcept {
        auto it = _overrideSeed.find(id);
        return (it != _overrideSeed.end()) ? it->second : (id ^ _seed);
    }

    // Rare per-NPC "unusual body" roll (deterministic):
    //   -1 = normal, 0 = ultra-petite, 1 = ultra-thick.
    // Drives an out-of-distribution frame score + intensity + boosted per-part
    // independence in GetFrameScore / GetActorIntensity / GetMorphValue.
    int UnusualVariant(RE::FormID id) const {
        std::mt19937 rng{ GetActorSeed(id) ^ 0x0DDBA117u };
        if (std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) >= _unusualRatio) return -1;
        return (std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) < 0.5f) ? 0 : 1;
    }

    // Male equivalent: -1 = normal, 0 = ultra-skinny, 1 = ultra-huge.
    int MaleUnusualVariant(RE::FormID id) const {
        std::mt19937 rng{ GetActorSeed(id) ^ 0x0DDFE110u };
        if (std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) >= _unusualRatio) return -1;
        return (std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) < 0.5f) ? 0 : 1;
    }

    // "Snu snu": a rare athletic female taken to the extreme — maxed muscle definition
    // + a big muscular frame (Amazon). ~12% of athletic women. Same rng sequence the
    // muscle-tone code uses (draw1 = athletic, draw2 = snu snu).
    bool IsSnuSnu(RE::FormID id) const {
        std::mt19937 rng{ GetActorSeed(id) ^ 0x0A7E1E70u };
        if (std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) >= _athleticRatio) return false;
        return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) < 0.12f;
    }
    void Save(SKSE::SerializationInterface* a_intf);
    void Load(SKSE::SerializationInterface* a_intf);
    void Revert();

    WeightMode    _mode{ WeightMode::kSeeded };
    BodyMode      _bodyMode{ BodyMode::kProcedural };
    float         _bias{ 0.0f };
    float         _morphScale{ 1.0f };
    float         _presetOrient{ 0.5f };   // body mode 2 blend strength (0 = pure procedural, 1 = pure preset)
    float         _fantasyRatio{ 0.15f };
    float         _unusualRatio{ 0.06f };
    float         _breastUnusualRatio{ 0.06f };
    float         _athleticRatio{ 0.15f };
    bool          _femaleBodies{ true };   // process female NPCs at all (morphs)
    bool          _maleBodies{ true };     // process male NPCs at all (weight + morphs)
    float         _maleBuild{ 1.0f };      // player-tunable male build multiplier
    bool          _debugLog{ false };      // verbose diagnostics (MCM, off by default)
    std::int32_t  _reRollKey{ 26 };  // [ / { key
    std::uint32_t _seed{ 0 };
    mutable std::recursive_mutex                  _mutex;  // guards the containers below
    std::unordered_set<RE::FormID>                _processed;
    std::unordered_set<RE::FormID>                _morphsApplied;
    std::vector<RE::FormID>                       _morphQueue;
    std::unordered_map<RE::FormID, std::uint32_t> _overrideSeed;
    std::unordered_map<RE::FormID, int>           _fallbackWatch;   // id -> grace ticks before self-distributing

    static constexpr int kFallbackGraceTicks = 2;   // SweepFallback ticks (~2s each) to wait for OBody first

public:
    static constexpr std::uint32_t kRecordUID  = 'OBWS';

private:
    static constexpr std::uint32_t kRecordSeed = 'SEED';
    static constexpr std::uint32_t kRecordCfg  = 'WCFG';
    static constexpr std::uint32_t kRecordBody = 'BODY';
    static constexpr std::uint32_t kRecordOvr  = 'OVRS';
    static constexpr std::uint32_t kRecordScl  = 'MSCL';
    static constexpr std::uint32_t kRecordOri  = 'PORI';   // preset-orientation strength (body mode 2)
    static constexpr std::uint32_t kRecordFan  = 'FANR';
    static constexpr std::uint32_t kRecordUnu  = 'UNUS';
    static constexpr std::uint32_t kRecordBUn  = 'BUNU';
    static constexpr std::uint32_t kRecordAth  = 'ATHL';
    static constexpr std::uint32_t kRecordKey  = 'RKEY';
    static constexpr std::uint32_t kRecordMale = 'MALE';
    static constexpr std::uint32_t kRecordFem  = 'FMLE';   // female-bodies master toggle
    static constexpr std::uint32_t kRecordMBld = 'MBLD';
    static constexpr std::uint32_t kRecordDbg  = 'DBGL';   // debug-logging toggle
    static constexpr std::uint32_t kRecordVer  = 1;
};

}  // namespace OBW
