/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinOrderGesture.h
 * @brief   Render-side, Blueprintable interpreter that turns a right-click /
 *          right-click-drag into an order's GUIDE geometry + a nominated
 *          formation. The pluggable "what does a drag MEAN" seam.
 *
 *          Runs on the issuing client ONLY (ASeinPlayerController invokes it on
 *          command release). Its float output is converted to fixed-point and
 *          baked into the lockstep command, so every client replays an identical
 *          order — determinism is preserved without the gesture being sim code.
 *          Default: a click yields no guide (→ the resolver's default formation,
 *          a blob); a drag yields a line (start→end) nominating
 *          SeinARTS.Formation.Box. Subclass for spline / path-march / box / etc.
 *          and select via ASeinPlayerController::OrderGestureClass.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "SeinOrderGesture.generated.h"

/** Raw captured gesture handed to USeinOrderGesture (render-side, float world space). */
USTRUCT(BlueprintType)
struct FSeinOrderGestureInput
{
	GENERATED_BODY()

	/** True when the cursor moved past the drag threshold during the hold. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Order Gesture")
	bool bIsDrag = false;

	/** World-space drag start (press point). For a click, equals EndWorld. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Order Gesture")
	FVector StartWorld = FVector::ZeroVector;

	/** World-space drag end (release point) / the click point. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Order Gesture")
	FVector EndWorld = FVector::ZeroVector;

	/** Distance-sampled world path captured during the drag (start … end). */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Order Gesture")
	TArray<FVector> PathWorld;

	/** Whether the queue modifier (shift) was held. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Order Gesture")
	bool bShiftHeld = false;
};

/** Result of interpreting a gesture: the order's guide + nominated formation. */
USTRUCT(BlueprintType)
struct FSeinOrderGestureResult
{
	GENERATED_BODY()

	/** Ordered guide geometry in world space. Empty = a simple click (no guide).
	 *  The PC nav-projects + converts these to fixed-point for the command. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Order Gesture")
	TArray<FVector> GuidePoints;

	/** Formation the broker resolver should use (mapped via its FormationsByTag).
	 *  Invalid → the resolver's default formation. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Order Gesture")
	FGameplayTag FormationTag;
};

UCLASS(Blueprintable, EditInlineNew, ClassGroup = (SeinARTS), meta = (DisplayName = "Order Gesture"))
class SEINARTSFRAMEWORK_API USeinOrderGesture : public UObject
{
	GENERATED_BODY()

public:
	USeinOrderGesture();

	/** Formation nominated for a DRAG order. Mapped to a USeinFormation by the
	 *  command broker resolver's FormationsByTag. Default = SeinARTS.Formation.Box. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|Order Gesture",
		meta = (DisplayName = "Drag Formation Tag"))
	FGameplayTag DragFormationTag;

	/** True → forward every sampled drag point as the guide (path / spline orders).
	 *  False (default) → forward just start + end (a straight line). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|Order Gesture",
		meta = (DisplayName = "Forward Full Path"))
	bool bForwardFullPath = false;

	/**
	 * Interpret a captured gesture into an order guide + nominated formation.
	 * Default: click → empty result (resolver's default formation); drag → a line
	 * (start→end, or the full sampled path when bForwardFullPath) nominating
	 * DragFormationTag. Override (BP or C++) to author custom drag semantics.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Order Gesture", meta = (DisplayName = "Build Order"))
	FSeinOrderGestureResult BuildOrder(const FSeinOrderGestureInput& Input);
	virtual FSeinOrderGestureResult BuildOrder_Implementation(const FSeinOrderGestureInput& Input);
};
