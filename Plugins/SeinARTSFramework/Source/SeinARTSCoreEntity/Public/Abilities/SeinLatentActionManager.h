#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Core/SeinEntityHandle.h"
#include "SeinLatentActionManager.generated.h"

class USeinLatentAction;
class USeinAbility;
class USeinWorldSubsystem;

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
	 *  callback first tick on the next simulation tick. */
	void RegisterAction(USeinLatentAction* Action);

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
	 *  synchronously by cancellation callbacks are discarded as part of the hard
	 *  reset. Used by the
	 *  snapshot-restore path to drop in-flight latent state before the
	 *  ability pool is rebuilt — without this, latent actions hold stale
	 *  refs into the about-to-be-replaced ability instances. */
	void CancelAllActions();

	/** Remove completed and cancelled actions from the active list */
	void CleanupCompleted();

	/** Get the number of currently active actions */
	int32 GetActiveActionCount() const;

	/** True if the entity has any active (not completed/cancelled) latent action.
	 *  Callers use this to tell "idle" from "executing an order" — e.g. gating a
	 *  system-initiated order so it doesn't stack on a unit already moving. */
	bool HasActiveActionForEntity(FSeinEntityHandle Handle) const;

private:
	UPROPERTY()
	TArray<TObjectPtr<USeinLatentAction>> ActiveActions;
};
