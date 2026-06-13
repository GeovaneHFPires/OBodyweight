#include "WeightManager.hpp"
#include "FastRandom.hpp"
#include "Config.hpp"
#include <algorithm>
#include <cctype>
#include <random>
#include <unordered_map>

namespace OBW {

WeightManager::WeightManager() {
    _morphScale   = Config::g_defaultMorphScale;
    _fantasyRatio = Config::g_defaultFantasyRatio;
    _unusualRatio = Config::g_defaultUnusualRatio;
    _breastUnusualRatio = Config::g_defaultBreastUnusualRatio;
    _athleticRatio = Config::g_defaultAthleticRatio;
    _reRollKey = Config::g_defaultReRollKey;
}

WeightManager& WeightManager::GetSingleton() noexcept {
    static WeightManager instance;
    return instance;
}

float WeightManager::GenerateWeight(RE::Actor* a_actor) {
    if (!a_actor) return 50.0f;
    std::scoped_lock lock(_mutex);

    float raw = 50.0f;

    switch (_mode) {
    case WeightMode::kNpcDefault:
        if (auto* base = a_actor->GetActorBase())
            raw = base->GetWeight();
        break;

    case WeightMode::kRandom:
    case WeightMode::kSeeded: {
        // Weight lives on the ActorBase (per-base in the engine). Seed by the BASE
        // formID so every instance of a shared base computes the SAME weight —
        // avoids the "last instance processed overwrites the others" inconsistency.
        RE::FormID seedId = a_actor->GetFormID();
        if (auto* base = a_actor->GetActorBase()) seedId = base->GetFormID();
        std::mt19937 rng{ GetActorSeed(seedId) };
        std::uniform_real_distribution<float> dist(0.0f, 100.0f);
        raw = dist(rng);
        break;
    }
    }

    return std::clamp(raw + _bias, 0.0f, 100.0f);
}

void WeightManager::ApplyWeight(RE::Actor* a_actor, float a_weight) {
    if (!a_actor) return;
    auto* base = a_actor->GetActorBase();
    if (!base) return;
    base->weight = a_weight;
    // DoReset3D intentionally omitted: it clears OBody's "obody_processed" NiOverride
    // morph, causing OBody to re-fire OnActorGenerated → infinite loop + lag.
    // The weight field is persisted on the ActorBase; SKEE picks it up when
    // applying morphs, and the vanilla mesh blend updates on the next natural 3D refresh.
    SKSE::log::debug("OBW: weight {:.1f} set on {:#010x}", a_weight, a_actor->GetFormID());
}

// ---------------------------------------------------------------------------
// Procedural morphs
// ---------------------------------------------------------------------------

namespace {

struct MorphDef {
    float low;          // value at T=0 (petite)
    float high;         // value at T=100 (extreme)
    float independence; // noise amplitude as fraction of [0,100]
};

// Target: CBBE 3BA slider names (also valid for vanilla CBBE).
// Keys are LOWERCASE: Papyrus passes string literals lowercased (confirmed via log,
// "Breasts" -> "breasts"), so we normalize both sides for a case-insensitive match.
// Magnitudes calibrated to the real distribution of a 1900+ BodySlide preset library
// (per-slider p10/p50/p90): Breasts > Butt > Waist > Hips. The "high" ~= preset p90.
// Waist inverts: lower T = wider waist (petite), higher T = narrower (hourglass).
static const std::unordered_map<std::string, MorphDef> kMorphTable{
    // Bust vs lower body (butt+hips+thighs) are balanced so the DEFAULT body isn't
    // top-heavy — the waist definition then decides Hourglass vs Rectangle, and traits
    // push top (busty) or bottom (wide hips). Higher independence decorrelates them so
    // some NPCs lean bust-heavy and others hip-heavy without a trait.
    { "breasts",       { 12.0f, 88.0f, 0.34f } },  // was 100 — reduce top-heavy bias
    { "butt",          { 10.0f, 92.0f, 0.27f } },
    { "belly",         {  0.0f, 30.0f, 0.35f } },
    { "hips",          {  8.0f, 86.0f, 0.26f } },  // was 68 — lower body now matches bust
    { "thighs",        { 10.0f, 80.0f, 0.26f } },  // was 62
    // Arms track body fullness (same frame score) at a REDUCED ratio so they match
    // the body without becoming as dramatic as bust/butt. Low independence keeps the
    // four arm sliders coherent with each other → a natural taper (Arms > Forearm >
    // Wrist), fixing the "thick body / thin arms" mismatch.
    { "arms",          {  5.0f,  50.0f, 0.10f } },
    { "forearmsize",   {  4.0f,  36.0f, 0.08f } },
    { "wristsize",     {  0.0f,  18.0f, 0.06f } },
    { "chubbyarms",    {  0.0f,  26.0f, 0.12f } },  // only fuller bodies
    // Waist base is modest + some noise; the pronounced waist comes from the
    // Wasp/Straight TRAIT below, not from body size. NOT amplified by fantasy.
    { "waist",         { 15.0f,  12.0f, 0.25f } },
};

// ── Trait system ────────────────────────────────────────────────────────────
// Each NPC rolls AT MOST ONE option per category (mutually exclusive), seeded
// deterministically. Subtle density: ~18-22% chance per category → most NPCs
// have 0-1 traits. Slider names are lowercase (Papyrus passes them lowercased).
struct TMod { const char* slider; float delta; };  // delta in 0-100 space, additive
struct TOpt { const char* name; std::vector<TMod> mods; };
struct TCat { std::uint32_t salt; float chance; std::vector<TOpt> options; };

static const std::vector<TCat> kTraitCategories{
    // Breasts shape. Sag/perkiness are NOT random traits — they're derived from breast
    // size in GetMorphValue (big -> mild sag, small -> perky), so busty/flat below feed
    // them automatically.
    { 0x0B5EA511u, 0.22f, {
        { "busty",     { { "breasts", 35.0f } } },
        { "flat",      { { "breastsgone", 55.0f }, { "breasts", -25.0f } } },
        { "separated", { { "breastwidth", 50.0f } } },
    } },
    // Butt / Hips. Bottom-heavy options affect the whole lower body (the classifier
    // averages butt+hips+thighs), so they clearly create Pear/Spoon, not a weak nudge.
    { 0x0B077A55u, 0.22f, {
        { "bubble",   { { "butt", 38.0f }, { "bigbutt", 30.0f }, { "thighs", 14.0f } } },
        { "flatbutt", { { "butt", -35.0f } } },
        { "widehips", { { "hips", 35.0f }, { "thighs", 22.0f }, { "butt", 14.0f } } },
        { "hipdips",  { { "hipbone", 55.0f } } },
    } },
    // Waist / Torso. Arm deltas keep arms coherent with overall body mass:
    // a soft/fuller body reads wrong with stick arms, a slim one with thick arms.
    { 0x0DA15700u, 0.20f, {
        { "waspwaist",     { { "waist", 45.0f } } },
        { "straighttorso", { { "waist", -30.0f } } },
        { "softbelly",     { { "belly", 32.0f }, { "chubbyarms", 14.0f }, { "arms", 8.0f } } },
    } },
    // Thighs
    { 0x07416B00u, 0.18f, {
        { "thick", { { "thighs", 38.0f }, { "thighinsidethicc_v2", 35.0f }, { "arms", 10.0f }, { "forearmsize", 6.0f } } },
        { "gap",   { { "slimthighs", 55.0f }, { "thighs", -20.0f }, { "arms", -10.0f }, { "forearmsize", -6.0f } } },
    } },
};

// Male trait categories (HIMBO). Female-equivalent regions where they make sense:
// chest (~breasts), torso V-taper (~waist), belly, legs (~thighs). No breast sag/perk.
static const std::vector<TCat> kMaleTraitCategories{
    // Chest
    { 0x0C4E5700u, 0.22f, {
        { "bigpecs",     { { "pecssize", 32.0f } } },
        { "flatchest",   { { "pecssize", -25.0f }, { "pecsflatten", 40.0f } } },
        { "definedpecs", { { "pecssize", 12.0f } } },
    } },
    // Torso shape (abs/definition are now driven continuously by muscle TONE, not a trait).
    { 0x07025000u, 0.22f, {
        { "vtaper", { { "torsoshoulderinc", 35.0f }, { "torsowaistsize", -20.0f } } },
        { "barrel", { { "torsowidth", 30.0f } } },
    } },
    // Belly
    { 0x0BE11A00u, 0.20f, {
        { "gut",         { { "torsobelly", 35.0f }, { "chubby", 18.0f } } },
        { "flatstomach", { { "torsobelly", -25.0f } } },
    } },
    // Legs
    { 0x01E65000u, 0.18f, {
        { "thicklegs",  { { "legssize", 35.0f } } },
        { "skinnylegs", { { "legsthinner", 42.0f } } },
    } },
};

// Summed trait delta for one slider, from a category list and an actor's seed.
float TraitDeltaFrom(const std::vector<TCat>& cats, std::uint32_t a_seedBase, std::string_view a_key) {
    float total = 0.0f;
    for (const auto& cat : cats) {
        std::mt19937 rng{ a_seedBase ^ cat.salt };
        if (std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) >= cat.chance) continue;
        const int idx = std::uniform_int_distribution<int>(0, static_cast<int>(cat.options.size()) - 1)(rng);
        for (const auto& m : cat.options[idx].mods)
            if (a_key == m.slider) total += m.delta;
    }
    return total;
}

float TraitDelta(std::uint32_t s, std::string_view k)     { return TraitDeltaFrom(kTraitCategories, s, k); }
float MaleTraitDelta(std::uint32_t s, std::string_view k) { return TraitDeltaFrom(kMaleTraitCategories, s, k); }

std::string ToLower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// Bimodal draw: pushes distribution toward extremes.
// 42% low (0-35), 16% average (35-65), 42% high (65-100).
float BimodalDraw(std::mt19937& rng) {
    std::uniform_real_distribution<float> pick(0.0f, 1.0f);
    float r = pick(rng);
    if (r < 0.42f) return std::uniform_real_distribution<float>(0.0f, 35.0f)(rng);
    if (r < 0.58f) return std::uniform_real_distribution<float>(35.0f, 65.0f)(rng);
    return std::uniform_real_distribution<float>(65.0f, 100.0f)(rng);
}

}  // namespace

