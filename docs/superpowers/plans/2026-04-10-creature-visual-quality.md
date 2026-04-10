# Creature Visual Quality Overhaul — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make SDF-rendered creatures look like living organisms — organic shapes, animated flagella, rich colour, crisp edges — closing the gap with the HTML demo reference.

**Architecture:** 7 new genome fields in `MorphologyGenome` → derived into traits → packed into an expanded 64-float TBO stride (up from 56) → read by an overhauled `creature_sdf.frag` shader with improved body blending, 3-segment Bezier flagella, larger fins, organic ridge spikes, longer whiskers, pixel-scale edges, and wider colour ranges.

**Tech Stack:** C++20, GLSL 410, OpenGL 4.1 Core (macOS), SDL2, instanced rendering with float TBO.

**Spec:** `docs/superpowers/specs/2026-04-10-creature-visual-quality-design.md`

---

## File Structure

| File | Responsibility | Action |
|------|---------------|--------|
| `src/sim/Simulation.hpp` | Genome struct definitions | Modify: add 7 fields to `MorphologyGenome` |
| `src/sim/Simulation.cpp` | Genome defaults, mutation, trait derivation | Modify: initialize + mutate new fields, derive new traits |
| `src/render/CreatureSDF.hpp` | TBO stride constant | Modify: `kDataStride` 56→64 |
| `src/render/CreatureSDF.cpp` | TBO data packing | Modify: pack 6 new floats, update colour formulas |
| `src/render/shaders/creature_sdf.frag` | SDF fragment shader | Modify: all 6 visual quality improvements |
| `src/render/shaders/creature_dot.frag` | Dot LOD fragment shader | Modify: update texels-per-creature from 14→16 |

---

### Task 1: Add Genome Fields and Wire Mutation

**Files:**
- Modify: `src/sim/Simulation.hpp:62-81` (MorphologyGenome struct)
- Modify: `src/sim/Simulation.cpp:1268-1287` (defaultGenome)
- Modify: `src/sim/Simulation.cpp:1403-1420` (mutateGenome)

- [ ] **Step 1: Add 7 new fields to `MorphologyGenome`**

In `src/sim/Simulation.hpp`, add these fields after the existing `pattern` field (line 80):

```cpp
    float pattern = 0.5f;
    float blendStiffness = 0.5f;
    float tailWaveSpeed = 0.5f;
    float tailWaveLength = 0.5f;
    float tailTaper = 0.5f;
    float finShape = 0.5f;
    float saturation = 0.5f;
    float lightness = 0.5f;
};
```

- [ ] **Step 2: Set defaults in `defaultGenome()`**

In `src/sim/Simulation.cpp`, add after `genome.morphology.pattern = 0.48f;` (line 1287):

```cpp
    genome.morphology.blendStiffness = 0.5f;
    genome.morphology.tailWaveSpeed = 0.5f;
    genome.morphology.tailWaveLength = 0.5f;
    genome.morphology.tailTaper = 0.5f;
    genome.morphology.finShape = 0.5f;
    genome.morphology.saturation = 0.55f;
    genome.morphology.lightness = 0.55f;
```

- [ ] **Step 3: Add mutation calls in `mutateGenome()`**

In `src/sim/Simulation.cpp`, add after `mutateScalar(rng, volatility, child.morphology.pattern, 0.55f);` (line 1420):

```cpp
    mutateScalar(rng, volatility, child.morphology.blendStiffness);
    mutateScalar(rng, volatility, child.morphology.tailWaveSpeed);
    mutateScalar(rng, volatility, child.morphology.tailWaveLength);
    mutateScalar(rng, volatility, child.morphology.tailTaper);
    mutateScalar(rng, volatility, child.morphology.finShape);
    mutateScalar(rng, volatility, child.morphology.saturation);
    mutateScalar(rng, volatility, child.morphology.lightness);
```

- [ ] **Step 4: Build and run smoke test**

```bash
cmake --build build && ctest --test-dir build -R alife_smoke_seed1 --output-on-failure
```

Expected: Build succeeds. Smoke test passes (new fields have defaults, simulation logic unchanged).

- [ ] **Step 5: Commit**

