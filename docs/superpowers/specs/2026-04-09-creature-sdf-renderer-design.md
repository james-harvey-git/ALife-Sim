# Creature SDF Renderer Design

## Summary

Replace the current geometry-based creature rendering in `Renderer.cpp` with a dedicated SDF (Signed Distance Field) renderer module that produces smooth, organic creature visuals matching the quality of the `creature-renderer-v3.html` WebGL demo, while scaling to populations of 5,000+ creatures via instanced rendering and a 3-tier LOD system.

## Goals

1. **Smooth organic silhouettes** — body parts blend seamlessly via `smin()`, no visible segment boundaries or stacked primitives.
2. **Rich shading** — rim lighting, subsurface scatter hints, inner glow, energy-responsive tinting. Creatures look like living translucent organisms.
3. **Full feature vocabulary** — diet-dependent mouth carving, tail filaments, dorsal fins, spikes, whiskers, multi-layer eyes, body markings. Extensible for future limbs, horns, and new appendage types.
4. **Physics-first truthfulness** — the SDF body follows the 6-segment articulated chain from the simulation. The creature looks exactly like what the physics says it is.
5. **Population scalability** — instanced rendering with a float data texture handles thousands of creatures at 3 draw calls per frame.
6. **Graceful LOD degradation** — full SDF quality for nearby creatures, simplified SDF for medium distance, colored dots for distant. No visual popping.

## Non-Goals

- Replacing environment, HUD, or overlay rendering (those stay in `Renderer.cpp`).
- Adding new genome traits (limbs, horns). The architecture supports them but implementation is deferred.
- Compute shaders or Metal — we stay within OpenGL 4.1 Core on macOS.

## Architecture

### Module Structure

New files:

- `src/render/CreatureSDF.hpp` — public interface
- `src/render/CreatureSDF.cpp` — instanced rendering setup, data packing, draw calls, embedded GLSL shaders

The shaders are embedded as `constexpr const char*` string literals inside `CreatureSDF.cpp`, matching the existing convention in `Renderer.cpp`. They can be extracted to standalone `.vert`/`.frag` files later if they grow large enough to warrant it.

### Public Interface

```cpp
class CreatureSDF {
public:
    bool initialize();
    void shutdown();

    // Called once per frame by Renderer::draw()
    void render(
        const Simulation& sim,
        const SDL_FRect& worldViewport,
        float cameraZoom,
        const Vec2& cameraCenter
    );

private:
    GLuint sdfProgram_;          // full + simple SDF (LOD selected by uniform)
    GLuint dotProgram_;          // trivial dot shader
    GLuint vao_, vbo_;           // unit quad (4 vertices), reused for all instances
    GLuint dataTexture_;         // TBO holding per-creature packed floats
    GLuint dataBuffer_;          // GL buffer backing the TBO
    int dataStride_;             // floats per creature (56)
    int textureCapacity_;        // current max creatures before realloc

    // CPU-side buffers
    std::vector<float> packedData_;
    std::vector<float> quadBounds_;  // per-instance screen AABB

    void packCreatureData(const Simulation& sim, float worldScale, const Vec2& camera);
    void classifyLOD(float cameraZoom);
};
```

### Integration with Renderer

`Renderer` owns a `CreatureSDF` member:

```cpp
// In Renderer.hpp
CreatureSDF creatureSdf_;

// In Renderer::initialize()
creatureSdf_.initialize();

// In Renderer::draw() — replaces the ~600-line drawCreature lambda
creatureSdf_.render(sim, worldViewport_, cameraZoom_, cameraCenter_);

// In Renderer::shutdown()
creatureSdf_.shutdown();
```

Selection debug overlays (sensor arc, bite arc, collision ring, joint strain lines, velocity trail) remain as geometry in `Renderer.cpp` — they are debug UI, not creature appearance.

### OpenGL Version Bump

The GL context is bumped from 3.2 Core to **4.1 Core** (GLSL 410). This gives us `glDrawArraysInstancedBaseInstance` for cleaner LOD batching — a single VBO with all LOD tiers packed contiguously, drawn as three instanced calls with base-instance offsets.

The existing shaders in `Renderer.cpp` are bumped from `#version 150` to `#version 410` (backwards compatible — no code changes needed, just the version directive).

## Data Pipeline

### Per-Frame Flow

```
Simulation state
    │
    ▼
CPU: LOD classification (screen-size bucketing)
    │
    ▼
CPU: Pack creature data into float buffer (per LOD tier, contiguous)
    │
    ▼
CPU → GPU: glBufferSubData upload to TBO
    │
    ▼
GPU: 3 instanced draw calls (full SDF, simple SDF, dot)
    │
    ▼
Framebuffer: creatures composited over environment via alpha blending
```

