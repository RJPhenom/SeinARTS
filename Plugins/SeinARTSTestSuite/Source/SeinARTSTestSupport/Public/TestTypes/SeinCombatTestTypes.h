/**
 * SeinARTS Test Suite - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinCombatTestTypes.h
 * @author       RJ Macklem
 * @created      21 Aug 2026
 * @latest       21 Aug 2026
 * @brief        Declares native Combat policy doubles used by framework tests.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Combat/SeinTargetScorer.h"
#include "SeinCombatTestTypes.generated.h"

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
