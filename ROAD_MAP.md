# ALife Sim V1 Roadmap

## North Star

Build a native C++/SDL2 artificial life simulation where:

- all organisms descend from a single ancestral lineage
- trophic roles emerge through physics, control, and selection rather than species scripts
- controller complexity can grow through NEAT-style structural evolution with recurrent memory
- visibly different morphologies emerge because different bodies are actually better for different niches
- the world is interesting enough to watch for long stretches because it keeps producing new local dynamics
- the renderer and HUD tell the truth about what the simulation is doing

This is not aiming for "perfect digital life." V1 should instead be the first version that plausibly supports open-ended adaptive radiation, visible lineage divergence, and recurring collective and individual complex behaviors across multiple seeds.

## Core Principles

- Physics first. Movement, collision, feeding, sensing, injury, and survivability should fall out of body geometry, mass, drag, torque, reach, substrate, and flow.
- Open-ended ecology. Prefer continuous tradeoffs and common resource laws over predator/prey special cases.
- Brains must be allowed to complexify. Structural mutations, recurrent memory, and species protection are central, not optional extras.
- Morphology must matter. Different body plans should create different capabilities, costs, and failure modes.
- Visuals must be mechanically truthful. A lineage should look the way it behaves because the same phenotype data drives both.
- Observability is part of the product. If we cannot inspect why a lineage succeeded, we cannot tune for open-endedness with confidence.
- Parameters should increasingly migrate toward physics and environment parameters rather than ecology-specific magic numbers.

## Reference Direction

- From the current repo: preserve the physics-first architecture, the right-side inspector bias, the live brain overlay, and the separation between simulation and rendering.
- From the old `evolving-sim`: preserve strong watchability and inspectability, especially population history, stable entity selection, save/load, camera control, live network introspection, and "change parameters while learning from the world" ergonomics.
- From Bibites: take inspiration from coherent biology/physics/evolution, procedural appearances linked to genes, and the ambition for lifelike emergent behavior. Do not copy mechanics just because Bibites has them; only keep what strengthens the physics-first direction.
- From the Game Studio planning/UI guidance: protect the playfield, keep persistent UI density under control, keep long-form information in inspector surfaces rather than all over the viewport, and treat debugging/overlay tooling as first-class infrastructure.

## What V1 Must Prove

V1 should convincingly demonstrate all of the following:

- A single ancestor can diverge into multiple persistent ecological strategies in the same world.
- Brain topology and recurrent memory actually complexify over evolutionary time rather than being pruned back immediately.
- Morphological divergence is visible and functionally meaningful.
- Collective behaviors can appear without hard-coded group bonuses.
- The simulation is stable enough across seeds that we are studying evolutionary dynamics, not one lucky run.
- The tooling is good enough that a human observer can understand what happened in a run and why.

## V1 Exit Criteria

V1 is reached when all of the following are true:

- At least 5 independent long-run seeds remain dynamically alive for 30+ simulated minutes each without forced resets.
- Those long runs show sustained births, deaths, and lineage turnover rather than only demographic stasis.
- At least 3 recurring ecological strategies appear across seeds from a single ancestor. Example categories: reef ambushers, open-water grazers, scavenging opportunists, fast pursuit hunters, nursery-clustering shoalers.
- At least 2 clearly distinct body-plan families appear across repeated seeds and their performance differences can be explained through mechanics.
- Structural brain mutations persist for many generations, and speciation or lineage protection measurably helps novel controllers survive.
- At least 2 collective behaviors are observed in repeated runs without being scripted. Example behaviors: schooling, alarm clustering, pack pursuit, territorial clustering, scavenger swarming, nursery congregation.
- The HUD and debug tooling can inspect any selected organism's body, brain, memory, energy budget, local habitat, lineage context, and recent actions.
- Save/load, replayable snapshots, benchmark mode, and headless batch runs exist and are reliable enough for regression testing.
- Interactive performance is good enough to watch large live populations on the target MacBook Pro, and headless performance is high enough to run serious multi-seed experiments.

## V1 Non-Goals

- No hand-authored species roster.
- No ecology built around many lineage-specific privileges.
- No major art-only polish pass before the mechanics are trustworthy.
- No huge menu jungle full of low-signal tuning sliders.
- No AI-generated sprite production pipeline until the phenotype representation is stable enough to keep visuals truthful.

## Recommended Phase Order

The phases below are ordered by dependency, not by glamour.

## Phase 0: Experimental Backbone

Goal: make the simulation measurable, reproducible, and tunable before adding too much complexity.

Deliverables:

- Deterministic or near-deterministic seeded runs where practical.
- Stable save/load of simulation state and configuration.
- Batch runner for multi-seed headless experiments.
- Benchmark mode with consistent scenario presets.
- Snapshot capture for world state, selected entity state, lineage summaries, and performance counters.
- Regression suite for smoke tests, ecology sanity tests, and benchmark thresholds.
- Better config organization so physics, environment, brain evolution, and rendering/debug toggles are separated cleanly.

