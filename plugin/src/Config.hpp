#pragma once

namespace OBW::Config {

// Default values read from Data/SKSE/Plugins/OBodyNGWeight.ini at plugin load.
// These seed a new game (and Revert); existing saves restore from the cosave instead.
// A FOMOD installs one of several INI variants (Realistic / Balanced / Fantasy).
inline float g_defaultMorphScale   = 1.0f;
inline float g_defaultFantasyRatio = 0.15f;
inline float g_defaultUnusualRatio       = 0.06f;
inline float g_defaultBreastUnusualRatio = 0.06f;
inline float g_defaultAthleticRatio      = 0.15f;
inline int   g_defaultReRollKey          = 26;  // [ / { key
// Male bodies: when false, OBW leaves male NPCs entirely alone (no weight, no morphs) —
// OBody / vanilla handle them. Toggleable in the MCM.
inline bool  g_defaultMaleBodies         = true;

// Parse the INI. Call once in SKSEPluginLoad, before WeightManager is constructed.
void Load();

}  // namespace OBW::Config
