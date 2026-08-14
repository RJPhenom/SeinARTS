# Ability Authoring

**Document version:** 0.1

SeinARTS Ability Blueprints run inside the deterministic fixed-tick simulation. They are
authoritative state, not presentation scripts. This guide covers the supported creation,
activation, lifecycle, and checkpoint-continuation contract.

## Create and grant an ability

1. In the Content Browser, choose **SeinARTS Ability**, select an Ability parent class, and name
   the asset. The factory creates `On Activate`, `On Tick`, and `On End` events and derives a unique
   `Ability Tag` when automatic tag generation is enabled.
2. Add the new class to the unit's `FSeinAbilityComponent` entry in the entity bridge's
   `ComponentData`, under `Granted Abilities`.
3. For smart right-click behavior, add a `Default Commands` mapping from input-context tags to the
   ability tag. Set `Fallback Ability Tag` for unmatched contexts, normally the unit's move ability.
4. Compile, save, and run **Validate Assets** before testing the unit.

Runtime grants are reference-counted. Use **Grant Ability** and the matching revoke nodes when an
effect or scripted progression changes a unit's loadout. Force-revoke nodes intentionally discard
all remaining grant owners and should be exceptional.

## Configure targeting and arbitration

- Give every ability a stable, unique `Ability Tag`.
- `Target Type` defines the authoritative input shape. Point and Area normally pair with a
  `Point Targeter Spec` for point preview/capture or a
  `Point + Facing Targeter Spec` for placement rotation and footprint preview. Line and corridor
  targeters are not shipped.
- `bIsPassive` controls automatic activation on first grant. The `Passive` target type alone does
  not activate an ability.
- Mark exactly one granted ability as `Is Move Ability` when the entity uses `Auto Move Then` or
  production rally dispatch. The framework resolves that flag rather than a hard-coded move tag.
- Author required/blocked entity and player tags, target tags, range, visibility, costs, cooldown,
  dispatch mode, cancellation tags, and owned tags declaratively. Use `Can Activate` only for the
  final deterministic rule that those fields cannot express.

## Lifecycle

- `Can Activate` runs after cooldown, tag, target, visibility, and pathability checks, but before
  resource deduction, cancellation arbitration, and `On Activate`.
- `On Activate` initializes deterministic state and starts admitted actions. It does not complete
  the ability. Call `End Ability` on natural completion or `Cancel Ability` on forced termination.
- `On Tick` runs once per fixed simulation tick. Its `Delta Time` is fixed-point simulation seconds.
- `On End` runs after active indexes, owned tags, funding policy, and latent actions are finalized.
  `bWasCancelled` distinguishes cancellation from normal completion.
- Initial native passive grants activate before the entity's `BaseTags` are seeded. Do not depend
  on `BaseTags` inside that first passive `On Activate`; later ticks see the seeded tags.

Self-state writes made during Ability lifecycle callbacks are tracked automatically. If custom
native or Blueprint code mutates an Ability from outside its own lifecycle callback, call
`Mark Deterministic State Dirty` after the mutation.

## Activation APIs

Use **Issue Ability Command** for ordinary gameplay and for one Ability chaining another. It enters
the lockstep command queue for the next simulation tick and reruns authority, cooldown, tag,
target/path, `Can Activate`, capacity, cost, and cancellation gates.

**Activate Ability (Direct)** is a low-level debug, cheat, or replay-reconstruction primitive. It
bypasses the command queue, command authority, tag/target/path/`Can Activate` checks,
`Auto Move Then`, cost deduction, cancellation arbitration, footprint placement, and rejection
reporting. It is not the normal authoring path.

**Get Ability Availability** is an advisory UI query. The queued command remains authoritative and
may still reject for command authority, changed state, pathability, footprint placement, or owned
tag capacity.

## Deterministic state and calls

Ability member variables may use booleans, integer types, names, enums, and structs marked
`SeinDeterministic`, including fixed-point values/vectors/transforms, entity handles, and player
IDs. Blueprint-declared Ability state is canonical even when it is not instance-editable.

Do not store floats/doubles, Unreal vectors/rotators/transforms, strings/text, or object/class/soft
references as Ability member state. Do not call unseeded engine random functions. Use SeinARTS
fixed-point math and deterministic random state. Float or Unreal-transform conversion nodes are
presentation-only and are blocked in Ability and movement graphs. Debug printing and other
presentation calls also belong outside the simulation graph.

`FFixedRandom` stores its complete 128-bit state in the canonical Ability snapshot/hash stream. Seed
it deterministically and keep it as Ability member state when draws span ticks; snapshot, reconnect,
and replay continuation then resume the exact sequence.

Validation is blocking: unsafe member types, untrusted calls, unseeded randomness, unsupported
latent work, and presentation-only calls fail Data Validation rather than producing a dismissible
warning. A deterministic-looking parameter/return signature does not certify a function. Calls must
belong to an explicitly certified function or class, apart from the validator's narrow audited
Kismet Math fallback.

## Checkpoint-safe continuation

An Ability may cross simulation time only through an admitted Sein async node whose declared
latent-action class has a registered checkpoint codec. The shipped **Move To** node satisfies this
contract. UE Delay nodes, Timelines, latent UFunctions, custom async K2 nodes, and unregistered Sein
actions fail with `[SEIN-CHECKPOINT-CONTINUATION]`.

Move To exposes Completed, Failed, Waypoint Reached, Cancelled, Partial Path, and Path Recomputed.
Its result contains `Failure Reason`, zero-based `Waypoint Index`, and `Total Waypoints`. Terminal
outputs do not end the Ability automatically.

Compiler-frame temporaries are not canonical continuation state. If a value will be needed after
Move To or after another downstream async boundary, persist it in deterministic Ability/component
state before that boundary and read or recompute it in the later callback. Unsafe carry-over fails
with `[SEIN-MOVETO-CONTINUATION]`. `End Ability`, `Cancel Ability`, and
`Mark Deterministic State Dirty` are continuation-safe.

## Qualification checklist

1. Compile, save, and run **Validate Assets** on the Ability and owning unit Blueprint.
2. Trigger the Ability through the ordinary command path, not direct activation.
3. Exercise success, every intended rejection, cancellation, refund, cooldown, and rapid reissue.
4. For multi-tick or async Abilities, take and restore a snapshot while active, reconnect a peer,
   and seek a replay checkpoint beyond the activation.
5. Run two-player PIE and confirm both peers remain responsive and report equal canonical roots.