Why it matters:

- Open-endedness work is otherwise impossible to evaluate.
- Most false positives in ALife come from anecdotal single-seed impressions.

Exit criteria:

- Any major simulation change can be checked with a standard multi-seed regression pass and compared against a saved baseline.

## Phase 1: Body Physics 2.0

Goal: replace the current mostly head-driven articulated chain with a genuinely mechanically meaningful body model.

Deliverables:

- Jointed or constraint-based multi-segment bodies where segment motion feeds back into propulsion and turning.
- Evolving segment count, segment radii, body taper, fin placement, tail length, jaw placement, and armor distribution.
- Local drag, thrust, and collision resolution applied per body region.
- Morphology-derived tissue upkeep, damage susceptibility, and inertial properties.
- Contact-based substrate interaction so some shapes do better in reefs, narrow passages, or calmer wakes.
- Better injury model so damage can penalize locomotion or feeding in body-specific ways.

Why it matters:

- Open-ended ecology needs multiple physically meaningful ways to move, feed, evade, and persist.
- If all bodies reduce to "same creature, different ellipse stats," visual divergence will stall early.

Exit criteria:

- Different shapes have obviously different motion signatures and niche affinities in the same physics field.

## Phase 2: Full NEAT-Style Recurrent Brains

Goal: restore and extend true topology evolution so controller complexity can grow instead of staying in a shallow local optimum.

Deliverables:

- Explicit node genes and connection genes with stable innovation IDs.
- Structural mutations for add-node, add-connection, disable-connection, and recurrent-link creation.
- Crossover between compatible genomes.
- Speciation or species-style novelty protection based on compatibility distance.
- Recurrent memory channels and optional explicit memory nodes as part of evolution, not just hard-coded controller baggage.
- Complexity costs tied to actual network structure rather than arbitrary penalties.
- Better lineage/species inspector with topology deltas, innovation history, and memory activity traces.

Why it matters:

- Truly open-ended control evolution needs a safe path from simple reactive policies to temporally rich behavior.
- Without speciation, novelty often dies before it has a chance to find a niche.

Exit criteria:

- Novel topologies survive, branch, and occasionally dominate without collapsing the whole ecology.

## Phase 3: Unified Energy, Digestion, and Reproduction Pipeline

Goal: make feeding and life history flow through one coherent metabolic system rather than several loosely connected gain terms.

Deliverables:

- Ingestion, digestion, reserve storage, and expenditure as explicit stages.
- Common energy accounting for grazing, carrion feeding, biting, movement, repair, and reproduction.
- Reproductive investment model with offspring provisioning, maturation, and growth costs.
- Damage and starvation tied to the same physiology budget.
- Detritus and corpse handling as part of matter flow, not only as a special-case reward.
- Optional waste or byproduct field if it helps nutrient cycling without adding too much complexity.

Why it matters:

- If energy flow is not coherent, ecology becomes balance-patch theater instead of natural selection.
- Distinct lifestyles should come from how organisms move matter and energy through their bodies.

Exit criteria:

- The same metabolic rules support grazing, scavenging, predation, fasting, overshoot, collapse, and recovery.

## Phase 4: Habitat Physics and Resource Transport

Goal: create a world with enough structure for multiple niches to coexist and interact.

Deliverables:

- Stronger current topology with transport lanes, eddies, wake shadows, and calm zones.
- Substrate diversity such as reefs, dense obstacle fields, open water, nursery pockets, detritus sinks, and nutrient upwellings.
- Spatial nutrient transport instead of mostly local resource regeneration.
- Seasonal or long-timescale environmental modulation that changes pressures without hard-resetting the world.
- Habitat cues available to sensors through gradients, line-of-sight, contact, and flow.
- Eventually, niche construction opportunities if simple physical forms of world modification prove tractable.

Why it matters:

- Open-ended evolution needs more than one answer to the question "where should I live and how should I move?"
- Habitat diversity is a major driver of adaptive radiation.

Exit criteria:

- Repeated seeds produce durable spatial niche partitioning rather than one globally optimal lifestyle.

## Phase 5: Action Space and Social Emergence

Goal: allow individual and collective complexity to emerge from richer embodied action and perception.

Deliverables:

- Action outputs for modulating gait, posture, burst effort, braking, bite activation, grazing mode, and signaling.
- Sensing upgrades for body contact, flow gradients, occlusion, local crowding, and signal channels.
- Persistent internal state and recurrent memory that controllers can use for pursuit, retreat, foraging loops, or social coordination.
- Signaling that is physically or energetically costly enough to matter.
- Parenting or local offspring association if it emerges cleanly from shared space and provisioning.
- Removal of any remaining explicit pack-hunting or defense bonuses in favor of genuine interaction dynamics.

Why it matters:

- Complex collective behavior requires both richer perception and richer temporal control.
- V1 should support the possibility of schools, swarms, ambush groups, alarm cascades, and nursery clustering.

Exit criteria:

- Social and collective behavior can be observed repeatedly without scripted group perks.

## Phase 6: Truthful Phenotype Rendering and Animation

Goal: make evolved role and morphology visible at a glance without separating appearance from mechanics.

Deliverables:

- Procedural phenotype renderer driven directly from body plan parameters and local physical state.
- Better silhouette language for segment chains, fins, jaws, armor, sensor organs, and body taper.
- Animation driven by actual actuator state, contact, feeding, bite timing, damage, and locomotor waveform.
- Lineage-level palette and pattern drift tied to genotype, not arbitrary cosmetics.
- Optional AI-assisted sprite or texture workflows only if they are seeded from approved mechanically truthful phenotype frames and normalized using the sprite-pipeline discipline.
- Visual overlays for body forces, flow alignment, digestion state, and attack windows when debugging.

Why it matters:

- Morphological evolution should be immediately visible to the observer.
- Watchability is part of the scientific utility here because humans need to notice when something novel is happening.

Exit criteria:

- A screenshot should communicate something real about diet, locomotion style, and habitat affinity.

## Phase 7: Observer-Grade UI and Debugging

Goal: make the sim understandable enough to tune for emergence and enjoyable enough to watch for long stretches.

Deliverables:

- Strong camera controls including zoom-to-cursor, pan, bookmarks, and selection follow.
- Right-side observability panel inspired by the old repo, but better structured and less cluttered.
- Population history, trait-distribution graphs, lineage tables, ecological role summaries, and extinction diagnostics.
- Selected-organism inspector with body blueprint, actuator state, energy budget, controller activity, memory state, lineage ancestry, and local habitat context.
- Input/output hover labels and topology introspection similar to the old neural network UI.
- Save/load, replay, timeline scrubbing, and snapshot comparison.
- Debug overlay stack for nutrient, shelter, shear, population density, trophic pressure, lineage territories, and performance hot spots.
- Layout discipline from the Game Studio UI guidance: preserve the playfield, keep persistent HUD density moderate, push long-form details into drawers or scrollable inspectors, and avoid center-screen clutter.

Why it matters:

- Open-ended systems are useless if every interesting event is invisible or impossible to explain afterward.

Exit criteria:

- A developer or observer can answer "what is this lineage doing and why is it succeeding?" without digging through raw code or logs.

## Phase 8: Performance, Balance, and V1 Lock

Goal: turn the sim from an interesting prototype into a reliable long-run instrument.

Deliverables:

- Hot-path profiling and optimization for body physics, brain evaluation, sensing, collision, and rendering.
- Better spatial partitioning and LOD strategy that never changes simulation truth silently.
- Separate interactive and headless performance profiles.
- Multi-seed tuning that prioritizes physics knobs and world-structure parameters over ecology-specific patches.
- Standard benchmark scenarios and release gates.
- V1 presets for "open ocean," "reef basin," and "mixed habitat" if presets help compare outcomes without becoming hand-authored species arenas.

Why it matters:

- Large populations and long runs are necessary for selection pressure to dominate drift noise.
- V1 must be something we can trust, not only admire.

Exit criteria:

- The sim meets the V1 proof bar consistently enough that new work can shift toward deeper open-endedness rather than basic stabilization.

## Cross-Cutting Workstreams

These should run throughout the roadmap rather than waiting for a single phase.

### A. QA and Scientific Discipline

- Always test across multiple seeds.
- Keep headless probes and live visual QA in the loop.
- Save representative runs when a new behavior appears.
- Track collapse modes separately: starvation lock, reproduction runaway, trophic imbalance, habitat monopolization, brain bloat, and performance collapse.

### B. Parameter Hygiene

- Prefer deleting ecology-specific knobs when they can be replaced with physics, geometry, transport, digestion, or sensing laws.
- Tag any temporary balance constant that exists only to bridge a missing mechanic.

### C. Data and Tooling

- Log lineage, morphology, controller, and ecology summaries in a machine-readable way.
- Add offline analysis tools for trait distribution, diversification, and extinction-cause review.

### D. Visual Honesty

- Do not add art layers that imply mechanics that do not exist.
- Keep debug visuals available even after the production visuals improve.

## Immediate Next Milestones From The Current State

The most sensible near-term order from today is:

1. Upgrade the segmented body into a more genuinely jointed propulsion model.
2. Complete the NEAT stack with crossover, speciation, and stronger recurrent support.
3. Add save/load, reproducible batch experiments, and better benchmark tooling.
4. Expand habitat physics beyond reefs into a more varied but still physically coherent world.
5. Deepen the inspector and timeline tooling so new behaviors can be diagnosed instead of merely spotted.

## How To Use This Roadmap

- Use it to decide sequencing, not to freeze discovery.
- If a proposed feature does not strengthen open-endedness, observability, or truthful watchability, it is probably not a V1 priority.
- If a feature sounds exciting but requires lots of special-case rules, pause and ask whether the same outcome can emerge from better body physics, resource flow, sensing, or brain evolution instead.