```bash
git add src/sim/Simulation.hpp src/sim/Simulation.cpp
git commit -m "Add 7 morphology genome fields for visual quality overhaul

blendStiffness, tailWaveSpeed, tailWaveLength, tailTaper,
finShape, saturation, lightness — all mutable with standard
volatility. Defaults chosen for backward-compatible rendering."
```

---

### Task 2: Expand TBO Stride and Pack New Floats

**Files:**
- Modify: `src/render/CreatureSDF.hpp:40` (kDataStride)
- Modify: `src/render/CreatureSDF.cpp:405-503` (packAndClassify)
- Modify: `src/render/shaders/creature_dot.frag:12` (texels-per-creature)

- [ ] **Step 1: Update `kDataStride` to 64**

In `src/render/CreatureSDF.hpp`, change line 40:

```cpp
    static constexpr int kDataStride = 64;
```

- [ ] **Step 2: Pack new floats and update colour formulas in `packAndClassify()`**

In `src/render/CreatureSDF.cpp`, replace the section from `// [32] Hue` through `// [52-55] Reserved` (lines 458–494) with:

```cpp
        // [32] Hue
        d[32] = c.genome.morphology.hue;

        // [33] Saturation: genome-driven base with small dynamic offsets
        float baseSat = 0.30f + 0.62f * c.genome.morphology.saturation;
        d[33] = clamp01(baseSat + (armor * 0.08f + aggression * 0.06f - 0.07f));

        // [34] Lightness: genome-driven base with energy offset
        float baseLit = 0.32f + 0.40f * c.genome.morphology.lightness;
        d[34] = clamp01(baseLit + (energyLevel * 0.10f - 0.05f));

        // [35] Energy level
        d[35] = energyLevel;

        // [36] Diet
        d[36] = diet;

        // [37] Aggression (blended)
        d[37] = aggression;

        // [38-51] Genome morphology traits (14 values — same layout as before)
        const auto& morph = c.genome.morphology;
        d[38] = morph.finArea;
        d[39] = morph.finPlacement;
        d[40] = morph.tailLength;
        d[41] = morph.tailFlex;
        d[42] = morph.tailFork;
        d[43] = morph.jawLength;
        d[44] = morph.jawArc;
        d[45] = morph.sensorRange;
        d[46] = morph.sensorSpan;
        d[47] = morph.spikes;
        d[48] = morph.pattern;
        d[49] = morph.bodyTaper;
        d[50] = morph.armor;
        d[51] = morph.coreSize;

        // [52-57] New visual quality floats
        d[52] = morph.blendStiffness;
        d[53] = morph.tailWaveSpeed;
        d[54] = morph.tailWaveLength;
        d[55] = morph.tailTaper;
        d[56] = morph.finShape;
        d[57] = morph.bodyTaper;  // taperCurve (same as bodyTaper, shader-accessible alias)

        // [58-63] Reserved (already zeroed by memset)
```

- [ ] **Step 3: Update dot shader texels-per-creature**

In `src/render/shaders/creature_dot.frag`, change line 12:

```glsl
    return texelFetch(uCreatureData, creatureIdx * 16 + texelIdx);
```

- [ ] **Step 4: Update SDF shader texels-per-creature**

In `src/render/shaders/creature_sdf.frag`, change line 16:

```glsl
    return texelFetch(uCreatureData, creatureIdx * 16 + texelIdx);
```

- [ ] **Step 5: Build and run smoke test**

```bash
cmake --build build && ctest --test-dir build -R alife_smoke_seed1 --output-on-failure
```

Expected: Build succeeds. Smoke test passes. Rendering should look the same as before (new floats packed but not yet consumed by shader logic).

- [ ] **Step 6: Commit**

```bash
git add src/render/CreatureSDF.hpp src/render/CreatureSDF.cpp src/render/shaders/creature_dot.frag src/render/shaders/creature_sdf.frag
git commit -m "Expand TBO stride to 64 floats, pack new morphology data

Pack blendStiffness, tailWaveSpeed, tailWaveLength, tailTaper,
finShape, taperCurve into slots 52-57. Update texels-per-creature
from 14 to 16 in both shaders. Widen saturation range to
[0.30, 0.92] and lightness to [0.32, 0.72] with genome-driven
base values."
```

---

### Task 3: Body Shape — Variable Blend Kernel and Asymmetric Head

