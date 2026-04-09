# Creature Visual Quality Overhaul — Design Spec

**Sub-project A** of a two-part effort. This spec covers shader quality, visual fidelity, and evolvable morphology genome fields. Sub-project B (physics-coupled evolvable morphology) follows separately.

**Goal:** Close the visual gap between the current SDF creature renderer and the HTML demo reference (`~/Downloads/creature-renderer-v3.html`), making creatures look like living organisms rather than artificial blobs.

**Approach:** Full pipeline pass — shader quality overhaul, TBO data enrichment, colour formula widening, multi-segment Bezier flagella, and new genome morphology fields that Sub-project B will evolve on.

**Constraints:**
- Maintain 5000+ creature instanced rendering performance (3-tier LOD)
- All visual changes must degrade gracefully in simple/dot LOD tiers
- New genome fields must be mutable and initialized to sensible defaults
- Spike rendering must remain subtle and organic at all gene values
- Colour space must be wide enough for future evolution (camouflage, mimicry, warning coloration) but current defaults should produce attractive creatures

---

## 1. Body Shape & Silhouette

### Problem
The shader blends all 6 body segments with a single generous `blendK = avgRadius * 0.55`, smoothing everything into near-perfect ovals regardless of genome taper/elongation values.

### Changes

**Shader: Variable blend kernel.** Replace the uniform `blendK` with a per-segment kernel that tapers along the body:
```glsl
float segT = float(i) / float(segCount - 1);
float k = avgRadius * mix(0.55, 0.18, segT * segT * taperCurve);
```
Head-to-neck gets a generous blend (smooth dome), mid-to-tail segments get tighter blend (visible articulation). `taperCurve` is per-creature from TBO.

**Shader: Asymmetric head.** The head segment (index 0) uses a slightly elongated ellipse oriented along the forward axis. Elongation factor 1.0–1.15x, driven by diet (carnivores more pointed, herbivores rounder). Implemented by scaling the SDF distance computation along the forward axis before the circle test.

**TBO: Pack `taperCurve`.** Derived from `genome.morphology.bodyTaper` — maps the existing gene to a shader-readable float controlling blend kernel falloff.

**Genome: New field `blendStiffness` (0–1, default 0.5).** Scales the overall blend kernel magnitude. `taperCurve` controls how much the kernel falls off along the body (head-to-tail gradient), while `blendStiffness` scales the entire kernel uniformly (global smoothness). The combined formula is:
```glsl
float k = avgRadius * mix(0.55, 0.18, segT * segT * taperCurve) * mix(1.2, 0.5, blendStiffness);
```
Low stiffness + low taper = uniformly blobby. High stiffness + high taper = visibly segmented tail with smooth head. Added to `MorphologyGenome`, mutable with standard volatility. Sub-project B can tie this to structural integrity trade-offs (smoother = hydrodynamic, segmented = armored).

---

## 2. Flagella & Tails

### Problem
Tail filaments use `sin(gaitPhase + t*1.3)` which bends the whole filament uniformly — a rigid stick wiggling, not a wave propagating along a flexible flagellum.

### Changes

**Shader: 3-segment Bezier flagella.** Replace the 2-segment (root→mid→tip) approach with a 3-segment curve using 4 control points (root, P1, P2, tip). Each segment is a tapered capsule SDF blended with its neighbours:
```glsl
vec2 P0 = root;
vec2 P1 = root + tailAxis * fLen * 0.33 + perpAxis * wave1 * fLen * 0.35;
vec2 P2 = root + tailAxis * fLen * 0.66 + perpAxis * wave2 * fLen * 0.25;
vec2 P3 = root + tailAxis * fLen         + perpAxis * wave3 * fLen * 0.10;
```

**Shader: Spatial wave propagation.** Each control point gets its own wave phase that progresses along the flagellum length:
```glsl
float wave1 = sin(gaitPhase * waveSpeed + float(t) * 1.3 + 0.0) * amplitude;
float wave2 = sin(gaitPhase * waveSpeed + float(t) * 1.3 + 2.1) * amplitude * 0.8;
float wave3 = sin(gaitPhase * waveSpeed + float(t) * 1.3 + 4.2) * amplitude * 0.5;
```
Phase offsets (+2.1, +4.2) create a wave that propagates root-to-tip. Decreasing amplitude (1.0→0.8→0.5) makes the tip trail naturally.

