# ALife Sim

A native C++/SDL2 artificial life sandbox focused on physics-first evolution.

This repository is a fresh start from the older predator/prey project. Instead of hard-coding ecological roles, the new sim begins from a single ancestral body plan and lets trophic niches emerge through continuous tradeoffs in morphology, control, and metabolism.

## Current Direction

- One ancestral species at startup, no fixed predator or prey classes
- Physics-first phenotype model where geometry drives movement and interaction
- Continuous grazing, scavenging, and hunting tradeoffs instead of discrete diets
- Segmented procedural sprite rendering tied directly to evolved body traits
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
- evolving recurrent controllers
- segmented phenotypes with curved body chains and body-part collision envelopes
- grazing, carrion feeding, and live predation
- reproduction with mutation from a single ancestor genome
- native HUD with history graphs, ecology drift readouts, and controller inspection

This is intentionally the foundation, not the end state. The long-term plan is to grow from phenotype-driven rigid bodies toward richer articulated morphologies and stronger environmental physics.

## Controls

- `Space`: pause or resume
- `1`: normal speed
- `2`: 2x speed
- `3`: 4x speed
- `R`: reseed the world
- `C`: clear selection
- `Left Click`: select the nearest creature in the sim view
- `Esc`: quit

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
