/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCapturePointData.h
 * @brief   Capture-point component.
 *          Ships as a sim data struct + pluggable `CaptureAbility`. Framework
 *          never computes capture logic itself — the designer-assigned ability
 *          drives progress / ownership transition / contesting.
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Core/SeinEntityHandle.h"
#include "Core/SeinPlayerID.h"
#include "SeinCapturePointData.generated.h"

class USeinAbility;

USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinCapturePointData
{
	GENERATED_BODY()

	/** Ability that drives capture logic. Framework ships `USeinCaptureAbility_Proximity`
	 *  as a starter; designers override with their own (action-capture, aura-capture, etc.). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|CapturePoint")
	TSubclassOf<USeinAbility> CaptureAbility;

	/** Effective radius of capture influence (world units). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|CapturePoint")
	FFixedPoint CaptureRadius = FFixedPoint::FromInt(500);

	/** Time (seconds) for a single unit's uncontested capture. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|CapturePoint")
	FFixedPoint CaptureTime = FFixedPoint::FromInt(10);

	/** Current progress in [0..1] toward CapturingPlayer completing the capture. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|CapturePoint")
	FFixedPoint CurrentProgress = FFixedPoint::Zero;

	/** Currently-owning player (Neutral before first capture). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|CapturePoint")
	FSeinPlayerID CurrentOwner;

	/** Player currently advancing capture (set by the capture ability). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|CapturePoint")
	FSeinPlayerID CapturingPlayer;

	/** Entities within capture radius advancing / contesting progress. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|CapturePoint")
	TArray<FSeinEntityHandle> Contesters;
};
