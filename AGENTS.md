# AGENTS.md

## Purpose

This repository is a fresh native C++/SDL2/OpenGL ALife simulation aimed at open-ended evolution from a single ancestral lineage.

The long-term goal is not a scripted predator/prey toy. The goal is a watchable, high-performance ecosystem where trophic roles, social structure, and morphology emerge from physics, control, and selection pressure.

## Product Priorities

1. Physics first.
   Morphology must affect movement, collision, sensing, feeding, and survivability through the physics model.
2. Open-ended ecology.
   Avoid hard-coded species roles where possible. Prefer continuous tradeoffs over discrete classes.
3. Visuals must be truthful.
   Rendering should communicate the actual evolved phenotype, not a cosmetic layer disconnected from mechanics.
4. Strong observability.
   Preserve and improve the old sim's strengths around inspection, graphs, and selection tooling.
5. Performance matters.
   This project is intended to support large enough populations for selection pressure to dominate drift noise on a modern MacBook Pro.

## Strong User Preferences

- Use the old repo at `~/Development/Personal-Code-Projects/evolving-sim` as an important reference, especially for UI scaffolding and inspectability.
- Use Bibites as inspiration, but aim for something more open-ended and visually stronger.
- If the mechanics are sound, the parameters we should ideally be tuning are physics-model parameters, not lots of ad hoc ecology knobs.
- Make regular git commits.
- Do not be shy about requesting elevated permissions when they are genuinely useful.
- Defer fully fleshed-out eating and attacking animations until after higher-priority mechanics work such as environment, physics, and base interaction loops are solid.

## Current Architecture

- [src/main.cpp](/Users/jamesharvey/Development/Personal-Code-Projects/ALife-Sim/src/main.cpp)
  Native app loop, input handling, and `--smoke-test`.
- [src/sim/Simulation.hpp](/Users/jamesharvey/Development/Personal-Code-Projects/ALife-Sim/src/sim/Simulation.hpp)
- [src/sim/Simulation.cpp](/Users/jamesharvey/Development/Personal-Code-Projects/ALife-Sim/src/sim/Simulation.cpp)
  Source of truth for genomes, grouped `SimulationConfig`, traits, body geometry, controller updates, ecology, reproduction, persistence, and history.
- [src/render/Renderer.hpp](/Users/jamesharvey/Development/Personal-Code-Projects/ALife-Sim/src/render/Renderer.hpp)
- [src/render/Renderer.cpp](/Users/jamesharvey/Development/Personal-Code-Projects/ALife-Sim/src/render/Renderer.cpp)
  Thin native rendering and HUD layer. The runtime now goes through a real OpenGL-backed path for both world and HUD. Keep simulation rules out of here.

## Current Model

- Single ancestral species at reset.
- Continuous grazer/scavenger/hunter tradeoffs.
- Articulated segmented body chains with persistent per-segment state, flow-coupled lag, and segment-aware collisions.
- Phase 1 body-physics work has started: morphology now also drives regional segment radii, mass, drag, stiffness, tail drive, fin placement, tail shape, and body-region durability.
- Morphology-derived thrust, drag, turning, bite reach, sensing, upkeep, and reproduction thresholds.
- Damage is now partly regional: head and tail injury can degrade sensing/feeding and locomotion instead of only subtracting flat health.
- Toroidal world with a depletable substrate nutrient reservoir, current field, reef substrate, blooms, and carrion.
- Sparse topology-evolving controller with innovation-aware structural mutations, bucketed sensory inputs, and recurrent memory channels.
- Reef habitat modulates local flow, nutrient growth, and contact physics, and creatures now observe substrate proximity plus local shear.
- Reef contact is now resolved per segment with morphology-derived hold vs scrape behavior, so some bodies brace or skim structure better while others grind themselves up on it.
- Reef wakes now create lee-shelter niches downstream of structures, and creatures observe shelter as well as substrate proximity/contact and shear.
- Grazers and blooms now compete over the same nutrient substrate, and reproduction is modulated by recent foraging success instead of only stored energy.
- Branching lineage tracking for major brain-topology divergences so structural novelty can be observed as clades instead of anonymous drift.
- Native inspection HUD with history graphs, ecology drift, lineage telemetry, a live brain-topology overlay, a scrollable selection inspector, and selection readouts including slip/curvature-style body metrics.
- The app now boots with a subject selected, and the HUD exposes explicit observer picks (`random subject`, `top energy`, `dominant lineage`, `newest branch`) to support QA and watchability.
- The world view now has a real follow camera with zoom controls, and the app boots in a closer subject-following view to make visual QA and screenshotting practical.
- Observer selection is lineage-aware: when a watched creature dies, the sim should try to stay on that clade instead of immediately jumping to an unrelated organism.
- The renderer also has habitat overlay modes (`V`) for visual QA of nutrient, lee-shelter, and shear fields.
- The world view and the right-hand HUD now share the same OpenGL-backed frame path. Avoid reintroducing split SDL-renderer hacks.
- Phase 0 backbone is now live: grouped config, save/load including config and RNG state, headless batch mode, structured world snapshots, and regression coverage for smoke, determinism, and round-trip persistence.