### LOD Classification

For each living creature, compute screen-space diameter:

```
screenDiameter = 2.0 * creature.bodyRadii[0] * worldScale * cameraZoom
```

Classify:

| Tier | Screen Diameter | Shader | Features |
|------|----------------|--------|----------|
| Full SDF | > 30px | `sdfProgram_` with `u_lodTier = 0` | 6-segment body, all appendages, mouth, eyes, full shading |
| Simple SDF | 8–30px | `sdfProgram_` with `u_lodTier = 1` | 3-segment body (head/mid/tail), base colour + rim + outline |
| Dot | < 8px | `dotProgram_` | Coloured ellipse from genome hue + energy brightness |

Hysteresis band of ±2px on each threshold prevents popping during zoom.

Off-screen creatures (bounding box entirely outside viewport, accounting for toroidal wrapping) are culled and not packed.

### Data Texture Layout

Each creature is encoded as a fixed stride of **56 floats (224 bytes)** in a GL_TEXTURE_BUFFER (TBO) backed by GL_RGBA32F:

| Offset | Count | Content |
|--------|-------|---------|
| 0–11 | 12 | Segment screen positions (6 x vec2) |
| 12–17 | 6 | Segment radii in screen pixels |
| 18–19 | 2 | Head screen position (cached for eye/mouth placement) |
| 20–21 | 2 | Forward axis (heading unit vector) |
| 22 | 1 | Heading angle |
| 23 | 1 | Gait phase (creature-local animation time) |
| 24 | 1 | Thrust drive (0–1) |
| 25 | 1 | Bite drive (0–1) |
| 26–31 | 6 | Segment integrity (1.0 = undamaged, 0.0 = destroyed) |
| 32 | 1 | Hue (0–360) |
| 33 | 1 | Saturation (0–1) |
| 34 | 1 | Lightness (0–1) |
| 35 | 1 | Energy level (0–1) |
| 36 | 1 | Diet (0 = herbivore, 1 = carnivore) |
| 37 | 1 | Aggression (0–1) |
| 38 | 1 | Fin area |
| 39 | 1 | Fin placement (segment index, 0–1 normalized) |
| 40 | 1 | Tail length |
| 41 | 1 | Tail flex |
| 42 | 1 | Tail fork |
| 43 | 1 | Jaw length |
| 44 | 1 | Jaw arc |
| 45 | 1 | Sensor range (drives eye count) |
| 46 | 1 | Sensor expressiveness (drives whisker count, eye spread) |
| 47 | 1 | Spikes (0–1) |
| 48 | 1 | Pattern (0–1) |
| 49 | 1 | Body taper |
| 50 | 1 | Armor |
| 51 | 1 | Core size |
| 52–55 | 4 | Reserved (future: limb count, limb length, horn size, etc.) |

Segment positions are stored in **screen-pixel space** — the CPU applies the camera/world transform before packing. This means the shader works entirely in pixel coordinates, making anti-aliasing trivial (`smoothstep` over 1px).

Memory: 5,000 creatures x 224 bytes = 1.12 MB. Negligible.

The TBO uses `GL_RGBA32F` internal format, so each `texelFetch` returns a `vec4` (4 floats). With 56 floats per creature, the shader needs **14 `texelFetch` calls** per creature to read all data. The base texel offset for instance `i` is `i * 14`. This is a modest texture bandwidth cost — well within Apple Silicon's unified memory throughput.

### Per-Instance Quad Bounds

In addition to the data texture, each instance needs a screen-space bounding quad. This is passed via a separate instanced vertex attribute buffer:

- 4 floats per instance: `(minX, minY, maxX, maxY)` in screen pixels
- The vertex shader expands the unit quad to these bounds

The quad AABB is computed on the CPU by taking the bounding box of all screen-space segment positions, padded by the maximum segment radius plus appendage extent (estimated from tail length, fin area, etc.).

## SDF Shader Design

### Composition Pipeline

The fragment shader is a linear sequence of SDF operations:

**Stage 1 — Body Core (6-segment blend)**

Read 6 segment positions and radii from the data texture. Evaluate `sdCircle()` for each. Blend all 6 with `smin()` to produce `dBody` — a single smooth organic envelope that follows the articulated physics chain.

```glsl
float dBody = 1e9;
float blendK = avgRadius * 0.55;  // smooth-minimum blend radius
for (int i = 0; i < 6; i++) {
    vec2 seg = fetchSegPos(i);
    float r = fetchSegRadius(i);
    dBody = smin(dBody, sdCircle(fragPos - seg, r), blendK);
}
```

