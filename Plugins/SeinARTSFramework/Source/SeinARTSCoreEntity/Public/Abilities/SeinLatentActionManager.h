#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Core/SeinEntityHandle.h"
#include "SeinLatentActionManager.generated.h"

class USeinLatentAction;
class USeinAbility;
class USeinWorldSubsystem;
class FSeinLatentActionRestorePlan;

/**
 * Manages all active latent actions in the simulation.
 * Ticked during the AbilityExecution phase of the sim loop.
 * Provides entity-level and ability-level cancellation.
 */
UCLASS()
class SEINARTSCOREENTITY_API USeinLatentActionManager : public UObject
{
	GENERATED_BODY()

public:
	/** Register a new latent action. Registrations made from a running action or
	 *  callback first tick on the next simulation tick. Returns false without
	 *  adopting the action when identity or ownership is invalid. */
	bool RegisterAction(USeinLatentAction* Action);

	/** Tick all active actions and clean up completed ones */
	void TickAll(FFixedPoint DeltaTime, USeinWorldSubsystem& World);

	/** Cancel all latent actions belonging to the given entity */
	void CancelActionsForEntity(FSeinEntityHandle Handle);

	/** Cancel the given entity's active latent actions that are of (or derive from)
	 *  ActionClass — e.g. pass USeinMoveToAction to cancel ONLY its movement, leaving
	 *  any other latent actions (channels, waits) running. Null ActionClass = no-op. */
	void CancelActionsForEntityOfClass(FSeinEntityHandle Handle, TSubclassOf<USeinLatentAction> ActionClass);

	/** Cancel all latent actions belonging to the given ability */
	void CancelActionsForAbility(USeinAbility* Ability);

	/** Cancel every active latent action across the entire sim. Actions registered
	 *  synchronously by cancellation callbacks are rejected as part of the hard
	 *  reset. */
	void CancelAllActions();

	/** Remove completed and cancelled actions from the active list */
	void CleanupCompleted();

	/** Get the number of currently active actions */
	int32 GetActiveActionCount() const;

	/** Exact authoritative order used by tick, snapshot, and canonical hashing. */
	TConstArrayView<TObjectPtr<USeinLatentAction>> GetActiveActions() const
	{
		return ActiveActions;
	}

	int64 GetNextActionID() const { return NextActionID; }

	/** True if the entity has any active (not completed/cancelled) latent action.
	 *  Callers use this to tell "idle" from "executing an order" — e.g. gating a
	 *  system-initiated order so it doesn't stack on a unit already moving. */
	bool HasActiveActionForEntity(FSeinEntityHandle Handle) const;

private:
	/**
	 * Drop an abandoned timeline without running cancellation callbacks. Restore
	 * must not manufacture gameplay events that did not occur in the checkpoint.
	 */
	void AbandonAllForSnapshotRestore();

	/** Infallible restore adoption; never allocates new identities or reorders. */
	void AdoptRestoredActions(
		TArray<TObjectPtr<USeinLatentAction>>&& Actions,
		int64 InNextActionID);

	UPROPERTY()
	TArray<TObjectPtr<USeinLatentAction>> ActiveActions;

	/** Prevent cancellation callbacks from extending a timeline being reset. */
	bool bHardResetInProgress = false;

	/** MAX_int64 is a valid exhausted cursor; it is never allocated as an ID. */
	int64 NextActionID = 1;

	friend class USeinWorldSubsystem;
	friend class FSeinLatentActionRestorePlan;
};