float WeightManager::GetFrameScore(RE::Actor* a_actor) {
    if (!a_actor) return 50.0f;
    std::scoped_lock lock(_mutex);

    const RE::FormID id = a_actor->GetFormID();
    const std::uint32_t seedBase = GetActorSeed(id);

    // Rare "unusual body": out-of-distribution frame score at either extreme —
    // ultra-petite (a band below normal low) or ultra-thick (pinned at the top).
    // The disproportionate look comes from boosted independence in GetMorphValue,
    // and ultra-thick also gets a boosted intensity in GetActorIntensity.
    const int uv = UnusualVariant(id);
    if (uv == 0) {
        std::mt19937 rng{ seedBase ^ 0x0DDBA200u };
        return std::uniform_real_distribution<float>(0.0f, 15.0f)(rng);    // ultra-petite
    }
    if (uv == 1) {
        std::mt19937 rng{ seedBase ^ 0x0DDBA200u };
        return std::uniform_real_distribution<float>(90.0f, 100.0f)(rng);  // ultra-thick
    }

    // Normal bimodal. Different salt from GenerateWeight so it doesn't mirror weight.
    std::mt19937 rng{ seedBase + 0xF00DC0FFu };
    return BimodalDraw(rng);
}

float WeightManager::GetMorphValue(RE::Actor* a_actor, float a_frameScore, std::string_view morphName) {
    const std::string key = ToLower(morphName);
    std::scoped_lock lock(_mutex);

    const std::uint32_t seedBase = a_actor ? GetActorSeed(a_actor->GetFormID()) : _seed;

    // Unusual bodies act like a top-level trait: they REPLACE the category traits
    // (their character comes from the extreme frame/intensity + boosted independence),
    // so the normal trait deltas are suppressed → no contradictions (e.g. ultra-thick
    // with a "thigh gap", or ultra-petite with "busty").
    const bool unusualBody = a_actor && (UnusualVariant(a_actor->GetFormID()) >= 0);

    // Breast sag (BreastGravity2) and perkiness are DERIVED from breast size, not random:
    // larger busts sag a little, smaller busts sit perky, with a smooth crossover at
    // mid-size. Capped low (careful — never exaggerated) and only a small per-NPC
    // variation. This automatically reflects the busty/flat traits via the size.
    if (key == "breastgravity2" || key == "breastperkiness") {
        const auto& bdef = kMorphTable.find("breasts")->second;
        const float t = std::clamp(a_frameScore, 0.0f, 100.0f) / 100.0f;
        const float breastTrait = unusualBody ? 0.0f : TraitDelta(seedBase, "breasts");
        const float breastSize = std::clamp(
            bdef.low + (bdef.high - bdef.low) * t + breastTrait, 0.0f, 100.0f);

        // Rare "unusual" breast shape: an extreme outlier — very saggy OR very perky.
        // Extreme SAG is gated by size: small busts can't sag (looks broken), they get
        // extreme perky instead. Deterministic across both slider calls.
        {
            std::mt19937 ur{ seedBase ^ 0x0DDB5A60u };
            if (std::uniform_real_distribution<float>(0.0f, 1.0f)(ur) < _breastUnusualRatio) {
                const bool wantSag = std::uniform_real_distribution<float>(0.0f, 1.0f)(ur) < 0.5f;
                const bool doSag   = wantSag && breastSize >= 45.0f;  // sag needs volume
                if (key == "breastgravity2")
                    return doSag ? std::uniform_real_distribution<float>(55.0f, 85.0f)(ur) : 0.0f;
                return doSag ? 0.0f : std::uniform_real_distribution<float>(60.0f, 90.0f)(ur);
            }
        }

        float val = (key == "breastgravity2")
            ? std::clamp((breastSize - 50.0f) * 0.45f, 0.0f, 35.0f)   // only larger busts sag
            : std::clamp((50.0f - breastSize) * 0.55f, 0.0f, 42.0f);  // only smaller busts perk

        if (a_actor) {
            const auto h = static_cast<std::uint32_t>(std::hash<std::string>{}(key));
            std::mt19937 nr{ seedBase ^ h };
            val += std::uniform_real_distribution<float>(-8.0f, 8.0f)(nr);  // gentle variation
        }
        return std::clamp(val, 0.0f, 100.0f);
    }

    // Female muscle TONE (definition): a fraction of women are athletic. Independent
    // roll, suppressed by belly/softness — like the male tone. Drives the CBBE 3BA
    // muscle sliders. Definition (kDef in Papyrus), so fantasy never blows it up.
    if (key == "muscle" || key == "muscleabs" || key == "musclearms" ||
        key == "musclelegs" || key == "veramuscletones" ||
        key == "musclemoreabs_v2" || key == "musclemorearms_v2" || key == "musclemorelegs_v2") {
        std::mt19937 ar{ seedBase ^ 0x0A7E1E70u };
        const bool athletic = std::uniform_real_distribution<float>(0.0f, 1.0f)(ar) < _athleticRatio;
        const bool snusnu   = athletic && (std::uniform_real_distribution<float>(0.0f, 1.0f)(ar) < 0.12f);
        const float baseTone = snusnu
            ? std::uniform_real_distribution<float>(92.0f, 110.0f)(ar)   // super toned, Amazon
            : athletic ? std::uniform_real_distribution<float>(55.0f, 90.0f)(ar)
                       : std::uniform_real_distribution<float>(0.0f, 22.0f)(ar);
        const float t = std::clamp(a_frameScore, 0.0f, 100.0f) / 100.0f;
        const float belly = std::clamp(30.0f * t + TraitDelta(seedBase, "belly"), 0.0f, 100.0f);
        const float bellyFactor = snusnu ? 0.20f : 0.55f;   // snu snu stays lean & cut
        const float tone = std::clamp(baseTone - belly * bellyFactor, 0.0f, 100.0f);

        // "More" sliders only for snu snu — the deep-definition overflow above mid-tone.
        const float overflow = std::clamp(tone - 50.0f, 0.0f, 100.0f);
        if (key == "musclemoreabs_v2")  return snusnu ? overflow * 0.90f : 0.0f;
        if (key == "musclemorearms_v2") return snusnu ? overflow * 0.70f : 0.0f;
        if (key == "musclemorelegs_v2") return snusnu ? overflow * 0.65f : 0.0f;

        if (key == "veramuscletones") return tone * 0.90f;
        if (key == "muscleabs")       return tone * 0.85f;
        if (key == "musclearms")      return tone * 0.65f;
        if (key == "musclelegs")      return tone * 0.60f;
        return tone * 0.50f;  // "Muscle" overall
    }

    // Volume base: frameScore-driven interpolation + per-part noise (table sliders only).
    // Shape/trait-only sliders have no table entry → base 0, value comes from traits.
    float base = 0.0f;
    auto it = kMorphTable.find(key);
    if (it != kMorphTable.end()) {
        const auto& def = it->second;
        const float t = std::clamp(a_frameScore, 0.0f, 100.0f) / 100.0f;
        base = def.low + (def.high - def.low) * t;
        if (a_actor) {
            const auto morphHash = static_cast<std::uint32_t>(std::hash<std::string>{}(key));
            std::mt19937 noiseRng{ seedBase ^ morphHash };
            // Unusual bodies get boosted per-part independence → parts deviate from the
            // frame score, producing a disproportionate / atypical silhouette.
            float indep = def.independence;
            if (unusualBody) indep *= 2.5f;
            base += std::uniform_real_distribution<float>(-50.0f, 50.0f)(noiseRng) * indep;
        }
    }

    // Add per-NPC trait contributions — suppressed on unusual bodies (they replace
    // the category traits, see unusualBody above).
    const float td = unusualBody ? 0.0f : TraitDelta(seedBase, key);

    return std::clamp(base + td, 0.0f, 100.0f);
}

