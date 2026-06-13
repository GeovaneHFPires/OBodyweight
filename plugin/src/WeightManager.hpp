#pragma once
#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <mutex>
#include <random>
#include <unordered_set>

namespace OBW {

enum class WeightMode : std::uint8_t {
    kRandom     = 0,
    kSeeded     = 1,
    kNpcDefault = 2,
};

enum class BodyMode : std::uint8_t {
    kProcedural  = 0,  // generate per-part NiOverride morphs — no preset files needed
    kOBodyPreset = 1,  // let OBody pick the preset; we only set weight
};

class WeightManager {
public:
    static WeightManager& GetSingleton() noexcept;

    // Gera peso (0-100) para o actor conforme o modo configurado
    float GenerateWeight(RE::Actor* a_actor);

    // Aplica peso ao ActorBase do actor (afeta todas as instâncias do mesmo base)
    static void ApplyWeight(RE::Actor* a_actor, float a_weight);

    // Procedural morph generation — no preset files required.
    // GetFrameScore: one call per actor; returns 0-100, bimodal (pushed toward extremes).
    // GetMorphValue: pass the score from GetFrameScore to keep all parts correlated.
    float GetFrameScore(RE::Actor* a_actor);
    float GetMorphValue(RE::Actor* a_actor, float a_frameScore, std::string_view morphName);

    // Male procedural morphs (HIMBO sliders). Triggered by OBody's OnActorGenerated
    // (OBody has a male preset DB too), drained by OBW_Quest's sex-branched ApplyMorphs.
    // Mirrors the female system: build (muscle+fat) + fantasy intensity + traits +
    // unusual + tone, minus breast sag/perk.
    float GetMaleMorphValue(RE::Actor* a_actor, std::string_view morphName);
    // Per-NPC male intensity: realistic (~1.0) or fantasy (1.3-2.0), or unusual extreme.
    float GetMaleIntensity(RE::Actor* a_actor);

    // Configuração
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

    // Per-NPC effective intensity: realistic (~1.0) or fantasy (~1.3-2.2),
    // times the global scale. Papyrus calls this once per NPC and applies it to
    // every slider. This is what produces a realistic-majority + fantasy-minority mix.
    float GetActorIntensity(RE::Actor* a_actor);

    // Per-session processed set — prevents re-applying weight on every cell crossing.
    // Locked: cell-attach (loading thread) and Papyrus VM both touch these containers.
    bool HasProcessed(RE::FormID id) const  { std::scoped_lock l(_mutex); return _processed.contains(id); }
    void MarkProcessed(RE::FormID id)       { std::scoped_lock l(_mutex); _processed.insert(id); }
    void ClearProcessed()                   { std::scoped_lock l(_mutex); _processed.clear(); _morphQueue.clear(); }

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

    // Force re-generation for a specific actor (used by the re-generate hotkey).
    // Assigns a new random seed to this actor so the result differs from the default.
    void RegenerateActor(RE::Actor* a_actor);

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
    float         _fantasyRatio{ 0.15f };
    float         _unusualRatio{ 0.06f };
    float         _breastUnusualRatio{ 0.06f };
    float         _athleticRatio{ 0.15f };
    std::int32_t  _reRollKey{ 26 };  // [ / { key
    std::uint32_t _seed{ 0 };
    mutable std::recursive_mutex                  _mutex;  // guards the containers below
    std::unordered_set<RE::FormID>                _processed;
    std::unordered_set<RE::FormID>                _morphsApplied;
    std::vector<RE::FormID>                       _morphQueue;
    std::unordered_map<RE::FormID, std::uint32_t> _overrideSeed;

public:
    static constexpr std::uint32_t kRecordUID  = 'OBWS';

private:
    static constexpr std::uint32_t kRecordSeed = 'SEED';
    static constexpr std::uint32_t kRecordCfg  = 'WCFG';
    static constexpr std::uint32_t kRecordBody = 'BODY';
    static constexpr std::uint32_t kRecordOvr  = 'OVRS';
    static constexpr std::uint32_t kRecordScl  = 'MSCL';
    static constexpr std::uint32_t kRecordFan  = 'FANR';
    static constexpr std::uint32_t kRecordUnu  = 'UNUS';
    static constexpr std::uint32_t kRecordBUn  = 'BUNU';
    static constexpr std::uint32_t kRecordAth  = 'ATHL';
    static constexpr std::uint32_t kRecordKey  = 'RKEY';
    static constexpr std::uint32_t kRecordVer  = 1;
};

}  // namespace OBW