**Files:**
- Modify: `src/render/shaders/creature_sdf.frag:107-137` (Stage 1: Body core)

- [ ] **Step 1: Add fetch helpers for new TBO data**

In `src/render/shaders/creature_sdf.frag`, add after the existing `fetchFloat` function (after line 61):

```glsl
// ── New data fetch helpers for visual quality floats ──
float fetchBlendStiffness(int ci) { return fetchFloat(ci, 52); }
float fetchTailWaveSpeed(int ci)  { return fetchFloat(ci, 53); }
float fetchTailWaveLength(int ci) { return fetchFloat(ci, 54); }
float fetchTailTaper(int ci)      { return fetchFloat(ci, 55); }
float fetchFinShape(int ci)       { return fetchFloat(ci, 56); }
float fetchTaperCurve(int ci)     { return fetchFloat(ci, 57); }
```

- [ ] **Step 2: Replace body core blend with variable kernel**

In `src/render/shaders/creature_sdf.frag`, replace the body core section (from `// Compute average radius` through the body blend loop, approximately lines 123-137) with:

```glsl
    // Compute average radius for reference scale
    float avgRadius = 0.0;
    for (int i = 0; i < segCount; i++) {
        avgRadius += fetchSegRadius(ci, segIndices[i]);
    }
    avgRadius /= float(segCount);

    // Per-creature blend parameters
    float taperCurve = fetchTaperCurve(ci);
    float blendStiffness = fetchBlendStiffness(ci);

    for (int i = 0; i < segCount; i++) {
        int si = segIndices[i];
        vec2 segPos = fetchSegPos(ci, si);
        float segR = fetchSegRadius(ci, si);

        // Variable blend kernel: generous at head, tight at tail
        float segT = float(i) / max(float(segCount - 1), 1.0);
        float k = avgRadius
                * mix(0.55, 0.18, segT * segT * taperCurve)
                * mix(1.2, 0.5, blendStiffness);

        float d = sdCircle(vFragPos - segPos, segR);
        dBody = smin(dBody, d, k);
    }
```

- [ ] **Step 3: Add asymmetric head elongation**

Add this immediately after the body blend loop (after the new code from Step 2), before `float dOrganism = dBody;`:

```glsl
    // Asymmetric head: slight forward-axis elongation driven by diet
    if (uLodTier == 0) {
        float diet = fetchFloat(ci, 36);
        vec2 headPos = fetchSegPos(ci, 0);
        float headR = fetchSegRadius(ci, 0);
        vec2 fwd = fetchForwardAxis(ci);

        // Elongation factor: 1.0 (herbivore) to 1.15 (carnivore)
        float headElong = 1.0 + diet * 0.15;
        // Compress distance along forward axis to create ellipse
        vec2 toFrag = vFragPos - headPos;
        float fwdDist = dot(toFrag, fwd);
        vec2 squeezed = toFrag - fwd * fwdDist * (1.0 - 1.0 / headElong);
        float dHeadEllipse = length(squeezed) - headR;

        // Blend elongated head into body with generous kernel
        float headK = avgRadius * 0.5 * mix(1.2, 0.5, blendStiffness);
        dBody = smin(dBody, dHeadEllipse, headK);
    }
```

- [ ] **Step 4: Build and run visually**

```bash
cmake --build build && ./build/alife_sim --smoke-test --seed 1 --smoke-steps 100
```

Expected: Build succeeds. Creatures should now show more body shape variety — some more articulated toward the tail, some smoother. Carnivores should have slightly more pointed heads.

- [ ] **Step 5: Commit**

```bash
git add src/render/shaders/creature_sdf.frag
git commit -m "Body shape: variable blend kernel and asymmetric head

Blend kernel now tapers along body (generous at head, tight at
tail) controlled by taperCurve and blendStiffness per-creature.
Head segment gets subtle forward-axis elongation driven by diet."
```

---

### Task 4: Flagella — 3-Segment Bezier with Spatial Wave Propagation

**Files:**
- Modify: `src/render/shaders/creature_sdf.frag` (Stage 2: Tail filaments section, approximately lines 159-190)

- [ ] **Step 1: Replace tail filament rendering**

In `src/render/shaders/creature_sdf.frag`, replace the entire `// ── Tail filaments ──` section (from `int tailCount` through `dOrganism = smin(dOrganism, dTails, tailR * 0.4);`) with:

```glsl
        // ── Tail filaments — 3-segment Bezier with spatial wave ──
        int tailCount = 1 + int(tailFork * 2.0 + tailFlex * 0.5);
        tailCount = clamp(tailCount, 1, 4);

        vec2 tailSeg = fetchSegPos(ci, 5);
        float tailR = fetchSegRadius(ci, 5);
        vec2 tailDir = tailSeg - fetchSegPos(ci, 4);
        float tailDirLen = length(tailDir);
        vec2 tailAxis = (tailDirLen > 0.001) ? tailDir / tailDirLen : vec2(1.0, 0.0);
        vec2 tailPerp = vec2(-tailAxis.y, tailAxis.x);

        float waveSpeed = fetchTailWaveSpeed(ci);
        float waveLen = fetchTailWaveLength(ci);
        float tailTaperGene = fetchTailTaper(ci);

        // Map genome values to usable ranges
        float waveSpeedMul = 0.6 + waveSpeed * 1.9;   // [0.6, 2.5]
        float waveLenMul = 1.2 + waveLen * 3.0;        // [1.2, 4.2] phase offset per segment
        float amplitude = (0.14 + tailFlex * 0.42) * (0.28 + thrustDrive * 0.72);

        float dTails = 1e9;
        for (int t = 0; t < 4; t++) {
            if (t >= tailCount) break;
            float spread = (float(t) - float(tailCount - 1) * 0.5) * 0.35;
            vec2 root = tailSeg + tailAxis * tailR * 0.6
                      + tailPerp * spread * tailR;

            float fLen = tailLength * avgRadius * 2.5;

            // Spatial wave — phase progresses along length
            float basePhase = gaitPhase * waveSpeedMul * 1.18 + float(t) * 1.3;
            float w1 = sin(basePhase) * amplitude;
            float w2 = sin(basePhase + waveLenMul * 0.33) * amplitude * 0.8;
            float w3 = sin(basePhase + waveLenMul * 0.66) * amplitude * 0.5;

            // 4 control points for S-curve
            vec2 P0 = root;
            vec2 P1 = root + tailAxis * fLen * 0.33 + tailPerp * w1 * fLen * 0.35;
            vec2 P2 = root + tailAxis * fLen * 0.66 + tailPerp * w2 * fLen * 0.25;
            vec2 P3 = root + tailAxis * fLen         + tailPerp * w3 * fLen * 0.10;

            // Tapered widths — root to tip narrowing
            float rootW = tailR * 0.28;
            float tipW = tailR * mix(0.10, 0.03, tailTaperGene);

            // 3 tapered capsule segments blended smoothly
            float w01 = mix(rootW, rootW * 0.6, 0.5);
            float w12 = mix(rootW * 0.6, tipW * 2.0, 0.5);
            float s1 = sdTaperedCapsule(vFragPos, P0, P1, rootW, w01);
            float s2 = sdTaperedCapsule(vFragPos, P1, P2, w01, w12);
            float s3 = sdTaperedCapsule(vFragPos, P2, P3, w12, tipW);
            float dFil = smin(s1, s2, rootW * 0.6);
            dFil = smin(dFil, s3, rootW * 0.4);
            dTails = min(dTails, dFil);
        }
        dOrganism = smin(dOrganism, dTails, tailR * 0.4);
```

- [ ] **Step 2: Build and run visually**

```bash
cmake --build build && ./build/alife_sim --smoke-test --seed 1 --smoke-steps 100
```

Expected: Build succeeds. Tails should now show S-curve wave propagation — the wave travels from root to tip. Multiple-tail creatures should look dramatically more alive.

- [ ] **Step 3: Commit**

```bash
git add src/render/shaders/creature_sdf.frag
git commit -m "Flagella: 3-segment Bezier with spatial wave propagation

Replace rigid tail filaments with 4-control-point S-curves.
Wave propagates root-to-tip with per-creature speed and
wavelength. Tail taper gene controls root-to-tip narrowing."
```

---

### Task 5: Fins — Larger, Curved, with Shape Gene

**Files:**
- Modify: `src/render/shaders/creature_sdf.frag` (Stage 2: Dorsal fin section, approximately lines 192-213)