float WeightManager::GetActorIntensity(RE::Actor* a_actor) {
    if (!a_actor) return _morphScale;
    std::scoped_lock lock(_mutex);

    const RE::FormID id = a_actor->GetFormID();
    std::mt19937 rng{ GetActorSeed(id) ^ 0x5A11FA57u };

    // Unusual bodies override the normal realistic/fantasy roll to nail the extreme:
    //   ultra-thick  → high intensity (genuinely huge)
    //   ultra-petite → low intensity  (stays tiny)
    const int uv = UnusualVariant(id);
    if (uv == 1) return std::uniform_real_distribution<float>(1.7f, 2.4f)(rng) * _morphScale;
    if (uv == 0) return std::uniform_real_distribution<float>(0.70f, 0.95f)(rng) * _morphScale;

    // Snu snu: big, powerful muscular frame to match the maxed muscle tone.
    if (IsSnuSnu(id)) return std::uniform_real_distribution<float>(1.35f, 1.7f)(rng) * _morphScale;

    // Normal: realistic majority + fantasy minority.
    const float roll = std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
    float intensity;
    if (roll < _fantasyRatio) {
        // Fantasy: pushed into the preset tail (p90+). The bombshells.
        intensity = std::uniform_real_distribution<float>(1.3f, 2.2f)(rng);
    } else {
        // Realistic: spread around 1.0, matching the bulk of the preset library.
        intensity = std::uniform_real_distribution<float>(0.85f, 1.15f)(rng);
    }
    return intensity * _morphScale;
}

