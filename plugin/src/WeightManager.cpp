#include "WeightManager.hpp"
#include "FastRandom.hpp"
#include "Config.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace OBW {

WeightManager::WeightManager() {
    _morphScale   = Config::g_defaultMorphScale;
    _presetOrient = Config::g_defaultPresetOrient;
    _fantasyRatio = Config::g_defaultFantasyRatio;
    _unusualRatio = Config::g_defaultUnusualRatio;
    _breastUnusualRatio = Config::g_defaultBreastUnusualRatio;
    _athleticRatio = Config::g_defaultAthleticRatio;
    _femaleBodies = Config::g_defaultFemaleBodies;
    _maleBodies = Config::g_defaultMaleBodies;
    _maleBuild = Config::g_defaultMaleBuild;
    _reRollKey = Config::g_defaultReRollKey;
    _debugLog = Config::g_defaultDebugLog;
    g_debugLog = _debugLog;
    _neckColorFix = Config::g_defaultNeckColorFix;
}

// NECK-SEAM COLOR FIX (head -> body tone). Reads the NPC's body skin tone (bodyTintColor) and blends the
// head's facegen skin TINT toward it by _neckColorFix. Body is untouched (its texture is the reference); only
// the head's tint material shifts, so a head<->body tone mismatch at the neck is reduced. This is a runtime
// tint pull (a multiplier), NOT the baked facegen texture - so it lessens, doesn't always erase, a seam rooted
// in very different textures. No-op if strength<=0 or the head 3D isn't loaded yet.
void WeightManager::ApplyNeckColor(RE::Actor* a_actor) {
    const float t = _neckColorFix;
    if (!a_actor || t <= 0.0f) return;
    auto* npc = a_actor->GetActorBase();
    if (!npc) return;
    const auto bt = npc->bodyTintColor;                          // body skin tone (the reference)
    const RE::NiColor body{ bt.red / 255.0f, bt.green / 255.0f, bt.blue / 255.0f };
    auto* faceNode = a_actor->GetFaceNode();
    if (!faceNode) return;                                        // head not built yet -> skip
    const float k = t > 1.0f ? 1.0f : t;
    int touched = 0;
    RE::BSVisit::TraverseScenegraphGeometries(faceNode, [&](RE::BSGeometry* a_geom) -> RE::BSVisit::BSVisitControl {
        auto* prop = a_geom->GetGeometryRuntimeData().properties[RE::BSGeometry::States::kEffect].get();
        auto* lp   = netimmerse_cast<RE::BSLightingShaderProperty*>(prop);
        if (!lp || !lp->material) return RE::BSVisit::BSVisitControl::kContinue;
        if (lp->material->GetFeature() != RE::BSShaderMaterial::Feature::kFaceGenRGBTint)
            return RE::BSVisit::BSVisitControl::kContinue;        // only the head's facegen-tint skin geometry
        auto* mat = static_cast<RE::BSLightingShaderMaterialFacegenTint*>(lp->material);
        mat->tintColor.red   += (body.red   - mat->tintColor.red)   * k;
        mat->tintColor.green += (body.green - mat->tintColor.green) * k;
        mat->tintColor.blue  += (body.blue  - mat->tintColor.blue)  * k;
        ++touched;
        return RE::BSVisit::BSVisitControl::kContinue;
    });
    if (_debugLog && touched)
        SKSE::log::info("NeckColor: '{}' head tint -> body tone (k={:.2f}, {} skin mat)",
                        npc->GetName(), k, touched);
}

// Deferred re-apply worker: the head/body tone settles LATE (RSV re-applies the head on NiNodeUpdate; the body
// variant lands deferred), so a single match reverts. We re-apply at a few delays so OBW corrects LAST and holds.
namespace {
struct PendingNeck { RE::FormID id; double dueMs; };
std::mutex                 s_neckMutex;
std::vector<PendingNeck>   s_neckQueue;
std::atomic<bool>          s_neckThread{ false };
double NowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}
}

void WeightManager::ScheduleNeckColor(RE::FormID a_id) {
    if (_neckColorFix <= 0.0f || !a_id) return;
    const double now = NowMs();
    {
        std::lock_guard lk(s_neckMutex);
        for (double d : { 2000.0, 6000.0, 12000.0 })       // re-apply after the late actors settle
            s_neckQueue.push_back({ a_id, now + d });
    }
    if (s_neckThread.exchange(true)) return;               // worker already running
    std::thread([] {
        for (;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            const double now = NowMs();
            std::vector<RE::FormID> due;
            {
                std::lock_guard lk(s_neckMutex);
                for (auto it = s_neckQueue.begin(); it != s_neckQueue.end();) {
                    if (it->dueMs <= now) { due.push_back(it->id); it = s_neckQueue.erase(it); }
                    else ++it;
                }
            }
            for (RE::FormID id : due) {
                if (auto* t = SKSE::GetTaskInterface())
                    t->AddTask([id] {
                        if (auto* a = RE::TESForm::LookupByID<RE::Actor>(id))
                            WeightManager::GetSingleton().ApplyNeckColor(a);
                    });
            }
        }
    }).detach();
}

WeightManager& WeightManager::GetSingleton() noexcept {
    static WeightManager instance;
    return instance;
}

float WeightManager::GenerateWeight(RE::Actor* a_actor) {
    if (!a_actor) return 50.0f;
    std::scoped_lock lock(_mutex);

    // Random and Seeded both draw a simulated weight (Random vs Seeded differ only in whether _seed
    // is regenerated each load). Per-BASE seed so all instances of a shared base agree.
    RE::FormID seedId = a_actor->GetFormID();
    if (auto* base = a_actor->GetActorBase()) seedId = base->GetFormID();
    std::mt19937 rng{ GetActorSeed(seedId) };
    const float raw = std::uniform_real_distribution<float>(0.0f, 100.0f)(rng);
    return std::clamp(raw + _bias, 0.0f, 100.0f);
}

