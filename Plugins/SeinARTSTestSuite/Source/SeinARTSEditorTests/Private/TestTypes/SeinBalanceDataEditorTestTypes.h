#pragma once

#include "Abilities/SeinAbility.h"
#include "Actor/SeinActor.h"
#include "Components/SeinComponent.h"
#include "StructUtils/InstancedStruct.h"
#include "Types/FixedPoint.h"
#include "SeinBalanceDataEditorTestTypes.generated.h"

USTRUCT(meta = (SeinDeterministic, SeinSubData))
struct FSeinBalanceEditorTestNestedData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	FFixedPoint TurnRate = FFixedPoint::Zero;

	UPROPERTY(EditAnywhere)
	int32 GearCount = 0;
};

USTRUCT(meta = (SeinDeterministic))
struct FSeinBalanceEditorTestComponent : public FSeinComponent
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	FFixedPoint Speed = FFixedPoint::Zero;

	UPROPERTY(EditAnywhere)
	int32 Armor = 0;

	UPROPERTY(EditAnywhere)
	bool bEnabled = true;

	UPROPERTY(EditAnywhere)
	FInstancedStruct ModeData;

	UPROPERTY()
	int32 RuntimeOnly = 0;
};

UCLASS(Abstract)
class ASeinBalanceEditorTestEntityRoot : public ASeinActor
{
	GENERATED_BODY()
};

UCLASS()
class ASeinBalanceEditorTestEntityA : public ASeinBalanceEditorTestEntityRoot
{
	GENERATED_BODY()

public:
	ASeinBalanceEditorTestEntityA();
};

UCLASS()
class ASeinBalanceEditorTestEntityB : public ASeinBalanceEditorTestEntityRoot
{
	GENERATED_BODY()

public:
	ASeinBalanceEditorTestEntityB();
};

UCLASS(Abstract)
class ASeinBalanceEditorDuplicateRoot : public ASeinActor
{
	GENERATED_BODY()
};

UCLASS(Abstract)
class USeinBalanceEditorTestAbilityRoot : public USeinAbility
{
	GENERATED_BODY()
};

UCLASS()
class USeinBalanceEditorTestAbilityA : public USeinBalanceEditorTestAbilityRoot
{
	GENERATED_BODY()

public:
	USeinBalanceEditorTestAbilityA();
};

UCLASS()
class USeinBalanceEditorTestAbilityB : public USeinBalanceEditorTestAbilityRoot
{
	GENERATED_BODY()

public:
	USeinBalanceEditorTestAbilityB();
};

UCLASS(Abstract)
class USeinBalanceEditorSiblingAbilityRoot : public USeinAbility
{
	GENERATED_BODY()
};

UCLASS(Abstract)
class USeinBalanceEditorPersistedAbilityRoot : public USeinAbility
{
	GENERATED_BODY()
};

UCLASS()
class USeinBalanceEditorSiblingAbilityA
	: public USeinBalanceEditorSiblingAbilityRoot
{
	GENERATED_BODY()

public:
	USeinBalanceEditorSiblingAbilityA();

	UPROPERTY(EditAnywhere)
	FFixedPoint SiblingTuning = FFixedPoint::Zero;
};

UCLASS()
class USeinBalanceEditorSiblingAbilityB
	: public USeinBalanceEditorSiblingAbilityRoot
{
	GENERATED_BODY()

public:
	USeinBalanceEditorSiblingAbilityB();

	UPROPERTY(EditAnywhere)
	FFixedPoint SiblingTuning = FFixedPoint::Zero;
};
