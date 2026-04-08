# ALife Sim

A native C++/SDL2 artificial life sandbox focused on physics-first evolution.

This repository is a fresh start from the older predator/prey project. Instead of hard-coding ecological roles, the new sim begins from a single ancestral body plan and lets trophic niches emerge through continuous tradeoffs in morphology, control, and metabolism.

## Current Direction

- One ancestral species at startup, no fixed predator or prey classes
- Physics-first phenotype model where geometry drives movement and interaction
- Continuous grazing, scavenging, and hunting tradeoffs instead of discrete diets
- Segmented procedural sprite rendering tied directly to evolved body traits
- Articulated body chains that lag, bend, and couple to local flow
- Native SDL2 runtime so we can push simulation complexity without browser overhead

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

- toroidal water world with nutrient-driven bloom patches
- current field that nudges body motion
- sparse topology-evolving controllers with recurrent memory inputs
- segmented phenotypes with articulated body chains, flow-coupled lag, and body-part collision envelopes
- grazing, carrion feeding, and live predation
- reproduction with mutation from a single ancestor genome
- native HUD with history graphs, ecology drift readouts, live brain topology overlay, and observer controls

This is intentionally still an early foundation. The long-term plan is to grow from these articulated soft-body approximations toward richer jointed morphologies, stronger environmental physics, and deeper lineage/ecology observability.

## Controls

- `Space`: pause or resume
- `1`: normal speed
- `2`: 2x speed
- `3`: 4x speed
- `R`: reseed the world
- `N`: select a random subject
- `F`: select the highest-energy subject
- `C`: clear selection
- `Left Click`: select the nearest creature in the sim view
- `Esc`: quit

On launch, the sim now starts with one creature already selected so the brain overlay and inspection readouts are immediately visible.

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
```

## Repo Layout

- `src/sim`: simulation state, genomes, physics, and ecology
- `src/render`: SDL2 renderer, HUD, graphs, and procedural phenotype rendering
- `docs`: architecture notes and future direction
