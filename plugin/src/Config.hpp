#pragma once

#include <string>
#include <unordered_set>
#include <vector>

namespace RE { class Actor; }

namespace OBW::Config {

// Default values read from Data/SKSE/Plugins/OBodyNGWeight.ini at plugin load.
// These seed a new game (and Revert); existing saves restore from the cosave instead.
// A FOMOD installs one of several INI variants (Realistic / Balanced / Fantasy).
inline float g_defaultMorphScale   = 1.0f;
inline float g_defaultPresetOrient = 0.5f;   // body mode 2 blend strength (0 = pure procedural, 1 = pure preset)
inline float g_defaultFantasyRatio = 0.15f;
inline float g_defaultUnusualRatio       = 0.06f;
inline float g_defaultBreastUnusualRatio = 0.06f;
inline float g_defaultAthleticRatio      = 0.15f;
inline int   g_defaultReRollKey          = 26;  // [ / { key
// Female bodies: when false, OBW leaves female NPCs entirely alone (no morphs) — OBody /
// vanilla handle them. Toggleable in the MCM.
inline bool  g_defaultFemaleBodies       = true;
// Male bodies: when false, OBW leaves male NPCs entirely alone (no weight, no morphs) —
// OBody / vanilla handle them. Toggleable in the MCM.
inline bool  g_defaultMaleBodies         = true;
// Male build multiplier (1.0 = default). Scales the whole male body uniformly.
inline float g_defaultMaleBuild          = 1.0f;
// Debug logging (verbose per-NPC/per-event diagnostics). Off by default; MCM-toggleable.
inline bool  g_defaultDebugLog           = false;
// Neck-seam COLOR fix: blend the head's facegen skin TINT toward the body skin tone (bodyTintColor) by this
// strength (0 = off, 1 = head tint forced to the body tone). Reduces a head<->body tone mismatch at the neck
// (a runtime tint pull, not the baked texture - so it lessens, not always erases, the seam). MCM-tunable.
inline float g_defaultNeckColorFix       = 0.5f;

// Parse the INI. Call once in SKSEPluginLoad, before WeightManager is constructed.
void Load();

// Plugins (lowercased) whose NPCs OBW leaves untouched. Two effective sources:
//  - g_mcmExcluded : managed by the MCM checkboxes, saved to OBodyNGWeight_Exclusions_MCM.txt (global).
//  - g_fileExcluded: any other OBodyNGWeight_Exclusions*.txt (hand-edited / patches), read-only.
inline std::unordered_set<std::string> g_mcmExcluded;
inline std::unordered_set<std::string> g_fileExcluded;

// (Re)load both sets from disk. Call once at load.
void LoadExclusions();

// True if the actor's base (or reference) originates from / is overridden by an excluded plugin.
bool IsActorExcluded(RE::Actor* a_actor);

// MCM helpers: checkbox state, toggle (rewrites the MCM file), and the list of NPC-adding plugins.
bool IsPluginExcluded(const char* a_plugin);
void SetPluginExcluded(const char* a_plugin, bool a_on);
std::vector<std::string> GetNpcPlugins();

}  // namespace OBW::Config
