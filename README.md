# OBodyNG Weight

Procedural NPC weight **and** body randomization for **OBody NG** — for both **women
(CBBE 3BA)** and **men (HIMBO)**. Every NPC gets a unique, proportional body with no
BodySlide preset library required, and equipped clothing follows the generated shape.

## Features

- **Procedural body shapes** generated per NPC via SKEE morphs — no preset files needed.
  Women use CBBE 3BA sliders, men use HIMBO. Magnitudes calibrated to a 1900+ preset library.
- **Weight randomization** applied before the body is built, so face/hands match.
- **Realistic + fantasy mix** — most NPCs are grounded; a tunable minority are exaggerated.
- **Body traits** — independent per-NPC features. Women: busty, perky/saggy (size-derived),
  wide hips, hip dips, wasp waist, soft belly, thick thighs, thigh gap. Men: big/flat pecs,
  V-taper, barrel chest, gut, thick/skinny legs.
- **Muscle tone** — women have a tunable "athletic" fraction with visible abs/arm/leg
  definition (and a rare **snu snu** super-toned Amazon); men get tone automatically from
  their build. Suppressed by body fat, so it reads correctly.
- **Unusual bodies** — a rare out-of-distribution roll: ultra-petite/ultra-thick (women) or
  ultra-skinny/ultra-huge (men), disproportionate and atypical.
- **Re-roll hotkey** (`O`) — aim at an NPC for a brand-new body.
- **Clothing refit** — armor built with morphs follows the new shape.
- **Performance** — distance-aware lazy loading drains the morph queue gradually and
  nearest-first, so entering a crowded cell doesn't stutter.
- **Neck-seam safe** — weight is only applied before the head/body are built together.
- All logic, RNG and state live in the C++ SKSE plugin; Papyrus is a thin relay.

## MCM

Distribution mode (Seeded / Random / NPC Default), Bias, Seed, Morph intensity (master),
**Fantasy NPCs %**, **Unusual bodies %**, **Unusual breasts %**, **Athletic women %**, and
a Body Shape mode (Procedural Morphs / OBody Presets).

## Requirements

- Skyrim SE/AE + **SKSE64**
- **Address Library for SKSE Plugins**
- **OBody NG** (hard requirement — master of the plugin)
- **RaceMenu / SKEE** (NiOverride body morphs)
- **SkyUI** (MCM)
- A **CBBE / CBBE 3BA** female body and a **HIMBO** male body, both **built in BodySlide with
  "Build Morphs" checked** (the `.tri` data). Build your armors with morphs for clothing to follow.

## Installation

Install with a mod manager and enable `OBodyNGWeight.esp`. Load it after OBody NG. Open the
MCM, choose Procedural Morphs, and (recommended) set OBody's actor-selection hotkey to None so
`O` only triggers this mod's re-roll.

## How it works

OBody (which has both female and male preset databases) fires its actor-generated event; this
mod clears OBody's morphs and applies its own under key `OBW`, branching on sex. In OBody
Presets mode it leaves OBody's presets alone and only randomizes weight.

## Compatibility

- Female slider names target CBBE 3BA; male names target HIMBO. Other bodies use different
  slider names and would need the tables adjusted.

## Credits

- **OBody NG** — Aietos (original **OBody** by Sinhime)
- **RaceMenu / SKEE** — Expired · **SKSE64** — ianpatt, behippo, purplelunchbox
- **Address Library** — meh321 · **CommonLibSSE-NG** — CharmedBaryon (orig. Ryan-rsm-McKenzie)
- **SkyUI** — SkyUI Team · **CBBE / 3BA** — Ousnius & Caliente, Acro748 · **HIMBO** — Shino
- **BodySlide** — Ousnius & Caliente · **spdlog** — gabime