// ---------------------------------------------------------------------------
// Male procedural morphs (HIMBO). Two driving values per NPC: muscle and fat.
// ---------------------------------------------------------------------------

namespace {

// Muscle level 0-100: bimodal. Ceiling kept moderate — HIMBO sliders distort past
// ~1.0, so even the buff band tops out around 78 (×intensity stays sane).
float MaleMuscle(std::uint32_t seed) {
    std::mt19937 rng{ seed ^ 0x4D5C1E00u };
    const float r = std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
    if (r < 0.40f) return std::uniform_real_distribution<float>(0.0f, 22.0f)(rng);
    if (r < 0.70f) return std::uniform_real_distribution<float>(22.0f, 45.0f)(rng);
    return std::uniform_real_distribution<float>(45.0f, 68.0f)(rng);
}

// Fat level 0-100: skewed low. Ceiling tamed so chunky bodies don't blow out.
float MaleFat(std::uint32_t seed) {
    std::mt19937 rng{ seed ^ 0x7A700000u };
    const float r = std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
    if (r < 0.60f) return std::uniform_real_distribution<float>(0.0f, 20.0f)(rng);
    if (r < 0.85f) return std::uniform_real_distribution<float>(20.0f, 40.0f)(rng);
    return std::uniform_real_distribution<float>(40.0f, 60.0f)(rng);
}

// HIMBO sliders derived from muscle (M) and fat (F). Lowercase keys (Papyrus lowercases).
// Only the base (build) sliders here; shape sliders come from male traits.
float MaleSlider(std::string_view key, float M, float F) {
    if (key == "muscle")                                return M;
    if (key == "armsbiceps" || key == "armsdelts")      return M * 0.85f;
    if (key == "pecssize"   || key == "pecsmass")       return M * 0.70f + (M + F) * 0.05f;
    if (key == "chubby"     || key == "legschubby")     return F;
    if (key == "torsobelly" || key == "torsobellychub") return F * 0.90f;
    if (key == "bodymass"   || key == "torsomass")      return M * 0.42f + F * 0.48f;
    if (key == "buttbooty")                             return 18.0f + M * 0.22f + F * 0.26f;
    if (key == "legssize")                              return M * 0.35f + F * 0.40f;
    if (key == "lean")                                  return std::clamp(75.0f - M * 0.6f - F * 0.9f, 0.0f, 100.0f);

    // Muscle TONE = definition (cut/ripped vs soft). Emerges from the distribution:
    // muscle present + low fat → abs/ribs/V-cut show; high fat smooths them away.
    const float tone = std::clamp(M - F * 1.1f + 5.0f, 0.0f, 100.0f);
    if (key == "torsoflatabs")        return tone * 0.85f;
    if (key == "torsovline")          return tone * 0.60f;
    if (key == "torsoribsdefinition") return tone * 0.50f;
    if (key == "armstrapsvalleys")    return tone * 0.45f;

    return 0.0f;  // pure trait sliders (pecsflatten, vtaper...) start at 0
}

}  // namespace