- [ ] **Step 1: Replace dorsal fin rendering**

Replace the entire `// ── Dorsal fin ──` section (from `if (finArea > 0.1)` through its closing brace) with:

```glsl
        // ── Dorsal fin — curved paddles with shape gene ──
        if (finArea > 0.1) {
            int finSeg = 1 + int(finPlacement * 3.0);
            finSeg = clamp(finSeg, 1, 4);
            vec2 finPos = fetchSegPos(ci, finSeg);
            float finR = fetchSegRadius(ci, finSeg);
            vec2 finDir = fetchSegPos(ci, finSeg - 1) - finPos;
            float finDirLen = length(finDir);
            vec2 finAxis = (finDirLen > 0.001) ? finDir / finDirLen : vec2(1.0, 0.0);
            vec2 finPerp = vec2(-finAxis.y, finAxis.x);

            float finShapeGene = fetchFinShape(ci);
            float flap = sin(gaitPhase * 1.36) * (0.12 + finArea * 0.48)
                       * (0.25 + thrustDrive * 0.75);

            for (int side = 0; side < 2; side++) {
                float sSign = (side == 0) ? -1.0 : 1.0;
                vec2 finBase = finPos + finPerp * sSign * finR * 0.7;

                // Fin length: larger than before
                float fLength = finR * (0.8 + finArea * 1.4);
                // Midpoint bows backward under thrust for paddle shape
                vec2 finMid = finBase
                    + finPerp * sSign * fLength * 0.55
                    + finAxis * (flap * finR * sSign - fLength * 0.12);
                vec2 finTip = finBase
                    + finPerp * sSign * fLength
                    + finAxis * flap * finR * sSign * 0.6;

                // Width varies by finShape gene: blade (narrow) to fan (broad)
                float baseW = finR * 0.15 * finArea;
                float midW = baseW * (0.6 + finShapeGene * 1.4);  // paddle bulge at mid
                float tipW = baseW * 0.25;

                float d1 = sdTaperedCapsule(vFragPos, finBase, finMid, baseW, midW);
                float d2 = sdTaperedCapsule(vFragPos, finMid, finTip, midW, tipW);
                float dFin = smin(d1, d2, midW * 0.8);
                // Generous blend where fin meets body
                dOrganism = smin(dOrganism, dFin, baseW * 2.4);
            }
        }
```

- [ ] **Step 2: Build and run visually**

```bash
cmake --build build && ./build/alife_sim --smoke-test --seed 1 --smoke-steps 100
```

Expected: Build succeeds. Fins should be much more prominent — paddle-shaped, with visible flap animation when creatures swim. `finShape` gene creates variety from narrow blades to broad fans.

- [ ] **Step 3: Commit**

```bash
git add src/render/shaders/creature_sdf.frag
git commit -m "Fins: larger curved paddles with finShape gene

Fin length increased 1.7x, midpoint bows backward for paddle
shape. Width at midpoint varies by finShape gene (blade to fan).
Generous smin blend for organic body join."
```

---

### Task 6: Spikes — Organic Dorsal Ridges

**Files:**
- Modify: `src/render/shaders/creature_sdf.frag` (Stage 2: Spikes section, approximately lines 237-256)

- [ ] **Step 1: Replace spike rendering with organic ridges**

Replace the entire `// ── Spikes ──` section (from `if (spikes > 0.28)` through its closing brace) with:

```glsl
        // ── Spikes — smooth dorsal ridges ──
        if (spikes > 0.28) {
            vec2 headFwd = fetchForwardAxis(ci);
            // Determine dorsal side consistently (perpendicular to forward, pick one side)
            vec2 dorsalDir = vec2(-headFwd.y, headFwd.x);

            for (int s = 1; s <= 4; s++) {
                vec2 sPos = fetchSegPos(ci, s);
                float sR = fetchSegRadius(ci, s);
                vec2 sDir = fetchSegPos(ci, s - 1) - sPos;
                float sDirLen = length(sDir);
                vec2 sAxis = (sDirLen > 0.001) ? sDir / sDirLen : vec2(1.0, 0.0);

                // Ridge protrudes from dorsal side, angled slightly backward
                float ridgeLen = sR * (0.05 + spikes * 0.15);
                float ridgeW = sR * 0.14;

                vec2 ridgeBase = sPos + dorsalDir * sR * 0.8;
                vec2 ridgeTip = ridgeBase + dorsalDir * ridgeLen
                              - sAxis * ridgeLen * 0.35;  // backward curve

                // Organic blend into body
                dOrganism = smin(dOrganism,
                    sdTaperedCapsule(vFragPos, ridgeBase, ridgeTip, ridgeW, ridgeW * 0.4),
                    ridgeW * 2.0);
            }
        }
```

