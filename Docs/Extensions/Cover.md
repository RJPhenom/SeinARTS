# Cover Extension

The Cover extension turns any Blueprint into a cover provider: a protected zone (and/or a set of
snap-to slots) that gameplay can query for damage modifiers, and that the order system can snap
squads and units onto automatically. It is opt-in and physically independent of Squads — you can
ship Cover alone, Squads alone, or both. If you want squad-formation orders to snap onto cover
(rather than only individual units), you also need the separate **Cover + Squad bridge** plugin,
which depends on both parent extensions and supplies a cover-aware squad dispatch resolver.

Enable the plugin from the Plugins browser (`SeinARTS Cover Extension`), and the bridge separately
(`SeinARTS Cover + Squad Bridge`) if you use Squads and want squad dispatch to respect cover.

## Authoring a cover provider

1. Open (or create) the Blueprint that should provide cover — a sandbag wall, a foxhole, a
   destroyed vehicle, a low fence.
2. On its Entity Bridge component, add a **Cover Component** to Component Data.
3. Set the fields:
   - **Quality Tag** — the cover tier this provider grants: `SeinARTS.Cover.Heavy`,
     `SeinARTS.Cover.Light`, `SeinARTS.Cover.Negative`, or your own tag under
     `SeinARTS.Cover`. One tag per provider — every slot and the area volume on this provider
     report the same quality. If you want mixed qualities on one actor (heavy sandbag corners,
     light fence in the middle), add a second Cover Component and split the geometry between them.
   - **Is Directional** — on for a wall/fence you can be flanked around (cover strength depends on
     shot angle), off for an omnidirectional pit like a foxhole or crater. The slot generator sets
     this automatically (see below); flip it by hand for custom layouts.
   - **Slots** — an array of local-space positions that formation orders can snap members onto.
     Leave empty for an area-only provider.
   - **Area** — a Box or Sphere volume (local space) that grants cover to anything standing inside
     it, regardless of slots. Shape `None` disables area cover for this provider. The area can
     extend past the actor's physical collision (e.g. the protected side behind a wall) — it is
     independent of the actor's Extents Component, which drives physical collision, navigation,
     and fog instead.
   - **Slot Radius** — the footprint radius of a unit standing on a slot. Used at query time to
     reject a slot that overlaps a wall's solid body, to decide which area a slot's quality is
     drawn from, and to de-duplicate slots whose footprints overlap (best quality wins).

### Generating slots

Rather than hand-placing every slot, set up the **Generate** fields and click **Generate Slots** in
the Details panel (added by the Cover editor module directly under the Cover Component):

- **Generate Mode**:
  - **Edge — slots around perimeter**: walks the perimeter of a sibling Extents Component's Box
    shape (the provider's physical body), placing slots outside it by `Generate Slot Inset` so
    units stand on the protected side, inside the cover Area but outside the wall. Sets
    `Is Directional = true`. Requires a Box shape authored on Extents.
  - **Area — slots filling interior**: fills the Cover Component's own Area (Box or Sphere) with
    concentric inset rings, for foxholes, craters, and room interiors. Sets
    `Is Directional = false`. Reads `Area` directly — no sibling shape needed.
- **Generate Slot Count** — total slots to distribute.
- **Generate Slot Inset** — gap from the wall/area edge to each slot's center; in Area mode this is
  also the ring spacing and, with scatter on, the minimum slot-to-slot distance.
- **Scatter Slots** — off gives an evenly spaced perimeter/ring layout; on jitters (Edge) or
  randomizes (Area) slot placement for a less regimented look. Each click re-rolls a fresh layout.

Generate Slots replaces the `Slots` array wholesale — hand-edit individual slots afterward if you
need a specific position the generator won't produce. This is an editor-only convenience; nothing
about it runs at play time.

## Terrain-derived cover

Cover doesn't need a placed provider at all — you can grant cover from baked terrain. Open
**Project Settings → Plugins → SeinARTS Cover Extension** and add entries to **Terrain Cover
Quality**: each row maps one of your project's terrain gameplay tags (authored under
**Project Settings → SeinARTS → Terrain**) to a cover quality tag. For example, mapping
`SeinARTS.Terrain.Road` to `SeinARTS.Cover.Negative` makes exposed roads take more damage with no
providers placed on the map at all. Terrain cover is omnidirectional and, unlike provider cover, is
**not** fog-gated — terrain type isn't hidden information. If a unit is also inside stronger
provider cover (a sandbag wall's Heavy tag, say), the best quality present wins; terrain only wins
when it's the strongest cover at that point.

## How orders use cover

Cover-seeking is per-entity, not global: tag an entity class's base tags with
`SeinARTS.Cover.UsesCover` to make it a cover consumer. Infantry typically carries this tag;
vehicles, aircraft, and buildings typically don't. Untagged members always get their ordinary
formation position, regardless of nearby cover.

When you issue a move order (or draw a formation preview) near cover, and it involves at least one
cover-seeking member:

1. Nearby slots are gathered within **Cover Snap Radius** (Project Settings → Plugins → SeinARTS
   Cover Extension) of the move target — measured per-member, so distant squad members in a large
   formation don't get pulled toward cover that's only near the click point, not near them.
2. A deterministic assignment solves which member goes to which slot, in this priority order:
   maximum number of members placed in cover, then minimum use of a slot's "wrong side" (the side
   facing away from the cover body, if the slot is directional), then minimum total travel distance
   across all reassignments. Members that don't fit inside the radius, or for which no free slot
   remains, keep their ordinary formation position.