The `smin` blend radius is proportional to segment size, producing tighter blends for small creatures and smoother blends for large ones.

**Stage 2 — Appendages (smooth union)**

Each appendage type is an SDF primitive blended into the body via `smin()`:

- **Tail filaments** (1–4 based on `tailFork` + `tailFlex`): Tapered capsules from tail segment, animated with sine wave driven by gait phase and thrust drive.
- **Dorsal fin** (if `finArea > 0.1`): Capsule pair at `finPlacement` segment, animated flapping.
- **Whiskers** (1–2 based on `sensorExpressiveness`): Thin capsules from head sides, animated wiggle.
- **Spikes** (if `spikes > 0.28`): Small triangles protruding from body segments.
- **Body markings** (if `pattern < 0.5`): Not SDF-blended — rendered as colour variation in Stage 5.

```glsl
float dOrganism = dBody;
dOrganism = smin(dOrganism, dTailFilaments, tailWidth * 1.4);
dOrganism = smin(dOrganism, dDorsalFin, finWidth * 1.8);
// ... etc
```

Future appendage types (limbs, horns) follow the same pattern: compute SDF, `smin()` into `dOrganism`.

**Stage 3 — Subtractive features (mouth carving)**

The mouth is an SDF subtracted from the organism:

```glsl
dOrganism = max(dOrganism, -dMouth);
```

Mouth shape morphs by diet:
- Herbivore (diet ≈ 0): Wide rounded ellipse — sucker opening
- Carnivore (diet ≈ 1): Narrow horizontal lens with pointed ends

Bite phase animates vertical gape. `dMouth` is retained for interior colouring in Stage 5.

**Stage 4 — Overlay features (eyes)**

Eyes are not blended into `dOrganism`. They are separate SDFs rendered during colour composition:
- 1–3 eyes based on sensor range
- Each eye: `sdCircle` for sclera, smaller for iris, smaller for pupil, offset for catchlight
- Positioned relative to head segment along the forward axis

**Stage 5 — Colour composition**

All shading is derived from the SDF distance value `dOrganism`:

| Effect | Technique |
|--------|-----------|
| Anti-aliased edge | `smoothstep(1.0, -1.0, dOrganism)` → alpha |
| Outline | `smoothstep` band at `d ≈ 0`, darker rim colour |
| Rim lighting | Narrow band just inside surface, hue-shifted |
| Base colour | Genome hue/saturation/lightness via HSL→RGB |
| Inner glow | Deeper inside body, lighter + desaturated |
| Subsurface scatter | Warm tint near head, modulated by energy |
| Energy highlight | Additive warm glow, proportional to energy level |
| Damage | Per-segment integrity modulates local brightness/hue |
| Mouth interior | Dark fleshy cavity where `dMouth < 0`, diet-tinted |
| Eyes | Layered sclera → iris → pupil → catchlight |
| Body markings | Colour bands at segment intervals if `pattern` is expressed |

**Stage 6 — Anti-aliasing and output**

The fragment outputs `vec4(colour, alpha)` where alpha comes from `smoothstep` on the organism SDF. The quad is rendered with `GL_BLEND` enabled (`SRC_ALPHA, ONE_MINUS_SRC_ALPHA`) so pixels outside the creature are transparent.

SDF-based anti-aliasing is resolution-independent — works perfectly at any zoom level without MSAA.

### Simple SDF Tier (LOD 1)

Same shader, controlled by `u_lodTier` uniform:

```glsl
if (u_lodTier == 1) {
    // Only blend 3 segments: head (0), mid (3), tail (5)
    // Skip appendages, mouth carving, eyes
    // Simplified shading: base colour + rim + outline only
}
```

This keeps the smooth organic silhouette at medium distance while cutting per-pixel cost by roughly 60%.

### Dot Tier (LOD 2)

A trivial separate shader. The vertex shader sizes the quad to the creature's screen diameter. The fragment shader draws a soft circle coloured by genome hue with energy-modulated brightness:

```glsl
float d = length(fragPos - center);
float alpha = smoothstep(radius, radius - 1.0, d);
fragColor = vec4(hsl2rgb(hue, 0.6, 0.3 + energy * 0.3), alpha);
```

## Coordinate System

All SDF evaluation happens in **screen-pixel space**.

On the CPU each frame:
1. Transform each creature's 6 segment world positions to screen positions via the camera transform (accounting for toroidal wrapping relative to camera center).
2. Compute a tight AABB around all screen-space segment positions.
3. Pad the AABB by `maxScreenRadius + appendageExtent` (estimated from tail length, fin area scaled to screen pixels).
4. Store the AABB as the quad bounds for this instance.
5. Store segment screen positions and screen-pixel radii in the data texture.

