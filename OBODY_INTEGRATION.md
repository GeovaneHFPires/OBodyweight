# OBW ↔ OBody NG integration — future migration plan

**Status: PLANNED (not started). 2026-06-23.** Current shipping behavior = the "re-assert" fix (OBW
re-applies its body each actor load to beat OBody's re-distribution; competes but works). This doc is the
plan to replace that with real integration via OBody NG's C++ plugin API.

## Why
OBW and OBody currently COMPETE (both write SKEE morphs; OBody re-fires every cell crossing; OBW re-asserts
to win — see `WeightManager::WatchForFallback`/`SweepFallback`, the `_processed` gates removed). Cleaner =
OBW reacts to / feeds OBody so there's ONE system.

## OBody NG has a real C++ plugin API (verified from source)
Repo: https://github.com/Aietos/OBody-NG (cloned at `C:\MO2\OBody-NG`). Header `include/API/API.h` is meant
to be copied wholesale into the consumer. Flow:
- At `kPostPostLoad`, dispatch `RequestPluginInterface` (with an `IOBodyReadinessEventListener`) to "OBody"
  → OBody writes an `IPluginInterface*` back. Use it only between `OBodyIsReady`/`OBodyIsNoLongerReady`.
- Key methods: `RegisterEventListener(IActorChangeEventListener&)`, `RemoveOBodyMorphsFromActor`,
  `ApplyOBodyMorphsToActor`, `ActorIsProcessed`, `GetPresetAssignedToActor`, `AssignPresetToActor`.
- **Event** `IActorChangeEventListener::OnActorGenerated` fires just after OBody assigns+applies/queues a
  preset to an actor (INCLUDING re-fires on cell crossings). Payload has `responsiblePluginInterface`
  (null if OBody did it; `"_Papyrus"` for OBody's Papyrus path; YOUR interface if you caused it) → use it
  to skip your own changes = NO LOOP.

## Phase 1 — OBW-side only, NO OBody change (do this first; replaces the re-assert)
Register an `IActorChangeEventListener`. On `OnActorGenerated` where `responsiblePluginInterface` is NOT
OBW → `RemoveOBodyMorphsFromActor(actor)` + apply OBW's body. Event-driven (no polling, no grace, no
`_processed`), loop-safe, reacts exactly to OBody's apply/re-fire. Retire `TESObjectLoadedEvent` fallback +
`SweepFallback`. Copy `API.h` into `plugin/src/`, request the interface in `main.cpp` at kPostPostLoad.

## Phase 2 — OBody PR: make OBody NOT name-centric (custom-VALUE presets)
So OBody can DISTRIBUTE + RE-FIRE OBW's continuous per-NPC body (OBody truly owns it). Additive + ABI-safe;
named presets untouched (zero regression — the PR selling point).

**Design:**
- `PresetManager`: a custom-preset path. OBody's `Preset` = name + `SliderSet` (slider→value); OBW's body
  already IS a SliderSet. Re-fire resolves actor→`presetIndex` (ActorTracker) → DB preset by INDEX, so a
  custom preset must live in the indexed DB (`allFemalePresets`/`allMalePresets` + the index maps +
  `nextFemalePresetIndex`, see `PresetContainer::AssignPresetIndexes`).
- New API (append to `IPluginInterface`, bump `PluginAPIVersion` v1→v2, ABI-safe append-only):
  `bool AssignCustomPresetToActor(Actor*, AssignCustomPresetPayload&)` where payload =
  `{ const char* name; BodyType body; const {name,value}* sliders; size_t count; uint64_t flags; }`.
  Impl: build a `Preset` → register into the indexed DB (mirror AssignPresetIndexes for one entry) →
  reuse `AssignPresetToActor`/`GenerateBodyByPreset` (index path) → applies + tracks + re-fires.
- **Complexity / open issue — save-stability:** runtime presets get SESSION indices (unstable across DB
  changes, since `presetIndex` is serialized). So: custom presets are **session-only**; the consumer (OBW)
  RE-REGISTERS + re-assigns on each load (the actor→index assignment is OBody-serialized, but the values
  aren't). In-session OBody owns re-fires; cross-save OBW re-establishes. (For SAVE-STABLE ownership you'd
  need name-based actor tracking + serialization of custom presets — bigger change to `ActorTracker`.)
- Continuous per-NPC ⇒ session custom presets (this plan). Finite archetypes ⇒ stable indices (simpler but
  loses OBW's continuous variation — rejected).

**Key files (OBody NG, `C:\MO2\OBody-NG`):**
- `include/API/API.h` — `PluginAPIVersion` enum (line ~88: `{Invalid=0, v1=1, Latest=v1}`), `IPluginInterface`,
  `IActorChangeEventListener` (OnActorGenerated/OnActorPresetChangedWithoutGeneration), payload structs.
- `include/PresetManager/PresetManager.h` — `Slider`/`SliderSet`/`Preset`/`PresetContainer`/`AssignedPresetIndex`;
  `GetPresetByName`/`GetPresetByNameForRandom`/`AssignPresetIndexes`.
- `src/PresetManager/PresetManager.cpp` — `GetPresetByName` (~181), `GetPresetByNameForRandom` (~199),
  `AssignPresetIndexes`.
- `src/API/PluginInterface.cpp` — `AssignPresetToActor` (~140, the index-tracking pattern to mirror).
- `src/Body/Body.cpp` — `GenerateActorBody` (~161, the distribution/re-fire), `ApplyMorphs` (~36),
  re-fire resolves `ActorTracker::GetPresetNameForActor` (~392).
- `src/Papyrus/PapyrusBody.cpp` — Papyrus binds (optionally expose a Papyrus `AssignCustomPreset` too).

**Then PR** to Aietos/OBody-NG (additive, ABI-safe, named presets intact). Afterward OBW switches from
Phase-1 event-react to feeding OBody the custom preset (Phase-2 ownership).
