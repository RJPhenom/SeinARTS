# DECIDED — Line/Corridor Targeter Shape

**Status:** DECIDED by RJ 2026-08-15, implemented same day (branch
`codex/feat03-frozen-destinations`). PIE feel check batched.

## The ruling

RJ rejected the framing that A (drag line), B (multi-click polyline), and C (drag with opt-in
segments) are alternatives: they describe **different use cases that need equal support**. A
click-click-click trench UX and a click-drag strafing-run UX are both first-class; the choice is
per-ability.

## What shipped

One spec, both interactions:

- **`USeinLineTargeterSpec`** (CoreEntity, `SeinTargeterSpec.h`) with:
  - `CaptureMode` — `ESeinLineTargeterCapture::Drag` (press-anchor → drag → release captures the
    segment; TargetCount independent gestures) or `MultiClick` (each click after the first commits
    a segment chained from the previous vertex; early-finish by clicking within
    `FinishClickTolerance` of the last vertex).
  - `Width` (`FFixedPoint`, 0 = pure line) — corridor width for preview and gameplay; the ability
    reads it off the spec CDO (identical on every client, so it never rides the wire).
  - `MaxSegmentLength` (0 = unlimited) — `ValidateClient` reports Blocked beyond it; combine with
    `bRejectClickWhenBlocked` for strict enforcement.
  - `TargetCount` (inherited) is the segment count in both modes.
- **Uniform wire encoding:** every captured `FSeinTargeterPoint` is one segment
  (`Location` = start, `AuxLocation` = end) regardless of capture mode — ability `OnActivate`
  logic never cares which interaction produced the segments. Rides the existing bounded
  `TargeterPoints` field; **no protocol change**.
- **`ASeinLineTargeterPreview`** (framework) — ground-projected rectangle decal stretched
  anchor→cursor, width from the spec, validity tinting via the same `TintColor` MID contract as
  the point preview. Committed segments stay visible as frozen decals during multi-segment
  sessions via the new `ASeinTargeterPreview::NotifyPointCaptured`/`OnPointCaptured` hook
  (BP-overridable). Per-ability visual swap via the spec's `PreviewClass`, as before.
- **Subsystem:** line-Drag reuses the existing Dragging state; MultiClick chains vertices inside
  `WaitingForCapture` with in-progress-segment validation and preview anchoring.

Deferred (recorded, non-blocking): footprint-aware corridor validation ("does the lane fit") can
land later inside `ValidateClient`/server validation without any API change.
