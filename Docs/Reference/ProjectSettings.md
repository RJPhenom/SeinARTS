# Project Settings Reference

## Pages

- **Game → SeinARTS** — everything owned by the core framework.
- **Plugins → SeinARTS Squad Extension / SeinARTS Cover Extension** — extension settings appear
  only when the extension is installed.

## The picker convention

Every pluggable system is selected by a class picker with one uniform rule:

- **None / empty = that system is deliberately OFF.** The framework runs without it and posts a
  one-time on-screen notice describing the consequences (no navigation = move orders fail; no
  fog = nothing hidden; no collision = bodies overlap).
- **A set-but-unloadable or abstract class is a mistake**, reported as an error and replaced by
  the shipped default.
- A valid class replaces the default wholesale. Custom classes must satisfy the canonical-state
  coverage contract (declare statelessness or register their state) or the match refuses to
  start.

Pickers: Level Data, Navigation, Fog of War, Collision Resolver, Avoidance, Default Broker
Resolver, Formation Preview Actor, Cover System (extension), plus per-squad and per-unit soft
class overrides.

## Groups you will actually tune

- **Simulation** — tick rate (default 30 Hz), max ticks per frame, parallelism and async
  pathfinding toggles (per-machine performance choices that never change results).
- **Terrain Types** — each entry: a gameplay tag, physical-material bindings, and three
  independent dials — routing cost (path preference), speed multiplier, vision multiplier.
- **Navigation** — cell size fallback, max step height, walkable slope, projection radii, wall
  padding, path-request budget. A* search tuning lives on the navigation class's Blueprint
  defaults, not here.
- **Movement / Avoidance harness** — the model-agnostic knobs (avoidance on/off class pick, bend
  cap, idle re-seek group). Avoidance model shape lives on the avoidance class defaults.
- **Fog of War** — vision cell size, tick interval, up to six named custom vision layers
  (thermal and similar), minimap fog appearance.
- **Collision** — channel registry with an Unreal-style response matrix, mass-ratio cutoff.
- **Formations** — default formation, drag formation, single-click formations, preview enable.
- **Match / Network** — input delay turns, determinism-check cadence, drop policy, replay
  checkpoint interval, config-parity enforcement.

## Freezing

Simulation-affecting settings freeze at match start into the lockstep config fingerprint:
editing them mid-match fail-stops the session with an on-screen message, and peers whose values
differ refuse to join each other. Presentation-only settings (colors, debug draw budgets, fog
overlay appearance) stay live-editable.

## Blueprint class defaults as tuning

Deliberately outside this page: per-class tuning rides Blueprint class defaults — A* weights on
your navigation subclass, avoidance shape on your avoidance subclass, movement feel in each
unit's movement-class data. Subclass, retune, and pick your class; the determinism system
captures class identity and content automatically.