In the shader:
- `gl_FragCoord.xy` gives the fragment's screen position.
- Segment positions from the data texture are already in screen pixels.
- SDF distances are in pixels — `smoothstep(1.0, -1.0, d)` gives a perfect 1-pixel feathered edge.

## Migration Plan

### What Gets Removed from Renderer.cpp

- The `drawCreature` lambda (~600 lines) and all its supporting code:
  - `drawBodyRibbon` lambda
  - Tail filament drawing
  - Dorsal fin drawing
  - Whisker drawing
  - Spike/spine drawing
  - Nose/snout drawing
  - Mouth/jaw drawing
  - Eye drawing
  - Body marking drawing
  - Aura/signal glow drawing
- The creature iteration loop that calls `drawCreature`

### What Stays in Renderer.cpp

- Selection debug overlays: sensor arc, bite arc, collision ring, joint strain visualization, velocity trail
- Environment rendering: nutrient grid, reef substrate, bloom patches, current visualization
- All HUD panels, graphs, text rendering, inspector
- Camera system, viewport management, input handling
- The existing `colorProgram` and `textProgram` shaders (version bumped to `#version 410`)
- Habitat overlay modes

### What Changes in Renderer.cpp

- GL context version bumped from 3.2 to 4.1:
  ```cpp
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  ```
- Existing shader version directives bumped from `#version 150` to `#version 410`
- `CreatureSDF` member added, initialized/shutdown alongside existing GL resources
- Creature rendering call site replaced with single `creatureSdf_.render()` call

### Build Changes

- `CreatureSDF.hpp` and `CreatureSDF.cpp` added to `CMakeLists.txt`
- No new external dependencies

## Performance Estimates

Target hardware: Apple M5 Pro (20-core GPU, 48GB unified memory).

### Fill Rate

| Scenario | Creatures | Avg Screen Size | Fragments | Estimated Cost |
|----------|-----------|----------------|-----------|---------------|
| Zoomed in | 10–30 full SDF | 80–200px | ~500K | ~50M ops |
| Medium zoom | 50–100 full + 200 simple | 30–80px / 15–30px | ~400K | ~30M ops |
| Zoomed out | 50 full + 300 simple + 2000 dots | mixed | ~200K | ~15M ops |

M5 Pro GPU throughput: billions of ops/sec. All scenarios are well within budget.

### Draw Calls

3 instanced draw calls per frame (one per LOD tier) via `glDrawArraysInstancedBaseInstance`. Negligible overhead.

### Data Upload

5,000 creatures x 224 bytes = 1.12 MB per frame via `glBufferSubData`. Well within PCIe/unified-memory bandwidth.

### CPU Packing

Linear pass over all creatures: LOD classify, transform positions, pack floats. Straightforward to SIMD-optimize later if needed, but unlikely to be a bottleneck.

## Extensibility

Adding a new morphological feature (e.g., lateral limbs) requires:

1. **Genome**: Add float(s) to `MorphologyGenome` (e.g., `limbCount`, `limbLength`).
2. **Data texture**: Use reserved slots 52–55 (or extend the stride).
3. **Shader Stage 2**: Add an SDF function for the new appendage, `smin()` it into `dOrganism`.
4. **Quad sizing**: Update the appendage-extent padding estimate to account for the new feature.

Nothing else changes — the data pipeline, LOD system, colour composition, and integration points are all untouched. The reserved 4-float slots in the data texture stride are specifically for this purpose.

## Open Questions

- **Shader branching cost**: The `u_lodTier` uniform controls which code paths run in the shared SDF shader. On Apple Silicon GPUs, uniform-based branching is well-predicted and cheap. If profiling shows otherwise, we can split into two separate shader programs.
- **Quad overdraw**: Creatures with long tails or extended fins will have large bounding quads with many transparent pixels. If this becomes a fill-rate concern, tighter bounding geometry (e.g., oriented bounding boxes or convex hulls) could reduce overdraw at the cost of more complex CPU-side math.
- **Data texture vs UBO**: The design uses a TBO for unlimited creature counts. If populations stay under ~250 and the TBO `texelFetch` latency proves higher than UBO access, switching to a UBO is straightforward.
- **Metal migration**: OpenGL 4.1 is the right choice for this phase — the SDF workload is fragment-shader-bound, so API overhead is not the bottleneck, and the entire existing renderer is OpenGL. The `CreatureSDF` module's clean separation makes a future Metal port straightforward: the SDF algorithms translate almost line-for-line from GLSL to MSL. A Metal migration would be a natural fit for Phase 8 (Performance & V1 Lock) or a dedicated rendering overhaul phase.
