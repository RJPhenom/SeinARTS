# Authoring Abilities

Everything a unit does — move, attack, produce, garrison, reinforce, set rally — is an ability.
An ability is a Blueprint subclass of `SeinAbility` with latent execution graphs; there is no
hardcoded command enum beyond the activation/cancel plumbing. `Units.md` summarizes abilities
briefly; this is the deep-dive.

## Create

Content Browser → right-click → SeinARTS → Ability. This creates a **SeinARTS Ability** Blueprint
and stamps a unique `AbilityTag` from the asset name (renaming the asset regenerates it, unless
you've hand-edited the tag — that flips ownership to you). Grant the ability to a unit through its
`Ability Component`, which also maps right-click contexts (ground, enemy, friendly) to abilities.

## Identity

`Ability Name` and `Icon` drive action buttons (UI falls back to the name text if the icon is
null). `Target Type` drives action-slot target capture. `Is Passive` runs the ability on first
grant into the passive set instead of the single primary slot. `Is Move Ability` marks this as the
entity's "move" ability for auto-move plumbing (production auto-move, range-then-attack
pre-pending) — leave it unset on every ability to opt an entity out entirely.

## Activation gates

Cost, cooldown, and target checks are enforced identically by the simulation and the UI — action
buttons grey out for the same reason activation would fail.

- **Cost** — `Resource Cost` deducts on activation. `Cost Timing` is Immediate (default) or
  Production Queue, which defers the completion bucket to `Enqueue Production`. `Refund Cost On
  Cancel` returns the deducted snapshot on a forced cancel.
- **Cooldown** — `Cooldown` (sim-seconds), `Cooldown Start Timing` (On Activate / On End), and
  `Cooldown Scope` (Squad, the default and shared across the squad, or Member for stackable
  per-soldier abilities). `Refund Cooldown On Cancel` clears a cooldown a mid-use cancel started.
- **Targeting checks** — `Max Range` (zero = unlimited), `Valid Target Tags`, `Requires Line Of
  Sight` (fog-gated; permissive with no fog implementation bound), `Requires Pathable Target`
  (rejects unreachable destinations), `Requires Free Footprint` (footprint check, currently only
  meaningful with a Point + Facing spec carrying a building class). `Out Of Range Behavior` either
  rejects an out-of-range command or queues the unit's move ability first.
- **Area Radius** — AoE radius; drives the default Point-targeter's preview ring and is readable
  from `OnActivate`.
- **Can Activate** — an overridable final gate, evaluated after cooldown/tag/target/pathability
  checks but before cost deduction and `OnActivate`. Return false to reject.

## Tag arbitration

Four containers, evaluated against the entity's tags (base tags plus every currently-granted tag):
`Granted Tags` apply while active and release on end (refcounted across overlapping sources).
`Required Entity Tags` and `Required Player Tags` gate activation on entity state and player
tech/unlocks respectively. `Blocked Tags` refuses activation if the entity holds any of them.
`Cancel Abilities With Tag` cancels any active ability (including this one) whose `Granted Tags`
intersect the set on activation — include the ability's own tag for a self-cancelling reissue.

## Dispatch policy

When triggered from a multi-member context (squad, multi-unit selection), `Dispatch Mode` decides
who fires: **All** (default — every capable member, correct for Move/Attack/Hold), **Single**
(exactly one, via `Dispatch Selector`: Leader / ByTag / FirstAvailable, with `Dispatch Fallback`
covering a missing preferred member), or **ByTag** (every member whose tags contain `Dispatch
Preferred Tag`; empty result silently no-ops). Designers tag the unit itself, so the same
authoring works identically in squads and ad-hoc selections.

## Targeting

A `Targeter Spec` describes how the action-slot targeting UI captures points before submitting;
right-click smart commands already have the click point and ignore it.

- **Point Targeter Spec** — one world position per cycle (ground casts, AoE with `Area Radius`).
- **Point + Facing Targeter Spec** — the building-placement spec: press locks location, drag sets
  facing (`Rotation Step Degrees` snaps it; 0 = free), release confirms. `Building Class` supplies
  the extents/mesh for footprint checks and the hologram.
- **Line Targeter Spec** — a line or corridor of one or more segments. `Capture Mode` is per-
  ability: **Drag** (press-drag-release per segment — strafing runs, barrage lines) or
  **MultiClick** (each click chains a segment from the previous vertex — trench networks; clicking
  near the last vertex finishes early). `Width` makes it a corridor; `Max Segment Length` caps a
  segment. Both modes encode every segment identically (start + end), so `OnActivate` reads
  `Targeter Points` uniformly regardless of capture mode.

`Target Count` (base spec) is how many points/segments to capture before the command submits.

## Lifecycle

`Can Activate` (final gate), `On Activate` (start committed work, launch latent actions), `On Tick`
(`Delta Time` in sim-seconds), and `On End` (`bWasCancelled`) are the four Blueprint events. Call
**End Ability** for natural completion or **Cancel Ability** for forced termination — a latent node
completing does not implicitly end the ability.

### Move To

The async movement node is **Move To** (`Ability → Move To (Dest)`):

```
OnActivate -> Move To (Destination)
                 |- Completed / Failed / WaypointReached / PartialPath / PathRecomputed / Cancelled
```

Acceptance radius comes from the unit's `Navigation Component`, not the call site. The node
survives save/restore and reconnection as a registered checkpoint-safe latent action. If the
ability validator rejects your graph around this node, route the value you need afterward through
deterministic ability/component state rather than a local Blueprint variable read across the
latent boundary.

## Custom simulation data

Read and write designer structs (right-click → SeinARTS → Component) from ability graphs with the
typed Get/Set Component nodes; the struct editor strips non-deterministic field types on save.

## Economy and construction

Harvesting is composed from abilities rather than a hardcoded worker type. Store node stock and
worker cargo in deterministic components, transfer those values with the typed component nodes,
then call **Grant Income** when a worker reaches a valid dropoff. Resource writes are accepted only
inside bootstrap or simulation callbacks; malformed income maps fail atomically.

For construction, add a `Construction Component` to the building and have the worker ability call
**Add Construction Progress** from `On Tick`. The building receives the refcounted
`State.UnderConstruction` tag at spawn. Crossing `Time To Completion` removes the component,
releases that framework-owned tag grant, and applies the optional completion effect. Multiple
workers stack naturally; apply any diminishing-return policy in the worker ability.

## Determinism notes

- Ability Blueprints compile against a dedicated determinism validator: unsafe member types,
  untrusted calls, and presentation-only function calls fail the build. Randomness must go through
  the deterministic PRNG (`FFixedRandom`) — unseeded/engine RNG calls are denied.
- A save-time validator covers the `Move To` continuation contract: values needed after the latent
  node resumes must live in deterministic state, not a compiler-frame local, since that frame's
  residue is authenticated and discarded across a snapshot restore. Native calls genuinely needed
  after a restore must be marked `SeinContinuationSafe`.
- Self-state writes inside `OnActivate`/`OnTick`/`OnEnd` are tracked automatically; a mutation from
  outside those callbacks needs an explicit **Mark Deterministic State Dirty** call.
- Cost, cooldown, tags, and targeter points are canonical simulation state — identical on every
  peer for the same command stream.
