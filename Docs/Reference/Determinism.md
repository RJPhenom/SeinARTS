# Determinism Rules

Every peer runs the whole simulation and must produce bit-identical state every tick. These are
the rules that keep that true, and the tooling that enforces them so mistakes surface in the
editor instead of as a mid-match desync.

## The rules

1. **Simulation math is fixed-point.** Simulation code and data use `FixedPoint` types (a 64-bit
   32.32 format), `FixedVector`, `FixedRotator`, `FixedTransform`, and the deterministic random
   stream. `float`, engine vector math, and unseeded randomness are presentation-only.
2. **Simulation state lives in simulation components.** A unit's authoritative data is the
   Component Data array on its Entity Bridge, copied into simulation storage at spawn. The actor,
   its meshes, and its Blueprint variables are presentation.
3. **Entities are handles, never pointers.** Simulation references between entities use
   generational entity handles.
4. **The simulation talks to the render layer one way.** Presentation reads simulation state and
   reacts to visual events. Input reaches the simulation only as commands (which the lockstep
   layer schedules identically on every peer).
5. **Anything that can affect a future tick must be canonical state.** If you build a custom
   system (navigation, fog, collision, cover) it must either declare itself stateless or register
   its state with the canonical-state registry — bootstrap refuses to start otherwise.

## What enforces them

You do not have to memorize the rules; the tooling holds the line:

- **The component picker** only accepts deterministic structs in Component Data.
- **The struct validator** physically removes non-deterministic fields (floats, object
  references, strings) from designer-created simulation structs on edit, with a notification.
- **Blueprint validators** block or flag calls to non-deterministic functions in ability,
  movement, command, and formation graphs — including unseeded engine randomness and
  presentation-only conversion nodes. Blocking validators fail the save/cook.
- **The continuation compile gate** rejects Blueprint graph shapes around latent actions (such
  as Move To) that could not survive a mid-action save/restore or reconnection.
- **The config fingerprint** freezes simulation-affecting project settings at match start and
  re-validates them every tick; live edits fail-stop the match with an on-screen message.
- **The simulation-content manifest** digests your simulation content so mismatched peers fail
  at join.
- **State roots** — a 128-bit digest of the entire canonical state — are exchanged between peers
  during play. Divergence is announced on every screen with each peer's root.

## Practical guidance

- Author gameplay as **Abilities** (Blueprintable, latent-action driven). Move, attack, build,
  and produce are all abilities; there is no hardcoded command list to fight.
- Use the typed **Get/Set Component** Blueprint nodes for simulation data access; they are
  deterministic and validated.
- Keep visuals reactive: subscribe to visual events on the Entity Bridge from your own render
  components instead of polling or mutating simulation state.
- Editor-time randomness (for example scattering authored cover slots) is fine **only** because
  the result is serialized as fixed-point data; never repeat a random operation at runtime.
- When you need to verify a change yourself: the console command family under `Sein.` includes
  state-root dumps, incremental-root verification, desync simulation, and replay save/load.