- [ ] **Step 2: Build and run visually**

```bash
cmake --build build && ./build/alife_sim --smoke-test --seed 1 --smoke-steps 100
```

Expected: Build succeeds. Spikes appear as subtle, rounded dorsal ridges that flow organically from the body. Even at `spikes=1.0`, they look like natural spine bumps rather than needles.

- [ ] **Step 3: Commit**

```bash
git add src/render/shaders/creature_sdf.frag
git commit -m "Spikes: soft dorsal ridges replacing needle spikes

Single dorsal row using smin blend, wider bases, backward
curve, reduced protrusion length. Organic at all gene values."
```

---

### Task 7: Whiskers — Longer with Dual Harmonic Animation

**Files:**
- Modify: `src/render/shaders/creature_sdf.frag` (Stage 2: Whiskers section, approximately lines 215-234)

- [ ] **Step 1: Replace whisker rendering**

Replace the entire `// ── Whiskers ──` section (from `int whiskerCount` through its closing braces) with:

```glsl
        // ── Whiskers — sensory antennae ──
        float sensorExpr2 = sensorExpr;
        int whiskerCount = (sensorExpr2 > 0.80) ? 3 :
                           (sensorExpr2 > 0.55) ? 2 :
                           (sensorExpr2 > 0.25) ? 1 : 0;
        if (whiskerCount > 0) {
            vec2 headPos2 = fetchSegPos(ci, 0);
            float headR = fetchSegRadius(ci, 0);
            vec2 headAxis = fetchForwardAxis(ci);
            vec2 headPerp = vec2(-headAxis.y, headAxis.x);

            for (int w = 0; w < 3; w++) {
                if (w >= whiskerCount) break;

                float wSide;
                if (whiskerCount == 1) wSide = 0.0;  // central
                else if (whiskerCount == 2) wSide = (w == 0) ? -1.0 : 1.0;
                else wSide = (w == 0) ? -1.0 : ((w == 1) ? 0.0 : 1.0);

                vec2 wBase = headPos2 + headPerp * wSide * headR * 0.5
                           + headAxis * headR * 0.15;  // angled forward

                // Dual harmonic trembling
                float wiggle = sin(gaitPhase * 2.6 + float(w) * 1.4) * 0.18
                             + sin(gaitPhase * 4.1 + float(w) * 2.3) * 0.06;

                vec2 wTip = wBase
                    + headPerp * wSide * headR * 1.2
                    + headAxis * (headR * 0.6 + wiggle * headR)  // forward reach
                    ;
                // For central whisker (wSide=0), extend purely forward
                if (abs(wSide) < 0.1) {
                    wTip = wBase + headAxis * headR * 1.8
                         + headPerp * wiggle * headR * 0.5;
                }

                float ww = headR * 0.05;
                dOrganism = smin(dOrganism,
                    sdTaperedCapsule(vFragPos, wBase, wTip, ww, ww * 0.08),
                    ww * 1.5);
            }
        }
```

- [ ] **Step 2: Build and run visually**

```bash
cmake --build build && ./build/alife_sim --smoke-test --seed 1 --smoke-steps 100
```

Expected: Build succeeds. Whiskers are longer, thinner at tips, angled forward, with complex trembling animation. High-sensor creatures show up to 3 whiskers.

- [ ] **Step 3: Commit**

```bash
git add src/render/shaders/creature_sdf.frag
git commit -m "Whiskers: longer antennae with dual harmonic trembling

Up to 3 whiskers, forward-angled, dual-harmonic wave animation,
delicate tapered tips. Thresholds widened for more whisker variety."
```

---

### Task 8: Edge Quality — Pixel-Scale Smoothstep and Outline

**Files:**
- Modify: `src/render/shaders/creature_sdf.frag` (Stage 5: Colour composition, approximately lines 325-440)

