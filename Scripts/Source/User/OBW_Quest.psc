Scriptname OBW_Quest extends Quest

; Thin relay only — C++ owns the morph queue and all decisions.
; Timer is armed on every enqueue (not a persistent heartbeat) so it can never
; "die" on a reloaded save where OnInit no longer runs.

int _reRollKey = 26   ; [ / { — overwritten from the plugin on init/load

Event OnInit()
    OBodyNative.RegisterForOBodyEvent(self as Quest)
    RegisterForModEvent("OBW_RebindKey", "OnRebindKey")
    RegisterForModEvent("OBW_Reprocess", "OnReprocess")
    BindReRollKey()
    RegisterForSingleUpdate(2.0)   ; start the persistent manual-assign poll (self-perpetuates)
EndEvent

Event OnPlayerLoadGame()
    OBodyNative.RegisterForOBodyEvent(self as Quest)
    RegisterForModEvent("OBW_RebindKey", "OnRebindKey")
    RegisterForModEvent("OBW_Reprocess", "OnReprocess")
    BindReRollKey()
    RegisterForSingleUpdate(2.0)   ; start the persistent manual-assign poll (self-perpetuates)
EndEvent

; Fired by the MCM "Reprocess all loaded NPCs" button: re-queue every loaded NPC and arm the drain,
; so the current generation logic + CBPC physics re-apply without cell reloads.
Event OnReprocess(string asEvent, string asStr, float afNum, Form akSender)
    int n = OBW_Native.ReprocessAllLoaded()
    Debug.Notification("OBodyNG Weight: reprocessing " + n + " loaded NPC(s)...")
    RegisterForSingleUpdate(0.3)
EndEvent

; Arms the persistent manual-assign poll (OnUpdate self-perpetuates from here). Called on every
; game load by OBW_MCM.OnGameReload, since OnPlayerLoadGame doesn't fire on Quest scripts.
Function StartPoll()
    RegisterForSingleUpdate(2.0)   ; arm FIRST so a Log issue can never block the poll
    OBW_Native.Log("StartPoll: manual-assign poll armed")
EndFunction

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
    ; Re-roll works in every mode: procedural (0/2) re-rolls the body; OBody Sim Weight (1) re-rolls the
    ; mock weight so the preset re-interpolates. (Plain OBody = assign manually via OBody; OBW won't touch it.)
    ; Pick the target: VR has no crosshair (GetCurrentCrosshairRef is None), so there we
    ; cone-cast from the HMD gaze in C++; desktop uses the normal crosshair (the cone-cast
    ; never runs there). Either way, fall back to the player if nothing is targeted.
    Actor a
    if OBW_Native.IsVR()
        a = OBW_Native.GetVRLookTarget()
    else
        a = Game.GetCurrentCrosshairRef() as Actor
    endif
    if !a
        a = Game.GetPlayer()
    endif
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
    int gm = OBW_Native.GetBodyMode()
    ; Excluded plugins: never queue NPCs from an excluded .esp.
    if OBW_Native.IsExcluded(akActor)
        return
    endif
    ; The HasMorphsApplied one-shot breaks the re-fire loop for the PROCEDURAL modes (0/2), which
    ; re-apply unconditionally. Mode 1 must NOT consume it: it breaks its own loop (it unassigns the
    ; preset + early-outs on "no preset"), and eating the one-shot here would drop a genuine MANUAL
    ; preset re-assignment that the user makes through OBody's own menu.
    if gm != 1 && OBW_Native.HasMorphsApplied(akActor)
        return
    endif
    ; Per-sex master toggles: don't even queue a sex the user disabled. 0 = male, 1 = female.
    int sex = akActor.GetActorBase().GetSex()
    if sex == 0 && !OBW_Native.GetMaleBodies()
        return
    endif
    if sex == 1 && !OBW_Native.GetFemaleBodies()
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
        return
    endif

    ; RESPECT MANUAL OBody assignments: when you apply a preset yourself through OBody's menu (or any
    ; OBody feature that fires a preset on demand), OBW must NOT grab it - re-fitting it would break those
    ; OBody features. So OBW Sim Weight only varies AUTO-distributed presets (which arrive via
    ; OnActorGenerated); presets fired manually are left exactly as OBody applied them.
    ; Procedural fallback (independent distribution): self-distribute any loaded NPC OBody never handled,
    ; so procedural bodies apply even with an EMPTY preset library. No-op in OBody Sim Weight mode (gated in C++).
    if OBW_Native.SweepFallback() > 0
        RegisterForSingleUpdate(0.3)        ; drain the newly-enqueued NPCs now
        return
    endif
    RegisterForSingleUpdate(2.0)            ; persistent light poll for the procedural fallback
EndEvent

Function ApplyMorphs(Actor akActor)
    ; Excluded plugins (OBodyNGWeight_Exclusions*.txt): leave those NPCs entirely to OBody/vanilla.
    if OBW_Native.IsExcluded(akActor)
        return
    endif
    ; Body mode 1 = OBody Presets, now WEIGHT-DRIVEN: re-apply the OBody-assigned preset
    ; interpolated at the per-NPC mock weight (faithful curve + synthesized lean<->full on
    ; static volumes). Modes 0 (Procedural) and 2 (Procedural Oriented) use the path below.
    if OBW_Native.GetBodyMode() == 1
        ApplyPresetWeighted(akActor)
        return
    endif

    ; Per-sex master toggles: leave a disabled sex entirely alone — don't even touch OBody's
    ; morphs, so OBody's own presets keep working for that sex. 0 = male, 1 = female.
    bool isFemale = akActor.GetActorBase().GetSex() == 1
    if isFemale && !OBW_Native.GetFemaleBodies()
        return
    endif
    if !isFemale && !OBW_Native.GetMaleBodies()
        return
    endif

    ; OBody's "processed" flag lives under the "OBody" morph key, named by OBody's CURRENT distribution key
    ; (read from the same StorageUtil value OBody's DLL uses; default "obody_processed"). Read its value so
    ; we can re-assert it after removing OBody's contribution (so OBody leaves the actor to us).
    string obKey = StorageUtil.GetStringValue(None, "obody_ng_distribution_key", "obody_processed")
    float wasProcessed = NiOverride.GetBodyMorph(akActor, obKey, "OBody")

    ; Orientation strength: only body mode 2 (Procedural Oriented) blends toward the OBody preset; 0 = pure.
    float orient = 0.0
    if OBW_Native.GetBodyMode() == 2
        orient = OBW_Native.GetPresetOrient()
    endif

    ; Apply our procedural morphs (key "OBW"). The OBody preset is STILL present at this point, so the
    ; oriented blend below can read it before we remove it.
    if isFemale
        ApplyFemaleMorphs(akActor)
    else
        ApplyMaleMorphs(akActor)
    endif
    if orient > 0.0
        BlendWithPreset(akActor, orient, isFemale)   ; pull each "OBW" slider toward the preset value
    endif

    ; Take OBody out of the equation for this actor: UNASSIGN its preset (nothing to re-apply) + clear its
    ; morphs via OBody's OWN native, then re-assert the processed flag so OBody skips the actor forever. Our
    ; "OBW" morphs (persisted by SKEE) are then the only ones -> OBW supplants OBody persistently. (Doing
    ; this AFTER our apply/blend is what lets the oriented blend read the preset first.)
    OBodyNative.AssignPresetToActor(akActor, "", false, true)   ; unassign preset, do not apply morphs
    OBodyNative.ResetActorOBodyMorphs(akActor)                   ; clear OBody's "OBody" morphs (canonical native)
    if wasProcessed == 0.0
        wasProcessed = 1.0
    endif
    NiOverride.SetBodyMorph(akActor, obKey, "OBody", wasProcessed)

    ; Apply the new shape with the FEWEST body rebuilds — each rebuild makes SKEE
    ; reprocess EVERY overlay, which is heavy with large overlay counts.
    Form bodyArmor = akActor.GetWornForm(0x00000004)
    if bodyArmor
        ; Clothed (2 rebuilds): re-equip the body-slot armor. The UNEQUIP rebuilds the
        ; (briefly visible) body with the persisted morphs, and the EQUIP applies them to
        ; the armor (OBody's trick — morphs apply at equip time). A separate
        ; UpdateModelWeight would be a redundant 3rd rebuild, so it's skipped here.
        OBW_Native.MarkMorphsApplied(akActor)   ; suppress unequip-triggered re-fire
        akActor.UnequipItem(bodyArmor, false, true)
        Utility.Wait(0.05)
        OBW_Native.MarkMorphsApplied(akActor)   ; suppress equip-triggered re-fire
        akActor.EquipItem(bodyArmor, false, true)
    else
        ; Nude (1 rebuild): nothing to re-equip — update the body directly.
        OBW_Native.MarkMorphsApplied(akActor)
        NiOverride.UpdateModelWeight(akActor)
    endif

    ; CBPC physics preset by archetype (soft dep — no-op without CBPC). Both sexes now (male pec/belly
    ; jiggle scales with the male archetype: firm for Fit/Bodybuilder, soft for Dadbod/Heavyset).
    ApplyPhysicsTier(akActor)
EndFunction

; ── Body mode 1 (OBody Presets, weight-driven) ────────────────────────────────────────────
; OBody still PICKS the preset (its distribution config is respected); OBW then RE-APPLIES it
; interpolated at the per-NPC mock weight, so NPCs sharing a preset vary and static presets gain
; a synthesized lean<->full axis. Values never exceed the preset's own (no vertex overshoot). Then
; OBW supplants OBody (same as the procedural path) so the result persists. Pure OBody passthrough
; (Weight mode = NPC Default) is filtered out by the callers before we get here.
Function ApplyPresetWeighted(Actor akActor)
    ; Per-sex master toggles: leave a disabled sex to OBody/vanilla (don't strip its preset).
    bool isFemale = akActor.GetActorBase().GetSex() == 1
    if isFemale && !OBW_Native.GetFemaleBodies()
        return
    endif
    if !isFemale && !OBW_Native.GetMaleBodies()
        return
    endif

    ; Which preset did OBody assign to this actor? (Empty = nothing assigned -> leave it alone.)
    string preset = OBodyNative.GetPresetAssignedToActor(akActor)
    if preset == ""
        return
    endif

    ; OBody's distribution key (its "processed" flag lives under it, morph key "OBody"; default
    ; "obody_processed"). Read from the same StorageUtil value OBody's DLL uses.
    string obKey = StorageUtil.GetStringValue(None, "obody_ng_distribution_key", "obody_processed")

    ; PREFERRED: apply the whole preset in C++ via SKEE — no 128-slider cap, far fewer calls. It sets
    ; every slider, drops OBody's "OBody"-key morphs, re-asserts "processed", and rebuilds (body + armor).
    if OBW_Native.ApplyPresetMorphs(preset, akActor, obKey)
        OBodyNative.AssignPresetToActor(akActor, "", false, true)   ; unassign -> poll won't re-inject
        OBW_Native.Log("preset-weight: applied '" + preset + "' to " + akActor.GetActorBase().GetName())
        return
    endif

    ; FALLBACK (SKEE C++ interface unavailable): the Papyrus array path (capped at 128 sliders).
    string[] names = OBW_Native.GetPresetSliders(preset)
    if names.Length == 0
        OBW_Native.Log("preset-weight: preset NOT FOUND '" + preset + "'")
        return
    endif
    float[] vals = OBW_Native.GetPresetMorphs(preset, akActor)
    if vals.Length != names.Length
        return
    endif
    float wasProcessed = NiOverride.GetBodyMorph(akActor, obKey, "OBody")
    int i = 0
    while i < names.Length
        NiOverride.SetBodyMorph(akActor, names[i], "OBW", vals[i])
        i += 1
    endwhile
    OBodyNative.AssignPresetToActor(akActor, "", false, true)
    OBodyNative.ResetActorOBodyMorphs(akActor)
    if wasProcessed == 0.0
        wasProcessed = 1.0
    endif
    NiOverride.SetBodyMorph(akActor, obKey, "OBody", wasProcessed)
    Form bodyArmor = akActor.GetWornForm(0x00000004)
    if bodyArmor
        OBW_Native.MarkMorphsApplied(akActor)
        akActor.UnequipItem(bodyArmor, false, true)
        Utility.Wait(0.05)
        OBW_Native.MarkMorphsApplied(akActor)
        akActor.EquipItem(bodyArmor, false, true)
    else
        OBW_Native.MarkMorphsApplied(akActor)
        NiOverride.UpdateModelWeight(akActor)
    endif
    OBW_Native.Log("preset-weight (PSC fallback): applied '" + preset + "' to " + akActor.GetActorBase().GetName())
EndFunction

; Per-body physics WITHOUT replacing the user's config: CBPC's ApplyBounceInterpolation scales the
; AMPLITUDE of the actor's EXISTING physics by a percentage (config UniqueName="OBW"). The percent
; comes from the archetype tier (firmer -> lower amplitude, jigglier -> higher). percent ~32 is the
; neutral point (amplitude ~1.0 = unchanged). No-op without CBPC; can't disable physics (additive).
Function ApplyPhysicsTier(Actor akActor)
    if !OBW_Native.HasCBPC()
        return
    endif
    ; Continuous, body-correlated: bounce follows size + softness, collision follows size.
    ; (Within one archetype a bigger body now jiggles more; muscle firms; fat softens.) Same OBW config
    ; bones serve males (Breast = pecs on the male skeleton); the male percent comes from the male archetype.
    if akActor.GetActorBase().GetSex() == 1
        CBPCPluginScript.ApplyBounceInterpolation(akActor, "OBW", OBW_Native.GetPhysicsPercent(akActor, 0))
        CBPCPluginScript.ApplyCollisionInterpolation(akActor, "OBW", OBW_Native.GetPhysicsPercent(akActor, 1))
    else
        CBPCPluginScript.ApplyBounceInterpolation(akActor, "OBW", OBW_Native.GetMalePhysicsPercent(akActor, 0))
        CBPCPluginScript.ApplyCollisionInterpolation(akActor, "OBW", OBW_Native.GetMalePhysicsPercent(akActor, 1))
    endif
EndFunction

Function ApplyFemaleMorphs(Actor akActor)
    ; SKEE body morphs are 0.0-1.0 (1.0 = 100% of the BodySlide slider).
    ; GetMorphValue returns 0-100; divide by 100 and apply the global intensity scale.
    float T = OBW_Native.GetFrameScore(akActor)
    ; Volume sliders use GetVolumeMorph: intensity (realistic vs fantasy) is baked in and the
    ; result is soft-capped to the sculpted vertex range, so big bodies never spike/break.
    ; Definition/trait sliders: master scale only — never the fantasy blow-up, so shape traits
    ; (waist, sag, hip dips...) stay anatomically plausible (always <= 1.0).
    float kDef = OBW_Native.GetMorphScale() / 100.0

    ; DEBUG (gated by the MCM debug toggle): log the archetype + key generated values so we can SEE the
    ; per-NPC variety (or lack of it) in the log. Cheap; no-op when debug logging is off.
    OBW_Native.Log("F " + akActor.GetDisplayName() + " arch=" + OBW_Native.GetArchetypeName(akActor) \
        + " fr=" + (T as int) \
        + " br=" + (OBW_Native.GetMorphValue(akActor, T, "Breasts") as int) \
        + " bt=" + (OBW_Native.GetMorphValue(akActor, T, "Butt") as int) \
        + " hp=" + (OBW_Native.GetMorphValue(akActor, T, "Hips") as int) \
        + " sh=" + (OBW_Native.GetMorphValue(akActor, T, "ShoulderWidth") as int) \
        + " rear=" + OBW_Native.GetButtShapeName(akActor) + " bust=" + OBW_Native.GetBreastShapeName(akActor))

    ; --- Volume (frame score + traits, amplified for fantasy NPCs, soft-capped) ---
    NiOverride.SetBodyMorph(akActor, "Breasts", "OBW", OBW_Native.GetVolumeMorph(akActor, T, "Breasts"))
    NiOverride.SetBodyMorph(akActor, "Butt",    "OBW", OBW_Native.GetVolumeMorph(akActor, T, "Butt"))
    NiOverride.SetBodyMorph(akActor, "Belly",   "OBW", OBW_Native.GetVolumeMorph(akActor, T, "Belly"))
    NiOverride.SetBodyMorph(akActor, "Hips",    "OBW", OBW_Native.GetVolumeMorph(akActor, T, "Hips"))
    NiOverride.SetBodyMorph(akActor, "Thighs",  "OBW", OBW_Native.GetVolumeMorph(akActor, T, "Thighs"))
    NiOverride.SetBodyMorph(akActor, "BigButt", "OBW", OBW_Native.GetVolumeMorph(akActor, T, "BigButt"))

    ; --- Arms: forearm/wrist are derived from the upper arm (guaranteed smooth taper) ---
    NiOverride.SetBodyMorph(akActor, "Arms",        "OBW", OBW_Native.GetVolumeMorph(akActor, T, "Arms"))
    NiOverride.SetBodyMorph(akActor, "ForearmSize", "OBW", OBW_Native.GetVolumeMorph(akActor, T, "ForearmSize"))
    NiOverride.SetBodyMorph(akActor, "WristSize",   "OBW", OBW_Native.GetVolumeMorph(akActor, T, "WristSize"))
    NiOverride.SetBodyMorph(akActor, "ChubbyArms",  "OBW", OBW_Native.GetVolumeMorph(akActor, T, "ChubbyArms"))

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

    ; --- NEW shape dimensions (more body types: shoulders, leg chain, butt shape, breast shape, waistline) ---
    ; Volume (soft-capped): leg chain derived from thighs + a real belly only on heavy bodies.
    NiOverride.SetBodyMorph(akActor, "CalfSize",             "OBW", OBW_Native.GetVolumeMorph(akActor, T, "CalfSize"))
    NiOverride.SetBodyMorph(akActor, "ThighOutsideThicc_v2", "OBW", OBW_Native.GetVolumeMorph(akActor, T, "ThighOutsideThicc_v2"))
    NiOverride.SetBodyMorph(akActor, "ThighFBThicc_v2",      "OBW", OBW_Native.GetVolumeMorph(akActor, T, "ThighFBThicc_v2"))
    NiOverride.SetBodyMorph(akActor, "ChubbyLegs",           "OBW", OBW_Native.GetVolumeMorph(akActor, T, "ChubbyLegs"))
    NiOverride.SetBodyMorph(akActor, "BigBelly",             "OBW", OBW_Native.GetVolumeMorph(akActor, T, "BigBelly"))
    ; Shape (master scale only): silhouette + butt shape (heart/round/shelf/dimpled) + breast shape + waistline.
    NiOverride.SetBodyMorph(akActor, "ShoulderWidth",   "OBW", OBW_Native.GetMorphValue(akActor, T, "ShoulderWidth")   * kDef)
    NiOverride.SetBodyMorph(akActor, "RoundAss",        "OBW", OBW_Native.GetMorphValue(akActor, T, "RoundAss")        * kDef)
    NiOverride.SetBodyMorph(akActor, "AppleCheeks",     "OBW", OBW_Native.GetMorphValue(akActor, T, "AppleCheeks")     * kDef)
    NiOverride.SetBodyMorph(akActor, "ButtClassic",     "OBW", OBW_Native.GetMorphValue(akActor, T, "ButtClassic")     * kDef)
    NiOverride.SetBodyMorph(akActor, "ButtShape2",      "OBW", OBW_Native.GetMorphValue(akActor, T, "ButtShape2")      * kDef)
    NiOverride.SetBodyMorph(akActor, "ButtDimples",     "OBW", OBW_Native.GetMorphValue(akActor, T, "ButtDimples")     * kDef)
    NiOverride.SetBodyMorph(akActor, "MuscleButt",      "OBW", OBW_Native.GetMorphValue(akActor, T, "MuscleButt")      * kDef)
    NiOverride.SetBodyMorph(akActor, "BreastsTogether", "OBW", OBW_Native.GetMorphValue(akActor, T, "BreastsTogether") * kDef)
    NiOverride.SetBodyMorph(akActor, "BreastCleavage",  "OBW", OBW_Native.GetMorphValue(akActor, T, "BreastCleavage")  * kDef)
    NiOverride.SetBodyMorph(akActor, "BreastHeight",    "OBW", OBW_Native.GetMorphValue(akActor, T, "BreastHeight")    * kDef)
    NiOverride.SetBodyMorph(akActor, "WideWaistLine",   "OBW", OBW_Native.GetMorphValue(akActor, T, "WideWaistLine")   * kDef)
    NiOverride.SetBodyMorph(akActor, "ChubbyWaist",     "OBW", OBW_Native.GetMorphValue(akActor, T, "ChubbyWaist")     * kDef)

    ; --- FINE shapes: torso breadth, ribs (lean only), hip projection, leg shape, breast shape ---
    NiOverride.SetBodyMorph(akActor, "BigTorso",        "OBW", OBW_Native.GetMorphValue(akActor, T, "BigTorso")        * kDef)
    NiOverride.SetBodyMorph(akActor, "ChestWidth",      "OBW", OBW_Native.GetMorphValue(akActor, T, "ChestWidth")      * kDef)
    NiOverride.SetBodyMorph(akActor, "RibsProminance",  "OBW", OBW_Native.GetMorphValue(akActor, T, "RibsProminance")  * kDef)
    NiOverride.SetBodyMorph(akActor, "HipForward",      "OBW", OBW_Native.GetMorphValue(akActor, T, "HipForward")      * kDef)
    NiOverride.SetBodyMorph(akActor, "LegShapeClassic", "OBW", OBW_Native.GetMorphValue(akActor, T, "LegShapeClassic") * kDef)
    NiOverride.SetBodyMorph(akActor, "BreastTopSlope",  "OBW", OBW_Native.GetMorphValue(akActor, T, "BreastTopSlope")  * kDef)
    NiOverride.SetBodyMorph(akActor, "BreastSideShape", "OBW", OBW_Native.GetMorphValue(akActor, T, "BreastSideShape") * kDef)
EndFunction

Function ApplyMaleMorphs(Actor akActor)
    ; Mirrors the female split: volume sliders use GetMaleVolumeMorph (intensity + HIMBO soft-cap, so a
    ; bodybuilder never breaks the mesh); definition/shape sliders use the master scale only.
    float kDef = OBW_Native.GetMorphScale() / 100.0

    ; DEBUG (gated by the MCM debug toggle): male archetype + key build values.
    OBW_Native.Log("M " + akActor.GetDisplayName() + " arch=" + OBW_Native.GetMaleArchetypeName(akActor) \
        + " mus=" + (OBW_Native.GetMaleMorphValue(akActor, "Muscle") as int) \
        + " mass=" + (OBW_Native.GetMaleMorphValue(akActor, "BodyMass") as int) \
        + " belly=" + (OBW_Native.GetMaleMorphValue(akActor, "TorsoBelly") as int) \
        + " shld=" + (OBW_Native.GetMaleMorphValue(akActor, "TorsoShoulderInc") as int))

    ; --- Volume (build: muscle/fat/mass; archetype-biased; soft-capped) ---
    NiOverride.SetBodyMorph(akActor, "Muscle",      "OBW", OBW_Native.GetMaleVolumeMorph(akActor, "Muscle"))
    NiOverride.SetBodyMorph(akActor, "BodyMass",    "OBW", OBW_Native.GetMaleVolumeMorph(akActor, "BodyMass"))
    NiOverride.SetBodyMorph(akActor, "PecsSize",    "OBW", OBW_Native.GetMaleVolumeMorph(akActor, "PecsSize"))
    NiOverride.SetBodyMorph(akActor, "PecsWidth",   "OBW", OBW_Native.GetMaleVolumeMorph(akActor, "PecsWidth"))
    NiOverride.SetBodyMorph(akActor, "ArmsBiceps",  "OBW", OBW_Native.GetMaleVolumeMorph(akActor, "ArmsBiceps"))
    NiOverride.SetBodyMorph(akActor, "ArmsShoulders","OBW", OBW_Native.GetMaleVolumeMorph(akActor, "ArmsShoulders"))
    NiOverride.SetBodyMorph(akActor, "ArmsTraps",   "OBW", OBW_Native.GetMaleVolumeMorph(akActor, "ArmsTraps"))
    NiOverride.SetBodyMorph(akActor, "ArmsFore",    "OBW", OBW_Native.GetMaleVolumeMorph(akActor, "ArmsFore"))
    NiOverride.SetBodyMorph(akActor, "TorsoBackSize","OBW", OBW_Native.GetMaleVolumeMorph(akActor, "TorsoBackSize"))
    NiOverride.SetBodyMorph(akActor, "Chubby",      "OBW", OBW_Native.GetMaleVolumeMorph(akActor, "Chubby"))
    NiOverride.SetBodyMorph(akActor, "TorsoBelly",  "OBW", OBW_Native.GetMaleVolumeMorph(akActor, "TorsoBelly"))
    NiOverride.SetBodyMorph(akActor, "TorsoBellyLHandles","OBW", OBW_Native.GetMaleVolumeMorph(akActor, "TorsoBellyLHandles"))
    NiOverride.SetBodyMorph(akActor, "LegsSize",    "OBW", OBW_Native.GetMaleVolumeMorph(akActor, "LegsSize"))
    NiOverride.SetBodyMorph(akActor, "LegsThigh",   "OBW", OBW_Native.GetMaleVolumeMorph(akActor, "LegsThigh"))
    NiOverride.SetBodyMorph(akActor, "LegsCalfSize","OBW", OBW_Native.GetMaleVolumeMorph(akActor, "LegsCalfSize"))
    NiOverride.SetBodyMorph(akActor, "ButtBooty",   "OBW", OBW_Native.GetMaleVolumeMorph(akActor, "ButtBooty"))
    NiOverride.SetBodyMorph(akActor, "ButtRoundy",  "OBW", OBW_Native.GetMaleVolumeMorph(akActor, "ButtRoundy"))

    ; --- Definition / shape (master scale only): V-taper, abs/back cuts, lean ---
    NiOverride.SetBodyMorph(akActor, "Lean",             "OBW", OBW_Native.GetMaleMorphValue(akActor, "Lean")             * kDef)
    NiOverride.SetBodyMorph(akActor, "PecsFlatten",      "OBW", OBW_Native.GetMaleMorphValue(akActor, "PecsFlatten")      * kDef)
    NiOverride.SetBodyMorph(akActor, "TorsoShoulderInc", "OBW", OBW_Native.GetMaleMorphValue(akActor, "TorsoShoulderInc") * kDef)
    NiOverride.SetBodyMorph(akActor, "TorsoWaistSize",   "OBW", OBW_Native.GetMaleMorphValue(akActor, "TorsoWaistSize")   * kDef)
    NiOverride.SetBodyMorph(akActor, "TorsoWidth",       "OBW", OBW_Native.GetMaleMorphValue(akActor, "TorsoWidth")       * kDef)
    NiOverride.SetBodyMorph(akActor, "TorsoFlatAbs",     "OBW", OBW_Native.GetMaleMorphValue(akActor, "TorsoFlatAbs")     * kDef)
    NiOverride.SetBodyMorph(akActor, "TorsoVLine",       "OBW", OBW_Native.GetMaleMorphValue(akActor, "TorsoVLine")       * kDef)
    NiOverride.SetBodyMorph(akActor, "TorsoRibsDefinition", "OBW", OBW_Native.GetMaleMorphValue(akActor, "TorsoRibsDefinition") * kDef)
    NiOverride.SetBodyMorph(akActor, "TorsoBackShape",     "OBW", OBW_Native.GetMaleMorphValue(akActor, "TorsoBackShape")     * kDef)
    NiOverride.SetBodyMorph(akActor, "TorsoBackDefinition","OBW", OBW_Native.GetMaleMorphValue(akActor, "TorsoBackDefinition") * kDef)
    NiOverride.SetBodyMorph(akActor, "ArmsTrapsValleys",   "OBW", OBW_Native.GetMaleMorphValue(akActor, "ArmsTrapsValleys")   * kDef)
    NiOverride.SetBodyMorph(akActor, "LegsThinner",      "OBW", OBW_Native.GetMaleMorphValue(akActor, "LegsThinner")      * kDef)
EndFunction

; ── Procedural Oriented (body mode 2): blend toward the OBody preset ──────────────────────────────
; Pulls each procedural "OBW" slider toward the OBody preset's value by `orient` (0 = keep procedural,
; 1 = match the preset). Only sliders the preset actually sets (value > 0) are oriented; the rest stay
; pure procedural. Called from ApplyMorphs AFTER our morphs and BEFORE we clear OBody (so the preset is
; still readable). final = obwVal*(1-orient) + presetVal*orient.
Function BlendWithPreset(Actor akActor, float orient, bool isFemale)
    string[] sliders
    if isFemale
        sliders = FemaleSliders()
    else
        sliders = MaleSliders()
    endif
    int i = 0
    while i < sliders.Length
        float pv = NiOverride.GetBodyMorph(akActor, sliders[i], "OBody")
        if pv > 0.0
            float obw = NiOverride.GetBodyMorph(akActor, sliders[i], "OBW")
            NiOverride.SetBodyMorph(akActor, sliders[i], "OBW", obw * (1.0 - orient) + pv * orient)
        endif
        i += 1
    endwhile
EndFunction

string[] Function FemaleSliders()
    string[] s = new string[49]
    s[0]  = "Breasts"
    s[1]  = "Butt"
    s[2]  = "Belly"
    s[3]  = "Hips"
    s[4]  = "Thighs"
    s[5]  = "BigButt"
    s[6]  = "Arms"
    s[7]  = "ForearmSize"
    s[8]  = "WristSize"
    s[9]  = "ChubbyArms"
    s[10] = "Waist"
    s[11] = "BreastsGone"
    s[12] = "BreastPerkiness"
    s[13] = "BreastGravity2"
    s[14] = "BreastWidth"
    s[15] = "HipBone"
    s[16] = "ThighInsideThicc_v2"
    s[17] = "SlimThighs"
    s[18] = "VeraMuscleTones"
    s[19] = "MuscleAbs"
    s[20] = "MuscleArms"
    s[21] = "MuscleLegs"
    s[22] = "MuscleMoreAbs_v2"
    s[23] = "MuscleMoreArms_v2"
    s[24] = "MuscleMoreLegs_v2"
    s[25] = "CalfSize"
    s[26] = "ThighOutsideThicc_v2"
    s[27] = "ThighFBThicc_v2"
    s[28] = "ChubbyLegs"
    s[29] = "BigBelly"
    s[30] = "ShoulderWidth"
    s[31] = "RoundAss"
    s[32] = "AppleCheeks"
    s[33] = "ButtClassic"
    s[34] = "ButtShape2"
    s[35] = "ButtDimples"
    s[36] = "MuscleButt"
    s[37] = "BreastsTogether"
    s[38] = "BreastCleavage"
    s[39] = "BreastHeight"
    s[40] = "WideWaistLine"
    s[41] = "ChubbyWaist"
    s[42] = "BigTorso"
    s[43] = "ChestWidth"
    s[44] = "RibsProminance"
    s[45] = "HipForward"
    s[46] = "LegShapeClassic"
    s[47] = "BreastTopSlope"
    s[48] = "BreastSideShape"
    return s
EndFunction

string[] Function MaleSliders()
    string[] s = new string[29]
    s[0]  = "Muscle"
    s[1]  = "BodyMass"
    s[2]  = "PecsSize"
    s[3]  = "ArmsBiceps"
    s[4]  = "Chubby"
    s[5]  = "TorsoBelly"
    s[6]  = "LegsSize"
    s[7]  = "ButtBooty"
    s[8]  = "Lean"
    s[9]  = "PecsFlatten"
    s[10] = "TorsoShoulderInc"
    s[11] = "TorsoWaistSize"
    s[12] = "TorsoWidth"
    s[13] = "TorsoFlatAbs"
    s[14] = "TorsoVLine"
    s[15] = "TorsoRibsDefinition"
    s[16] = "ArmsTrapsValleys"
    s[17] = "LegsThinner"
    s[18] = "PecsWidth"
    s[19] = "ArmsShoulders"
    s[20] = "ArmsTraps"
    s[21] = "ArmsFore"
    s[22] = "TorsoBackSize"
    s[23] = "TorsoBellyLHandles"
    s[24] = "LegsThigh"
    s[25] = "LegsCalfSize"
    s[26] = "ButtRoundy"
    s[27] = "TorsoBackShape"
    s[28] = "TorsoBackDefinition"
    return s
EndFunction
