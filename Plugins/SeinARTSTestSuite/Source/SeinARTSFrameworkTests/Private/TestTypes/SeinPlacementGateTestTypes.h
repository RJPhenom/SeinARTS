#pragma once
#include "Abilities/SeinAbility.h"
#include "Player/SeinTargeterSubsystem.h"
#include "Targeter/SeinPointFacingTargeterPreview.h"
#include "SeinPlacementGateTestTypes.generated.h"

UCLASS()
class USeinPlacementGateTestAbility : public USeinAbility
{
	GENERATED_BODY()
public:
	USeinPlacementGateTestAbility();
	virtual void OnActivate_Implementation() override;
};

/** Exercises placement through a point gesture and the new immutable definition. */
UCLASS()
class USeinPlacementPointTestAbility : public USeinPlacementGateTestAbility
{
 GENERATED_BODY()
public:
 USeinPlacementPointTestAbility();
};

/** Supplies the transient scenario world without a viewport/game-instance bootstrap. */
UCLASS()
class USeinPlacementGateTestTargeter : public USeinTargeterSubsystem
{
	GENERATED_BODY()
public:
	UWorld* TestWorld = nullptr;
	virtual UWorld* GetWorld() const override { return TestWorld; }
};

UCLASS()
class ASeinPlacementGateTestPreview : public ASeinPointFacingTargeterPreview
{
	GENERATED_BODY()
public:
	ESeinTargeterValidity GetValidity() const { return CurrentValidity; }
};
