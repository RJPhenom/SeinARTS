#pragma once

#include "Abilities/SeinAbility.h"
#include "Actor/SeinActor.h"
#include "SeinNavigation.h"
#include "SeinConsumerMovementQualification.generated.h"

/** Generated-consumer move ability used only by packaged qualification. */
UCLASS()
class SEINCONSUMER_API USeinConsumerQualificationMoveAbility
	: public USeinAbility
{
	GENERATED_BODY()

public:
	USeinConsumerQualificationMoveAbility();
	virtual void OnActivate_Implementation() override;
};

/** All-open deterministic plane used by the disposable Movement+ consumer. */
UCLASS()
class SEINCONSUMER_API USeinConsumerQualificationNavigation
	: public USeinNavigation
{
	GENERATED_BODY()

public:
	virtual bool HasRuntimeData() const override { return true; }
	virtual bool ComputeStaticEnvironmentDigest(
		FGuid& OutDigest,
		FString& OutError) const override;
	virtual bool ComputeStateCoverageClaim(
		FSeinNavigationStateCoverageClaim& OutClaim,
		FString& OutError) const override;
	virtual bool FindPath(
		const FSeinPathRequest& Request,
		FSeinPath& OutPath) const override;
	virtual bool FindCellPath(
		const FSeinPathRequest& Request,
		FSeinPath& OutPath) const override;
	virtual bool IsPassable(const FFixedVector& WorldPos) const override;
	virtual bool IsWorldPositionClear(
		const FFixedVector& WorldPos,
		uint8 AgentNavLayerMask) const override;
	virtual bool GetCellHeightAt(
		const FFixedVector& WorldPos,
		FFixedPoint& OutZ,
		bool bWalkableOnly = true) const override;
};

/** Abstract slot-2 wheeled fixture materialized by normal match bootstrap. */
UCLASS()
class SEINCONSUMER_API ASeinConsumerMovementUnit : public ASeinActor
{
	GENERATED_BODY()

public:
	ASeinConsumerMovementUnit();
};