- [ ] **Step 1: Replace smoothstep transitions and outline**

In `src/render/shaders/creature_sdf.frag`, replace the colour composition section (from `float bodyAlpha` through `col = mix(col, rimCol * 0.6, outline * 0.85);` approximately lines 336-399) with:

```glsl
    // ── Pixel-scale edge quality ──
    float aa = 0.7;  // anti-alias half-width in pixels
    float bodyAlpha = smoothstep(aa, -aa, dOrganism);

    // Hybrid rim/inner: pixel floor with gentle radius scaling
    float rimOuter = max(2.0, avgRadius * 0.08);
    float rimInner = max(5.0, avgRadius * 0.25);
    float rim = smoothstep(0.0, -rimOuter, dOrganism)
              * (1.0 - smoothstep(-rimOuter, -rimInner, dOrganism));
    float inner = smoothstep(-1.5, -max(8.0, avgRadius * 0.35), dOrganism);

    // Constant-width outline (1.2px)
    float outW = 1.2;
    float outline = smoothstep(outW, outW * 0.15, abs(dOrganism));

    vec3 col = mix(baseCol, innerCol, inner * 0.55);
    col = mix(col, rimCol, rim * 0.5);
    col += energyCol * energy * 0.20 * inner;

    // Outline: desaturated dark shadow edge
    vec3 outlineCol = hsl2rgb(hue, sat * 0.4, lit * 0.25);
    col = mix(col, outlineCol, outline * 0.85);
```

- [ ] **Step 2: Update shading layer intensities**

In the same shader, update the inner/rim colour definitions (approximately lines 332-334) to:

```glsl
    vec3 baseCol = hsl2rgb(hue, sat, lit);
    vec3 rimCol = hsl2rgb(hue - 0.03, sat * 1.1, lit - 0.24);
    vec3 innerCol = hsl2rgb(hue + 0.06, sat * 0.7, lit + 0.22);
    vec3 energyCol = hsl2rgb(hue + 0.1, 1.0, 0.72);
```

- [ ] **Step 3: Update SSS contribution**

Find the subsurface scatter section and update the contribution multiplier from `0.18` to `0.25`, and extend the influence:

```glsl
    // Subsurface scatter near head
    if (uLodTier == 0) {
        vec2 hp = fetchSegPos(ci, 0);
        float headD = sdCircle(vFragPos - hp, fetchSegRadius(ci, 0));
        float sss = smoothstep(0.0, -avgRadius * 0.7, headD)
                  * smoothstep(-avgRadius * 0.7, 0.0, headD + avgRadius * 0.35);
        col += hsl2rgb(hue + 0.08, 0.5, 0.9) * sss * 0.25 * (0.5 + energy * 0.5);
    }
```

- [ ] **Step 4: Update eye and mouth edge transitions**

Find the eye rendering section and replace the smoothstep values:

```glsl
    // Eyes (full LOD only)
    if (uLodTier == 0) {
        float aggro2 = fetchFloat(ci, 37);
        float eyeAlpha = smoothstep(0.6, -0.6, dEyes);
        float eyeInner2 = smoothstep(0.0, -eyeR, dEyes);
        float pupilA = smoothstep(0.5, -0.5, dPupils);
        float hlA = smoothstep(0.3, -0.3, dHighlights);

        // Sclera
        col = mix(col, vec3(0.95, 0.96, 0.97), eyeAlpha * 0.97);
        // Iris
        vec3 irisCol = hsl2rgb(hue * (1.0 - aggro2 * 0.5) + aggro2 * 0.04, 0.7, 0.45);
        col = mix(col, irisCol, eyeAlpha * eyeInner2 * 0.85);
        // Pupil
        col = mix(col, vec3(0.04, 0.05, 0.07), pupilA * 0.95);
        // Catchlight
        col = mix(col, vec3(1.0), hlA * 0.92);
        // Eye outline — pixel-scale
        float eyeOutline = smoothstep(0.8, 0.1, abs(dEyes));
        col = mix(col, outlineCol * 0.8, eyeOutline * 0.7);
    }
```

Find the mouth section and update lip edge:

```glsl
        float lipEdge = smoothstep(1.2, 0.2, abs(dMouth)) * smoothstep(aa, -aa, dBody);
        col = mix(col, rimCol * 0.75, lipEdge * 0.5);
```

