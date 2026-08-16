# DECISION REQUIRED — Line/Corridor Targeter Shape

**Status:** open product decision, reserved for RJ (OPEN_RISKS: "Public targeting lacks the
complete line/corridor/gesture policy surface"; the targeter API shape is on the explicit
product-decision list).
**Implementation cost once decided:** small-to-medium — the targeter subsystem's drag machinery,
validation flow, and preview-actor seam all exist; this adds one spec class + one preview actor.

## The question

Point specs (click) and point-with-facing specs (building-placement holograms with yaw snapping)
ship today. What is the authoring shape for LINE and CORRIDOR abilities — barrage lines, smoke
walls, trench/wall construction, breach lanes?

## Options

**A. One `LineTargeterSpec`: anchor-drag-end, authored width (0 = pure line).**
Capture = press anchors, drag draws the line, release captures `{Start, End}`; the ability reads
width from the spec. Corridors are just wide lines. Simplest API, one preview actor (rectangle
decal), covers barrage/smoke/wall. Multi-segment shapes need repeated activations.

**B. Multi-point polyline spec: N clicks then confirm.**
Covers trench networks and multi-segment walls in one activation; heavier capture UX (click …
click, confirm/cancel), heavier validation (per-segment), and most RTS line abilities are single
segments — the flexibility mostly taxes the common case.

**C. A with an optional per-spec `MaxSegments`: single-segment default, opt-in polyline.**
The spec carries `Width` and `MaxSegments` (default 1). Segment capture loops exactly like the
existing multi-target point loop already shipped. Slightly larger surface than A; B's capability
without a second class.

## Interactions either way

- Captured points ride the existing `TargeterPoints` command field (fixed-point, already
  wire-validated and bounded), so no protocol change.
- Validation reuses the tri-state client validity flow (valid/warning/blocked) per segment;
  footprint-aware corridor validation (does the lane fit) can come later without API change.
- The preview actor seam is already pluggable; a rectangle/segments decal actor is the only new
  render piece.

## Recommendation

**C** — it is A for every ability that wants A (the default), and the segment loop reuses shipped
capture machinery rather than inventing a new interaction. Name the spec `LineTargeterSpec`,
expose `Width` (0 = line) and `MaxSegments` (1 = classic drag-line), and ship the rectangle
preview actor as the default with the usual Blueprint override path.

**Decide A/B/C (or amend).** Nothing else blocks on this; it is the last unstarted item on the
autonomous continuation queue.
