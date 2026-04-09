# ALife Sim

A native C++/SDL2/OpenGL artificial life sandbox focused on physics-first evolution.

This repository is a fresh start from the older predator/prey project. Instead of hard-coding ecological roles, the new sim begins from a single ancestral body plan and lets trophic niches emerge through continuous tradeoffs in morphology, control, and metabolism.

## Current Direction

- One ancestral species at startup, no fixed predator or prey classes
- Physics-first phenotype model where geometry drives movement and interaction
- Continuous grazing, scavenging, and hunting tradeoffs instead of discrete diets
- Segmented procedural sprite rendering tied directly to evolved body traits
- Articulated body chains that lag, bend, and now feed back into propulsion and turning through regional hydrodynamics
- Reef-like substrate structures that create shelter, edge shear, and nutrient pockets
- Reef wakes now create lee-shelter niches that reduce local flow behind structures
- Depletable substrate nutrients that blooms and grazers compete over
- Native SDL2 runtime with an OpenGL-backed world and HUD path so we can push simulation complexity without browser overhead

## Physics-First Design

The simulation treats morphology as the source of truth. Each creature genome defines a body that is converted into:

- major and minor body axes
- mass and rotational inertia
- forward and lateral drag
- thrust efficiency and turn torque
- segmented body spacing and tail undulation
- bite reach and bite arc
- sensor range and viewing span
- collision radius and carrion value

Those derived traits affect both gameplay and rendering. A long, finned body glides differently from a squat armored grazer, and that same difference shows up in the generated sprite silhouette.

## First Milestone

The first playable milestone in this repo includes:

- toroidal water world with bloom patches and a depletable substrate nutrient reservoir
- current field plus reef substrate that reshape local flow and nutrient availability
- reef wakes that accumulate sheltered nutrient pockets downstream of structures
- sparse topology-evolving controllers with innovation-aware structural mutations and recurrent memory inputs
- segmented phenotypes with articulated body chains, flow-coupled lag, and body-part collision envelopes
- richer morphology genes for body taper, fin placement, tail length/forking, jaw offset, and armor distribution
- per-segment drag, drive, stiffness, armor, durability, and mass profiles derived from morphology
- regional damage that now penalizes head- and tail-dependent performance instead of being only a flat health number
- per-segment reef contact response with morphology-derived hold vs scrape tradeoffs, so reef-hugging and open-water bodies interact differently with substrate
- reef-aware observations including substrate proximity, lee shelter, and local shear
- visible spine/scute cues for spike-heavy bodies so substrate-specialized contact traits are not hidden from the renderer
- grazing, carrion feeding, and live predation
- reproduction with mutation from a single ancestor genome, tempered by recent foraging success
- branching lineage tracking for major brain-topology divergences
- native HUD with history graphs, ecology drift readouts, lineage telemetry, live brain topology overlay, regional integrity readouts, and lineage-aware observer controls
- scrollable selection inspector plus habitat-field overlay modes for QA and debugging

This is intentionally still an early foundation. The sim is now partway into Phase 1 of the roadmap: morphology drives segment-level hydrodynamics, regional injury, and substrate-contact specialization, but the body is still a compact chain model rather than a fully constraint-solved multi-part organism. The next body-physics steps are deeper joint mechanics and more evolved body-plan freedom on top of this stronger reef/contact niche differentiation.

Phase 0 of the roadmap is now in place: the sim has grouped configuration, state persistence that includes config/RNG/history, repeatable multi-seed headless runs, named benchmark presets, structured snapshots, and regression tests for smoke, save/load round-trip, and configured determinism.

## Controls

- `Space`: pause or resume
- `1`: normal speed
- `2`: 2x speed
- `3`: 4x speed
- `R`: reseed the world
- `N`: select a random subject
- `F`: select the highest-energy subject
- `L`: select a representative from the dominant lineage
- `B`: select a representative from the newest active branch
- `C`: clear selection
- `V`: cycle habitat overlay (`off`, `nutrient`, `lee shelter`, `shear`)
- `G`: toggle follow-camera lock on the selected creature
- `Z`: snap the camera back onto the current subject
- `+` / `-`: zoom the world view in or out
- `[` / `PageUp`: scroll the selection inspector upward
- `]` / `PageDown`: scroll the selection inspector downward
- `Mouse Wheel` over the world view: zoom the camera
- `Mouse Wheel` over the selection card: scroll the selection inspector
- `Left Click`: select the nearest creature in the sim view
- `Esc`: quit

On launch, the sim starts with one creature already selected and the camera already zoomed in and following that subject, so close-up QA screenshots are easier to capture. Follow mode now uses a dead-zone leash instead of a soft center-lock, so the subject can move around locally without making the whole world slide on every small wiggle. If a watched creature dies, the observer now tries to stay locked onto the same lineage before falling back to broader picks.

## Build

```bash
cmake -S . -B build
cmake --build build
```

Run the desktop app:

```bash
./build/alife_sim
```

Run a non-graphical smoke test:

```bash
./build/alife_sim --smoke-test
./build/alife_sim --smoke-test --report-format jsonl
```

Run a longer headless probe:

```bash
./build/alife_sim --smoke-test --seed 2 --smoke-steps 10800
```

Run a multi-seed batch probe:

```bash
./build/alife_sim --batch-run --seed 1 --batch-count 5 --smoke-steps 10800
```

Emit machine-readable JSONL summaries for offline analysis:

```bash
./build/alife_sim --batch-run --seed 1 --batch-count 5 --smoke-steps 10800 --report-format jsonl
```

Run a named benchmark preset:

```bash
./build/alife_sim --benchmark --benchmark-preset quick
./build/alife_sim --benchmark --benchmark-preset ecology
```

Run a named simulation config preset or targeted overrides:

```bash
./build/alife_sim --smoke-test --config-preset reef-dense
./build/alife_sim --smoke-test --config-preset open-water --target-blooms 255 --target-reefs 5 --nutrient-diffusion 0.24
```

Capture richer world snapshots in headless output:

```bash
./build/alife_sim --smoke-test --seed 1 --smoke-steps 3600 --snapshot
./build/alife_sim --benchmark --benchmark-preset quick --report-format jsonl --snapshot
```

Save and resume a simulation state:

```bash
./build/alife_sim --smoke-test --seed 1 --smoke-steps 1800 --save-state /tmp/alife_state.bin
./build/alife_sim --smoke-test --load-state /tmp/alife_state.bin --smoke-steps 1800
```

`--load-state` restores the saved simulation config as well as world/RNG/history state, so it should not be combined with `--config-preset` or config override flags.

Current benchmark presets:

- `quick`: short multi-seed regression probe
- `ecology`: medium multi-seed ecology baseline
- `stress`: longer heavier benchmark for performance and stability work

Run the built-in regression suite:

```bash
cd build
ctest --output-on-failure
```

The Phase 0 regression suite currently covers:

- a default smoke run
- a named benchmark preset
- save/load round-trip equivalence with a non-default config
- configured determinism on repeated seeded runs

## Repo Layout

- `src/sim`: simulation state, genomes, physics, and ecology
- `src/render`: native OpenGL-backed renderer, HUD, graphs, and procedural phenotype rendering
- `docs`: architecture notes and future direction
