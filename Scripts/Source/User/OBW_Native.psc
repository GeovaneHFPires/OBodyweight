Scriptname OBW_Native Hidden

; Retorna o peso que seria gerado para o actor (0-100), sem aplicar
float Function GetWeight(Actor akActor) global native

; Aplica o peso gerado ao ActorBase do actor e atualiza a geometria 3D
Function ApplyGeneratedWeight(Actor akActor) global native

; Modo de distribuição: 0=Random, 1=Seeded/Determinístico, 2=NpcDefault
int Function GetMode() global native
Function SetMode(int aiMode) global native

; Bias somado ao peso gerado (pode ser negativo); resultado é clampado em [0,100]
float Function GetBias() global native
Function SetBias(float afBias) global native

; Seed usada no modo Seeded (por partida)
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

; Multiplicador global de intensidade dos morphs (1.0 = padrao). Ajustavel no MCM.
float Function GetMorphScale() global native
Function SetMorphScale(float afScale) global native

; Proporcao de NPCs "fantasia" (exagerados), 0.0-1.0. Ajustavel no MCM.
float Function GetFantasyRatio() global native
Function SetFantasyRatio(float afRatio) global native

; Proporcao de NPCs com "unusual body" (fora da distribuicao: ultra-petite +
; desproporcional), 0.0-1.0. Ajustavel no MCM.
float Function GetUnusualRatio() global native
Function SetUnusualRatio(float afRatio) global native

; Proporcao de NPCs com "unusual breasts" (sag extremo ou perky extremo), 0.0-1.0.
float Function GetBreastUnusualRatio() global native
Function SetBreastUnusualRatio(float afRatio) global native

; Proporcao de FEMEAS atleticas (tom muscular/definicao visivel), 0.0-1.0.
float Function GetAthleticRatio() global native
Function SetAthleticRatio(float afRatio) global native

; Tecla de re-roll (scancode DirectInput). Padrao 26 = tecla [ / {. Bindavel no MCM.
int Function GetReRollKey() global native
Function SetReRollKey(int aiKey) global native

; Intensidade efetiva por NPC: realista (~1.0) ou fantasia (~1.3-2.2) x escala global.
; Chame UMA vez por NPC e aplique a todos os sliders.
float Function GetActorIntensity(Actor akActor) global native

; Procedural morph generation (no preset files needed).
; Call GetFrameScore ONCE per actor, then pass that T to each GetMorphValue call.
; This ensures all body parts are correlated (same frame score → proportional shape).
float Function GetFrameScore(Actor akActor) global native
float Function GetMorphValue(Actor akActor, float afFrameScore, string morphName) global native

; Morphs masculinos (HIMBO). Derivado de build (musculo+gordura) + traits + unusual.
float Function GetMaleMorphValue(Actor akActor, string morphName) global native
; Intensidade masculina por NPC (realista/fantasy/unusual). Para sliders de volume.
float Function GetMaleIntensity(Actor akActor) global native

; Re-gera peso e morphs para um actor específico (usado pelo hotkey de re-geração).
; Remove da lista de processados, re-rola o peso, e enfileira para morphs.
Function RegenerateActor(Actor akActor) global native
Function MarkMorphsApplied(Actor akActor) global native
bool Function HasMorphsApplied(Actor akActor) global native