## Important Design Rule

When adding new mechanics, ask:

1. Can this be expressed through body geometry, mass, drag, torque, reach, substrate interaction, flow, or constraints?
2. Can a lineage evolve into the behavior through selection instead of being assigned the role?
3. Can the renderer expose the change honestly?

If the answer is "only by adding a special-case species rule," that is usually a bad direction unless there is a very strong reason.

## Near-Term Best Next Steps

These are especially aligned with the current direction:

- Deepen the articulated-body model so appendages/fins/tails contribute more directly to propulsion and maneuvering.
- Continue Phase 1 from the new regional-body baseline rather than reverting to scalar body stats.
- Add more physically meaningful environmental structure such as substrate, shelter, shear, or obstacle interaction.
- Add lineage and clade observability so long-term evolutionary structure is easier to inspect.
- Introduce richer environmental physics instead of lots of ecology-specific hand tuning.
- Balance the new reef habitat system across long-run seeds before layering on more habitat types.
- Continue improving the right-side HUD and inspection tooling, borrowing the best ideas from the old sim.
- Add camera controls, save/load, and deeper debug views once the core dynamics justify them.

## UI Guidance

- Keep the playfield readable.
- Prefer a right-side observability panel over cluttering the center of the sim.
- History graphs and selection inspection are valuable and should keep improving.
- When adding new telemetry, prioritize signals that explain emergence: phenotype, controller state, resource pressures, lineage drift, and spatial/ecological structure.

## Rendering Guidance

- Procedural phenotype rendering is preferred when possible because it keeps visuals coupled to mechanics.
- If AI-assisted sprite workflows are ever introduced, they must preserve phenotype truthfulness and silhouette consistency.
- Do not let visuals drift away from the actual body model.

## Build And Test

Configure and build:

```bash
cmake -S . -B build
cmake --build build
```

Run the app:

```bash
./build/alife_sim
```

Run headless smoke tests:

```bash
./build/alife_sim --smoke-test --seed 1
./build/alife_sim --smoke-test --seed 2
./build/alife_sim --smoke-test --seed 3
./build/alife_sim --smoke-test --seed 2 --smoke-steps 10800
./build/alife_sim --batch-run --seed 1 --batch-count 5 --smoke-steps 10800
./build/alife_sim --batch-run --seed 1 --batch-count 5 --smoke-steps 10800 --report-format jsonl
./build/alife_sim --benchmark --benchmark-preset quick
./build/alife_sim --benchmark --benchmark-preset ecology
./build/alife_sim --smoke-test --config-preset reef-dense
./build/alife_sim --smoke-test --config-preset open-water --target-blooms 255 --target-reefs 5 --nutrient-diffusion 0.24
./build/alife_sim --benchmark --benchmark-preset quick --report-format jsonl --snapshot
./build/alife_sim --smoke-test --seed 1 --smoke-steps 1800 --save-state /tmp/alife_state.bin
./build/alife_sim --smoke-test --load-state /tmp/alife_state.bin --smoke-steps 1800
cd build && ctest --output-on-failure
```

Use multiple seeds when changing simulation dynamics.
Prefer the named benchmark presets for regression comparisons, and use `--snapshot` when a run needs richer world/lineage/focal-creature context.
Use the round-trip save/load path and `ctest` suite when touching Phase 0 infrastructure.
Do not combine `--load-state` with `--config-preset` or config override flags because persisted states already carry the saved config.

## Working Style

- Make small, coherent commits at useful checkpoints.
- Preserve the simulation/render separation.
- Prefer physics/environment changes over proliferating arbitrary balance constants.
- Keep hot-path changes mindful of performance.
- Kill live `alife_sim` processes after visual QA passes.
- Update `README.md` when the current milestone meaningfully changes.
