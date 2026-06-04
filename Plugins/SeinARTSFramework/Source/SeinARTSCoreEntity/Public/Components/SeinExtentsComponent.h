/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinExtentsComponent.h
 * @brief:   Volumetric "where is this entity in space" payload — the unified
 *           sim-side bounds AND blocker config for nav + fog of war.
 *
 *           Distinct from FSeinStampShape (which models effect footprints —
 *           sight cones, firing arcs, smoke clouds) because the shape vocab
 *           differs: an entity's body is a Box or a Capsule, never a cone.
 *           Cell-iteration consumers convert via SeinExtentsShape::AsStampShape
 *           and reuse SeinStampUtils for the actual rasterization math.
 *
 *           Phase 2 (current): consolidates FSeinNavBlockerData and
 *           FSeinVisionBlockerData. Designers author one shape set per entity
 *           plus the bBlocksNav / bBlocksFogOfWar flags + per-system layer
 *           masks. Layered nav blocking lets you author terrain that only
 *           certain agent classes pass (water → amphibious only, infantry-
 *           only doorways, vehicle-only highways).
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/SeinComponent.h"
// SeinFogVisibilityPolicy.h was included for the historic FogVisibilityPolicy
// field that lived on this struct pre-Phase-5+. Field moved to
// FSeinFogVisibilityComponent in SeinARTSFogOfWar; include dropped.
#include "Stamping/SeinStampShape.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Collision/SeinCollisionTypes.h"
#include "SeinExtentsComponent.generated.h"

UENUM(BlueprintType)
enum class ESeinExtentsShape : uint8
{
	/** Vertical capsule — top-down disc of `Radius`, vertical axis of length
	 *  `Height`. The default for upright units (infantry, props). Same
	 *  cell-coverage as a radial stamp; distinguished here because 3D hit
	 *  tests (future combat) treat it as a swept sphere, not a flat disc. */
	Capsule  UMETA(DisplayName = "Capsule"),

	/** Oriented box — XY half extents aligned with entity yaw + YawOffset,
	 *  vertical extent of `Height`. The default for vehicles, walls, and
	 *  rectangular buildings. Same cell-coverage as a rect stamp. */
	Box      UMETA(DisplayName = "Box")
};

/**
 * Nav layer bits. Mirrors the FoW layer-bit pattern (ESeinFogOfWarLayerBit)
 * for consistency. Bit 0 is the standard "Default" agent class — ungated
 * pathing. Bits 1..7 are designer-defined custom layers (amphibious,
 * heavy-vehicle, friendly-faction, etc.) — author the mapping in plugin
 * settings or just remember by index.
 *
 * Pathing is gated by intersection: if `(AgentNavLayerMask & Blocker.
 * BlockedNavLayerMask) != 0` the blocker affects this agent. So an
 * amphibious unit on bit 1 ignores a water blocker that only blocks bit 0.
 */
UENUM(BlueprintType)
enum class ESeinNavLayerBit : uint8
{
	Default  = 0  UMETA(DisplayName = "Default"),
	N0       = 1  UMETA(DisplayName = "Nav Layer 0"),
	N1       = 2  UMETA(DisplayName = "Nav Layer 1"),
	N2       = 3  UMETA(DisplayName = "Nav Layer 2"),
	N3       = 4  UMETA(DisplayName = "Nav Layer 3"),
	N4       = 5  UMETA(DisplayName = "Nav Layer 4"),
	N5       = 6  UMETA(DisplayName = "Nav Layer 5"),
	N6       = 7  UMETA(DisplayName = "Nav Layer 6")
};