**Genome: 3 new fields in `MorphologyGenome`:**
- `tailWaveSpeed` (0–1, default 0.5) — maps to 0.6–2.5x base wave rate. Faster = more energetic swimming stroke.
- `tailWaveLength` (0–1, default 0.5) — spatial frequency of the wave. Short wavelength = tight S-curves, long = lazy arcs.
- `tailTaper` (0–1, default 0.5) — how quickly the filament narrows root-to-tip. Thin tails are whip-like, thick ones are paddle-like.

**TBO: Pack `tailWaveSpeed`, `tailWaveLength`, `tailTaper`.** 3 additional floats per creature.

**Tail count:** The existing `tailFork` gene continues to drive tail count (1–4). With improved wave propagation, multiple tails become visually distinctive, creating real selection pressure in Sub-project B.

---

## 3. Fins

### Problem
Bilateral fins are tiny tapered capsules that barely break the silhouette.

### Changes

**Shader: Larger, curved fins.**
- Increase fin length multiplier from `0.6 + finArea * 0.8` to `0.8 + finArea * 1.4`.
- Add a curve — a 2-segment path with a midpoint that bows backward under thrust, creating a paddle/wing shape instead of a straight stick.
- Increase flap animation amplitude and tie more strongly to `thrustDrive` so fins visibly scull when the creature swims.
- Widen the `smin` blend radius where fin meets body for a smoother organic join.

**Genome: New field `finShape` (0–1, default 0.5).** Controls fin profile via a width multiplier on the fin capsule radius at the midpoint:
- Low (0.0–0.3): Narrow, blade-like fins — low drag, subtle sculling.
- Mid (0.3–0.7): Rounded paddle fins — balanced thrust/drag.
- High (0.7–1.0): Broad, fan-shaped fins — high thrust, more drag.

Sub-project B ties `finShape` directly to thrust coefficient and energy cost per stroke.

**TBO: Pack `finShape`.** 1 additional float.

---

## 4. Spikes — Organic Ridges

### Problem
Spikes are thin capsules protruding at 90° from the body with `min()` instead of `smin()` — they look mechanically bolted on. The radius (`sR * 0.06`) makes them hair-thin and the tip (`sw * 0.15`) is needle-sharp.

### Changes

