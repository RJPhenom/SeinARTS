#pragma once

#include "Abilities/SeinAbility.h"
#include "Abilities/SeinLatentAction.h"
#include "Actor/SeinActor.h"
#include "Effects/SeinEffect.h"
#include "SeinEffectMutationTestTypes.generated.h"

class USeinWorldSubsystem;

/** Spawnable native actor whose bridge is configured by replay tests. */
UCLASS()
class ASeinEffectReplayTestActor : public ASeinActor
{
	GENERATED_BODY()
};

/** Ordinary non-passive grant used to verify effect-ledger ownership. */
UCLASS()
class USeinEffectLedgerTestAbility : public USeinAbility
{
	GENERATED_BODY()
};

/** Passive grant whose activation callback can synchronously remove its source effect. */
UCLASS()
class USeinEffectRemovingPassiveTestAbility : public USeinAbility
{
	GENERATED_BODY()

public:
	using FActivationCallback = TFunction<void(FSeinEntityHandle)>;
	using FEndCallback = TFunction<void(FSeinEntityHandle)>;
	static FActivationCallback ActivationCallback;
	static FEndCallback EndCallback;

	USeinEffectRemovingPassiveTestAbility();
	virtual void OnActivate_Implementation() override;
	virtual void OnEnd_Implementation(bool bWasCancelled) override;
};

/** Test-only CDO hook for observing and mutating native effect callbacks. */
UCLASS(Abstract)
class USeinEffectMutationTestHook : public USeinEffect
{
	GENERATED_BODY()

public:
	using FCallback = TFunction<void(USeinEffectMutationTestHook&, FName, FSeinEntityHandle)>;
	static FCallback Callback;

	virtual void ProcessEvent(UFunction* Function, void* Parameters) override;
};

UCLASS()
class USeinEffectIdentityInstanceTestEffect : public USeinEffectMutationTestHook
{
	GENERATED_BODY()

public:
	USeinEffectIdentityInstanceTestEffect();
};

UCLASS()
class USeinEffectIdentityClassTestEffect : public USeinEffectMutationTestHook
{
	GENERATED_BODY()

public:
	USeinEffectIdentityClassTestEffect();
};

UCLASS()
class USeinEffectIdentityPlayerTestEffect : public USeinEffectMutationTestHook
{
	GENERATED_BODY()

public:
	USeinEffectIdentityPlayerTestEffect();
};

UCLASS()
class USeinEffectIdentityInstantTestEffect : public USeinEffectMutationTestHook
{
	GENERATED_BODY()

public:
	USeinEffectIdentityInstantTestEffect();
};

UCLASS()
class USeinEffectPeriodicATestEffect : public USeinEffectMutationTestHook
{
	GENERATED_BODY()

public:
	USeinEffectPeriodicATestEffect();
};

UCLASS()
class USeinEffectPeriodicBTestEffect : public USeinEffectMutationTestHook
{
	GENERATED_BODY()

public:
	USeinEffectPeriodicBTestEffect();
};

UCLASS()
class USeinEffectTimedPlayerTestEffect : public USeinEffectMutationTestHook
{
	GENERATED_BODY()

public:
	USeinEffectTimedPlayerTestEffect();
};

UCLASS()
class USeinEffectPassiveGrantTestEffect : public USeinEffectMutationTestHook
{
	GENERATED_BODY()

public:
	USeinEffectPassiveGrantTestEffect();
};

/** Controllable latent action used to exercise callback-driven list mutation. */
UCLASS()
class USeinLatentMutationTestAction : public USeinLatentAction
{
	GENERATED_BODY()

public:
	using FTickCallback = TFunction<bool(USeinLatentMutationTestAction&, USeinWorldSubsystem&)>;
	using FCancelCallback = TFunction<void(USeinLatentMutationTestAction&)>;
	static FTickCallback TickCallback;
	static FCancelCallback CancelCallback;

	int32 TickCount = 0;
	int32 CancelCount = 0;

	virtual bool TickAction(FFixedPoint DeltaTime, USeinWorldSubsystem& World) override;
	virtual void OnCancel() override;
};
