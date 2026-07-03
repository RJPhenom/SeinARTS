# Minimap — setup & PIE walkthrough

There is **no minimap widget class** — you build the minimap as a normal Widget Blueprint on the
generic **`SeinUserWidget`** base. C++ provides only the data (a view-model) and a few math/draw
helpers (BPFLs). Everything visual is composed in UMG, so the minimap is fully designer-owned and
re-skinnable, and a widget pays for minimap work only if it actually uses it.

## What the C++ gives you

- **`USeinMinimapViewModel`** — lazily created on first `Get Minimap View Model` (so projects that
  never show a minimap pay nothing); refreshed each sim tick once it exists. Exposes:
  - `Blips` — `TArray<FSeinMinimapBlip>` (north-up normalized pos, `Relation`, `SizeClass`,
    `bSelected`, `Entity`). Enemies already fog-culled; friendlies always shown.
  - `FogTexture` — fog overlay (visible = transparent, explored = dim, unexplored = opaque). Null if no fog.
  - `BackgroundTexture` — per-level override → baked top-down terrain.
  - `WorldBoundsMin` / `WorldBoundsMax` / `GroundZ` / `bHasBounds`.
- **`SeinUserWidget` accessor** — `Get Minimap View Model`.
- **BPFLs** (SeinARTS UI Library):
  - `Get Minimap Rotation Degrees(PC, bRotateWithCamera, RotationOffsetDeg)` — the one rotation value.
  - `Minimap Local To World(LocalPos, WidgetSize, BoundsMin, BoundsMax, GroundZ, MapRotationDeg, bCircle) → World, bInside` — click → world.
  - `Draw Camera Viewport To Render Target(PC, RT, BoundsMin, BoundsMax, GroundZ, Color, Thickness)` — draws the true camera-ground trapezoid (tilt-faithful) into a render target, north-up.
  - `Get Minimap Texture For Level` — override → baked background.
  - `World To Minimap` / `Minimap To World` / `Get Camera Frustum Corners` (existing).
- **Baked background** — **Bake Level Data** now synthesizes a top-down terrain texture into the
  level-data asset; **`Minimap Override Texture`** on `ASeinLevelVolume` overrides it per level.

## Rotation model (read this first)

Everything is authored **north-up** and rotated in exactly one place: a single panel's render
transform. Put the background, fog, blip canvas, and viewport-RT image all inside one **`MapRoot`**
panel, and each tick set `MapRoot`'s render-transform angle to `Get Minimap Rotation Degrees`.
Because the rotation is "align camera-forward with up", the viewport trapezoid ends up upright and
the blips/terrain rotate beneath it — map-rotates-under-a-fixed-arrow behavior, no per-element math.

Handle **clicks on the unrotated root** (not inside `MapRoot`), so `Minimap Local To World` only has
to invert that one rotation value.

## Step 1 — Bake the level (background)