float WeightManager::GetMaleMorphValue(RE::Actor* a_actor, std::string_view morphName) {
    if (!a_actor) return 0.0f;
    const std::string key = ToLower(morphName);
    std::scoped_lock lock(_mutex);

    const RE::FormID id = a_actor->GetFormID();
    const std::uint32_t seedBase = GetActorSeed(id);
    const int uv = MaleUnusualVariant(id);  // -1 normal, 0 ultra-skinny, 1 ultra-huge

    float M = MaleMuscle(seedBase);
    float F = MaleFat(seedBase);
    if (uv == 0) {                          // ultra-skinny
        std::mt19937 r{ seedBase ^ 0x0DDFE200u };
        M = std::uniform_real_distribution<float>(0.0f, 15.0f)(r);
        F = std::uniform_real_distribution<float>(0.0f, 10.0f)(r);
    } else if (uv == 1) {                   // ultra-huge: big muscle OR big fat (tamed)
        std::mt19937 r{ seedBase ^ 0x0DDFE201u };
        if (std::uniform_real_distribution<float>(0.0f, 1.0f)(r) < 0.5f)
            M = std::uniform_real_distribution<float>(60.0f, 76.0f)(r);
        else
            F = std::uniform_real_distribution<float>(48.0f, 65.0f)(r);
    }

    float val = MaleSlider(key, M, F);

    // Traits — suppressed on unusual bodies (they replace the trait set, like females).
    if (uv < 0) val += MaleTraitDelta(seedBase, key);

    // Per-part variation; boosted for unusual → disproportionate/atypical male body.
    const auto h = static_cast<std::uint32_t>(std::hash<std::string>{}(key));
    std::mt19937 nr{ seedBase ^ h ^ 0x0A1Eu };
    const float amp = (uv >= 0) ? 16.0f : 6.0f;
    val += std::uniform_real_distribution<float>(-amp, amp)(nr);

    return std::clamp(val, 0.0f, 100.0f);
}

