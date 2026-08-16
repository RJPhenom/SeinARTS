# Squads (SeinARTS Squad Extension)

This opt-in plugin adds persistent, heterogeneous-slot squads on top of the base framework: a
squad is one entity that owns a roster of member slots, dispatches orders to its members through
a slot-aware formation, and can be reinforced (paid, cancellable, per-slot) while the match is
live. Read `Docs/Guides/Units.md` and `Docs/Guides/Formations.md` first — this guide is the
deep-dive on the squad-specific pieces they only summarize.

Requires the base `SeinARTSFramework` plugin, and builds/runs fine when this extension is absent —
nothing in the base depends on it.

## Making a unit a squad

A squad is authored the same way as any other unit: a Blueprint subclass of `SeinActor`. Add a
**Squad Component** to the entity bridge's Component Data array to make that Blueprint a squad
container. The Squad Component's **Slots** array is the squad's recipe; each slot is authored
independently:

- **Entity** — the unit Blueprint class spawned to fill this slot, at squad-create time and every
  time the slot is reinforced. Squads are heterogeneous by design: one slot can be a sergeant,
  four can be riflemen, one a machine-gunner, all in the same squad.
- **Offset Transform** — position and facing relative to the squad's anchor, read by the default
  **Slot Formation** (below). The editor hides this field automatically when the squad's Formation
  Class doesn't consume authored offsets.
- **Reinforce Cost** / **Reinforce Build Time** / **Reinforce Cooldown** — per-slot economy, also
  heterogeneous (a sergeant slot can cost more than a rifleman slot). Build time 0 = instant;
  cooldown 0 = allows back-to-back reinforcement of the same slot.
- **Slot Tags** — descriptive/query metadata (e.g. "Leader", "Rifleman"), not identity: multiple
  slots may share a tag. Declaration index, not tag, is the runtime identity used for formation
  lookup and reinforcement targeting. Leave a slot's **Entity** empty to author a placeholder that
  starts unfilled and is only ever populated by reinforcement.

At match start (or whenever the squad entity spawns) the framework spawns each slot's Entity class
at the squad's transform composed with that slot's offset, wires the spawned member back to its
squad and slot index automatically, and promotes the first live occupant as squad leader — you
never hand-author the member-side back-reference.

Other Squad Component fields worth knowing while authoring: **Formation Class** (empty = the
default Slot Formation; point it at Grid/Wedge/Ring/a custom formation to lay members out by that
shape instead, ignoring the per-slot offsets for layout only), **Containment Mode** (`As One` = the
squad enters a container as a single occupant sized by slot count; `As N` = each member enters
individually), **Avoid As Blob** (off by default; other units route around the whole squad as one
obstacle instead of threading between members), **Reassign Slots Lateral / Depth** (opt-in
re-matching of live members to slots by rank as the squad turns, off by default pins each member to
its authored slot), and **Can Reinforce** (a designer/game-logic toggle the starter reinforce
ability checks).

## How squads behave

Selection is at the squad level: clicking a member selects its squad, and the squad is the sized
element that participates in multi-squad formation gestures.

A squad carries a persistent command broker whose dispatch resolver defaults to the **Squad
Dispatch Resolver**. That resolver treats the squad as one outer formation element in a multi-squad
or mixed order, then lays its own members out inside that element with whichever formation the
squad authored. Predetermined-ability orders dispatch to the squad leader rather than "first
capable member" — leader-performs semantics for squad-granted abilities. Right-click smart-command
orders still resolve per member normally.

The **Slot Formation** is the default layout: each live member goes to its own slot's Offset
Transform, rotated by the squad's facing and added to the squad's anchor. An unauthored squad
(every slot at the identity offset) falls back to a blob at the anchor rather than stacking
members. Preview and commit run the same formation computation, so the order preview shown for
each member is exactly what that member is dispatched to — the base framework's frozen-destination
guarantee applies per member here too.

