/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinTargeterSpec.h
 * @brief   Per-ability targeter specification — declarative data describing how
 *          the targeter UI should capture target points for an ability.
 *
 *          Authored as an instanced UObject on USeinAbility::TargeterSpec. The
 *          USeinTargeterSubsystem reads it on activation to:
 *            - decide how many target points to capture (TargetCount)
 *            - pick which preview class to spawn
 *            - run client-side validation per cycle
 *
 *          Subclasses cover the three input shapes (Phase 1 ships PointTargeterSpec;
 *          PointFacing + Line land in Phase 3 + 4 respectively). The hierarchy is
 *          composition-not-inheritance over USeinAbility — the spec lives on the
 *          ability CDO as an EditDefaultsOnly Instanced UObject so designers can
 *          author per-ability without subclassing the spec.
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/SoftObjectPtr.h"
#include "Types/FixedPoint.h"
#include "SeinTargeterSpec.generated.h"

class ASeinTargeterPreview;
class UStaticMesh;

/**
 * Tri-state result of a client-side targeter validation pass. The targeter
 * subsystem calls Spec->ValidateClient on every cursor tick and tints the
 * preview accordingly. Server re-validates authoritatively on ProcessCommands;
 * client validation is purely UX-feedback and (optionally) click-blocking.
 */
UENUM(BlueprintType)
enum class ESeinTargeterValidity : uint8
{
	/** Confirm allowed; preview tint normal. */
	Valid,
	/** Out of range / overlapping / missing requirement; preview tint warning.
	 *  By default the click is still accepted (server will reject) — flip
	 *  bRejectClickWhenBlocked on the spec for strict behavior (e.g. building placement). */
	Blocked,
	/** Allowed but suboptimal — yellow tint, click accepted. Reserved for future
	 *  uses (out of LOS, foggy area, etc.) — Phase 1 specs only emit Valid/Blocked. */
	Warning
};

/**
 * Base class for targeter specifications. Abstract — designers pick a concrete
 * subclass per ability.
 *
 * The spec describes:
 *   - How many target points the targeter must capture before submitting (TargetCount)
 *   - Which preview class to spawn (PreviewClass; subclass default if null)
 *   - Whether RMB-up beyond a drag threshold is meaningful (handled per-subclass)
 *   - Strict-click vs let-through behavior on Blocked validity (bRejectClickWhenBlocked)
 *
 * Subclasses add their own visualization parameters (radius, footprint ref,
 * line max length, etc.) that drive both preview rendering and client validation.
 */
UCLASS(Abstract, EditInlineNew, Blueprintable, DefaultToInstanced)
class SEINARTSCOREENTITY_API USeinTargeterSpec : public UObject
{
	GENERATED_BODY()

public:
	/** Number of target points the targeter must capture before submitting the
	 *  command. Most abilities are 1; multi-target abilities ("throw 3 grenades",
	 *  patrol-path waypoints) use higher values. After the Nth confirm, the
	 *  targeter packs all N FSeinTargeterPoints into the broker order payload
	 *  and submits. RMB cancels the in-progress capture (does not partial-submit).
	 *  Range [1, 16] — multi-target abilities beyond 16 should consider a
	 *  different UX (e.g., free-form waypoint mode in a future spec). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Targeter",
		meta = (ClampMin = "1", ClampMax = "16", UIMin = "1", UIMax = "8"))
	int32 TargetCount = 1;

	/** Preview actor class to spawn while the targeter is active. Empty path =
	 *  use the subclass default (each spec subclass exposes a sensible default
	 *  preview). Override per-ability to swap visual style (e.g., a grenade with
	 *  a custom thrown-arc preview vs. the default radius ring).
	 *
	 *  FSoftClassPath rather than TSoftClassPtr<ASeinTargeterPreview> so this
	 *  spec (in CoreEntity) can reference the preview actor base class (in
	 *  SeinARTSFramework) without forcing a cross-module hard dependency.
	 *  TSoftClassPtr would still emit a UHT forward declaration of the typed
	 *  class with the wrong DLL-export macro; FSoftClassPath stores only the
	 *  path string. Same pattern the plugin uses for the navigation class
	 *  picker (see USeinARTSCoreSettings::NavigationClass). The targeter
	 *  subsystem resolves the path on Activate (sync load, then cached). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Targeter",
		meta = (
			MetaClass = "/Script/SeinARTSFramework.SeinTargeterPreview"))
	FSoftClassPath PreviewClass;

	/** When true, RMB clicks on cells the spec considers Blocked are eaten —
	 *  the targeter stays active, the cycle does not advance. Use for strict
	 *  building placement. When false (default for combat abilities), the click
	 *  submits and the server rejects with the appropriate reason — gives the
	 *  player "I tried" feedback over silent input-eating. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Targeter")
	bool bRejectClickWhenBlocked = false;

	/** Run client-side validation for the cursor's current cycle. Default
	 *  implementation returns Valid; subclasses override to add range / footprint
	 *  / line-length checks. The result drives preview tinting and (if
	 *  bRejectClickWhenBlocked) click acceptance.
	 *
	 *  This is a blueprint-overridable hook so designers can layer ability-
	 *  specific rules (e.g., "smoke can only land on terrain not buildings") via
	 *  subclassing without C++. The result is advisory — server re-validates. */
	UFUNCTION(BlueprintNativeEvent, Category = "Targeter")
	ESeinTargeterValidity ValidateClient(const FFixedVector& CursorWorld,
		const FFixedVector& AuxWorld) const;
	virtual ESeinTargeterValidity ValidateClient_Implementation(const FFixedVector& CursorWorld,
		const FFixedVector& AuxWorld) const { return ESeinTargeterValidity::Valid; }