float WeightManager::GetMaleIntensity(RE::Actor* a_actor) {
    if (!a_actor) return _morphScale;
    std::scoped_lock lock(_mutex);

    const RE::FormID id = a_actor->GetFormID();
    std::mt19937 rng{ GetActorSeed(id) ^ 0x4A1E0000u };

    // Multipliers kept low — HIMBO volume distorts above ~1.1 (much tighter than CBBE).
    const int uv = MaleUnusualVariant(id);
    if (uv == 1) return std::uniform_real_distribution<float>(1.18f, 1.40f)(rng) * _morphScale; // ultra-huge
    if (uv == 0) return std::uniform_real_distribution<float>(0.68f, 0.88f)(rng) * _morphScale; // ultra-skinny

    const float roll = std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
    float intensity = (roll < _fantasyRatio)
        ? std::uniform_real_distribution<float>(1.05f, 1.28f)(rng)  // fantasy: bodybuilder
        : std::uniform_real_distribution<float>(0.82f, 1.05f)(rng);
    return intensity * _morphScale;
}

void WeightManager::QueueForMorphs(RE::Actor* a_actor) {
    if (!a_actor) return;
    std::scoped_lock lock(_mutex);
    _morphQueue.push_back(a_actor->GetFormID());
}

RE::Actor* WeightManager::GetNextMorphActor() {
    std::scoped_lock lock(_mutex);
    if (_morphQueue.empty()) return nullptr;

    // Distance-aware lazy drain: hand back the CLOSEST loaded actor to the player
    // first, and defer actors beyond the radius until the player approaches. Stale
    // (unloaded) actors are dropped — OBody re-queues them when their 3D reloads.
    auto* player = RE::PlayerCharacter::GetSingleton();
    const bool hasPlayer = player != nullptr;
    const RE::NiPoint3 pPos = hasPlayer ? player->GetPosition() : RE::NiPoint3{};
    constexpr float kRadiusSq = 8192.0f * 8192.0f;  // ~2 cells

    std::vector<RE::FormID> alive;
    alive.reserve(_morphQueue.size());
    RE::Actor*  best   = nullptr;
    RE::FormID  bestId = 0;
    float       bestDistSq = kRadiusSq;

    for (const RE::FormID id : _morphQueue) {
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
        if (!actor || !actor->Is3DLoaded()) continue;  // drop stale/unloaded
        alive.push_back(id);
        if (!hasPlayer) {
            if (!best) { best = actor; bestId = id; }
        } else {
            const float dSq = actor->GetPosition().GetSquaredDistance(pPos);
            if (dSq < bestDistSq) { bestDistSq = dSq; best = actor; bestId = id; }
        }
    }
    _morphQueue.swap(alive);

    if (!best) return nullptr;  // none within radius → keep them queued, process later
    auto it = std::find(_morphQueue.begin(), _morphQueue.end(), bestId);
    if (it != _morphQueue.end()) _morphQueue.erase(it);
    return best;
}