/**
 * One volumetric primitive on an entity. Compound entities (vehicle hull +
 * turret, L-shaped building) author multiple shapes; simple entities just
 * use one. Each shape's pose (LocalOffset + YawOffset) composes with the
 * entity transform — entity-local space.
 *
 * Cone is intentionally not a member of this enum — entity bodies aren't
 * cone-shaped. Sight cones, firing arcs, and similar directional effects
 * use FSeinStampShape on dedicated effect components.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinExtentsShape
{
	GENERATED_BODY()

	/** Primitive kind. Per-shape parameters below show/hide on this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Extents")
	ESeinExtentsShape Shape = ESeinExtentsShape::Capsule;

	/** Local-space position offset from the entity transform, rotated by
	 *  entity yaw at query time. Z is honored — elevated colliders (tank
	 *  turret on chassis, upper floor of a multi-story building) get their
	 *  Z bounds shifted by this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Extents")
	FFixedVector LocalOffset = FFixedVector::ZeroVector;

	/** Yaw rotation (degrees) added to the entity's yaw for this shape's
	 *  orientation. Drives Box axes; ignored for Capsule (rotationally
	 *  symmetric in XY). Use non-zero for off-axis hull sections (e.g.
	 *  tank turret rotated independently of chassis when authored at
	 *  rest-pose). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Extents")
	FFixedPoint YawOffsetDegrees = FFixedPoint::Zero;

	// =========================================================================
	// Capsule
	// =========================================================================

	/** Top-down disc radius (world units). Capsule only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Extents",
		meta = (EditCondition = "Shape == ESeinExtentsShape::Capsule", EditConditionHides, ClampMin = "0.0",
		        DisplayName = "Radius"))
	FFixedPoint Radius = FFixedPoint::FromInt(40);

	// =========================================================================
	// Box
	// =========================================================================

	/** Half extent along the entity's forward axis (after YawOffset). Box
	 *  only. Total length = 2 × HalfExtentX. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Extents",
		meta = (EditCondition = "Shape == ESeinExtentsShape::Box", EditConditionHides, ClampMin = "0.0",
		        DisplayName = "Half Extent (Forward)"))
	FFixedPoint HalfExtentX = FFixedPoint::FromInt(150);

	/** Half extent along the entity's right axis. Box only. Total width =
	 *  2 × HalfExtentY. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Extents",
		meta = (EditCondition = "Shape == ESeinExtentsShape::Box", EditConditionHides, ClampMin = "0.0",
		        DisplayName = "Half Extent (Right)"))
	FFixedPoint HalfExtentY = FFixedPoint::FromInt(100);

	// =========================================================================
	// Common
	// =========================================================================

	/** Vertical extent (world units) above the shape's LocalOffset Z.
	 *  Defines the shape's Z span; capsule top = `EntityZ + LocalOffset.Z +
	 *  Height`. Used by visibility-as-target, FoW shadowcast occlusion
	 *  (when this entity blocks vision), and future combat hit detection.
	 *  Typical: prone infantry ~80, standing infantry ~180, tank ~150,
	 *  building floor ~300.
	 *
	 *  IMPORTANT — Height = 0 means "no vertical extent." Nav blocking is
	 *  unaffected (it uses XY shape only), but FoW BLOCKING is silently
	 *  skipped because a zero-height occluder can't physically block sight
	 *  from any observer above ground. If you want a Capsule that reads
	 *  geometrically as a sphere (Radius == half-height), set Height to
	 *  ~Radius to keep FoW blocking working. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Extents",
		meta = (ClampMin = "0.0",
		        ToolTip = "Vertical extent above LocalOffset Z. Height = 0 disables FoW blocking for this shape (nav blocking still works). Set ~Radius for sphere-like capsule semantics with FoW blocking intact."))
	FFixedPoint Height = FFixedPoint::FromInt(180);

	/** Adapter: produce the equivalent FSeinStampShape for cell-iteration
	 *  consumers (visibility-as-target, nav stamping, FoW blocker stamping).
	 *  The conversion is cheap — Box → Rect, Capsule → Radial, with
	 *  LocalOffset.Z dropped (stamps are planar). Kept inline so the cost
	 *  is folded into the consumer's loop. */
	FSeinStampShape AsStampShape() const
	{
		FSeinStampShape S;
		S.bEnabled = true;
		S.LocalOffset = FFixedVector(LocalOffset.X, LocalOffset.Y, FFixedPoint::Zero);
		S.YawOffsetDegrees = YawOffsetDegrees;
		if (Shape == ESeinExtentsShape::Capsule)
		{
			S.Shape = ESeinStampShape::Radial;
			S.Radius = Radius;
		}
		else
		{
			S.Shape = ESeinStampShape::Rect;
			S.HalfExtentX = HalfExtentX;
			S.HalfExtentY = HalfExtentY;
		}
		return S;
	}
};