**Shader: Smooth dorsal ridges.**
- Use `smin()` with a generous blend radius so ridges flow organically from the body surface.
- Increase base width from `sR * 0.06` to `sR * 0.14` and tip from `sw * 0.15` to `sw * 0.4` — stubby ridges, not needles.
- Reduce protrusion length: `sR * (0.05 + spikes * 0.15)` instead of `sR * (0.05 + spikes * 0.22)`.
- Add slight backward curve — ridges angle toward the tail using the forward axis to bias tip direction.
- Render on one side only per segment — all ridges on the same side (determined by the body's lateral axis), forming a continuous dorsal spine row rather than bilateral needle pairs.

**Subtlety scale:** At `spikes=0.3` (activation threshold), creatures show barely-visible bumps. At `spikes=1.0`, prominent but still rounded dorsal ridges. No genome value produces jarring needle spikes.

No new genome fields needed — existing `spikes` gene is sufficient.

---

## 5. Whiskers

### Problem
Whiskers are short and barely animated.

### Changes

**Shader: Longer, more alive.**
- Increase length from `headR * 1.2` to `headR * 1.8`.
- Add a second harmonic: `sin(gait*2.6) + sin(gait*4.1)*0.3` for complex organic trembling.
- Make tips thinner: `ww * 0.08` instead of `ww * 0.2`.
- Angle whiskers slightly forward — like real sensory organs reaching into the environment.

**Whisker count thresholds (from existing `sensorExpr`):**
- `sensorExpr < 0.25` → 0 whiskers
- `0.25–0.55` → 1 whisker (central)
- `0.55–0.80` → 2 whiskers (bilateral)
- `> 0.80` → 3 whiskers (bilateral + central)

No new genome fields needed.

---

## 6. Edge Quality

### Problem
All smoothstep transitions use `avgRadius`-proportional widths. Outlines vanish on small creatures and edges are universally soft.

### Changes

**Shader: Hybrid pixel-scale edges.**
- Body alpha: `smoothstep(0.7, -0.7, dOrganism)` — tight 1.4px anti-aliased edge.
- Outline: constant 1.2px band — `smoothstep(1.2, 1.2*0.15, abs(dOrganism))`.
- Rim/inner: pixel-floor with gentle radius scaling — `max(2.0, avgR*0.08)` for rim outer, `max(5.0, avgR*0.25)` for rim inner, `max(8.0, avgR*0.35)` for inner.
- Eye/mouth edges: fixed pixel-scale smoothstep (0.5–0.8px transitions).

**Shader: Outline colour.** Change from `rimCol * 0.6` to `hsl2rgb(hue, sat*0.4, lit*0.25)` — desaturated dark outline that reads as natural shadow edge.

---

## 7. Colour

### Problem
Saturation range `[0.46, 0.78]` and lightness range `[0.56, 0.76]` — every creature is pale lavender.

### Changes

**C++ packing: Widen HSL ranges.**
```cpp
// Saturation: [0.30, 0.92]
float baseSat = lerp(0.30f, 0.92f, genome.morphology.saturation);
d[33] = clamp01(baseSat + (armor * 0.08f + aggression * 0.06f - 0.07f));

// Lightness: [0.32, 0.72]
float baseLit = lerp(0.32f, 0.72f, genome.morphology.lightness);
d[34] = clamp01(baseLit + (energyLevel * 0.10f - 0.05f));
```

The genome provides the baseline; armor/aggression/energy apply as small dynamic offsets rather than dominating the formula.

**Genome: 2 new fields in `MorphologyGenome`:**
- `saturation` (0–1, default 0.55) — base saturation, evolvable.
- `lightness` (0–1, default 0.55) — base lightness, evolvable.

**Shader: Richer shading layers.**
- Inner glow: hue shift +0.06 (from +0.04), lightness shift +0.22 (from +0.16).
- Rim darkening: lightness -0.24 (from -0.18).
- SSS contribution: 0.25 (from 0.18), extended influence area.
- Energy glow: 0.20 (from 0.12).

---

## Summary of New Genome Fields

| Field | Range | Default | Purpose |
|-------|-------|---------|---------|
| `blendStiffness` | 0–1 | 0.5 | Body segmentation vs smoothness |
| `tailWaveSpeed` | 0–1 | 0.5 | Flagellum wave propagation rate |
| `tailWaveLength` | 0–1 | 0.5 | Flagellum spatial wave frequency |
| `tailTaper` | 0–1 | 0.5 | Flagellum root-to-tip narrowing |
| `finShape` | 0–1 | 0.5 | Fin profile (blade→paddle→fan) |
| `saturation` | 0–1 | 0.55 | Base colour saturation |
| `lightness` | 0–1 | 0.55 | Base colour lightness |

All fields are added to `MorphologyGenome`, initialized to defaults in `defaultGenome()`, and mutated with `mutateScalar()` at standard volatility.

## Summary of TBO Changes

Current stride: 56 floats (14 texels).
New floats added: `taperCurve`, `blendStiffness`, `tailWaveSpeed`, `tailWaveLength`, `tailTaper`, `finShape` = **6 floats**.
New stride: **64 floats (16 texels)** (62 used + 2 padding for vec4 alignment).

## Files Modified

| File | Changes |
|------|---------|
| `src/sim/Simulation.hpp` | Add 7 fields to `MorphologyGenome` |
| `src/sim/Simulation.cpp` | Initialize defaults, add mutation calls, derive traits |
| `src/render/CreatureSDF.hpp` | Update `kDataStride` to 64 |
| `src/render/CreatureSDF.cpp` | Pack new floats into TBO, update texel fetch offsets |
| `src/render/shaders/creature_sdf.frag` | All 6 quality improvements (body, flagella, fins, spikes, whiskers, edges, colour) |
| `src/render/shaders/creature_dot.frag` | Update texel offsets if data layout shifts |