void WeightManager::RegenerateActor(RE::Actor* a_actor) {
    if (!a_actor) return;
    std::scoped_lock lock(_mutex);
    const RE::FormID id = a_actor->GetFormID();
    const std::uint32_t newSeed = OBW::CollectEntropy();  // new random body every press
    _overrideSeed[id] = newSeed;                          // morphs (per-reference)
    if (auto* base = a_actor->GetActorBase())
        _overrideSeed[base->GetFormID()] = newSeed;       // weight (per-base, next load)
    _processed.erase(id);

    // NECK SEAM PRECAUTION: the actor is already loaded (the player aimed at it), so
    // its head facegen is built at the current weight. Re-rolling base->weight now
    // would desync the body neck from the baked head → seam. So we only re-roll the
    // MORPHS (neck-safe); the new weight takes effect the next time the actor loads.
    QueueForMorphs(a_actor);
}

void WeightManager::RegenerateSeed() {
    std::scoped_lock lock(_mutex);
    _seed = OBW::CollectEntropy();
    _overrideSeed.clear();
    ClearProcessed();
    SKSE::log::info("OBW: seed regenerated → {}", _seed);
}

// ---------------------------------------------------------------------------
// Cosave
// ---------------------------------------------------------------------------

void WeightManager::Save(SKSE::SerializationInterface* a_intf) {
    // Seed
    if (a_intf->OpenRecord(kRecordSeed, kRecordVer))
        a_intf->WriteRecordData(_seed);

    // Configuração (modo + bias)
    if (a_intf->OpenRecord(kRecordCfg, kRecordVer)) {
        a_intf->WriteRecordData(static_cast<std::uint8_t>(_mode));
        a_intf->WriteRecordData(_bias);
    }

    // Body shape mode
    if (a_intf->OpenRecord(kRecordBody, kRecordVer))
        a_intf->WriteRecordData(static_cast<std::uint8_t>(_bodyMode));

    // Global morph intensity
    if (a_intf->OpenRecord(kRecordScl, kRecordVer))
        a_intf->WriteRecordData(_morphScale);

    // Fantasy ratio
    if (a_intf->OpenRecord(kRecordFan, kRecordVer))
        a_intf->WriteRecordData(_fantasyRatio);

    // Unusual-body ratio
    if (a_intf->OpenRecord(kRecordUnu, kRecordVer))
        a_intf->WriteRecordData(_unusualRatio);

    // Unusual-breast ratio
    if (a_intf->OpenRecord(kRecordBUn, kRecordVer))
        a_intf->WriteRecordData(_breastUnusualRatio);

    // Athletic (female muscle tone) ratio
    if (a_intf->OpenRecord(kRecordAth, kRecordVer))
        a_intf->WriteRecordData(_athleticRatio);

    // Re-roll hotkey
    if (a_intf->OpenRecord(kRecordKey, kRecordVer))
        a_intf->WriteRecordData(_reRollKey);

    // Per-actor override seeds (hotkey re-rolls) — count followed by id/seed pairs.
    if (a_intf->OpenRecord(kRecordOvr, kRecordVer)) {
        const std::uint32_t count = static_cast<std::uint32_t>(_overrideSeed.size());
        a_intf->WriteRecordData(count);
        for (const auto& [id, seed] : _overrideSeed) {
            a_intf->WriteRecordData(id);
            a_intf->WriteRecordData(seed);
        }
    }
}