- [ ] **Step 5: Build and run visually**

```bash
cmake --build build && ./build/alife_sim --smoke-test --seed 1 --smoke-steps 100
```

Expected: Build succeeds. Creatures should have much crisper silhouettes with defined outlines at every size. Richer colour depth from deeper rim, brighter inner glow, and stronger subsurface scatter.

- [ ] **Step 6: Commit**

```bash
git add src/render/shaders/creature_sdf.frag
git commit -m "Edge quality: pixel-scale smoothstep and richer shading

Tight 1.4px anti-aliased edges, constant 1.2px outline,
desaturated shadow outline colour. Deeper rim (-0.24 lit),
brighter inner glow (+0.22 lit), stronger SSS (0.25), and
energy glow (0.20). Pixel-scale eye and mouth edges."
```

---

### Task 9: AABB Margin Expansion for New Appendages

**Files:**
- Modify: `src/render/CreatureSDF.cpp` (AABB computation in packAndClassify, approximately lines 359-370)

The larger fins and longer whiskers may extend beyond the current AABB computed from segment positions + radii alone. If the quad is too tight, appendages get clipped at the edges.

- [ ] **Step 1: Expand AABB margins**

In `src/render/CreatureSDF.cpp`, find the AABB computation in the classification loop and add margin after the segment loop:

Find this code (approximately):
```cpp
        // Classify into LOD tier by screen-space head diameter
        float headScreenDiameter = 2.0f * cc.segRadiiScreen[0];
```

Insert just before that line:

```cpp
        // Expand AABB for appendages (fins, whiskers, tails extend beyond segment radii)
        float appendageMargin = avgScreenRadius * 1.8f;
        minX -= appendageMargin;
        minY -= appendageMargin;
        maxX += appendageMargin;
        maxY += appendageMargin;
```

We need an `avgScreenRadius` variable. Add this right after the segment loop closes:

```cpp
        float avgScreenRadius = 0.0f;
        for (int s = 0; s < kBodySegments; ++s) {
            avgScreenRadius += cc.segRadiiScreen[s];
        }
        avgScreenRadius /= static_cast<float>(kBodySegments);
```

- [ ] **Step 2: Build and run visually**

```bash
cmake --build build && ./build/alife_sim --smoke-test --seed 1 --smoke-steps 100
```

Expected: Build succeeds. Appendages (especially long tails and whiskers) should no longer clip at quad edges. May have slightly more overdraw but within performance budget.

- [ ] **Step 3: Commit**

```bash
git add src/render/CreatureSDF.cpp
git commit -m "Expand AABB margins to prevent appendage clipping

Add 1.8x avgRadius margin around segment-derived AABB to
accommodate longer tails, larger fins, and extended whiskers."
```

---

### Task 10: Final Smoke Test and Visual QA

**Files:** None (verification only)

- [ ] **Step 1: Run full smoke test suite**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: All 4 tests pass (smoke, benchmark, round-trip, determinism).

- [ ] **Step 2: Visual QA checklist**

Launch the sim and verify:

```bash
./build/alife_sim
```

Check each quality gap:
1. ☐ Body shapes show variety (some blobby, some articulated)
2. ☐ Tails have S-curve wave propagation (wave travels root-to-tip)
3. ☐ Fins are prominent paddle shapes that flap with thrust
4. ☐ Spikes appear as smooth dorsal ridges (no needles)
5. ☐ Whiskers are long, delicate, trembling antennae
6. ☐ Edges are crisp with thin dark outlines at all zoom levels
7. ☐ Colour range is wider (some dark creatures, some vivid, some pale)
8. ☐ No appendage clipping at quad edges
9. ☐ Simple LOD tier still renders correctly (3-segment blend)
10. ☐ Dot LOD tier still renders correctly (coloured circles)
11. ☐ Selection overlay still aligns with creatures
12. ☐ UI text and panels render correctly
13. ☐ Performance acceptable at 5000+ creatures

- [ ] **Step 3: Commit any final adjustments**

If visual tuning is needed (blend kernel constants, wave amplitudes, colour offsets), make adjustments and commit:

```bash
git add -A
git commit -m "Visual QA: tune shader constants after testing"
```
