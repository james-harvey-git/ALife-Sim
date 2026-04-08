# Physics-First Design

The core rule for this version of the sim is simple: bodies are not cosmetic.

## What This Means

Every creature genome is converted into a physical body description before it is allowed to act. The simulation derives movement and interaction capabilities from that body instead of assigning arbitrary role stats.

Examples:

- larger bodies gain momentum and collision authority but pay higher upkeep
- elongated bodies experience lower forward drag but worse turning response
- fins improve thrust and lateral control
- armor increases survival but raises mass and energy cost
- larger jaws increase bite reach and predation payoff
- broader sensor fans trade off against metabolic overhead

## Why This Matters

If the physics layer is the source of truth, ecology becomes less scripted. Predation, schooling, grazing, and scavenging can emerge from the interaction between body plans, environmental flow, resource fields, and controller evolution.

## Current Approximation

The first milestone uses a 2D soft-rigid approximation:

- anisotropic drag in body-local space
- continuous thrust and turn forces
- flow-field drift from the environment
- overlap-based collision resolution
- mouth-range interaction arcs for grazing and biting

That is a deliberate stepping stone. It gives us a simulation where morphology already matters while keeping the codebase light enough to iterate quickly.

## Next Physics Upgrades

- articulated body chains or jointed appendages
- local pressure and slipstream effects
- terrain and substrate interaction
- more explicit reproduction and developmental constraints
- species and clade tracking derived from body-plan divergence

