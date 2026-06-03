#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Core/SeinEntityHandle.h"
#include "SeinLatentAction.generated.h"

class USeinAbility;
class USeinWorldSubsystem;

/**
 * Base class for latent (multi-tick) ability nodes.
 * Latent actions run cooperatively on the sim thread — no OS threads involved.
 * Subclass and override TickAction to implement custom multi-frame behavior.
 */
UCLASS(Abstract)
class SEINARTSCOREENTITY_API USeinLatentAction : public UObject
{
	GENERATED_BODY()

public:
	/** The ability that owns this latent action */
	UPROPERTY()
	TObjectPtr<USeinAbility> OwningAbility;

	/** The entity that owns the ability */
	UPROPERTY()
	FSeinEntityHandle OwnerEntity;

	/** Whether this action has completed naturally */
	bool bCompleted = false;

	/** Whether this action was cancelled externally */
	bool bCancelled = false;

	/** Whether this action failed (bCompleted will also be set) */
	bool bFailed = false;

	/** Opaque failure reason code — subclasses define their own enum and cast. */
	uint8 FailureReason = 0;

	/**
	 * Called each sim tick while the action is active.
	 * @return true when the action is complete and should be removed.
	 */
	virtual bool TickAction(FFixedPoint DeltaTime, USeinWorldSubsystem& World) { return true; }

	/** Called when the action is cancelled externally (e.g., ability cancelled) */
	virtual void OnCancel() {}

	/** Called when the action fails. Override to notify observers. */
	virtual void OnFail(uint8 /*ReasonCode*/) {}

	/** Mark this action as completed */
	void Complete();

	/** Mark this action as cancelled */
	void Cancel();

	/** Mark this action as failed. Sets bFailed + bCompleted and calls OnFail(). */
	void Fail(uint8 ReasonCode);
};