FORCEINLINE uint32 GetTypeHash(const FSeinExtentsShape& Shape)
{
	uint32 Hash = GetTypeHash(static_cast<uint8>(Shape.Shape));
	Hash = HashCombine(Hash, GetTypeHash(Shape.LocalOffset));
	Hash = HashCombine(Hash, GetTypeHash(Shape.YawOffsetDegrees));
	Hash = HashCombine(Hash, GetTypeHash(Shape.Radius));
	Hash = HashCombine(Hash, GetTypeHash(Shape.HalfExtentX));
	Hash = HashCombine(Hash, GetTypeHash(Shape.HalfExtentY));
	Hash = HashCombine(Hash, GetTypeHash(Shape.Height));
	return Hash;
}

/**
 * Volumetric extents of a sim entity. Carried into sim storage by
 * USeinExtentsComponent.
 *
 * Single source of truth for an entity's physical presence. Three concerns
 * fold in:
 *   1. Where is this entity? — Shapes (capsule / box / compound)
 *   2. Does it block pathfinding? — bBlocksNav + BlockedNavLayerMask
 *   3. Does it occlude vision? — bBlocksFogOfWar + BlockedFogOfWarLayerMask
 *
 * All three flags default off (opt-in). A bare extents component just
 * provides visibility-as-target geometry; flip the flags to add path /
 * vision blocking semantics.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinExtentsComponent : public FSeinComponent
{
	GENERATED_BODY()

	/** One or more volumetric primitives describing the entity's body.
	 *  One entry covers most cases (capsule for infantry, box for tanks);
	 *  multiple for asymmetric or compound bodies. Empty array → consumers
	 *  fall back to single-point center-only checks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Extents")
	TArray<FSeinExtentsShape> Shapes;

	// =========================================================================
	// Navigation
	// =========================================================================
	//
	// Two complementary navigation concerns on one component:
	//   - `bBakesIntoNav` — STATIC bake (one-time, level cook). Walls,
	//     buildings, props rasterise their footprint into the nav grid
	//     before any sim runs.
	//   - `bBlocksNav` + `BlockedNavLayerMask` — DYNAMIC runtime blocking.
	//     Tanks, vehicles, deployable cover register/unregister as they
	//     spawn/die.
	// Authoring rule: bake what's permanent at design time; block what
	// moves at runtime. Both default OFF (opt-in).

	/** Whether this entity class's render-side primitives (meshes, colliders) get
	 *  rasterized into the STATIC nav bake — units can't path through them.
	 *  Distinct from `bBlocksNav`: that flag drives runtime per-entity blocking
	 *  (dynamic blockers added/removed as entities spawn/die), while THIS flag
	 *  drives the one-time bake of static geometry into the nav grid.
	 *
	 *  Default OFF (opt-in). Set true for static entity classes (buildings, walls,
	 *  props). The baker auto-skips any entity class whose CDO carries a movement
	 *  component, so mobile units (vehicles, infantry) don't carve themselves
	 *  into the static nav even when this flag is left on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Extents")
	bool bBakesIntoNav = false;

	/** Whether this entity's footprint blocks pathfinding for OTHER agents.
	 *  Default OFF — adding the component doesn't make you a path blocker;
	 *  set true for tanks, vehicles, buildings, deployable cover. Infantry
	 *  typically leaves this off (steering / penetration handles unit-on-unit
	 *  spacing without hard pathing blocks). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Extents")
	bool bBlocksNav = false;

	/** Layer mask of agents this blocker affects. Pathing is gated by
	 *  intersection: `(AgentMask & BlockedNavLayerMask) != 0` → blocked.
	 *  Default 0x01 = blocks bit 0 (Default agents). Water terrain authors
	 *  this as 0x01 (blocks default), then amphibious units carry an
	 *  additional bit (e.g. 0x02) so their `(0x03 & 0x01) = 0x01` still
	 *  blocks them — set water to 0x01 only and amphibious agents to 0x02
	 *  ALONE for the "amphibious skips water" pattern. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Extents",
		meta = (EditCondition = "bBlocksNav", Bitmask, BitmaskEnum = "/Script/SeinARTSCoreEntity.ESeinNavLayerBit"))
	uint8 BlockedNavLayerMask = 0x01; // Default bit

	// =========================================================================
	// Fog Of War
	// =========================================================================
	//
	// Same split as nav: `bBakesIntoFogOfWar` is the STATIC bake (level cook,
	// permanent occluders), `bBlocksFogOfWar` + `BlockedFogOfWarLayerMask`
	// are DYNAMIC runtime occluders (smoke clouds, destructible covers).
	// Both default OFF.

	/** Whether this entity class's render-side primitives bake into the static
	 *  fog-of-war grid — walls, buildings, and props occlude vision.
	 *  Distinct from `bBlocksFogOfWar`: that flag is the runtime per-entity
	 *  occluder (dynamic, e.g. smoke clouds), while THIS flag drives the
	 *  one-time bake of static occluders into the FoW grid.
	 *
	 *  Default OFF (opt-in). Intentionally a separate flag from
	 *  `bBakesIntoNav` because the use cases diverge:
	 *    - Fence: pathable (`bBakesIntoNav=false`) but vision-blocking
	 *      (`bBakesIntoFogOfWar=true`).
	 *    - Glass wall: occlude paths (`bBakesIntoNav=true`) but transparent
	 *      to sight (`bBakesIntoFogOfWar=false`).
	 *  Like the nav baker, the FoW baker auto-skips entity classes with a movement
	 *  component regardless of this flag. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Extents")
	bool bBakesIntoFogOfWar = false;

	/** Whether this entity's footprint occludes vision in the fog-of-war
	 *  shadowcast. Default OFF — set true for buildings, walls, smoke,
	 *  destructibles. Per-shape `Height` drives the occluder height
	 *  (multiple shapes stamp their own heights; per-cell max wins). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Extents")
	bool bBlocksFogOfWar = false;

	/** Which EVNNNNNN bits this blocker occludes. Bit 1 = V (Normal), bits
	 *  2..7 = N0..N5. Bit 0 (Explored) is never blocked (history is sticky).
	 *  Default 0xFE = blocks every layer. Per-layer policy: smoke might
	 *  block Normal but not Thermal — clear the Thermal bit and Thermal
	 *  vision passes through. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Extents",
		meta = (EditCondition = "bBlocksFogOfWar", Bitmask, BitmaskEnum = "/Script/SeinARTSFogOfWar.ESeinFogOfWarLayerBit"))
	uint8 BlockedFogOfWarLayerMask = 0xFE;

	// =========================================================================
	// Collision (extent-vs-extent)
	// =========================================================================
	//
	// The collision layer pushes overlapping bodies apart so two colliders never
	// occupy the same space — and, crucially, so a unit can never be shoved
	// THROUGH another solid collider (a wall especially). It reuses the Shapes
	// above as the entity's physical body and is INDEPENDENT of navigation: this
	// section never consults bBlocksNav or the nav grid, and nav never consults
	// these fields. A collider need not block nav; a nav blocker need not be a
	// collider. All opt-in (bCollisionEnabled default OFF), same as the nav/FoW
	// flags.

	/** Master switch for this entity's participation in the collision layer.
	 *  OFF (default) → invisible to collision: not inserted into the collision
	 *  broadphase, never pushed, never generates overlap events (the cheap path).
	 *  ON → the Shapes above act as colliders per the channel model below. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Collision")
	bool bCollisionEnabled = false;

	/** Whether this collider's transform can change at runtime.
	 *  Static  → never displaced (infinite mass), cached in the broadphase's
	 *            persistent tier, and Static↔Static pairs are skipped. Use for
	 *            walls, buildings, fixed scenery — this is what makes "can't be
	 *            pushed through a wall" hold. A destructible-but-fixed building
	 *            is still Static (removed from the broadphase on death, never
	 *            moved).
	 *  Movable → may move (units, pushable props, doors, turrets); finite mass;
	 *            rebuilt into the broadphase each tick.
	 *  NOT inferred from the presence of a movement component — author explicitly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Collision",
		meta = (EditCondition = "bCollisionEnabled", EditConditionHides))
	ESeinCollisionMobility Mobility = ESeinCollisionMobility::Movable;

	/** Relative push mass for Block separation — how hard this collider is to
	 *  shove. When two Movable colliders overlap, the heavier absorbs less of the
	 *  separation (split is mass-weighted: a body of mass M vs mass m moves
	 *  m/(M+m) of the overlap). Beyond the project's Collision Mass Ratio Cutoff
	 *  (Project Settings > Plugins > SeinARTS > Collision) a much-heavier collider
	 *  isn't pushed AT ALL by a much-lighter one — so a mob of infantry can't shove
	 *  a tank. Purely a collision property: NOTHING to do with footprint size, nav,
	 *  or movement. Irrelevant for Static / Stationary (those are infinite mass
	 *  regardless). Values are relative — only ratios matter; 100 is a baseline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Collision",
		meta = (EditCondition = "bCollisionEnabled && Mobility == ESeinCollisionMobility::Movable", EditConditionHides,
		        ClampMin = "0.0", DisplayName = "Mass"))
	FFixedPoint Mass = FFixedPoint::FromInt(100);

	/** Which collision channel this collider IS (its object type). Other
	 *  colliders' response to this channel decides whether they Block, Overlap,
	 *  or Ignore it. Picked from the channel registry in
	 *  Project Settings > Plugins > SeinARTS > Collision — the details panel
	 *  shows a dropdown of channel names. None = no object type → the collider
	 *  is inert even with collision enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Collision",
		meta = (EditCondition = "bCollisionEnabled", EditConditionHides))
	FSeinCollisionObjectType ObjectType;

	/** Per-channel response overrides (Ignore / Overlap / Block). Sparse — any
	 *  channel without an entry uses that channel's DefaultResponse from
	 *  settings. The pairwise outcome is the weaker of the two colliders'
	 *  responses to each other, so Block requires BOTH to Block. The details
	 *  panel renders this as the Unreal-style response matrix; the runtime
	 *  resolves it to a flat per-channel table once when the collider registers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Collision",
		meta = (EditCondition = "bCollisionEnabled", EditConditionHides))
	FSeinCollisionResponseContainer CollisionResponses;

	// =========================================================================
	// Fog-of-war post-reveal visibility was moved off this struct in Phase-5+
	// (2026-05-19). It now lives on `FSeinFogVisibilityComponent` in
	// SeinARTSFogOfWar — top-level so entities WITHOUT a physical body
	// (sim-side VFX anchors, audio emitters, scenario triggers) can still
	// author a visibility policy + emission mask. Authoring rule: if you
	// want non-default fog visibility behavior, add the
	// `FSeinFogVisibilityComponent` entry to ComponentData. Default behavior
	// (no entry) is VisionLayersOnly with Normal-bit emission.
};

FORCEINLINE uint32 GetTypeHash(const FSeinExtentsComponent& Component)
{
	uint32 Hash = GetTypeHash(Component.Shapes.Num());
	for (const FSeinExtentsShape& S : Component.Shapes)
	{
		Hash = HashCombine(Hash, GetTypeHash(S));
	}
	Hash = HashCombine(Hash, GetTypeHash(Component.bBlocksNav));
	Hash = HashCombine(Hash, GetTypeHash(Component.BlockedNavLayerMask));
	Hash = HashCombine(Hash, GetTypeHash(Component.bBlocksFogOfWar));
	Hash = HashCombine(Hash, GetTypeHash(Component.BlockedFogOfWarLayerMask));
	Hash = HashCombine(Hash, GetTypeHash(Component.bBakesIntoNav));
	Hash = HashCombine(Hash, GetTypeHash(Component.bBakesIntoFogOfWar));
	Hash = HashCombine(Hash, GetTypeHash(Component.bCollisionEnabled));
	Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Component.Mobility)));
	Hash = HashCombine(Hash, GetTypeHash(Component.Mass));
	Hash = HashCombine(Hash, GetTypeHash(Component.ObjectType));
	Hash = HashCombine(Hash, GetTypeHash(Component.CollisionResponses));
	return Hash;
}
