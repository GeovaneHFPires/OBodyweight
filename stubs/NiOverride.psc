Scriptname NiOverride Hidden

; Real SKEE/RaceMenu signatures (compile-time stub; runtime uses skee64.dll).
; Verified against the actual runtime via Papyrus error log:
;   - first param is ObjectReference (NOT Actor)
;   - UpdateModelWeight takes ONE argument (no bool)
Function SetBodyMorph(ObjectReference akRef, string aMorphName, string aKeyName, float aValue) global native
float Function GetBodyMorph(ObjectReference akRef, string aMorphName, string aKeyName) global native
Function ClearBodyMorphNames(ObjectReference akRef, string aKeyName) global native
Function UpdateModelWeight(ObjectReference akRef) global native