If a member dies, its slot goes empty (tag and offset stay intact for the next reinforcement) and
it drops from the broker's member list. Dying between an order's preview and admission drops only
that member's own destination — every surviving member still goes exactly where shown. A dead
leader is replaced by the next live occupant in slot declaration order. When every slot is empty,
the reinforcement queue is empty, and no orders are in flight, the squad culls itself.

## Reinforcement

The extension ships a starter **SeinARTS Squad Reinforce Ability**, attached to a squad's Ability
Component like any other ability. Activating it finds the first structurally eligible empty slot in
declaration order (empty, off cooldown, not already queued), charges that slot's authored Reinforce
Cost atomically — the ability itself carries no cost or cooldown, since those are per-slot data —
and appends a queue entry carrying the exact slot index, a monotonic request ID, and a snapshot of
the deducted cost and payer so later ownership or cost changes can't redirect a refund.

The squad system progresses only the front queue entry each tick, over that slot's Reinforce Build
Time. On completion it spawns the slot's Entity class at the squad's current transform (composed
with the slot offset), wires the new member's back-references the same way initial spawn does, and
starts the slot's Reinforce Cooldown; entries behind the front wait their turn.

Cancellation is exact: cancelling one request by its ID reverses its snapshotted deduction and
removes it, with no re-pricing at cancel time. A slot that becomes permanently unreachable (its
Entity class was cleared, or the slot was removed) is refunded automatically instead of retried
forever; a merely transient spawn failure keeps the entry queued for next tick.

Reinforcement is starter content, not a mandatory core rule — projects can replace the ability
outright. The Squad Mutation Library (restricted to Ability/Effect Blueprint graphs) exposes the
same enqueue/cancel calls (**Queue Squad Reinforcement**, **Cancel Squad Reinforcement**) plus
direct slot-fill/empty and membership mutators for custom flows.

## The Cover + Squad bridge

`SeinARTSCoverSquadExtension` is a separate, thin bridge plugin. Enable it only when a project has
**both** Cover and Squad installed — it exists purely so Cover and Squad stay independently
strippable while a project using both gets cover-aware squad dispatch.

The bridge's **Cover Aware Squad Dispatch Resolver** subclasses the Squad Dispatch Resolver and
applies Cover's slot query/claim logic to the squad's inner formation positions, so members claim
authored cover slots through the same shared assignment planner ordinary units use instead of the
plain Slot Formation offsets. Point a squad's **Dispatch Resolver Class** at it directly, or set it
project-wide via the Squad Extension's **Default Squad Dispatch Resolver Class** setting.

## Settings and lockstep

Under **Project Settings → Plugins → SeinARTS Squad Extension**: **Default Squad Dispatch Resolver
Class** (project-wide fallback for squads that don't set their own resolver; leave empty for the
plain slot formation, or point it at the Cover + Squad bridge resolver to make cover-aware dispatch
the project default), and **Pace Squads Together** (on by default — when one order spans more than
one squad, squads pace each other in transit the same way cohesion already keeps one squad's own
members together; off lets each squad keep its own pace and string out; only affects multi-squad
orders).

Both settings are sim-affecting and registered with the lockstep config fingerprint under the
extension's own frozen contributor ID — a client missing the extension, or running a mismatched
value, fails compatibility at join instead of silently desyncing.

## Determinism notes

- Squad Component data, slots, and reinforcement entries are pure fixed-point sim data — no
  Blueprint event graphs; behavior lives in the system, resolver, and ability code that acts on it.
- Slot declaration order is runtime identity. Reordering `Slots` after the squad has spawned breaks
  the back-references live members hold to their slot index; finalize a squad's slot list before
  placing/spawning it, and use the mutation library's index-based calls for any runtime edits.
- A slot's tag is descriptive only, never identity — target one exact slot by its index or by a
  reinforcement's request ID, not by tag.
