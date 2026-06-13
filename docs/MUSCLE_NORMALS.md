# Muscle Skin Normals — Design & Post‑Mortem (SHELVED)

> **Status: shelved.** The feature *worked mechanically* (per‑actor muscle normal maps,
> driven by tone, decoupled from weight) but the final result **rendered wrong** — the
> CBBE Muscle Solution normals applied via NiOverride looked flat/2D ("anime", "parts
> with depth, others not, like the skin bugs out"). That is a render/shader‑level
> incompatibility, not a logic bug. The **mesh muscle** (morph sliders `MuscleAbs` /
> `MuscleArms` / `MuscleLegs`, driven by the regional tone system) was kept — it gives
> real 3D muscle and has none of the problems below.
>
> This document captures everything needed to try again later.

---

## 1. Goal

Drive **CBBE Muscle Solution V2** muscle *normal maps* per‑NPC from a **tone/fitness**
score instead of the pack's native **weight‑slider** mechanism.

The pack ships 11 tiered body normal maps (`femalebody_1..11_msn.dds`) and an ESP that
swaps them by the actor's **weight** (0–100). That ties muscle to body size, which fights
OBW (OBW uses weight for *size*, and has a separate *tone* axis). The goal was to instead
choose the tier from OBW's **tone**, so an athletic‑but‑light woman reads muscular and a
heavy‑but‑soft woman reads smooth.

**Intended usage:** install the pack's *textures*, **disable its ESP**, let OBW assign the
per‑actor tier.

---

## 2. Asset layout (CBBE Muscle Solution V2, verified on disk)

```
textures/actors/character/
  female/            femalebody_1..11_msn.dds          (humans + mer + orc)
  argonianfemale/    argonianfemalebody_1..11_msn.dds  (Argonian)
  khajiitfemale/     femalebody_1..11_msn.dds          (Khajiit — NOTE: "femalebody" prefix!)
  ... also female/elf, female/nord, female/orc subfolders (race variants the ESP uses)
  ... hands tiers too (femalehands_1..11_msn) — body only was used here
```

**Tier numbering is INVERTED:** `_1` = MOST defined (muscular), high numbers = smoother.
The user's mapping spec: low fitness → `_10`, climbing to `_1` as tone rises.

**Race prefix gotcha (verified):** humans/mer/orc **and Khajiit** use the filename
`femalebody`; only **Argonian** uses `argonianfemalebody`. (Khajiit tiers live in the
`khajiitfemale` folder but keep the plain `femalebody` filename.)

**Dependency detection:** the base game / CBBE ship only `_1`; `_11` exists ONLY with the
pack. So `exists("…/<prefix>_11_msn.dds")` is a reliable "pack installed?" marker, and the
whole feature should fall back to off if it's missing.

---

## 3. The tone system (STILL IN THE CODE — drives the morphs)

`WeightManager.cpp` → `ComputeTones(seedBase, frameScore, athleticRatio)` returns a
`ToneSet { core, arms, legs, overall, snusnu }` (each 0–100). It is the single source of
truth for muscle:

- An athletic gate (`_athleticRatio`, default 0.15) + snu‑snu sub‑roll, same RNG stream as
  `IsSnuSnu` (don't reorder the draws).
- A **dominant region** per woman (core/arms/legs) so muscle is locally varied, but the
  dominant‑region bias is applied **only to athletic women** (non‑athletic get ±4 noise,
  so they stay smooth — otherwise everyone reads toned).
- Belly suppresses the core most (0.55), arms 0.30, legs 0.25; snu‑snu stays lean (0.20).

`GetToneScore(actor)` (kept, Papyrus‑exposed) returns `round(overall)`; a future tier
selection would reuse it: `tier = clamp(round(10 - tone*0.09), 1, 10)` (tone 0 → `_10`,
tone 100 → `_1`).

---

## 4. NiOverride skin‑override API (VERIFIED — extracted from RaceMenu.bsa)

The body normal is a **skin texture override**. Verified signatures (see also the
`reference-nioverride-api` memory):

```papyrus
Function AddSkinOverrideString(ObjectReference ref, bool isFemale, bool firstPerson,
        int slotMask, int key, int index, string value, bool persist) global native
bool Function HasSkinOverride(ObjectReference ref, bool isFemale, bool firstPerson,
        int slotMask, int key, int index) global native
Function RemoveSkinOverride(ObjectReference ref, bool isFemale, bool firstPerson,
        int slotMask, int key, int index) global native
Function ApplySkinOverrides(ObjectReference ref) global native
```

- **key 9** = `ShaderTexture`; **index** = texture slot 0‑8 (**1 = normal map / _msn**,
  0 = diffuse, 7 = specular).
- **slotMask** = biped‑slot bitmask: **0x04 = body (32)**, 0x08 = hands (33), 0x80 = feet.
- **value** = path relative to Data WITH the `textures\` prefix, e.g.
  `textures\actors\character\female\femalebody_3_msn.dds`.
- **persist** — see the trade‑off in §6.

> Extraction note: `RaceMenu.bsa` is SSE BSA v105 with **LZ4 frame** compression (NOT
> zlib). A ~60‑line Python BSA parser + `pip install lz4` (`lz4.frame.decompress`) pulls
> `NiOverride.psc` out; full source ended up at `C:\Temp\bsa_out\nioverride.psc`.

---

## 5. The implemented pipeline (for reference)

C++ `GetMuscleNormalPath(actor, slot)`:
1. gate: feature on? slot == body? (final version was body‑only)
2. race → folder + prefix (Argonian special‑cased)
3. dependency: `exists(base + "_11_msn.dds")` else `""`
4. `tone = GetToneScore(actor)` → `tier = clamp(round(10 - tone*0.09), 1, 10)`
5. `rel = base + "_" + tier + "_msn.dds"`; `exists()` check; return `rel`

Papyrus, inside `ApplyMorphs` (females), the *final working order* was:
```
ClearBodyMorphNames("OBody")
ApplyFemaleMorphs            ; SetBodyMorph(...) — the mesh muscle, KEPT
refit clothing (unequip/wait 0.05/equip body slot 0x04)
ApplyMuscleSkin             ; AddSkinOverrideString(...,0x04,9,1,path, persist=TRUE) — REGISTER only
UpdateModelWeight           ; ONE rebuild = morphs + persisted skin override together
```
Key lesson: **do NOT call `ApplySkinOverrides`** — its separate skin rebuild wiped the nude
body morphs. With `persist=true`, the single `UpdateModelWeight` reapplies both the morphs
and the persisted override in one pass, and being last it can never lose the body shape.

---

## 6. Problems & root causes (the saga)

| Problem | Root cause |
|---|---|
| Nude body lost its shape after applying normals | `ApplySkinOverrides` does a **separate skin rebuild** that drops the morphs. Fix: don't call it; let the final `UpdateModelWeight` carry both. |
| `persist=false` → normals never visible | A `persist=false` override is **dropped the moment the armor is unequipped** (SKEE doc). Since you must undress an NPC to see the body, the override is gone exactly when you'd see it. Only naturally‑nude NPCs would show it. |
| `persist=true` → save pollution | `persist=true` writes the override to the **SKEE co‑save**. It then sticks after uninstall / reverts and **fights RSV** (Racial Skin Variance), which broke a save badly. Needs an explicit cleanup path (toggle off → `RemoveSkinOverride` on reprocess; or `RemoveAllSkinBasedOverrides`, which also nukes RSV — bad). |
| Mid‑playthrough disruption | Changing the ESP's ESL flag, or the SKEE state, between tests required new games to test cleanly. |
| External `unequipall` "broke" generation | `unequipall` fires many unequip events → skin rebuilds that drop transient overrides and confuse testing. Test with naturally‑nude or revealing‑armor NPCs, never `unequipall`. |
| GBTNG (gender‑bender mod) noise | It hammered test NPCs every ~11s (`SetFactionRank` errors) and gender‑swapped them, confounding everything. Disable it when testing bodies. |

---

## 7. The final blocker (why it's shelved)

With the pipeline above the normals **did apply and survive undressing**, but the visual
was wrong: *"doesn't look like muscle; some parts have depth and others are flat/2D, like
the skin bugs out, anime‑like."*

That signature = a **normal‑map / shader incompatibility** at render time:
- likely a **tangent‑space vs the body's expected setup** mismatch, and/or
- the pack's normals were authored for a **specific body + its subsurface (SSS) / specular
  maps**; overriding only the **normal** (slot index 1) and leaving the actor's existing
  diffuse/subsurface/specular produces wrong lighting, especially over RSV/other skins.

This is below what NiOverride/Papyrus can fix — it's about the texture set as a whole and
the mesh's tangents.

---

## 8. Paths forward (for a future attempt)

1. **Override the whole texture SET, not just the normal.** Use
   `AddSkinOverrideTextureSet` (a TextureSet form) or override slots 0/1/7 together so the
   diffuse + normal + specular/subsurface are a matched set. Likely the real fix for the
   "2D" look. Requires authoring/looking up per‑tier TextureSets.
2. **Match the user's actual body/skin.** The pack's normals assume a base body; on RSV or
   custom skins they won't match. A correct version would need normals authored for (or
   blended with) the active skin — out of scope for a generic mod.
3. **Use the pack's native ESP** (weight‑slider) and instead make OBW's **weight** correlate
   with tone for the bodies that should look muscular — i.e. bend the existing mechanism
   rather than fight it. Loses the "muscular but light" case.
4. **Keep it mesh‑only (current state).** Honestly the best result so far: `MuscleAbs/Arms/
   Legs` morphs give real geometry‑based muscle, save‑safe, zero conflicts, no dependency.

If revisiting: start from §5's pipeline (it's the working mechanical baseline), and attack
§7 first with approach (1) — the TextureSet override — on a clean new game, with GBTNG
disabled, testing on a revealing‑armor NPC.
