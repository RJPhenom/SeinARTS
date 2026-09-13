---
title: Add Fog and a Minimap
description: Render the local player's vision and build a clickable minimap from the UI view model.
---

Complete [Add the Transport Truck](/guides/adding-the-transport-truck/) first. The units already emit vision. This chapter adds the darkened terrain overlay and a north-up minimap.

## 1. Create the fog material

Create `Content/Demo/Materials` and a Material named `M_DemoFog`. Set Material Domain to **Post Process** and choose the Blendable Location after tonemapping.

Add these parameters with their names exactly as written:

| Parameter | Type |
| --- | --- |
| FogTexture | Texture Sample Parameter 2D |
| FogWorldMin | Vector Parameter |
| FogWorldSize | Vector Parameter |

For the material preview, assign `/Engine/EngineResources/WhiteSquareTexture` to FogTexture and set FogWorldSize to `(1000, 1000, 0, 0)`. Enable Show Engine Content to find the texture. The fog render actor replaces the texture and bounds at runtime.

Build the material graph:

1. Add **Absolute World Position**, mask RG to get world X and Y.
2. Mask RG from FogWorldMin and FogWorldSize.
3. Subtract FogWorldMin from world XY, then divide by FogWorldSize. This is the fog UV.
4. Connect the UV to FogTexture.
5. Add **SceneTexture**, Scene Texture Id **PostProcessInput0**, and mask its RGB.
6. Add **Linear Interpolate**: A = scene RGB, B = FogTexture RGB, Alpha = FogTexture A.
7. Connect the result to Emissive Color.

Apply and save. Texture U follows world +X and V follows world +Y; do not flip one axis in this graph.

## 2. Place the fog renderer

Open `LVL_DemoSkirmish`. Add **Sein Fog Of War Render** from Place Actors. Assign `M_DemoFog` to **Fog Post Process Material**. Its post-process volume is unbound, so its transform does not determine the visible area.

Use Unexplored Opacity `1`, Explored Opacity `0.75`, and Smoothing Strength `1` as starting presentation values. Save and run Play Standalone.

Move the Soldier. Nearby terrain should be revealed; visited terrain should darken after leaving sight. If everything stays dark, verify the unit's vision stamp, owning slot, and level bake. If the overlay is absent, check the renderer's material assignment and parameter names.

## 3. Create the minimap widget

In `Content/Demo/Blueprints/UI`, create a SeinUserWidget named `WBP_DemoMinimap`. Use a Size Box root with Width Override and Height Override `240`, containing an Overlay.

Add these full-size layers in this order:

1. Border, background a dark gray.
2. Image named `TerrainImage`.
3. Image named `FogImage`.
4. Canvas Panel named `BlipCanvas`.

Set TerrainImage, FogImage, and BlipCanvas to Not Hit-Testable (Self & All Children). Let the outer widget receive mouse input. Mark the three named layers Is Variable.

Add WBP_DemoMinimap to the HUD Canvas, anchored bottom-right, alignment `(1, 1)`, offset `(-24, -24)`, size `(240, 240)`. Compile and save.

## 4. Read the map layers

In WBP_DemoMinimap, create `RefreshMinimap` and call it from Event Tick.

Call **Get Minimap View Model**. Return unless it is valid and **Has Bounds** is true.

- If Background Texture is valid, Set Brush From Texture on TerrainImage and show it; otherwise collapse that image so the gray Border is visible.
- If Fog Texture is valid, Set Brush From Texture on FogImage and show it; otherwise collapse FogImage.

The gray background is sufficient for the demo's flat floor. The view model supplies fog and blips without a Scene Capture actor.

## 5. Draw entity markers

Create a SeinUserWidget named `WBP_DemoBlip`. Its root is a Border with a white brush, desired size `6 × 6`, and Not Hit-Testable visibility. Add a Linear Color variable `Tint`, Instance Editable and Expose on Spawn. On Construct, set the Border's Brush Color from Tint.

Extend RefreshMinimap:

1. Clear Children on BlipCanvas.
2. For each entry in the view model's **Blips**, choose green for Friendly, red for Enemy, and yellow for Neutral. Use white when Selected is true.
3. Create WBP_DemoBlip with that Tint and the owning player.
4. Add Child to Canvas on BlipCanvas. Set the returned Canvas Panel Slot's Size to `(6, 6)`, Alignment to `(0.5, 0.5)`, and Position to `Normalized Pos * (240, 240)`.

The view model already filters enemy blips by visibility. Do not replace this list with an unfiltered actor search. Rebuilding these noninteractive markers each refresh is sufficient for the small demo.

## 6. Click to move the camera

Override **On Mouse Button Down** in WBP_DemoMinimap.

For Left Mouse Button, read the pointer event's screen-space position and use **Absolute to Local** with the event's Geometry. Divide by Geometry's Local Size to obtain normalized UV. Clamp both coordinates to `0..1`.

Call **Minimap To World** with that UV, the view model's World Bounds Min and Max, and Ground Z. Get Owning Player Pawn and cast it to `BP_DemoSkirmishCameraPawn`. Call **Set Camera State**:

| Input | Value |
| --- | --- |
| Pivot Location | Minimap To World result |
| Yaw | Get Camera Yaw |
| Pitch | Get Camera Pitch |
| Zoom Distance | Get Current Zoom Distance |

Return Handled for the left click, Unhandled for other buttons. This moves only the local camera.

## 7. Test vision and minimap

Run Play Standalone. Check that the local Soldier and Truck appear as friendly markers, deposits appear when permitted by visibility, and moving a unit changes the revealed region. Click different parts of the minimap and confirm the camera lands at the matching world locations.

Load Soldiers into the Truck. Their separate vision should cease while contained. Unload them and check it returns. After the lobby chapter, verify enemy markers disappear outside your sight even though the other player's units still exist.

## Checkpoint

- Fog is rendered from the local observer's vision data.
- The minimap uses the same baked world bounds and visibility-filtered entities.
- A minimap click moves the local camera without issuing a unit order.

Continue with [Create the Skirmish Lobby](/guides/creating-the-skirmish-lobby/).
