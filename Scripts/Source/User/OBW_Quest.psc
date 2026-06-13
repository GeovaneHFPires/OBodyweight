Scriptname OBW_Quest extends Quest

; Thin relay only — C++ owns the morph queue and all decisions.
; Timer is armed on every enqueue (not a persistent heartbeat) so it can never
; "die" on a reloaded save where OnInit no longer runs.

int _reRollKey = 26   ; [ / { — overwritten from the plugin on init/load

Event OnInit()
    OBodyNative.RegisterForOBodyEvent(self as Quest)
    RegisterForModEvent("OBW_RebindKey", "OnRebindKey")
    BindReRollKey()
EndEvent

Event OnPlayerLoadGame()
    OBodyNative.RegisterForOBodyEvent(self as Quest)
    RegisterForModEvent("OBW_RebindKey", "OnRebindKey")
    BindReRollKey()
EndEvent

Function BindReRollKey()
    UnregisterForKey(_reRollKey)
    _reRollKey = OBW_Native.GetReRollKey()
    RegisterForKey(_reRollKey)
EndFunction

; Fired by the MCM when the re-roll key is rebound.
Event OnRebindKey(string asEvent, string asStr, float afNum, Form akSender)
    BindReRollKey()
EndEvent

Event OnKeyDown(int keyCode)
    if keyCode != _reRollKey
        return
    endif
    if OBW_Native.GetBodyMode() != 0
        return
    endif
    Actor a = Game.GetCurrentCrosshairRef() as Actor
    if !a
        return
    endif
    Debug.Notification("Regenerating body: " + a.GetDisplayName())
    OBW_Native.RegenerateActor(a)
    RegisterForSingleUpdate(0.3)   ; arm the drain
EndEvent

Event OnActorGenerated(Actor akActor, string presetName)
    ; HasMorphsApplied is one-shot: returns true and auto-clears if we just called
    ; UpdateModelWeight(true) for this actor. Suppresses the OBody re-fire loop.
    if OBW_Native.HasMorphsApplied(akActor)
        return
    endif
    if OBW_Native.GetBodyMode() == 1
        return
    endif
    ; Male-bodies master toggle: don't even queue males when the feature is off.
    if akActor.GetActorBase().GetSex() == 0 && !OBW_Native.GetMaleBodies()
        return
    endif
    OBW_Native.QueueForMorphs(akActor)
    RegisterForSingleUpdate(0.3)   ; arm the drain (resets on each enqueue → fires after last)
EndEvent

Event OnUpdate()
    ; Lazy / throttled drain: process only a few actors per tick and reschedule if
    ; more remain. Entering a crowded cell queues many actors at once — draining them
    ; all in one frame (each does ~14 morphs + UpdateModelWeight + armor re-equip)
    ; caused the cell-entry freeze. Spreading the work across ticks removes the hitch.
    int budget = 2
    int processed = 0
    while budget > 0
        ; GetNextMorphActor returns the CLOSEST loaded actor within radius, or None
        ; if the queue is empty OR all remaining actors are too far (deferred).
        Actor a = OBW_Native.GetNextMorphActor()
        if !a
            budget = 0
        else
            ApplyMorphs(a)
            budget -= 1
            processed += 1
        endif
    endwhile

    if OBW_Native.HasMorphsPending()
        if processed > 0
            RegisterForSingleUpdate(0.15)   ; more in range — keep draining fast
        else
            RegisterForSingleUpdate(1.0)    ; only distant actors left — poll slowly
        endif
    endif
EndEvent

Function ApplyMorphs(Actor akActor)
    if OBW_Native.GetBodyMode() != 0
        return
    endif

    ; Male-bodies master toggle: leave males entirely alone when off — don't even clear
    ; OBody's morphs, so OBody's own male presets keep working. 0 = male, 1 = female.
    bool isFemale = akActor.GetActorBase().GetSex() == 1
    if !isFemale && !OBW_Native.GetMaleBodies()
        return
    endif

    ; Clear OBody's preset morphs (key "OBody") — OBody has a male preset DB too, so
    ; without this the male HIMBO preset would stack with our "OBW".
    NiOverride.ClearBodyMorphNames(akActor, "OBody")

    ; Sex-branched: females use CBBE 3BA sliders, males use HIMBO.
    if isFemale
        ApplyFemaleMorphs(akActor)
    else
        ApplyMaleMorphs(akActor)
    endif

    OBW_Native.MarkMorphsApplied(akActor)
    NiOverride.UpdateModelWeight(akActor)

    ; Refit equipped clothing. SKEE applies body morphs at EQUIP time, so to make
    ; already-worn armor follow the new shape we re-equip the body-slot item —
    ; the same trick OBody uses (OBodyNGScript.psc:197-209). Slot 0x04 = body (32).
    Form bodyArmor = akActor.GetWornForm(0x00000004)
    if bodyArmor
        OBW_Native.MarkMorphsApplied(akActor)   ; cover any equip-triggered re-fire
        akActor.UnequipItem(bodyArmor, false, true)
        Utility.Wait(0.05)
        akActor.EquipItem(bodyArmor, false, true)
    endif
EndFunction

Function ApplyFemaleMorphs(Actor akActor)
    ; SKEE body morphs are 0.0-1.0 (1.0 = 100% of the BodySlide slider).
    ; GetMorphValue returns 0-100; divide by 100 and apply the global intensity scale.
    float T = OBW_Native.GetFrameScore(akActor)
    ; Volume sliders: per-NPC intensity (realistic vs fantasy), includes global scale.
    float kVol = OBW_Native.GetActorIntensity(akActor) / 100.0
    ; Definition/trait sliders: master scale only — never the fantasy 2.2x blow-up,
    ; so shape traits (waist, sag, hip dips...) stay anatomically plausible.
    float kDef = OBW_Native.GetMorphScale() / 100.0

    ; --- Volume (frame score + traits, amplified for fantasy NPCs) ---
    NiOverride.SetBodyMorph(akActor, "Breasts", "OBW", OBW_Native.GetMorphValue(akActor, T, "Breasts") * kVol)
    NiOverride.SetBodyMorph(akActor, "Butt",    "OBW", OBW_Native.GetMorphValue(akActor, T, "Butt")    * kVol)
    NiOverride.SetBodyMorph(akActor, "Belly",   "OBW", OBW_Native.GetMorphValue(akActor, T, "Belly")   * kVol)
    NiOverride.SetBodyMorph(akActor, "Hips",    "OBW", OBW_Native.GetMorphValue(akActor, T, "Hips")    * kVol)
    NiOverride.SetBodyMorph(akActor, "Thighs",  "OBW", OBW_Native.GetMorphValue(akActor, T, "Thighs")  * kVol)
    NiOverride.SetBodyMorph(akActor, "BigButt", "OBW", OBW_Native.GetMorphValue(akActor, T, "BigButt") * kVol)

    ; --- Arms: track body fullness at a reduced ratio (match the body, natural taper) ---
    NiOverride.SetBodyMorph(akActor, "Arms",        "OBW", OBW_Native.GetMorphValue(akActor, T, "Arms")        * kVol)
    NiOverride.SetBodyMorph(akActor, "ForearmSize", "OBW", OBW_Native.GetMorphValue(akActor, T, "ForearmSize") * kVol)
    NiOverride.SetBodyMorph(akActor, "WristSize",   "OBW", OBW_Native.GetMorphValue(akActor, T, "WristSize")   * kVol)
    NiOverride.SetBodyMorph(akActor, "ChubbyArms",  "OBW", OBW_Native.GetMorphValue(akActor, T, "ChubbyArms")  * kVol)

    ; --- Definition / shape traits (master scale only) ---
    NiOverride.SetBodyMorph(akActor, "Waist",               "OBW", OBW_Native.GetMorphValue(akActor, T, "Waist")               * kDef)
    NiOverride.SetBodyMorph(akActor, "BreastsGone",         "OBW", OBW_Native.GetMorphValue(akActor, T, "BreastsGone")         * kDef)
    NiOverride.SetBodyMorph(akActor, "BreastPerkiness",     "OBW", OBW_Native.GetMorphValue(akActor, T, "BreastPerkiness")     * kDef)
    NiOverride.SetBodyMorph(akActor, "BreastGravity2",      "OBW", OBW_Native.GetMorphValue(akActor, T, "BreastGravity2")      * kDef)
    NiOverride.SetBodyMorph(akActor, "BreastWidth",         "OBW", OBW_Native.GetMorphValue(akActor, T, "BreastWidth")         * kDef)
    NiOverride.SetBodyMorph(akActor, "HipBone",             "OBW", OBW_Native.GetMorphValue(akActor, T, "HipBone")             * kDef)
    NiOverride.SetBodyMorph(akActor, "ThighInsideThicc_v2", "OBW", OBW_Native.GetMorphValue(akActor, T, "ThighInsideThicc_v2") * kDef)
    NiOverride.SetBodyMorph(akActor, "SlimThighs",          "OBW", OBW_Native.GetMorphValue(akActor, T, "SlimThighs")          * kDef)

    ; --- Muscle tone (athletic women: visible definition; suppressed by belly) ---
    NiOverride.SetBodyMorph(akActor, "VeraMuscleTones", "OBW", OBW_Native.GetMorphValue(akActor, T, "VeraMuscleTones") * kDef)
    NiOverride.SetBodyMorph(akActor, "MuscleAbs",       "OBW", OBW_Native.GetMorphValue(akActor, T, "MuscleAbs")       * kDef)
    NiOverride.SetBodyMorph(akActor, "MuscleArms",      "OBW", OBW_Native.GetMorphValue(akActor, T, "MuscleArms")      * kDef)
    NiOverride.SetBodyMorph(akActor, "MuscleLegs",      "OBW", OBW_Native.GetMorphValue(akActor, T, "MuscleLegs")      * kDef)
    ; "Snu snu" deep definition (rare super-toned Amazon) — 0 for everyone else.
    NiOverride.SetBodyMorph(akActor, "MuscleMoreAbs_v2",  "OBW", OBW_Native.GetMorphValue(akActor, T, "MuscleMoreAbs_v2")  * kDef)
    NiOverride.SetBodyMorph(akActor, "MuscleMoreArms_v2", "OBW", OBW_Native.GetMorphValue(akActor, T, "MuscleMoreArms_v2") * kDef)
    NiOverride.SetBodyMorph(akActor, "MuscleMoreLegs_v2", "OBW", OBW_Native.GetMorphValue(akActor, T, "MuscleMoreLegs_v2") * kDef)
EndFunction

Function ApplyMaleMorphs(Actor akActor)
    ; Mirrors the female split: volume sliders use per-NPC intensity (realistic/fantasy/
    ; unusual); definition/shape sliders use the master scale only.
    float kVol = OBW_Native.GetMaleIntensity(akActor) / 100.0
    float kDef = OBW_Native.GetMorphScale() / 100.0

    ; --- Volume (build: muscle/fat/mass; amplified for fantasy/huge) ---
    NiOverride.SetBodyMorph(akActor, "Muscle",     "OBW", OBW_Native.GetMaleMorphValue(akActor, "Muscle")     * kVol)
    NiOverride.SetBodyMorph(akActor, "BodyMass",   "OBW", OBW_Native.GetMaleMorphValue(akActor, "BodyMass")   * kVol)
    NiOverride.SetBodyMorph(akActor, "PecsSize",   "OBW", OBW_Native.GetMaleMorphValue(akActor, "PecsSize")   * kVol)
    NiOverride.SetBodyMorph(akActor, "ArmsBiceps", "OBW", OBW_Native.GetMaleMorphValue(akActor, "ArmsBiceps") * kVol)
    NiOverride.SetBodyMorph(akActor, "Chubby",     "OBW", OBW_Native.GetMaleMorphValue(akActor, "Chubby")     * kVol)
    NiOverride.SetBodyMorph(akActor, "TorsoBelly", "OBW", OBW_Native.GetMaleMorphValue(akActor, "TorsoBelly") * kVol)
    NiOverride.SetBodyMorph(akActor, "LegsSize",   "OBW", OBW_Native.GetMaleMorphValue(akActor, "LegsSize")   * kVol)
    NiOverride.SetBodyMorph(akActor, "ButtBooty",  "OBW", OBW_Native.GetMaleMorphValue(akActor, "ButtBooty")  * kVol)

    ; --- Definition / shape traits (master scale only) ---
    NiOverride.SetBodyMorph(akActor, "Lean",             "OBW", OBW_Native.GetMaleMorphValue(akActor, "Lean")             * kDef)
    NiOverride.SetBodyMorph(akActor, "PecsFlatten",      "OBW", OBW_Native.GetMaleMorphValue(akActor, "PecsFlatten")      * kDef)
    NiOverride.SetBodyMorph(akActor, "TorsoShoulderInc", "OBW", OBW_Native.GetMaleMorphValue(akActor, "TorsoShoulderInc") * kDef)
    NiOverride.SetBodyMorph(akActor, "TorsoWaistSize",   "OBW", OBW_Native.GetMaleMorphValue(akActor, "TorsoWaistSize")   * kDef)
    NiOverride.SetBodyMorph(akActor, "TorsoWidth",       "OBW", OBW_Native.GetMaleMorphValue(akActor, "TorsoWidth")       * kDef)
    NiOverride.SetBodyMorph(akActor, "TorsoFlatAbs",     "OBW", OBW_Native.GetMaleMorphValue(akActor, "TorsoFlatAbs")     * kDef)
    NiOverride.SetBodyMorph(akActor, "TorsoVLine",       "OBW", OBW_Native.GetMaleMorphValue(akActor, "TorsoVLine")       * kDef)
    NiOverride.SetBodyMorph(akActor, "TorsoRibsDefinition", "OBW", OBW_Native.GetMaleMorphValue(akActor, "TorsoRibsDefinition") * kDef)
    NiOverride.SetBodyMorph(akActor, "ArmsTrapsValleys",   "OBW", OBW_Native.GetMaleMorphValue(akActor, "ArmsTrapsValleys")   * kDef)
    NiOverride.SetBodyMorph(akActor, "LegsThinner",      "OBW", OBW_Native.GetMaleMorphValue(akActor, "LegsThinner")      * kDef)
EndFunction