Select your `ASeinLevelVolume` → **SeinARTS ▸ Bake ▸ Bake Level Data**. The baked asset now carries a
`MinimapTexture`. (Blips, fog, and the viewport box don't need the bake — only the terrain background does.)

## Step 2 (optional) — hand-authored background

On the `ASeinLevelVolume`, set **SeinARTS ▸ Minimap ▸ Minimap Override Texture** to any `Texture2D` —
it wins over the baked one (the path to painted hand-authored art).

## Step 3 — create `WBP_Minimap` (parent = SeinUserWidget)

Add ▸ User Interface ▸ Widget Blueprint → reparent to **Sein User Widget** (or pick it as parent).

## Step 4 — widget tree

```
[Root]  (SizeBox, e.g. 256×256)            ← handles mouse; NOT rotated
 └─ Overlay
     ├─ MapRoot (Overlay or Canvas)         ← Visibility = HitTestInvisible; rotated each tick
     │   ├─ BackgroundImage (Image, fill)   ← brush = VM.BackgroundTexture
     │   ├─ FogImage        (Image, fill)   ← brush = VM.FogTexture
     │   ├─ BlipCanvas      (Canvas, fill)  ← pooled blip widgets
     │   └─ ViewportImage   (Image, fill)   ← brush = your Render Target
     └─ (optional frame/border art on top)
```

For a **circle** minimap, apply a circular mask material to the images (or wrap `MapRoot` in a
`RetainerBox` with a round mask), and pass `bCircle = true` to `Minimap Local To World`.

## Step 5 — graph

**Event Construct**
- `VM = Get Minimap View Model` (store it).
- `ViewportRT = Create Render Target 2D` (e.g. 256×256) → `ViewportImage ▸ Set Brush From Texture (ViewportRT)`.
- Init a blip pool: `USeinWorldWidgetPool ▸ Initialize(BlipCanvas, WBP_Blip, ~64)` where `WBP_Blip`
  is a tiny UserWidget (an Image) with a "set color/size" function. (A plain `CanvasPanel` +
  `UImage` children works too.)

**Event Tick**
- `Rot = Get Minimap Rotation Degrees(PC, bRotateWithCamera, RotationOffset)` → `MapRoot ▸ Set Render Transform Angle(Rot)`.
- `Draw Camera Viewport To Render Target(PC, ViewportRT, VM.WorldBoundsMin, VM.WorldBoundsMax, VM.GroundZ, BoxColor, Thickness)`.
- Background/fog: `BackgroundImage ▸ Set Brush From Texture(VM.BackgroundTexture)`, same for `FogImage ← VM.FogTexture` (once, or each tick — cheap).
- Blips: `pool ▸ ReleaseAll`; for each `VM.Blips`: `w = pool ▸ Acquire(blip.Entity)`; set its
  `Render Translation = blip.NormalizedPos * BlipCanvas size`; color it from `blip.Relation`
  (green/red/yellow) and scale from `blip.SizeClass`; outline if `blip.bSelected`.
  (Positions are north-up; `MapRoot`'s rotation handles the display.)

**On Mouse Button Down** (on the Root)
- `Local = Geometry ▸ Absolute To Local(MouseEvent ▸ Screen Space Position)`; `Size = Geometry ▸ Get Local Size`.
- `Rot = Get Minimap Rotation Degrees(...)` (same as tick).
- `Minimap Local To World(Local, Size, VM.Bounds…, VM.GroundZ, Rot, bCircle) → World, bInside`.
- If `bInside`:
  - **Right** mouse → `PlayerController ▸ Issue Smart Command Ex(World, null, IsShiftDown, (0,0,0))`.
  - **Left** mouse → `Cast PlayerController ▸ Get Pawn to SeinCameraPawn` → `Focus On World Point(World)`. (Recenters the pivot's XY; the camera's ground-follow eases Z to the terrain there — only `World.XY` is used. Don't use `Set Camera State` for this — that's snapshot restore and it slams the pivot Z to a flat value, jumping the altitude.)
- Return **Handled** (and capture the mouse on left-press if you want drag-to-scrub).

## Step 6 — HUD

Add `WBP_Minimap` to your HUD (corner anchor). Keep the chain hit-testable (Root = Visible) so it
receives clicks; `MapRoot` stays HitTestInvisible so clicks fall through to the Root.

## Step 7 — PIE checklist

1. Background fills the minimap (baked or override).
2. Blips: green friendly / red enemy / yellow neutral; selected units get an outline; enemies vanish in fog.
3. Fog dims unexplored/explored areas (only if the level has Fog of War active).
4. Viewport trapezoid tracks the camera and **widens as you tilt the camera flatter / shrinks as you tilt top-down**.
5. Left-click jumps the camera; left-drag scrubs (if you captured the mouse).
6. Right-click with units selected → move there; shift-right-click queues.
7. Rotate the camera (Q/E or Alt+MMB) → the whole map rotates so your view points up; the viewport box stays upright.

### Tuning

**Project-wide fog defaults** live in **Project Settings ▸ Plugins ▸ SeinARTS ▸ UI ▸ Minimap**:
`Fog Texture Resolution` (default 256), `Fog Update Interval` (4), `Fog Blur Radius` (0 = off), and the
`Fog Explored`/`Fog Unexplored` overlay colors. The minimap view-model seeds itself from these on
init — set them once and every minimap picks them up. For a one-off per-widget override, set the
matching property on the VM in Construct (e.g. `VM ▸ Set Fog Blur Radius`); the settings are just
the default.

Other knobs (no C++):
- **"Up" is wrong / spins the wrong way** → `Rotation Offset Deg` (try 90, 180, -90).
- **Background mirrored vs the world** (N/S or E/W swapped) → that's a UV-axis convention, not rotation.
  Ping me and I'll add a `bFlipMinimapY` toggle to the bake / `World To Minimap`.
- **Blip size/color** → your `WBP_Blip` + the `Relation`/`SizeClass` mapping.
- **A non-unit entity is blipping** (smoke / vfx emitter, environmental prop) → add the
  `SeinARTS.UI.Minimap.Hidden` tag to that entity's bridge **Base Tags**. The minimap skips
  anything carrying it; no tag = shown, so it never hides a real unit by accident.
  (Presence-less *abstract* entities — command brokers spawned per order, scenario owners, etc. —
  are skipped automatically since they have no render actor; the tag is only for real actors.)
- **Fog still chunky** → raise `Fog Blur Radius` and/or `Fog Texture Resolution` in the settings page above.
- **Viewport box at very low camera tilt** → if the camera looks near the horizon, some screen
  corners stop hitting the ground and the box legitimately disappears (it would be infinite). The
  camera pawn clamps pitch to ≤ -5°, so this is an edge case; tell me if you want a clamp instead.

## Known follow-ups (polish, not blocking)

- Per-type blip **icons** are supported: set `Minimap Icon` on the unit's `FSeinIdentityComponent`
  and the blip carries it as `Icon` (the VM populates it). `WBP_Blip` draws it (tint by relation),
  falling back to the dot when unset. Per-type *size* is still a `Medium` stub — derive size from
  the icon art, or ask for a `MinimapIconScale` field on identity.
- Drag-select on the minimap (left-drag currently scrubs the camera).
- A shipped circular-mask material so circle mode is one click.
- Ping markers / objective icons — layer more images in `MapRoot`.
