/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementSubsystem.h
 * @brief   World subsystem hook for the movement module's sim systems AND the
 *          persistent per-unit movement-instance registry (CP2.1, D-R2).
 *
 *          Registers its systems during subsystem Initialize so execution
 *          topology can freeze before match launch:
 *            - FSeinAvoidanceSystem — local unit-unit avoidance steering (PreTick).
 *            - FSeinMovementDriverSystem — the always-on per-unit driver
 *              (AbilityExecution, after the order ticks). Its first-contact
 *              ground snap subsumed the retired FSeinInitialSnapSystem.
 *
 *          The registry owns ONE persistent USeinMovement instance per unit,
 *          resolved from FSeinMovementComponent::MovementClass and created
 *          lazily — move orders BORROW the instance (OnMoveBegin is the
 *          per-order reset point), the driver ticks it idle between orders.
 *          (The old per-order instance lifecycle, and the separately removed
 *          FSeinPositionKeepSystem, are both superseded by this — the driver
 *          IS the ground-up redesign the strip was deferred for.)
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Core/SeinEntityHandle.h"
#include "SeinMovementSubsystem.generated.h"

class FSeinAvoidanceSystem;
class FSeinMovementDriverSystem;
class FSeinMovementPresentationSystem;
class FSeinMovementTraceSystem;
class FSeinNavContainmentSystem;
class USeinAvoidance;
class USeinMovement;
class USeinWorldSubsystem;
struct FSeinMovementCanonicalStateProvider;
struct FSeinMovementComponent;
struct FSeinComponentPropertyPatch;
struct FSeinMovementRoutineRootCache;

UCLASS()
class SEINARTSMOVEMENT_API USeinMovementSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** The entity's PERSISTENT movement instance (CP2.1, D-R2). Resolves
	 *  `FSeinMovementComponent::MovementClass` (null / unresolved / abstract →
	 *  USeinBasicMovement), creates the instance lazily on first request, and
	 *  re-creates it if the authored class changes at runtime (effect-driven
	 *  movement-class swaps start fresh — by design, a different mode's
	 *  kinematic state is meaningless to carry). Returns null only on
	 *  NewObject failure. Callers never own the result — the registry roots it
	 *  for the entity's lifetime. */
	USeinMovement* GetOrCreateMovementInstance(FSeinEntityHandle Handle, const FSeinMovementComponent& Move);

	/** The shared movement-class resolution cascade: soft path → TryLoadClass →
	 *  (null / abstract) → USeinBasicMovement. Single source of truth for the
	 *  registry and any CDO-level queries. */
	static UClass* ResolveMovementClass(const FSeinMovementComponent& Move);

	/** The avoidance-class resolution cascade: `USeinARTSCoreSettings::AvoidanceClass`
	 *  soft path → TryLoadClass → (empty / invalid / abstract) → USeinAvoidanceDefault.
	 *  Mirrors the NavigationClass / CollisionResolverClass picker pattern. */
	static UClass* ResolveAvoidanceClass();

	/** Remove registry entries whose entity is no longer alive in the pool.
	 *  Called once per tick by the driver. Touches no sim state — handle
	 *  generations already prevent stale-entry collisions — so its timing and
	 *  iteration order are inert to lockstep. */
	void SweepStaleMovementInstances(USeinWorldSubsystem& World);

	/** Exact current instance, or null. Never creates or resolves a fallback. */
	USeinMovement* FindMovementInstance(FSeinEntityHandle Handle) const
	{
		return MovementInstanceMap.FindRef(Handle);
	}

	USeinAvoidance* GetAvoidanceInstance() const
	{
		return AvoidanceInstance;
	}

	int32 GetMovementInstanceCount() const
	{
		return MovementInstanceMap.Num();
	}

	/** Automatic write barriers for the open movement/avoidance policy seams.
	 *  Revisions are local digest-cache evidence and are never serialized. */
	void MarkMovementStateDirty(FSeinEntityHandle Handle);
	void MarkAvoidanceStateDirty();

	/**
	 * Tear down every system and UObject reference whose executable behavior
	 * lives in this module. Called from both PreUnloadCallback and Deinitialize;
	 * safe to call repeatedly.
	 */
	void ReleaseModuleOwnedStateForModuleUnload();

	/**
	 * Extension PreUnload seam. Terminally releases Core sim state, including
	 * reflected native payloads, then drops movement/avoidance instances whose
	 * native class hierarchy touches OwnerModuleId before its vtables disappear.
	 */
	void ReleaseNativeClassStateForModuleUnload(FName OwnerModuleId);

private:
	friend struct FSeinMovementCanonicalStateProvider;
	friend struct FSeinMovementSubsystemTestAccess;
	void HandleAuthoritativeStateRestored();
	void HandleComponentPropertyLiveTuned(
		FSeinEntityHandle Entity,
		const UScriptStruct& ComponentType,
		const FSeinComponentPropertyPatch& Patch);

	/** Local unit-unit avoidance steering system (PreTick). A thin delegator owned
	 *  here; registered with the sim loop during initialization, unregistered + deleted
	 *  on teardown. Delegates to AvoidanceInstance. */
	FSeinAvoidanceSystem* AvoidanceSystem = nullptr;

	/** The active pluggable avoidance impl (USeinARTSCoreSettings::AvoidanceClass →
	 *  default USeinAvoidanceDefault). GC-rooted by this UPROPERTY; AvoidanceSystem
	 *  holds a raw pointer to it. Created once in Initialize. */
	UPROPERTY(Transient)
	TObjectPtr<USeinAvoidance> AvoidanceInstance;

	/** The always-on per-unit movement driver (AbilityExecution, priority 10).
	 *  Same ownership / lifecycle as AvoidanceSystem. */
	FSeinMovementDriverSystem* DriverSystem = nullptr;

	/** Nav-containment pass (PostTick 11, after collision): pulls movable
	 *  colliders the nav-pure collision floor shoved off-walkable back onto nav.
	 *  Same ownership / lifecycle as the others. */
	FSeinNavContainmentSystem* NavContainmentSystem = nullptr;

	/** Render-only final-motion sampler (FinalObservation 89). It observes transforms
	 * after collision/containment and dispatches typed presentation updates to
	 * the persistent movement instance without committing canonical state. */
	FSeinMovementPresentationSystem* PresentationSystem = nullptr;

	/** Observation-only movement trace (FinalObservation 90, after everything that moves
	 *  bodies): the crowd-jam "written picture" logger behind `log LogSeinMoveTrace
	 *  Verbose`. Silent by default; never touches sim state. Same ownership /
	 *  lifecycle as the others. */
	FSeinMovementTraceSystem* TraceSystem = nullptr;

	/** Handle → persistent instance index. Plain map (the codebase idiom for
	 *  handle-keyed maps); ownership / GC-rooting lives in MovementInstancePool. */
	TMap<FSeinEntityHandle, USeinMovement*> MovementInstanceMap;

	/** GC root for the persistent movement instances (UPROPERTY-array rooting
	 *  idiom — see e.g. the ability pools). Entries are added/removed in
	 *  lockstep with MovementInstanceMap. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USeinMovement>> MovementInstancePool;

	TMap<FSeinEntityHandle, uint64> MovementStateRevisions;
	uint64 MovementStateMutationRevision = 0;
	uint64 MovementStateTopologyRevision = 1;
	uint64 AvoidanceStateRevision = 0;
	mutable TSharedPtr<FSeinMovementRoutineRootCache> RoutineRootCache;
	void BumpMovementTopologyRevision();
};