float WeightManager::GetPresetWeight(RE::Actor* a_actor) {
    if (!a_actor) return 50.0f;
    std::scoped_lock lock(_mutex);

    // Per-INSTANCE seed (actor formID, not base) so NPCs that share a base still get distinct
    // weights -> distinct preset interpolation. Random vs Seeded comes from _seed (see Load()).
    std::mt19937 rng{ GetActorSeed(a_actor->GetFormID()) ^ 0x7E16E700u };
    const float raw = std::uniform_real_distribution<float>(0.0f, 100.0f)(rng);
    return std::clamp(raw + _bias, 0.0f, 100.0f);
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
// Magnitudes calibrated to the real distribution of a large BodySlide preset library
// (per-slider p10/p50/p90): Breasts > Butt > Waist > Hips. The "high" ~= preset p90.
// Waist inverts: lower T = wider waist (petite), higher T = narrower (hourglass).
static const std::unordered_map<std::string, MorphDef> kMorphTable{
    // Bust vs lower body (butt+hips+thighs) are balanced so the DEFAULT body isn't
    // top-heavy — the waist definition then decides Hourglass vs Rectangle, and traits
    // push top (busty) or bottom (wide hips). Higher independence decorrelates them so
    // some NPCs lean bust-heavy and others hip-heavy without a trait.
    // Independence bumped (~x1.25) in the 2026-06-17 variety pass so same-archetype NPCs
    // decorrelate more (less "clones"); arms stay low to preserve the taper. See DeltaJitter.
    { "breasts",       { 12.0f, 88.0f, 0.42f } },  // was 100 — reduce top-heavy bias
    { "butt",          { 10.0f, 92.0f, 0.34f } },
    { "belly",         {  0.0f, 30.0f, 0.43f } },
    { "hips",          {  8.0f, 86.0f, 0.33f } },  // was 68 — lower body now matches bust
    { "thighs",        { 10.0f, 80.0f, 0.33f } },  // was 62
    // Arms track body fullness (same frame score) at a REDUCED ratio so they match
    // the body without becoming as dramatic as bust/butt. Low independence keeps the
    // four arm sliders coherent with each other → a natural taper (Arms > Forearm >
    // Wrist), fixing the "thick body / thin arms" mismatch.
    { "arms",          {  5.0f,  50.0f, 0.10f } },
    { "forearmsize",   {  4.0f,  36.0f, 0.08f } },
    { "wristsize",     {  0.0f,  18.0f, 0.06f } },
    { "chubbyarms",    {  0.0f,  26.0f, 0.13f } },  // only fuller bodies
    // Waist base is modest + some noise; the pronounced waist comes from the
    // Wasp/Straight TRAIT below, not from body size. NOT amplified by fantasy.
    { "waist",         { 15.0f,  12.0f, 0.31f } },
};

// Per-NPC per-region perturbation of the archetype delta (2026-06-17 variety pass): each instance
// of the same archetype gets its own offset, so two Pears (or two Balanced) aren't clones.
// Raised to +/-10 in the 2026-06-18 differentiation pass (sim_motor.cpp): cuts same-archetype
// near-twins while keeping region junctions smooth and archetypes legible; suppressed for unusual
// bodies (their character is the extreme frame). amplitude in 0-100 slider space.
constexpr float kDeltaJitter = 10.0f;
// Global multiplier on the per-region independence noise (the dominant variety lever, per the
// ablation in sim_motor.cpp). x1.3: roughly halves same-archetype clones vs x1.0, at a modest
// junction/separability cost (still well inside the smooth, archetype-legible frontier).
constexpr float kIndepScale = 1.3f;

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
    { 0x0B5EA511u, 0.13f, {
        { "busty",     { { "breasts", 35.0f } } },
        { "flat",      { { "breastsgone", 55.0f }, { "breasts", -25.0f } } },
        { "separated", { { "breastwidth", 50.0f } } },
    } },
    // Butt / Hips. Bottom-heavy options affect the whole lower body (the classifier
    // averages butt+hips+thighs), so they clearly create Pear/Spoon, not a weak nudge.
    { 0x0B077A55u, 0.13f, {
        { "bubble",   { { "butt", 38.0f }, { "bigbutt", 30.0f }, { "thighs", 14.0f } } },
        { "flatbutt", { { "butt", -35.0f } } },
        { "widehips", { { "hips", 35.0f }, { "thighs", 22.0f }, { "butt", 14.0f } } },
        { "hipdips",  { { "hipbone", 55.0f } } },
    } },
    // Waist / Torso. Arm deltas keep arms coherent with overall body mass:
    // a soft/fuller body reads wrong with stick arms, a slim one with thick arms.
    { 0x0DA15700u, 0.12f, {
        { "waspwaist",     { { "waist", 45.0f } } },
        { "straighttorso", { { "waist", -30.0f } } },
        { "softbelly",     { { "belly", 32.0f }, { "chubbyarms", 14.0f }, { "arms", 8.0f } } },
    } },
    // Thighs
    { 0x07416B00u, 0.11f, {
        { "thick", { { "thighs", 38.0f }, { "thighinsidethicc_v2", 35.0f }, { "arms", 10.0f }, { "forearmsize", 6.0f } } },
        { "gap",   { { "slimthighs", 55.0f }, { "thighs", -20.0f }, { "arms", -10.0f }, { "forearmsize", -6.0f } } },
    } },
    // Butt SHAPE (independent of size, ~30% get a pronounced one): heart (full low + round),
    // round (bubble), shelf (athletic high/squared), dimpled (soft). Drives the dedicated 3BA
    // butt-shape sliders (applecheeks/buttclassic/buttshape2/buttdimples) + roundass.
    { 0x0B077B22u, 0.30f, {
        { "heart",   { { "applecheeks", 55.0f }, { "roundass", 35.0f } } },
        { "round",   { { "roundass", 48.0f }, { "buttclassic", 25.0f } } },
        { "shelf",   { { "buttshape2", 45.0f }, { "buttclassic", 22.0f } } },
        { "dimpled", { { "buttdimples", 50.0f }, { "roundass", 18.0f } } },
    } },
    // Breast SHAPE (independent of size, ~30%): round (full upper pole), teardrop (natural, less top),
    // east-west (point outward), wide-set (separated). Drives breasttopslope/breastsideshape/breastwidth.
    { 0x0B5EA522u, 0.30f, {
        { "round",    { { "breasttopslope", 48.0f } } },
        { "teardrop", { { "breasttopslope", -22.0f } } },
        { "eastwest", { { "breastsideshape", 45.0f }, { "breastwidth", 25.0f } } },
        { "wideset",  { { "breastwidth", 50.0f } } },
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
    // Shoulders / frame width (2026-06-19 male diff pass) — broad vs narrow upper body.
    { 0x05401D00u, 0.20f, {
        { "broad",  { { "torsoshoulderinc", 30.0f }, { "armsshoulders", 18.0f } } },
        { "narrow", { { "torsoshoulderinc", -18.0f } } },
    } },
    // Back / traps — thick lats or a strong yoke.
    { 0x0BAC0B00u, 0.18f, {
        { "wideback",   { { "torsobacksize", 32.0f } } },
        { "thicktraps", { { "armstraps", 30.0f } } },
    } },
    // Glutes — a fuller, rounder rear (rare-ish).
    { 0x06077B00u, 0.14f, {
        { "bubble", { { "buttbooty", 28.0f }, { "buttroundy", 22.0f } } },
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

// Per-region female muscle tone (0-100). The single source of truth for the muscle morph
// sliders (and GetToneScore). A woman is athletic as a whole (gate + base), but one region
// dominates → local variety ("leg day", strong core, defined arms). Belly suppresses the
// core most.
struct ToneSet { float core; float arms; float legs; float overall; bool snusnu; };

float ArchetypeDelta(std::uint32_t seedBase, const std::string& key);  // defined below

// a_toneBias: archetype muscle-tone lift (Athletic/Amazon), added to every region's base tone.
ToneSet ComputeTones(std::uint32_t a_seedBase, float a_frameScore, float a_athleticRatio,
                     float a_toneBias = 0.0f) {
    // SAME stream as IsSnuSnu — draw1 = athletic gate, draw2 = snu snu. Don't reorder.
    std::mt19937 ar{ a_seedBase ^ 0x0A7E1E70u };
    const bool athletic = std::uniform_real_distribution<float>(0.0f, 1.0f)(ar) < a_athleticRatio;
    const bool snusnu   = athletic && (std::uniform_real_distribution<float>(0.0f, 1.0f)(ar) < 0.12f);
    float baseTone = snusnu
        ? std::uniform_real_distribution<float>(92.0f, 110.0f)(ar)
        : athletic ? std::uniform_real_distribution<float>(55.0f, 90.0f)(ar)
                   : std::uniform_real_distribution<float>(0.0f, 22.0f)(ar);
    baseTone = std::clamp(baseTone + a_toneBias, 0.0f, 115.0f);  // archetype tone lift

    const float t = std::clamp(a_frameScore, 0.0f, 100.0f) / 100.0f;
    const float belly = std::clamp(30.0f * t + ArchetypeDelta(a_seedBase, "belly")
                                   + TraitDelta(a_seedBase, "belly"), 0.0f, 100.0f);

    // One dominant region per woman; the others sit slightly lower (sum ≈ neutral).
    std::mt19937 dr{ a_seedBase ^ 0x70E50D01u };
    const int dom = static_cast<int>(dr() % 3u);

    auto region = [&](int r, float bellyFactor, std::uint32_t salt) -> float {
        std::mt19937 nr{ a_seedBase ^ salt };
        // Dominant-region split ONLY for athletic women — otherwise the +bias would lift
        // non-athletic (baseTone 0-22) women into visible muscle and everyone looks toned.
        // Non-athletic get only tiny noise so they stay smooth, like before.
        const float bias = athletic
            ? (r == dom ? 18.0f : -10.0f) + std::uniform_real_distribution<float>(-6.0f, 6.0f)(nr)
            : std::uniform_real_distribution<float>(-4.0f, 4.0f)(nr);
        const float bf = snusnu ? 0.20f : bellyFactor;  // snu snu stays lean & cut all over
        return std::clamp(baseTone + bias - belly * bf, 0.0f, 100.0f);
    };

    ToneSet ts;
    ts.core    = region(0, 0.55f, 0x0C0DE001u);  // abs hidden by belly the most
    ts.arms    = region(1, 0.30f, 0x0A4D5002u);
    ts.legs    = region(2, 0.25f, 0x0CE65003u);
    ts.overall = (ts.core + ts.arms + ts.legs) / 3.0f;
    ts.snusnu  = snusnu;
    return ts;
}

std::string ToLower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// Center-weighted draw (triangular, peak/median at 50): the MEDIAN body is AVERAGE in
// every measure, with petite and curvy bodies tapering off symmetrically as variety.
// Replaces the old bimodal (42% low / 16% avg / 42% high) that made extreme bodies the
// majority and produced the "prominent hourglass" look (a high frame score drove bust +
// butt + hips + thighs up together). Triangular keeps the full 0-100 range but ~51% now
// land in 35-65 (average), ~24.5% petite, ~24.5% curvy.
float CenterDraw(std::mt19937& rng) {
    std::uniform_real_distribution<float> u(0.0f, 100.0f);
    return (u(rng) + u(rng)) * 0.5f;   // average of two uniforms -> triangular, median 50
}

// ── Body archetypes ──────────────────────────────────────────────────────────
// Each NPC gets ONE coherent archetype (seeded) that composes the system's feature axes —
// overall size (frame), per-region volume, waist definition, muscle tone, intensity — into a
// rich, legible body type, instead of independent trait rolls that can contradict each other.
// Balanced/Slim/Pear dominate (the common average body); Hourglass/Obese/Amazon are rare.
// Light trait rolls still layer on top for micro-variation. Deltas are 0-100 slider space;
// dWaist>0 = cinched (narrower), <0 = wider. frameBias shifts size; frameScale spreads it.
struct Archetype {
    const char* name; float weight; float frameBias; float frameScale;
    float dBreasts, dButt, dHips, dThighs, dBelly, dWaist, dArms, dShoulders;
    float toneBias;       // additive muscle tone (0-100 space)
    float intensityBias;  // additive SKEE intensity multiplier
};
static const std::vector<Archetype> kArchetypes{
    //  name             wt     frB    frS   brs  butt  hips  thgh belly waist arms  shld  tone   int
    // weight column flattened in the 2026-06-17 variety pass: top 4 (Balanced/Rect/Slim/Pear) went
    // from ~50% to ~38% of NPCs, rarer types raised, so fewer NPCs land on the same archetype.
    // shld (2026-06-19): shoulder breadth = the silhouette axis. Narrow on Pear/Petite (true pear),
    // broad on Amazon/Stocky/Athletic/Rectangle/TopHeavy (V-taper / strong / boyish). Big "type" expander.
    { "Balanced",      10.0f,   0.0f, 1.00f,   0,    0,    0,    0,    0,    5,    0,    0,    0,  0.00f },
    { "Rectangle",      9.0f,   0.0f, 0.95f,  -6,   -8,   -8,   -4,    0,  -12,    0,   14,    8, -0.03f },  // straight/boyish, broad-ish, no waist cinch
    { "Slim",           8.0f, -12.0f, 0.90f,  -8,   -6,   -6,   -6,    0,    0,   -4,   -4,    5, -0.05f },
    { "Pear",           9.0f,   2.0f, 1.00f,  -8,   22,   26,   20,    0,   12,    0,  -16,    0,  0.00f },  // narrow shoulders + wide hips = true pear
    { "TopHeavy",       7.0f,   2.0f, 1.00f,  26,   -8,  -10,   -6,    0,    8,    0,   14,    0,  0.00f },  // broad upper + big bust = inverted triangle
    { "Hourglass",      7.0f,   4.0f, 1.00f,  20,   18,   20,    8,    0,   40,    0,    2,    0,  0.04f },
    { "Voluptuous",     7.0f,  14.0f, 1.00f,  18,   18,   12,   10,    0,   18,    4,    4,    0,  0.12f },
    { "AppleSoft",      6.0f,   8.0f, 1.00f,   8,    4,    6,    8,   35,  -10,   14,    6,    0,  0.05f },
    { "BBW",            6.0f,  18.0f, 1.00f,  24,   24,   20,   22,   30,    0,   20,   10,    0,  0.18f },  // big soft curvy (between Voluptuous & Obese)
    { "Athletic",       6.0f,  -4.0f, 0.95f,   0,    8,    0,    6,   -5,   14,   -2,   18,   55,  0.00f },  // broad shoulders, V-taper
    { "AthleticCurvy",  5.0f,   2.0f, 1.00f,  12,   18,    6,   10,   -6,   16,    0,   10,   45,  0.00f },  // toned WITH curves (fitness)
    { "Obese",          4.0f,  24.0f, 1.00f,  20,   20,   18,   28,   70,  -15,   30,   12,    0,  0.22f },
    { "Stocky",         4.0f,   6.0f, 0.85f,  -4,   16,   20,   20,    6,   -8,   10,   22,   25,  0.05f },  // short sturdy, broad + wide lower (warrior races)
    { "Petite",         3.0f, -28.0f, 0.70f, -10,   -8,   -8,   -8,    0,    0,   -6,  -16,    0, -0.08f },  // narrow shoulders too (small frame)
    { "Amazon",         3.0f,  22.0f, 1.00f,  10,   16,   14,   20,    0,   10,    0,   24,   65,  0.10f },  // broad/strong
    // NEW (2026-06-19) — silhouettes the shoulder axis + shape dims now make distinct. Complete the
    // classic body-shape taxonomy (we already had Pear/Rectangle/Hourglass/TopHeavy/Apple/Round).
    { "Inverted Triangle", 4.0f, 0.0f, 0.95f, 10,  -14,  -16,  -12,   -4,   10,    2,   26,   18, -0.02f },  // broad shoulders, narrow lower (counterpart to Pear)
    { "Diamond",         4.0f,   8.0f, 1.00f,   4,   -6,   -6,   -8,   30,  -22,    6,  -14,    0,  0.06f },  // narrow shoulders, midsection-heavy, slim limbs
    { "Spoon",           4.0f,   4.0f, 1.00f,  -4,   20,   30,   22,    6,   18,    2,  -14,    0,  0.04f },  // pronounced pear: wide hip shelf + cinched waist
    { "MILF",            4.0f,  10.0f, 1.00f,  18,   14,   18,   12,   14,   20,    6,    2,    0,  0.08f },  // mature curvy-soft: full (slightly heavy) bust, soft low belly, kept waist
    { "Slim Thick",      4.0f,   2.0f, 1.00f,   6,   26,   18,   20,   -8,   35,    0,    2,   25,  0.02f },  // tiny cinched waist + thick toned lower
    { "Bottom Hourglass",4.0f,   4.0f, 1.00f,  10,   24,   26,   14,    0,   38,    0,   -2,    0,  0.04f },  // hourglass weighted LOW
    { "Top Hourglass",   4.0f,   4.0f, 1.00f,  28,    8,    6,    2,    0,   38,    2,    8,    0,  0.04f },  // hourglass weighted UP (cinched waist, unlike TopHeavy)
    { "Lollipop",        3.0f,  -8.0f, 0.92f,  34,   -8,   -8,   -8,   -4,   18,   -4,    0,    5,  0.02f },  // slim frame + large bust
    { "Strongwoman",     3.0f,  18.0f, 1.00f,   8,   16,   14,   18,   20,   -6,   18,   24,   60,  0.12f },  // heavy + broad + muscular (bruiser/powerlifter, soft over muscle)
};
const Archetype& GetArchetype(std::uint32_t seedBase) {
    std::mt19937 rng{ seedBase ^ 0x0A5C1759u };
    float total = 0.0f; for (const auto& a : kArchetypes) total += a.weight;
    float r = std::uniform_real_distribution<float>(0.0f, total)(rng);
    for (const auto& a : kArchetypes) { if (r < a.weight) return a; r -= a.weight; }
    return kArchetypes.front();
}
// Per-NPC, per-region jitter on the archetype delta (deterministic per seed+region, so it's
// consistent across every call for the same actor). Different salt from the volume noise so the
// two randoms don't correlate.
float DeltaJitter(std::uint32_t seedBase, const std::string& key) {
    const auto h = static_cast<std::uint32_t>(std::hash<std::string>{}(key));
    std::mt19937 rng{ seedBase ^ h ^ 0x0DE17A00u };
    return std::uniform_real_distribution<float>(-kDeltaJitter, kDeltaJitter)(rng);
}
float ArchetypeDelta(std::uint32_t seedBase, const std::string& key) {
    const Archetype& a = GetArchetype(seedBase);
    float d;
    if      (key == "breasts") d = a.dBreasts;
    else if (key == "butt")    d = a.dButt;
    else if (key == "hips")    d = a.dHips;
    else if (key == "thighs")  d = a.dThighs;
    else if (key == "belly")   d = a.dBelly;
    else if (key == "waist")   d = a.dWaist;
    else if (key == "arms")    d = a.dArms;
    else if (key == "shoulderwidth") d = a.dShoulders;
    else return 0.0f;
    return d + DeltaJitter(seedBase, key);
}
// Index of the chosen archetype (same deterministic roll as GetArchetype) — for the physics tier.
int GetArchetypeIndex(std::uint32_t seedBase) {
    std::mt19937 rng{ seedBase ^ 0x0A5C1759u };
    float total = 0.0f; for (const auto& a : kArchetypes) total += a.weight;
    float r = std::uniform_real_distribution<float>(0.0f, total)(rng);
    for (std::size_t i = 0; i < kArchetypes.size(); ++i) { if (r < kArchetypes[i].weight) return static_cast<int>(i); r -= kArchetypes[i].weight; }
    return 0;
}
// CBPC physics tier per archetype (SAME order as kArchetypes):
// 0 default / 1 toned (firm) / 2 lean (small) / 3 full (soft) / 4 heavy (max jiggle).
// ...InvTri,Diamond,Spoon,MILF,SlimThick,BottomHour,TopHour,Lollipop,Strongwoman
static const int kArchPhysTier[24] = { 0, 2, 2, 0, 0, 0, 3, 3, 4, 1, 1, 4, 1, 2, 1, 1, 3, 3, 3, 1, 0, 0, 2, 1 };
// Softness bias per archetype (jiggle character, independent of size): muscle firms (negative),
// fat softens (positive). Added to 0.5*frameScore in GetPhysicsPercent so physics CORRELATES with
// the real body. Same order as kArchetypes.
static const float kArchSoftBias[24] = { 7, -2, 0, 8, 6, 8, 30, 34, 50, -12, -2, 55, -6, -2, -8, -4, 28, 20, 22, 4, 8, 6, -2, 12 };

// Volume slider base (0-100): frameScore interpolation + per-part noise + traits.
// Factored out of GetMorphValue so the arm chain can derive forearm/wrist from "arms".
float TableVolume(std::uint32_t seedBase, float frameScore, const std::string& key,
                  bool unusualBody, bool hasActor) {
    float base = 0.0f;
    auto it = kMorphTable.find(key);
    if (it != kMorphTable.end()) {
        const auto& def = it->second;
        const float t = std::clamp(frameScore, 0.0f, 100.0f) / 100.0f;
        base = def.low + (def.high - def.low) * t;
        if (hasActor) {
            const auto morphHash = static_cast<std::uint32_t>(std::hash<std::string>{}(key));
            std::mt19937 noiseRng{ seedBase ^ morphHash };
            float indep = def.independence * kIndepScale;
            if (unusualBody) indep *= 2.5f;
            base += std::uniform_real_distribution<float>(-50.0f, 50.0f)(noiseRng) * indep;
        }
    }
    // Archetype gives the coherent shape; the light trait roll adds micro-variation on top.
    // Both are suppressed for the rare "unusual" outlier (its character is the extreme frame).
    const float td = unusualBody ? 0.0f : (ArchetypeDelta(seedBase, key) + TraitDelta(seedBase, key));
    return std::clamp(base + td, 0.0f, 100.0f);
}

// Soft knee above 1.0 (SKEE space): values stay linear up to the sculpted max (1.0),
// then compress smoothly toward an asymptote so a fantasy/unusual body is visibly bigger
// but NEVER stretches a vertex past where the slider mesh holds (no spikes / no breaks).
float SoftCap(float v) {
    constexpr float knee = 1.0f, ceiling = 1.30f, range = ceiling - knee;
    if (v <= knee) return v;
    return knee + range * (1.0f - std::exp(-(v - knee) / range));
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

    // Normal: center-weighted size, then shifted/scaled by the NPC's archetype (e.g. Petite
    // pulls the frame down, Obese/Amazon up). Different salt from GenerateWeight so it doesn't
    // mirror weight.
    std::mt19937 rng{ seedBase + 0xF00DC0FFu };
    const Archetype& arch = GetArchetype(seedBase);
    // _bias is the MCM "Bias" dial: a global heavier(+)/leaner(-) shift. Wired here so it affects
    // the procedural body size (modes 0/2); mode 1 picks it up via GetPresetWeight.
    const float fr = 50.0f + (CenterDraw(rng) - 50.0f) * arch.frameScale + arch.frameBias + _bias;
    return std::clamp(fr, 0.0f, 100.0f);
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
        const float breastTrait = unusualBody ? 0.0f
            : (ArchetypeDelta(seedBase, "breasts") + TraitDelta(seedBase, "breasts"));
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
        const ToneSet ts = ComputeTones(seedBase, a_frameScore, _athleticRatio,
                                        GetArchetype(seedBase).toneBias);

        // "More" sliders only for snu snu — deep-definition overflow above mid-tone,
        // per region so each region's depth tracks its own tone.
        if (key == "musclemoreabs_v2")  return ts.snusnu ? std::clamp(ts.core - 50.0f, 0.0f, 100.0f) * 0.90f : 0.0f;
        if (key == "musclemorearms_v2") return ts.snusnu ? std::clamp(ts.arms - 50.0f, 0.0f, 100.0f) * 0.70f : 0.0f;
        if (key == "musclemorelegs_v2") return ts.snusnu ? std::clamp(ts.legs - 50.0f, 0.0f, 100.0f) * 0.65f : 0.0f;

        if (key == "veramuscletones") return ts.overall * 0.90f;
        if (key == "muscleabs")       return ts.core * 0.85f;
        if (key == "musclearms")      return ts.arms * 0.65f;
        if (key == "musclelegs")      return ts.legs * 0.60f;
        return ts.overall * 0.50f;  // "Muscle" overall
    }

    // Forearm and wrist are DERIVED from the upper-arm value as fixed fractions, so the
    // limb ALWAYS tapers monotonically (arms > forearm > wrist). Independent per-part noise
    // used to invert the taper ~12% of the time → a lumpy "thin arm / fat forearm". Fractions
    // match the original slider tops (arms 50, forearm 36≈0.72, wrist 18≈0.36) and inherit the
    // arm's traits/noise, so the whole limb stays one smooth, coherent shape.
    if (key == "forearmsize" || key == "wristsize") {
        const float armsV = TableVolume(seedBase, a_frameScore, "arms", unusualBody, a_actor != nullptr);
        return (key == "forearmsize") ? armsV * 0.72f : armsV * 0.40f;
    }

    // ── New shape dimensions (2026-06-19): more expressible body types, every one DERIVED from a parent
    //    region or the archetype, so they stay coherent — no new independent random axis (no extra clones)
    //    and no spikes (volume ones go through GetVolumeMorph/SoftCap; shape ones apply *kDef <=1.0).
    auto region = [&](const char* k) {
        return TableVolume(seedBase, a_frameScore, k, unusualBody, a_actor != nullptr);
    };
    // Shoulders = the silhouette axis (archetype-driven), modestly frame-correlated.
    if (key == "shoulderwidth") {
        const float t = std::clamp(a_frameScore, 0.0f, 100.0f) / 100.0f;
        const float d = unusualBody ? 0.0f : ArchetypeDelta(seedBase, "shoulderwidth");
        return std::clamp(8.0f + 22.0f * t + d, 0.0f, 100.0f);
    }
    // Leg chain — derived from thighs so calf/outer/front-back ALWAYS track thigh thickness (a thick
    // thigh never sits on a stick calf). chubbylegs also reads belly (overall lower-body fullness).
    if (key == "calfsize")             return region("thighs") * 0.45f;
    if (key == "thighoutsidethicc_v2") return region("thighs") * 0.55f;
    if (key == "thighfbthicc_v2")      return region("thighs") * 0.45f;
    if (key == "chubbylegs")
        return std::clamp(0.45f * region("thighs") + 0.30f * region("belly") - 18.0f, 0.0f, 100.0f);
    // Butt shape: rounder with size + the butt-shape trait (heart/round/dimpled); firmer for toned women.
    if (key == "roundass")
        return std::clamp(region("butt") * 0.55f + (unusualBody ? 0.0f : TraitDelta(seedBase, "roundass")), 0.0f, 100.0f);
    if (key == "musclebutt") {
        const ToneSet ts = ComputeTones(seedBase, a_frameScore, _athleticRatio, GetArchetype(seedBase).toneBias);
        return ts.legs * 0.50f;
    }
    // Breast shape: cleavage/togetherness scale with size, cut by the "separated" trait (breastwidth);
    // smaller/perkier busts sit higher.
    if (key == "breaststogether" || key == "breastcleavage" || key == "breastheight") {
        const auto& bdef = kMorphTable.find("breasts")->second;
        const float t = std::clamp(a_frameScore, 0.0f, 100.0f) / 100.0f;
        const float bt = unusualBody ? 0.0f : (ArchetypeDelta(seedBase, "breasts") + TraitDelta(seedBase, "breasts"));
        const float breastSize = std::clamp(bdef.low + (bdef.high - bdef.low) * t + bt, 0.0f, 100.0f);
        const float width = region("breastwidth");   // the "separated" trait raises this
        if (key == "breaststogether") return std::clamp(breastSize * 0.45f - width * 0.5f, 0.0f, 100.0f);
        if (key == "breastcleavage")  return std::clamp(breastSize * 0.40f - width * 0.4f, 0.0f, 100.0f);
        return std::clamp((100.0f - breastSize) * 0.40f, 0.0f, 70.0f);   // breastheight: small/perky = higher set
    }
    // Belly/waist fullness: a real protruding belly only on heavy bodies; wider waistline on fuller frames.
    if (key == "bigbelly") return std::clamp((region("belly") - 45.0f) * 1.10f, 0.0f, 100.0f);
    if (key == "widewaistline" || key == "chubbywaist") {
        const float t = std::clamp(a_frameScore, 0.0f, 100.0f) / 100.0f;
        const float full = std::clamp(0.45f * region("belly") + 30.0f * t - 10.0f, 0.0f, 100.0f);
        return (key == "chubbywaist") ? full : full * 0.90f;
    }
    // Torso breadth/depth: broad shoulders -> broader ribcage/torso (keeps the upper body coherent).
    if (key == "bigtorso" || key == "chestwidth") {
        const float t = std::clamp(a_frameScore, 0.0f, 100.0f) / 100.0f;
        const float shld = unusualBody ? 0.0f : ArchetypeDelta(seedBase, "shoulderwidth");
        const float v = std::clamp(6.0f + 18.0f * t + 0.5f * shld, 0.0f, 100.0f);
        return (key == "chestwidth") ? v * 0.80f : v;
    }
    // Ribs show only on LEAN bodies (low frame + low belly) — never on soft/heavy.
    if (key == "ribsprominance")
        return std::clamp(40.0f - 0.40f * a_frameScore - 0.50f * region("belly"), 0.0f, 40.0f);
    // Hip projection tracks hip width (curvier hips push forward a little).
    if (key == "hipforward") return region("hips") * 0.30f;
    // Leg shape (classic curve) tracks thigh fullness — subtle, for silhouette variety.
    if (key == "legshapeclassic") return region("thighs") * 0.30f;

    // Volume base + traits (frameScore interpolation + per-part noise). Shape/trait-only
    // sliders have no table entry → base 0, value comes from traits.
    return TableVolume(seedBase, a_frameScore, key, unusualBody, a_actor != nullptr);
}

// Volume morph already combined with the per-NPC intensity and soft-capped (SKEE space).
// Papyrus uses this for the kVol (volume) sliders so fantasy/unusual bodies are bigger but
// never overshoot the sculpted vertex range. Definition sliders keep using GetMorphValue*kDef.
float WeightManager::GetVolumeMorph(RE::Actor* a_actor, float a_frameScore, std::string_view morphName) {
    const float raw = GetMorphValue(a_actor, a_frameScore, morphName)
                    * (GetActorIntensity(a_actor) / 100.0f);  // SKEE 0 .. ~2.1
    return SoftCap(raw);
}

// Overall muscle-tone score 0-100 (average of the three regions). Exposed for other mods'
// body classifiers; OBW itself only uses the regional tones inside GetMorphValue.
int WeightManager::GetToneScore(RE::Actor* a_actor) {
    if (!a_actor) return 0;
    std::scoped_lock lock(_mutex);
    const std::uint32_t seedBase = GetActorSeed(a_actor->GetFormID());
    const ToneSet ts = ComputeTones(seedBase, GetFrameScore(a_actor), _athleticRatio,
                                    GetArchetype(seedBase).toneBias);
    return static_cast<int>(std::lround(ts.overall));
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
    // Archetype lean: fuller types (Voluptuous/Obese/Amazon) read a bit bigger, Slim/Petite smaller.
    intensity = std::clamp(intensity + GetArchetype(GetActorSeed(id)).intensityBias, 0.55f, 2.6f);
    return intensity * _morphScale;
}

int WeightManager::GetPhysicsTier(RE::Actor* a_actor) {
    if (!a_actor) return 0;
    std::scoped_lock lock(_mutex);
    const int idx = GetArchetypeIndex(GetActorSeed(a_actor->GetFormID()));
    return (idx >= 0 && idx < static_cast<int>(kArchetypes.size())) ? kArchPhysTier[idx] : 0;
}

int WeightManager::GetArchetypeId(RE::Actor* a_actor) {
    if (!a_actor) return -1;
    std::scoped_lock lock(_mutex);
    return GetArchetypeIndex(GetActorSeed(a_actor->GetFormID()));
}

std::string WeightManager::GetArchetypeName(RE::Actor* a_actor) {
    if (!a_actor) return "";
    std::scoped_lock lock(_mutex);
    const int idx = GetArchetypeIndex(GetActorSeed(a_actor->GetFormID()));
    return (idx >= 0 && idx < static_cast<int>(kArchetypes.size())) ? kArchetypes[idx].name : "";
}

// Which mutually-exclusive option of a female trait category fired for this NPC (deterministic), or "".
// Public body API so other mods can read notable fine shapes (butt shape, breast shape) without re-rolling.
namespace { const char* FiredOptionName(std::size_t catIdx, std::uint32_t seed) {
    if (catIdx >= kTraitCategories.size()) return "";
    const auto& c = kTraitCategories[catIdx];
    std::mt19937 r{ seed ^ c.salt };
    if (std::uniform_real_distribution<float>(0.0f, 1.0f)(r) >= c.chance) return "";
    const int idx = std::uniform_int_distribution<int>(0, static_cast<int>(c.options.size()) - 1)(r);
    return c.options[idx].name;
} }
std::string WeightManager::GetButtShapeName(RE::Actor* a_actor) {     // category 4 = butt shape
    if (!a_actor) return "";
    std::scoped_lock lock(_mutex);
    return FiredOptionName(4, GetActorSeed(a_actor->GetFormID()));
}
std::string WeightManager::GetBreastShapeName(RE::Actor* a_actor) {   // category 5 = breast shape
    if (!a_actor) return "";
    std::scoped_lock lock(_mutex);
    return FiredOptionName(5, GetActorSeed(a_actor->GetFormID()));
}

int WeightManager::GetPhysicsPercent(RE::Actor* a_actor, int a_kind) {
    if (!a_actor) return 32;
    const float frame = GetFrameScore(a_actor);   // self-locks; call BEFORE locking below
    std::scoped_lock lock(_mutex);
    const int idx = GetArchetypeIndex(GetActorSeed(a_actor->GetFormID()));
    const float soft = (idx >= 0 && idx < static_cast<int>(kArchetypes.size())) ? kArchSoftBias[idx] : 0.0f;
    // bounce: size + softness; collision: more size-driven (a big body has big colliders even if firm)
    const float pct = (a_kind == 1) ? (0.60f * frame + 0.30f * soft + 2.0f)
                                    : (0.50f * frame + soft);
    return static_cast<int>(std::clamp(pct, 5.0f, 98.0f));
}

// ---------------------------------------------------------------------------
// Male procedural morphs (HIMBO). Two driving values per NPC: muscle and fat.
// ---------------------------------------------------------------------------

namespace {

// Male body archetypes (HIMBO) — the male counterpart to the female archetypes. Each biases the
// muscle/fat draws (the two build axes) and adds coherent shape deltas (V-taper, waist, belly, arms),
// so men come in distinct TYPES (lean, dadbod, bodybuilder, heavyset, bear...) instead of a smear.
// dWaist > 0 = thicker waist (less V); < 0 = tighter (more V-taper). Muscle/fat in 0-100 draw space.
struct MaleArchetype {
    const char* name; float weight;
    float muscle, fat;        // added to the per-NPC muscle/fat draws
    float dShoulders;         // torsoshoulderinc (broad/V-taper)
    float dWaist;             // torsowaistsize (- = tight V, + = thick)
    float dBelly;             // torsobelly
    float dArms;              // armsbiceps/delts/shoulders
    float toneBias;           // definition lift (abs/cuts)
    float intensityBias;
};
static const std::vector<MaleArchetype> kMaleArchetypes{
    //  name           wt   musc  fat  shld  waist belly arms tone  int
    { "Average",      12.f,  0.f,  0.f,   0,   0,    0,   0,   0,  0.00f },
    { "Lean",         10.f, -6.f, -8.f,   4,  -4,   -6,   0,   8, -0.02f },
    { "Fit",           9.f, 12.f, -8.f,  18, -12,   -8,   6,  14,  0.00f },  // athletic V-taper, abs
    { "Swimmer",       5.f,  6.f, -6.f,  22, -10,   -6,   2,  12, -0.02f },  // broad shoulders, lean
    { "Soldier",       7.f, 14.f,  2.f,  12,  -4,    0,   8,   6,  0.02f },  // solid all-around
    { "Dadbod",        8.f,  4.f, 18.f,   4,   8,   22,   2,  -4,  0.05f },  // soft belly + ok arms
    { "Heavyset",      6.f, -2.f, 28.f,   0,  12,   30,   0,  -8,  0.10f },  // overweight, soft
    { "Stocky",        5.f, 14.f, 18.f,   6,  10,   14,  10,   0,  0.08f },  // broad + thick (bear)
    { "Lanky",         7.f, -8.f, -6.f,  -6,  -2,   -4,  -6,   2, -0.04f },  // narrow, slight
    { "Twink",         4.f,-10.f, -4.f,  -4,  -2,   -6,  -8,   4, -0.05f },  // very slim, soft
    { "Bodybuilder",   3.f, 24.f, -6.f,  28, -14,   -6,  20,  18,  0.02f },  // rare, huge cut
    { "Powerlifter",   3.f, 22.f, 18.f,   8,   6,   12,  18,   6,  0.10f },  // rare, massive thick
};
// CBPC bounce % per male archetype (pec/belly jiggle): firm for muscular/lean, soft for fat. Same order.
// Avg,Lean,Fit,Swim,Soldier,Dadbod,Heavyset,Stocky,Lanky,Twink,Bodybuilder,Powerlifter
static const float kMaleArchSoftBias[12] = { 28, 14, 12, 14, 20, 60, 80, 50, 16, 22, 16, 45 };
const MaleArchetype& GetMaleArchetype(std::uint32_t seed) {
    std::mt19937 rng{ seed ^ 0x0A5C2B71u };
    float total = 0.0f; for (const auto& a : kMaleArchetypes) total += a.weight;
    float r = std::uniform_real_distribution<float>(0.0f, total)(rng);
    for (const auto& a : kMaleArchetypes) { if (r < a.weight) return a; r -= a.weight; }
    return kMaleArchetypes.front();
}
int GetMaleArchetypeIndex(std::uint32_t seed) {
    std::mt19937 rng{ seed ^ 0x0A5C2B71u };
    float total = 0.0f; for (const auto& a : kMaleArchetypes) total += a.weight;
    float r = std::uniform_real_distribution<float>(0.0f, total)(rng);
    for (std::size_t i = 0; i < kMaleArchetypes.size(); ++i) { if (r < kMaleArchetypes[i].weight) return static_cast<int>(i); r -= kMaleArchetypes[i].weight; }
    return 0;
}
float MaleArchetypeDelta(std::uint32_t seed, const std::string& key) {
    const MaleArchetype& a = GetMaleArchetype(seed);
    float d;
    if      (key == "torsoshoulderinc" || key == "armsshoulders" || key == "torsowidth") d = a.dShoulders;
    else if (key == "torsowaistsize")                                                    d = a.dWaist;
    else if (key == "torsobelly" || key == "torsobellylhandles")                         d = a.dBelly;
    else if (key == "armsbiceps" || key == "armsdelts" || key == "armstraps")            d = a.dArms;
    else return 0.0f;
    const auto h = static_cast<std::uint32_t>(std::hash<std::string>{}(key));
    std::mt19937 jr{ seed ^ h ^ 0x0DE28B00u };
    return d + std::uniform_real_distribution<float>(-kDeltaJitter * 0.6f, kDeltaJitter * 0.6f)(jr);
}

// Tighter soft-knee for HIMBO: it distorts above ~1.0 (much tighter than CBBE's 1.30 head-room),
// so male volume asymptotes to ~1.12. Keeps a big bodybuilder visibly bigger but never breaks the mesh.
float SoftCapMale(float v) {
    constexpr float knee = 0.95f, ceiling = 1.12f, range = ceiling - knee;
    if (v <= knee) return v;
    return knee + range * (1.0f - std::exp(-(v - knee) / range));
}

// Muscle level 0-100. Widened (2026-06-19 male diff pass) so men span lean->huge (was capped ~55 = all
// small). Big values reach ~1.0 SKEE and SoftCapMale protects the HIMBO mesh past that.
float MaleMuscle(std::uint32_t seed) {
    std::mt19937 rng{ seed ^ 0x4D5C1E00u };
    const float r = std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
    if (r < 0.42f) return std::uniform_real_distribution<float>(0.0f, 18.0f)(rng);
    if (r < 0.74f) return std::uniform_real_distribution<float>(18.0f, 42.0f)(rng);
    return std::uniform_real_distribution<float>(42.0f, 68.0f)(rng);
}

// Fat level 0-100: skewed low but wider top so heavy men exist (Dadbod/Heavyset).
float MaleFat(std::uint32_t seed) {
    std::mt19937 rng{ seed ^ 0x7A700000u };
    const float r = std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
    if (r < 0.58f) return std::uniform_real_distribution<float>(0.0f, 18.0f)(rng);
    if (r < 0.85f) return std::uniform_real_distribution<float>(18.0f, 34.0f)(rng);
    return std::uniform_real_distribution<float>(34.0f, 55.0f)(rng);
}

// Per-region independence for males (the anti-clone lever the male side lacked — females have it via
// kMorphTable). Moderate so per-NPC regions decorrelate (pecs vs arms vs legs) WITHOUT breaking the
// buildScale proportion lock. Only volume sliders; shape/def stay tied to tone/M-F. Fraction of [0,100].
float MaleIndep(std::string_view key) {
    if (key == "muscle" || key == "bodymass" || key == "torsomass")                                   return 0.13f;
    if (key == "pecssize" || key == "pecsmass" || key == "pecswidth")                                  return 0.15f;
    if (key == "armsbiceps" || key == "armsdelts" || key == "armsshoulders" ||
        key == "armstraps" || key == "armsfore")                                                       return 0.13f;
    if (key == "chubby" || key == "torsobelly" || key == "torsobellylhandles")                         return 0.17f;
    if (key == "legssize" || key == "legsthigh" || key == "legscalfsize")                              return 0.13f;
    if (key == "buttbooty" || key == "buttroundy" || key == "torsobacksize")                           return 0.15f;
    return 0.0f;
}

// HIMBO sliders derived from muscle (M) and fat (F). Lowercase keys (Papyrus lowercases).
// Only the base (build) sliders here; shape sliders come from male traits.
float MaleSlider(std::string_view key, float M, float F) {
    if (key == "muscle")                                return M;
    if (key == "armsbiceps" || key == "armsdelts")      return M * 0.85f;
    if (key == "armsshoulders")                         return M * 0.60f;   // delts/shoulder cap
    if (key == "armstraps"   || key == "armstrapsmeat") return M * 0.50f;
    if (key == "armsfore"    || key == "armsbrachio")   return M * 0.55f;   // forearm chain (tracks biceps)
    if (key == "pecssize"   || key == "pecsmass")       return M * 0.70f + (M + F) * 0.05f;
    if (key == "pecswidth")                             return M * 0.50f;
    if (key == "chubby"     || key == "legschubby")     return F;
    if (key == "torsobelly" || key == "torsobellychub") return F * 0.90f;
    if (key == "torsobellylhandles")                    return F * 0.70f;   // love handles (fat)
    if (key == "bodymass"   || key == "torsomass")      return M * 0.42f + F * 0.48f;
    if (key == "torsobacksize")                         return M * 0.50f + F * 0.20f;  // lats/back bulk
    if (key == "buttbooty")                             return 18.0f + M * 0.22f + F * 0.26f;
    if (key == "buttroundy")                            return (18.0f + M * 0.22f + F * 0.26f) * 0.55f;
    if (key == "legssize")                              return M * 0.35f + F * 0.40f;
    if (key == "legsthigh")                             return M * 0.35f + F * 0.42f;
    if (key == "legscalfsize")                          return M * 0.30f + F * 0.30f;   // calf tracks leg mass
    if (key == "lean")                                  return std::clamp(75.0f - M * 0.6f - F * 0.9f, 0.0f, 100.0f);

    // Muscle TONE = definition (cut/ripped vs soft). Emerges from the distribution:
    // muscle present + low fat → abs/ribs/V-cut show; high fat smooths them away.
    const float tone = std::clamp(M - F * 1.1f + 5.0f, 0.0f, 100.0f);
    if (key == "torsoflatabs")         return tone * 0.85f;
    if (key == "torsovline")           return tone * 0.60f;
    if (key == "torsoribsdefinition")  return tone * 0.50f;
    if (key == "armstrapsvalleys")     return tone * 0.45f;
    if (key == "torsobackdefinition")  return tone * 0.45f;   // back cut
    if (key == "torsobackshape")       return tone * 0.40f;

    return 0.0f;  // pure trait/archetype sliders (pecsflatten, vtaper, torsoshoulderinc...) start at 0
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
            M = std::uniform_real_distribution<float>(50.0f, 64.0f)(r);
        else
            F = std::uniform_real_distribution<float>(42.0f, 54.0f)(r);
    } else {
        // Archetype biases the two build axes (coherent: every part derives from M/F) — gives
        // distinct male TYPES. Plus a per-NPC build jitter so men WITHIN one archetype aren't clones.
        // Capped so SKEE stays under HIMBO's distortion threshold (SoftCapMale).
        const MaleArchetype& ma = GetMaleArchetype(seedBase);
        std::mt19937 jr{ seedBase ^ 0x0B17DE00u };
        M = std::clamp(M + ma.muscle + std::uniform_real_distribution<float>(-6.0f, 6.0f)(jr), 0.0f, 88.0f);
        F = std::clamp(F + ma.fat    + std::uniform_real_distribution<float>(-6.0f, 6.0f)(jr), 0.0f, 78.0f);
    }

    float val = MaleSlider(key, M, F);

    // Traits + archetype shape deltas — suppressed on unusual bodies (they replace the set, like females).
    if (uv < 0) val += MaleTraitDelta(seedBase, key) + MaleArchetypeDelta(seedBase, key);

    // Proportionality "amarra": ONE per-NPC build scale (multiplicative → every part scales together,
    // staying proportional) THEN a per-region independence noise (the anti-clone lever) so two men with
    // the same build still differ region to region (pecs vs arms vs legs) — moderate, so proportions hold.
    std::mt19937 br{ seedBase ^ 0x0B01D000u };
    const float buildScale = std::uniform_real_distribution<float>(0.92f, 1.08f)(br);
    const auto h = static_cast<std::uint32_t>(std::hash<std::string>{}(key));
    std::mt19937 nr{ seedBase ^ h ^ 0x0A1Eu };
    float indep = MaleIndep(key) * kIndepScale;
    if (uv >= 0) indep *= 2.0f;   // unusual: more decorrelation (mirrors the female outlier boost)
    val = val * buildScale + std::uniform_real_distribution<float>(-50.0f, 50.0f)(nr) * indep;

    // Player-tunable male build (MCM "Male build"): scales the whole male body uniformly,
    // so proportions are preserved at any setting.
    val *= _maleBuild;

    return std::clamp(val, 0.0f, 100.0f);
}

float WeightManager::GetMaleIntensity(RE::Actor* a_actor) {
    if (!a_actor) return _morphScale;
    std::scoped_lock lock(_mutex);

    const RE::FormID id = a_actor->GetFormID();
    std::mt19937 rng{ GetActorSeed(id) ^ 0x4A1E0000u };

    // Multipliers kept LOW — HIMBO volume distorts above ~1.0 (much tighter than CBBE).
    const int uv = MaleUnusualVariant(id);
    if (uv == 1) return std::uniform_real_distribution<float>(1.08f, 1.25f)(rng) * _morphScale; // ultra-huge
    if (uv == 0) return std::uniform_real_distribution<float>(0.65f, 0.85f)(rng) * _morphScale; // ultra-skinny

    const float roll = std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
    float intensity = (roll < _fantasyRatio)
        ? std::uniform_real_distribution<float>(0.98f, 1.15f)(rng)  // fantasy: bodybuilder
        : std::uniform_real_distribution<float>(0.72f, 0.92f)(rng);
    intensity = std::clamp(intensity + GetMaleArchetype(GetActorSeed(id)).intensityBias, 0.55f, 1.30f);
    return intensity * _morphScale;
}

// Male volume morph already combined with intensity and SOFT-CAPPED for HIMBO (tighter ceiling than CBBE).
// Papyrus uses this for the male kVol sliders so a bodybuilder is bigger but never breaks the HIMBO mesh.
float WeightManager::GetMaleVolumeMorph(RE::Actor* a_actor, std::string_view morphName) {
    const float raw = GetMaleMorphValue(a_actor, morphName) * (GetMaleIntensity(a_actor) / 100.0f);
    return SoftCapMale(raw);
}

int WeightManager::GetMalePhysicsPercent(RE::Actor* a_actor, int a_kind) {
    if (!a_actor) return 20;
    std::scoped_lock lock(_mutex);
    const int idx = GetMaleArchetypeIndex(GetActorSeed(a_actor->GetFormID()));
    const float soft = (idx >= 0 && idx < static_cast<int>(kMaleArchetypes.size())) ? kMaleArchSoftBias[idx] : 20.0f;
    // collision a touch more size-driven; bounce is the archetype softness directly.
    const float pct = (a_kind == 1) ? (0.75f * soft + 8.0f) : soft;
    return static_cast<int>(std::clamp(pct, 5.0f, 95.0f));
}

int WeightManager::GetMaleArchetypeId(RE::Actor* a_actor) {
    if (!a_actor) return -1;
    std::scoped_lock lock(_mutex);
    return GetMaleArchetypeIndex(GetActorSeed(a_actor->GetFormID()));
}

std::string WeightManager::GetMaleArchetypeName(RE::Actor* a_actor) {
    if (!a_actor) return "";
    std::scoped_lock lock(_mutex);
    const int idx = GetMaleArchetypeIndex(GetActorSeed(a_actor->GetFormID()));
    return (idx >= 0 && idx < static_cast<int>(kMaleArchetypes.size())) ? kMaleArchetypes[idx].name : "";
}

void WeightManager::QueueForMorphs(RE::Actor* a_actor) {
    if (!a_actor) return;
    std::scoped_lock lock(_mutex);
    const RE::FormID id = a_actor->GetFormID();
    // Dedup against the LIVE queue only (NOT _processed): OBody re-fires on every cell crossing and we
    // must re-process then to clear its re-applied preset, so a processed actor CAN be re-queued by the
    // OBody / reprocess / re-roll paths. This guard only stops the same actor sitting in the queue twice
    // (e.g. OBody's enqueue + the procedural fallback's enqueue racing for a deferred, far-away NPC).
    if (std::find(_morphQueue.begin(), _morphQueue.end(), id) != _morphQueue.end()) return;
    _morphQueue.push_back(id);
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
    _processed.insert(bestId);   // "OBW handled this actor this session" — the procedural fallback skips it
    _fallbackWatch.erase(bestId);
    return best;
}

// Independent procedural distribution (so it works with an empty preset library / for OBody-skipped NPCs).
// The actor-load sink calls this when an NPC loads in a procedural mode; we hold it for a short grace so
// OBody (if it has presets) gets first crack via its own OnActorGenerated path.
void WeightManager::WatchForFallback(RE::FormID a_id) {
    if (!a_id) return;
    std::scoped_lock lock(_mutex);
    // Watch on EVERY 3D load (no _processed early-out): OBody re-distributes its preset on each cell
    // crossing, so OBW must RE-ASSERT each load to keep priority (it loses to OBody otherwise). The grace
    // window still lets OBody apply first; OBW's re-apply (which clears OBody's key + rebuilds) lands after.
    _fallbackWatch.try_emplace(a_id, kFallbackGraceTicks);
}

// Polled from Papyrus (OBW_Quest.OnUpdate). Counts down each watched NPC's grace; when it expires, if
// OBody never handled it (not processed) and it is a real, loaded, non-excluded NPC, we self-distribute
// (procedural). Returns how many were enqueued so the caller can arm the drain.
int WeightManager::SweepFallback() {
    std::scoped_lock lock(_mutex);
    // Preset mode relies entirely on OBody's distribution — no self-distribution there.
    if (_bodyMode == BodyMode::kOBodyPreset) { _fallbackWatch.clear(); return 0; }
    int enqueued = 0;
    for (auto it = _fallbackWatch.begin(); it != _fallbackWatch.end();) {
        if (--(it->second) > 0) { ++it; continue; }   // still within the OBody grace window
        const RE::FormID id = it->first;
        // Re-apply on EVERY load (no _processed gate) so OBW overrides OBody's re-distribution: the apply
        // (ApplyMorphs) itself clears OBody's morph key + rebuilds, so re-running it after OBody's re-fire
        // puts OBW back on top. The live-queue dedup in QueueForMorphs stops double-queuing within a drain.
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
        if (actor && actor->Is3DLoaded() && !actor->IsDead() && !Config::IsActorExcluded(actor)) {
            QueueForMorphs(actor);
            ++enqueued;
        }
        it = _fallbackWatch.erase(it);
    }
    return enqueued;
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

int WeightManager::ReprocessAllLoaded() {
    auto* pl = RE::ProcessLists::GetSingleton();
    auto* pc = RE::PlayerCharacter::GetSingleton();
    if (!pl) return 0;
    std::scoped_lock lock(_mutex);   // recursive
    // RANDOM mode -> reprocess RE-ROLLS each body (fresh per-actor seed); SEEDED -> keep the seed (re-apply
    // the same body, only picking up changed settings/physics). Matches the mode's intent.
    const bool reroll = (_mode == WeightMode::kRandom);
    int n = 0;
    for (auto& h : pl->highActorHandles) {
        auto ptr = h.get();
        RE::Actor* a = ptr.get();
        if (!a || a == pc || a->IsDead() || a->IsDisabled() || !a->Is3DLoaded()) continue;
        if (!a->GetActorBase()) continue;                 // sex/body-mode filtered later in ApplyMorphs
        const RE::FormID id = a->GetFormID();
        if (reroll) {                                     // new random body this press (distinct per actor)
            const std::uint32_t ns = OBW::CollectEntropy() ^ id;
            _overrideSeed[id] = ns;
            if (auto* base = a->GetActorBase()) _overrideSeed[base->GetFormID()] = ns;  // weight, next load
        }
        _processed.erase(id);                             // allow re-apply (current logic + physics)
        if (std::find(_morphQueue.begin(), _morphQueue.end(), id) == _morphQueue.end())
            _morphQueue.push_back(id);
        ++n;
    }
    SKSE::log::info("OBW: reprocess all loaded -> {} actors queued ({})", n, reroll ? "re-rolled" : "same seed");
    return n;
}

void WeightManager::RegenerateSeed() {
    std::scoped_lock lock(_mutex);
    // Option B: a new seed must only affect NPCs OBW hasn't generated yet. Pin every
    // already-processed NPC (and its base, which drives the weight) to its CURRENT effective
    // seed BEFORE changing the global one, so its body + archetype stay identical even across
    // reloads (_overrideSeed is serialized). try_emplace preserves any explicit per-actor
    // re-roll already set by the hotkey. Only NOT-yet-seen NPCs pick up the new seed.
    for (RE::FormID id : _processed) {
        _overrideSeed.try_emplace(id, GetActorSeed(id));
        if (auto* a = RE::TESForm::LookupByID<RE::Actor>(id)) {
            if (auto* base = a->GetActorBase())
                _overrideSeed.try_emplace(base->GetFormID(), GetActorSeed(base->GetFormID()));
        }
    }
    _seed = OBW::CollectEntropy();
    SKSE::log::info("OBW: seed regenerated → {} (pinned {} already-seen NPCs)", _seed, _processed.size());
}

// ---------------------------------------------------------------------------
// Cosave
// ---------------------------------------------------------------------------

void WeightManager::Save(SKSE::SerializationInterface* a_intf) {
    // Seed
    if (a_intf->OpenRecord(kRecordSeed, kRecordVer))
        a_intf->WriteRecordData(_seed);

    // Configuration (mode + bias)
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

    // Preset-orientation strength (body mode 2)
    if (a_intf->OpenRecord(kRecordOri, kRecordVer))
        a_intf->WriteRecordData(_presetOrient);

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

    // Female-bodies master toggle
    if (a_intf->OpenRecord(kRecordFem, kRecordVer))
        a_intf->WriteRecordData(_femaleBodies);

    // Male-bodies master toggle
    if (a_intf->OpenRecord(kRecordMale, kRecordVer))
        a_intf->WriteRecordData(_maleBodies);

    // Male build multiplier
    if (a_intf->OpenRecord(kRecordMBld, kRecordVer))
        a_intf->WriteRecordData(_maleBuild);

    // Debug-logging toggle
    if (a_intf->OpenRecord(kRecordDbg, kRecordVer))
        a_intf->WriteRecordData(_debugLog);

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
            _mode = (mode >= 2) ? WeightMode::kSeeded : static_cast<WeightMode>(mode);  // migrate old NPC Default(2)
            a_intf->ReadRecordData(_bias);
        } else if (type == kRecordBody) {
            std::uint8_t bm{};
            a_intf->ReadRecordData(bm);
            _bodyMode = static_cast<BodyMode>(bm);
        } else if (type == kRecordScl) {
            a_intf->ReadRecordData(_morphScale);
        } else if (type == kRecordOri) {
            a_intf->ReadRecordData(_presetOrient);
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
        } else if (type == kRecordFem) {
            a_intf->ReadRecordData(_femaleBodies);
        } else if (type == kRecordMale) {
            a_intf->ReadRecordData(_maleBodies);
        } else if (type == kRecordMBld) {
            a_intf->ReadRecordData(_maleBuild);
        } else if (type == kRecordDbg) {
            a_intf->ReadRecordData(_debugLog);
            g_debugLog = _debugLog;
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
    _presetOrient = Config::g_defaultPresetOrient;
    _fantasyRatio = Config::g_defaultFantasyRatio;
    _unusualRatio = Config::g_defaultUnusualRatio;
    _breastUnusualRatio = Config::g_defaultBreastUnusualRatio;
    _athleticRatio = Config::g_defaultAthleticRatio;
    _femaleBodies = Config::g_defaultFemaleBodies;
    _maleBodies   = Config::g_defaultMaleBodies;
    _maleBuild    = Config::g_defaultMaleBuild;
    _reRollKey = Config::g_defaultReRollKey;
    _debugLog     = Config::g_defaultDebugLog;
    g_debugLog    = _debugLog;
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
