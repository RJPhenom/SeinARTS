/**
 * SeinARTS Test Suite - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinEconomyLoopTestTypes.h
 * @author       RJ Macklem
 * @created      21 Aug 2026
 * @latest       21 Aug 2026
 * @brief        Declares designer-style economy components and abilities used
 *               to qualify harvest, dropoff, and construction composition.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Abilities/SeinAbility.h"
#include "Actor/SeinActor.h"
#include "Components/SeinPayload.h"
#include "Types/FixedPoint.h"
#include "SeinEconomyLoopTestTypes.generated.h"

USTRUCT(meta = (SeinDeterministic))
struct FSeinEconomyResourceNodeTestComponent : public FSeinPayload
{
	GENERATED_BODY()

	UPROPERTY()
	FFixedPoint Available = FFixedPoint::Zero;
};

USTRUCT(meta = (SeinDeterministic))
struct FSeinEconomyCargoTestComponent : public FSeinPayload
{
	GENERATED_BODY()

	UPROPERTY()
	FFixedPoint Amount = FFixedPoint::Zero;

	UPROPERTY()
	FFixedPoint Capacity = FFixedPoint::FromInt(20);
};

USTRUCT(meta = (SeinDeterministic))
struct FSeinEconomyDropoffTestComponent : public FSeinPayload
{
	GENERATED_BODY()
};

/** Actor-backed construction target whose bridge payload is scoped by tests. */
UCLASS()
class ASeinEconomyConstructionTestActor : public ASeinActor
{
	GENERATED_BODY()
};

/** Designer-style gather ability composed from generic component read/write. */
UCLASS()
class USeinEconomyGatherTestAbility : public USeinAbility
{
	GENERATED_BODY()

public:
	USeinEconomyGatherTestAbility();
	virtual void OnActivate_Implementation() override;

	UPROPERTY(EditDefaultsOnly)
	FFixedPoint GatherAmount = FFixedPoint::FromInt(10);
};

/** Designer-style dropoff ability composed from cargo and resource APIs. */
UCLASS()
class USeinEconomyDropoffTestAbility : public USeinAbility
{
	GENERATED_BODY()

public:
	USeinEconomyDropoffTestAbility();
	virtual void OnActivate_Implementation() override;
};

/** Designer-style worker ability that contributes progress each fixed tick. */
UCLASS()
class USeinEconomyConstructTestAbility : public USeinAbility
{
	GENERATED_BODY()

public:
	USeinEconomyConstructTestAbility();
	virtual void OnTick_Implementation(FFixedPoint DeltaTime) override;

	UPROPERTY(EditDefaultsOnly)
	FFixedPoint BuildRate = FFixedPoint::FromInt(30);
};
