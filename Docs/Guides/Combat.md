# Combat

The combat module ships the genre-free machinery every RTS needs — vitals, deterministic damage
resolution, weapon cycling, target acquisition, and two delivery primitives — and deliberately
nothing that decides what kind of game you are making. Damage arithmetic, target priorities, and
engagement doctrine are yours, authored in Blueprint on the seams below.

## Making a unit damageable

Add a **Vitals Component** to the unit's Component Data: current/max health, an armor-class tag
(`SeinARTS.Combat.Armor.*` — author your own children), and optional regeneration. Authored
health of zero seeds to max on the first simulation tick, so you normally set only Max Health.
An entity without vitals simply cannot be damaged or targeted — nothing requires combat
participation. Reaching zero health destroys the entity through the ordinary teardown path
(squads, garrisons, reservations all settle exactly as any other death), and the simulation
emits damage, death, and kill events your presentation layer can bind for hit flashes, ragdolls,
and kill feeds.

## Arming a unit

Add a **Weapon Component**: an array of weapon slots, each carrying range, an optional firing
arc, cycle timing (cooldown, or a magazine with a reload), a delivery kind, and a damage payload.
Slot order is the stable identity abilities fire by.

- **Instant** delivery resolves the same tick the weapon fires — the right default for most
  direct-fire weapons; tracers and muzzle flash are presentation reacting to the fire events.
- **Projectile** delivery spawns a real simulated shell that flies at the authored speed and
  resolves on impact. Projectiles are ordinary entities: they snapshot and replay exactly, and
  they can carry vitals of their own, which makes shooting them down (interception) work with no
  extra machinery. Set the slot's Projectile Class to a unit Blueprint when you want the shell
  rendered; leave it empty for a sim-only projectile.
- A payload's **Area Radius** turns any hit into splash: every vitals-bearing entity in the
  radius gets its own formula evaluation with its own distance from the impact (falloff is your
  formula's decision).

## The two policy seams

- **Damage Formula** — a Blueprint-subclassable class referenced from a payload. It receives the
  payload, both entities, the target's armor tag, and the impact distance, and returns the final
  damage. Armor tables, facing bonuses, cover interaction (read the target's cover tags), and
  splash falloff all live here. An empty formula field means final damage equals the authored
  base damage.
- **Target Scorer** — a Blueprint-subclassable class referenced from an acquisition query. It
  answers two questions: *may this candidate be targeted* (the default excludes your own units —
  alliance-aware games override this, typically consulting shared-vision or pair-capability
  state) and *how good is it* (the default prefers nearest). The framework handles the
  deterministic sweep, range/arc/tag gates, and fog line-of-sight; the scorer is pure judgment.

## Firing and engagement

Nothing in the framework decides to shoot. Abilities do, through two calls:

- **Find Targets** (any graph) runs one deterministic acquisition query.
- **Fire Weapon At** (ability/effect graphs) runs the full legality gate — readiness, range,
  arc, fog line-of-sight — and executes the slot's delivery on success.

The starter **SeinARTS Attack Ability** is the reference loop: fire every weapon that can
legally fire at the commanded target each tick until the target dies. It deliberately does not
chase or switch targets — engagement doctrine (pursuit, stances, retreat thresholds, focus
fire) is a game-defining behavior you author by subclassing it or replacing it, exactly like
movement feel. Scripted damage and healing (traps, area effects, medics) use **Apply Damage** /
**Apply Heal** from the same restricted library.

## Target acquisition at scale

Target queries transparently prefilter vitals-bearing entities through a Combat-owned spatial
index. The index is derived from canonical entity position and component membership, is rebuilt
after relevant mutation or restore, and is never serialized or hashed. Exact range, arc, tag,
line-of-sight, health, scorer, and canonical tie-order rules still run on the resulting candidates;
queries that are too broad or touch fixed-point numeric limits fall back to the exact full sweep.

The index makes an on-demand query cheaper; it does not turn acquisition into an always-on system.
Games still own engagement cadence in their abilities and should avoid querying every unit every
simulation tick unless that behavior is intentional and measured.

## Determinism notes

- Everything combat touches is fixed-point component data — weapon timers, projectile flight,
  and vitals participate in canonical state, snapshots, and replays automatically.
- Formula and scorer classes evaluate on their class defaults with deterministic inputs; they
  are policy functions, not stateful objects. Keep them pure — no reads of presentation state,
  no unseeded randomness (the ability validator enforces the same rules as everywhere else).
- Hit-chance style mechanics belong in your formula using the deterministic PRNG, seeded from
  sim state.