3. The order preview tints affected cells with the cover quality the assignment resolved, so
   players can see the plan before committing. Tint colors are a per-tag map (**Cover Quality
   Tints**) you author on your formation preview actor — it ships empty in the base framework, so
   map each quality tag you use (including custom tags) to a color there. Preview tint is
   fog-gated: providers your team hasn't scouted don't appear in the preview or the assignment,
   even though the provider exists in the world.
4. Whatever slot the preview shows is the exact slot the unit is delivered to. A cover slot is
   authoritative over the coarse navigation bake — a designer-placed slot sitting in a cell the nav
   mesh marks unreachable is treated as a false negative, not a reason to relocate the slot. The
   unit is never silently re-snapped to a different point after you've seen the preview.

To enable cover-snap for ordinary (non-squad) move orders, point
**Project Settings → SeinARTS → Navigation → Default Broker Resolver Class** at
**Cover-Aware Default Broker Resolver**. For squad formation dispatch, enable the Cover + Squad
bridge plugin and point **Default Squad Dispatch Resolver Class** (on the SeinARTS Squads
settings page) at its cover-aware squad dispatch resolver. Without the cover module loaded,
neither resolver class exists and normal formation behavior is unchanged.

## Reservations and contention

A cover slot claimed by an in-flight order is reserved from admission through arrival, and while
the unit stays settled there. Reservations only shape *future* previews — a slot already claimed by
one order is filtered out of the candidate list for a later order's preview and assignment, so
players plotting a second squad's move naturally avoid double-claiming a spot another squad is
already headed to.

Reservations never veto or reshape an order that has already been shown to the player. If two
orders race for the same slot inside the brief window before either is admitted (both previewed
before either committed), both are still delivered to the exact points their previews showed — the
physical collision layer settles which unit actually ends up standing there. An order for cover is
never rejected or silently re-planned because of contention. Once a unit is moving and settled,
later interval repaths may legitimately re-resolve a destination the changing world has made
unreachable — that's ordinary re-pathing, not a contention rejection.

## Project settings reference

All under **Project Settings → Plugins → SeinARTS Cover Extension**:

| Setting | Effect |
|---|---|
| Cover System Class | The active cover implementation providers register with and queries run against. Ships with **Sein Cover Default**; empty falls back to the same default. Replace for a spatial-indexed implementation at scale. |
| Cover Snap Radius | Distance (world units, default 500 ≈ 5m) around a move target within which cover slots are eligible for snap. Shared by both the default and squad-dispatch cover-aware resolvers so tuning it once keeps them aligned. |
| Terrain Cover Quality | Map from terrain gameplay tag to cover quality tag (see Terrain-derived cover above). Empty by default — terrain confers no cover until you opt in. |

`Cover System Class`, `Cover Snap Radius`, and `Terrain Cover Quality` all affect simulation
outcomes, so they're registered with the project's lockstep config fingerprint. Every peer in a
match must have matching values for these three settings, or the match fails compatibility at join
rather than risk a silent desync — treat them the same as any other sim-affecting project setting
when setting up a build for multiplayer testing.

## Determinism notes

Cover slot data (`Slots`, `Area`) is fixed-point and part of deterministic entity state, so runtime
queries and assignment are bit-identical across peers. The one place non-determinism is allowed is
the **Scatter Slots** editor button: it uses ordinary (non-seeded) randomness to place slot
candidates, but that only happens at authoring time in the editor. By the time you save the asset,
the scattered positions are serialized as ordinary fixed-point `Slots` values — ordinary deterministic
data from then on. Don't call slot generation from gameplay code or expect it to run at runtime; it
is an editor-only authoring convenience.
