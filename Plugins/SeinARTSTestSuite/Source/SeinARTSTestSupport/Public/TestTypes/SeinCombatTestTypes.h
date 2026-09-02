/**
 * SeinARTS Test Suite - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinCombatTestTypes.h
 * @author       RJ Macklem
 * @created      21 Aug 2026
 * @latest       23 Aug 2026
 * @brief        Declares native Combat policy doubles and the designer-style
 *               vitals fixture used by framework combat toolkit tests.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Combat/SeinTargetScorer.h"
#include "Components/SeinPayload.h"
#include "Types/FixedPoint.h"
#include "SeinCombatTestTypes.generated.h"

/** Stand-in for a DESIGNER-authored vitals component. The framework ships no
 *  vitals schema; this is exactly the kind of struct a game authors (natively
 *  or as a UDS) and then drives with Apply Field Delta. */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSTESTSUPPORT_API FSeinTestVitalsComponent : public FSeinPayload
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test")
	FFixedPoint Health = FFixedPoint::Zero;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test")
	FFixedPoint MaxHealth = FFixedPoint::Zero;

	/** A non-fixed-point field so tests can prove Apply Field Delta refuses
	 *  anything that is not an FFixedPoint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test")
	int32 Armor = 0;

	static FSeinTestVitalsComponent Make(int32 InMaxHealth)
	{
		FSeinTestVitalsComponent Vitals;
		Vitals.MaxHealth = FFixedPoint::FromInt(InMaxHealth);
		Vitals.Health = FFixedPoint::FromInt(InMaxHealth);
		return Vitals;
	}
};

/** Test policy that admits every mechanically valid candidate and prefers the
 *  farthest one, proving non-default scorer dispatch and bounded ordering. */
UCLASS()
class SEINARTSTESTSUPPORT_API USeinFarthestTargetScorerTestDouble
	: public USeinTargetScorer
{
	GENERATED_BODY()

public:
	virtual bool IsValidTarget_Implementation(
		const USeinWorldSubsystem* World,
		const FSeinTargetQuery& Query,
		FSeinEntityHandle Candidate) const override;

	virtual FFixedPoint ScoreTarget_Implementation(
		const USeinWorldSubsystem* World,
		const FSeinTargetQuery& Query,
		const FSeinTargetCandidate& Candidate) const override;
};

/** Test policy that rejects any candidate whose test vitals are at or below
 *  zero — a designer-style "don't shoot corpses" rule, proving validity is
 *  policy (and that it stays live while the position index is warm). */
UCLASS()
class SEINARTSTESTSUPPORT_API USeinLivingVitalsTargetScorerTestDouble
	: public USeinTargetScorer
{
	GENERATED_BODY()

public:
	virtual bool IsValidTarget_Implementation(
		const USeinWorldSubsystem* World,
		const FSeinTargetQuery& Query,
		FSeinEntityHandle Candidate) const override;
};