	/** Returns the soft preview class path to spawn — PreviewClass if non-empty,
	 *  else the subclass default. Subclasses override GetDefaultPreviewClass()
	 *  to declare their built-in preview type. Caller (USeinTargeterSubsystem)
	 *  resolves the path to a UClass* via TryLoadClass on Activate. */
	FSoftClassPath ResolvePreviewClass() const;

protected:
	/** Subclasses override to declare their default preview class path. Base
	 *  returns an empty path — concrete specs MUST override to provide a
	 *  default, and the framework subsystem falls back to
	 *  ASeinPointTargeterPreview if neither side returns a class. */
	virtual FSoftClassPath GetDefaultPreviewClass() const { return FSoftClassPath(); }
};

/**
 * Single-RMB-click point targeter. Captures one world position per cycle.
 *
 * Used for: smoke grenades, abilities targeting a ground point, AoE casts where
 * only the impact location matters. Optional AreaRadius drives a preview circle
 * for AoE feedback (the radius value is read from USeinAbility::AreaRadius
 * when this spec is attached — kept on the ability so designers don't have to
 * mirror it in two places).
 *
 * For multi-target ("throw 3 grenades"), set TargetCount > 1; the targeter
 * loops N click cycles before submitting.
 */
UCLASS(BlueprintType, EditInlineNew, meta = (DisplayName = "Point Targeter Spec"))
class SEINARTSCOREENTITY_API USeinPointTargeterSpec : public USeinTargeterSpec
{
	GENERATED_BODY()

public:
	USeinPointTargeterSpec();

protected:
	virtual FSoftClassPath GetDefaultPreviewClass() const override;
};

/**
 * Drag-rotate point-and-facing targeter — the building-placement spec.
 *
 * Capture flow (Phase 3):
 *   1. Player presses RMB on the world point where the building should land.
 *      The location is locked at this moment; subsequent mouse motion only
 *      affects rotation, not position.
 *   2. Mouse drag computes a direction vector from the locked point to the
 *      cursor; that vector's yaw is snapped to RotationStepDegrees and shown
 *      on the hologram.
 *   3. Player releases RMB to confirm. Captured point's RotationStep encodes
 *      which quantized step was chosen (0..StepCount-1).
 *   4. Releasing on a Blocked validity tint behaves per bRejectClickWhenBlocked
 *      — strict for buildings (eat the click), permissive for other uses.
 *
 * The spec references the building's Blueprint actor class via BuildingClass
 * (soft pointer). At Activate time the targeter pulls the CDO's extents
 * (USeinExtentsComponent + FSeinExtentsComponent) for footprint visualization and
 * validation; the preview reads the CDO's static mesh component for the
 * hologram. Designers can override the preview mesh per-spec via
 * PreviewMeshOverride when the runtime mesh is unsuitable for ghosting
 * (animated, multi-mesh assemblies, etc.).
 */
UCLASS(BlueprintType, EditInlineNew, meta = (DisplayName = "Point + Facing Targeter Spec"))
class SEINARTSCOREENTITY_API USeinPointFacingTargeterSpec : public USeinTargeterSpec
{
	GENERATED_BODY()

public:
	USeinPointFacingTargeterSpec();

	/** Soft reference to the actor Blueprint that this spec places. The
	 *  targeter reads the CDO at Activate time to pull extents (footprint
	 *  validation) and mesh (hologram). The eventual spawn at confirm time
	 *  is the ability's responsibility — typically OnActivate spawns this
	 *  same class at TargeterPoints[0].Location with the captured rotation.
	 *
	 *  Soft so the spec can live in CoreEntity and reference user-authored
	 *  ASeinActor BPs in game content without forcing a hard load until the
	 *  player triggers the ability. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Targeter")
	TSoftClassPtr<AActor> BuildingClass;

	/** Quantization step for the captured rotation, in degrees.
	 *
	 *    0   → FREE ROTATION (no snap). Hologram + captured yaw match cursor
	 *          direction continuously. Typical for standard click-to-place RTS placement.
	 *    45  → octagonal snap (8 steps).
	 *    90  → cardinal snap (4 steps). Typical for grid-aligned RTS.
	 *    any → 360 / RotationStepDegrees discrete steps.
	 *
	 *  When > 0, the captured point's RotationStep is the snapped index
	 *  (0..StepCount-1) AND its YawDegrees is the snapped degrees. When = 0,
	 *  RotationStep is always 0 and YawDegrees is the raw cursor yaw.
	 *
	 *  Most OnActivate logic should read YawDegrees uniformly — it works for
	 *  both snapped and free modes. RotationStep is kept for cases where the
	 *  integer step is genuinely useful (mapping to FSeinFootprintData's
	 *  90°-stepped Rotation, etc.). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Targeter",
		meta = (ClampMin = "0", ClampMax = "360", UIMin = "0", UIMax = "180"))
	int32 RotationStepDegrees = 0;

	/** Optional override for the hologram mesh. When null (default), the
	 *  preview pulls the static mesh component off BuildingClass's CDO.
	 *  Set this only when the runtime mesh is unsuitable for ghosting —
	 *  e.g., the building has a skeletal mesh, a multi-component assembly,
	 *  or a procedural mesh that costs too much to instance for preview. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Targeter")
	TSoftObjectPtr<UStaticMesh> PreviewMeshOverride;

protected:
	virtual FSoftClassPath GetDefaultPreviewClass() const override;
};
