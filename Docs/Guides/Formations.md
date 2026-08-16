# Formations, Gestures, and the Order Preview

## The invariant

The destinations shown by the order preview are exactly the destinations the committed order
executes. This is structural, not best-effort: the preview and the commit run the same
computation, and since the frozen-destination system landed, the previewed plan itself travels
with the command and is admitted or rejected as a whole — the simulation never silently
recomputes a destination you were shown.

## Gestures

Right-click behavior is a pluggable, Blueprintable **Order Gesture** policy evaluated on the
issuing client:

- **Click** — the selection forms up on the project's default formation at the cursor (or a
  simple gather when single-click formations are disabled).
- **Drag** — the drag line is the formation's **front edge**; the body extends behind it, and
  the facing arrow shows perpendicular-to-drag. The gesture nominates the drag formation
  (default Box) and passes the guide line through to layout.

Override the gesture class in settings to reinterpret drags (paths, splines, custom formation
nomination) without touching simulation code.

## Formations

Formation classes lay a member set out around an anchor: the framework ships Blob, Grid, Box,
Ring, Square, Wedge, and the squad Slot formation (authored per-slot offsets). Formations are
Blueprint-authorable; the layout toolkit and per-member footprint radii are exposed, and every
layout ends with a footprint-aware de-overlap pass so positions never stack.

Multi-squad orders compose: the gesture formation spaces the *squads* as elements (each squad
sized by its whole footprint), then each squad lays out its own members inside its element. Loose
units join the same outer shape.

## Destinations are inputs

A previewed destination is an input to movement, not an opinion navigation may relocate:

- Reachability is resolved once, in the shared preview/commit path (nearest-reachable projection
  of a genuinely unreachable click, elevation-aware so platform edges don't snap to the floor
  below).
- Cover slots are authoritative: an authored slot overrules the coarse navigation bake, the unit
  is delivered to the exact slot, and the preview shows the exact slot with its cover quality
  tinted (fog-gated to what you have scouted).
- Provider-backed destinations reserve their footprint from admission through arrival and while
  the unit stays settled there; contending orders are rejected with a reason rather than
  silently re-planned. Once a unit is moving, interval repaths may legitimately re-route around
  a changed world — the invariant binds the initial submission.

## Preview rendering

The preview renderer is a pluggable backend (mesh quads by default; decal and instanced-mesh
backends ship as alternatives; all five hooks are Blueprint-overridable). Cover quality arrives
as per-cell tint tags through an extension hook, so the base framework stays cover-agnostic.
