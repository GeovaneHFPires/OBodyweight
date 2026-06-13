Scriptname OBW_Native Hidden

; Returns the weight that would be generated for the actor (0-100), without applying it.
float Function GetWeight(Actor akActor) global native

; Applies the generated weight to the actor's ActorBase and refreshes the 3D geometry.
Function ApplyGeneratedWeight(Actor akActor) global native

; Distribution mode: 0=Random, 1=Seeded/Deterministic, 2=NpcDefault
int Function GetMode() global native
Function SetMode(int aiMode) global native

; Bias added to the generated weight (can be negative); the result is clamped to [0,100].
float Function GetBias() global native
Function SetBias(float afBias) global native

; Seed used in Seeded mode (per playthrough).
int Function GetSeed() global native
Function RegenerateSeed() global native

; Morph queue — C++ owns the queue; Papyrus only asks for the next actor.
Function QueueForMorphs(Actor akActor) global native
Actor Function GetNextMorphActor() global native
; True if the queue still has actors waiting (used for throttled/lazy draining).
bool Function HasMorphsPending() global native

; Body shape mode: 0 = Procedural NiOverride morphs, 1 = OBody Presets (weight only)
int Function GetBodyMode() global native
Function SetBodyMode(int aiMode) global native

; Global morph intensity multiplier (1.0 = default). Adjustable in the MCM.
float Function GetMorphScale() global native
Function SetMorphScale(float afScale) global native

; Fraction of "fantasy" (exaggerated) NPCs, 0.0-1.0. Adjustable in the MCM.
float Function GetFantasyRatio() global native
Function SetFantasyRatio(float afRatio) global native

; Fraction of NPCs with an "unusual body" (out of distribution: ultra-petite +
; disproportionate), 0.0-1.0. Adjustable in the MCM.
float Function GetUnusualRatio() global native
Function SetUnusualRatio(float afRatio) global native

; Fraction of NPCs with "unusual breasts" (extreme sag or extreme perk), 0.0-1.0.
float Function GetBreastUnusualRatio() global native
Function SetBreastUnusualRatio(float afRatio) global native

; Fraction of athletic FEMALES (visible muscle tone/definition), 0.0-1.0.
float Function GetAthleticRatio() global native
Function SetAthleticRatio(float afRatio) global native

; Re-roll key (DirectInput scancode). Default 26 = the [ / { key. Bindable in the MCM.
int Function GetReRollKey() global native
Function SetReRollKey(int aiKey) global native

; Master toggle for the male-body feature (weight + morphs). Off = OBW ignores men.
bool Function GetMaleBodies() global native
Function SetMaleBodies(bool abOn) global native

; Effective per-NPC intensity: realistic (~1.0) or fantasy (~1.3-2.2) x the global scale.
; Call ONCE per NPC and apply it to every slider.
float Function GetActorIntensity(Actor akActor) global native

; Procedural morph generation (no preset files needed).
; Call GetFrameScore ONCE per actor, then pass that T to each GetMorphValue call.
; This ensures all body parts are correlated (same frame score → proportional shape).
float Function GetFrameScore(Actor akActor) global native
float Function GetMorphValue(Actor akActor, float afFrameScore, string morphName) global native

; Male morphs (HIMBO). Derived from build (muscle+fat) + traits + unusual.
float Function GetMaleMorphValue(Actor akActor, string morphName) global native
; Per-NPC male intensity (realistic/fantasy/unusual). For volume sliders.
float Function GetMaleIntensity(Actor akActor) global native

; Muscle tone 0-100 (same value that drives the MuscleAbs/Arms/Legs sliders).
int Function GetToneScore(Actor akActor) global native

; Regenerates weight and morphs for a specific actor (used by the re-roll hotkey).
; Removes it from the processed set, re-rolls the weight, and queues it for morphs.
Function RegenerateActor(Actor akActor) global native
Function MarkMorphsApplied(Actor akActor) global native
bool Function HasMorphsApplied(Actor akActor) global native