void WeightManager::Load(SKSE::SerializationInterface* a_intf) {
    std::scoped_lock lock(_mutex);
    std::uint32_t type{}, ver{}, len{};
    while (a_intf->GetNextRecordInfo(type, ver, len)) {
        if (type == kRecordSeed) {
            a_intf->ReadRecordData(_seed);
        } else if (type == kRecordCfg) {
            std::uint8_t mode{};
            a_intf->ReadRecordData(mode);
            _mode = static_cast<WeightMode>(mode);
            a_intf->ReadRecordData(_bias);
        } else if (type == kRecordBody) {
            std::uint8_t bm{};
            a_intf->ReadRecordData(bm);
            _bodyMode = static_cast<BodyMode>(bm);
        } else if (type == kRecordScl) {
            a_intf->ReadRecordData(_morphScale);
        } else if (type == kRecordFan) {
            a_intf->ReadRecordData(_fantasyRatio);
        } else if (type == kRecordUnu) {
            a_intf->ReadRecordData(_unusualRatio);
        } else if (type == kRecordBUn) {
            a_intf->ReadRecordData(_breastUnusualRatio);
        } else if (type == kRecordAth) {
            a_intf->ReadRecordData(_athleticRatio);
        } else if (type == kRecordKey) {
            a_intf->ReadRecordData(_reRollKey);
        } else if (type == kRecordOvr) {
            std::uint32_t count{};
            a_intf->ReadRecordData(count);
            for (std::uint32_t i = 0; i < count; ++i) {
                RE::FormID id{};
                std::uint32_t seed{};
                a_intf->ReadRecordData(id);
                a_intf->ReadRecordData(seed);
                // Remap FormID across load-order changes; skip unresolvable (e.g. stale refs).
                RE::FormID newId{};
                if (a_intf->ResolveFormID(id, newId))
                    _overrideSeed[newId] = seed;
            }
        }
    }
    if (_seed == 0)
        _seed = OBW::CollectEntropy();

    // Random mode: discard the saved seed and overrides — regenerate so NPCs look
    // different each load, while staying consistent within a session.
    if (_mode == WeightMode::kRandom) {
        _seed = OBW::CollectEntropy();
        _overrideSeed.clear();
        ClearProcessed();
    }
}

void WeightManager::Revert() {
    std::scoped_lock lock(_mutex);
    // Reset config to defaults so a save lacking these records doesn't inherit
    // stale config from a previously-loaded save in the same session.
    _mode         = WeightMode::kSeeded;
    _bodyMode     = BodyMode::kProcedural;
    _bias         = 0.0f;
    _morphScale   = Config::g_defaultMorphScale;
    _fantasyRatio = Config::g_defaultFantasyRatio;
    _unusualRatio = Config::g_defaultUnusualRatio;
    _breastUnusualRatio = Config::g_defaultBreastUnusualRatio;
    _athleticRatio = Config::g_defaultAthleticRatio;
    _reRollKey = Config::g_defaultReRollKey;
    _seed         = OBW::CollectEntropy();
    _overrideSeed.clear();
    ClearProcessed();
}

void WeightManager::SaveCallback(SKSE::SerializationInterface* a_intf) {
    GetSingleton().Save(a_intf);
}
void WeightManager::LoadCallback(SKSE::SerializationInterface* a_intf) {
    GetSingleton().Load(a_intf);
}
void WeightManager::RevertCallback(SKSE::SerializationInterface* a_intf) {
    (void)a_intf;
    GetSingleton().Revert();
}

}  // namespace OBW
