/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementSubsystem.cpp
 * @brief   Registers the movement module's sim systems + hosts the persistent
 *          per-unit movement-instance registry (CP2.1, D-R2). See header.
 */

#include "SeinMovementSubsystem.h"
#include "Actions/SeinMoveToAction.h"
#include "Simulation/SeinAvoidanceSystem.h"
#include "Simulation/SeinMovementDriverSystem.h"
#include "Simulation/SeinMovementPresentationSystem.h"
#include "Simulation/SeinMovementTraceSystem.h"
#include "Simulation/SeinNavContainmentSystem.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Movement/SeinMovement.h"
#include "Movement/SeinBasicMovement.h"
#include "Movement/SeinAvoidance.h"
#include "Movement/SeinAvoidanceDefault.h"
#include "Settings/PluginSettings.h"
#include "Components/SeinMovementComponent.h"
#include "Components/SeinNavigationComponent.h"
#include "Components/SeinExtentsComponent.h"
#include "Abilities/SeinLatentActionManager.h"
#include "Abilities/SeinLatentAction.h"
#include "Simulation/SeinComponentLiveTuning.h"
#include "SeinARTSCoreEntityLog.h"
#include "UObject/UObjectIterator.h"

// The movement-trace channel ([EP]/[UNIT]/[ORPHAN]/[ARRIVE]/[THROTTLE] lines).
// Declared in Simulation/SeinMovementTraceLog.h; one switch: `log LogSeinMoveTrace Verbose`.
DEFINE_LOG_CATEGORY(LogSeinMoveTrace);

void USeinMovementSubsystem::Initialize(
	FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(USeinWorldSubsystem::StaticClass());

	UWorld* World = GetWorld();
	USeinWorldSubsystem* Sim =
		World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	if (!World || !Sim) return;

	// Local avoidance — the soft steering layer above the penetration floor. The
	// MODEL is pluggable (USeinARTSCoreSettings::AvoidanceClass → USeinAvoidanceDefault);
	// instantiate + GC-root it here, then register a thin delegator system that calls
	// it each PreTick. Same abstract-base + soft-class-picker pattern as Navigation /
	// CollisionResolver / FogOfWar.
	if (UClass* AvoidClass = ResolveAvoidanceClass())
	{
		AvoidanceInstance = NewObject<USeinAvoidance>(this, AvoidClass);
		AvoidanceSystem = new FSeinAvoidanceSystem(this, AvoidanceInstance);
		Sim->RegisterSystem(AvoidanceSystem);
	}
	else
	{
		// AvoidanceClass is None → avoidance off (WYSIWYG). Skip the instance + delegator system;
		// units keep their default (zero) AvoidanceOutput, so movement + collision are unchanged.
		USeinARTSCoreSettings::ReportDisabledSystem(TEXT("Avoidance"),
			TEXT("Units still move and collide but won't flow around each other; crowds grind at chokepoints."), /*bHighSeverity*/ false);
	}

	// The always-on per-unit movement driver (CP2.1, D-R2). Its first-contact
	// snap replaced FSeinInitialSnapSystem; its idle settle/coast is the
	// ground-up redesign the 2026-06-03 FSeinPositionKeepSystem strip was
	// deferred for (settle-in-place semantics — no return-to-home).
	DriverSystem = new FSeinMovementDriverSystem(this);
	Sim->RegisterSystem(DriverSystem);

	// Nav containment (PostTick 11) — keeps the nav-pure collision floor from
	// stranding units in baked walls / off the grid edge by pulling any
	// off-walkable movable collider back onto nav. Movement owns this (it may
	// know nav); the collision floor stays nav-agnostic.
	NavContainmentSystem = new FSeinNavContainmentSystem();
	Sim->RegisterSystem(NavContainmentSystem);

	// Presentation telemetry samples the final PostTick transform after every
	// authoritative mover. It writes only Transient RenderState values.
	PresentationSystem = new FSeinMovementPresentationSystem(this);
	Sim->RegisterSystem(PresentationSystem);
	Sim->OnAuthoritativeStateRestored.AddUObject(
		this,
		&USeinMovementSubsystem::HandleAuthoritativeStateRestored);
	Sim->OnComponentPropertyLiveTuned.AddUObject(
		this,
		&USeinMovementSubsystem::HandleComponentPropertyLiveTuned);

	// Observation-only crowd-jam trace (PostTick 90). Registered unconditionally on
	// every client — it no-ops unless `log LogSeinMoveTrace Verbose` and never writes
	// sim state, so lockstep is indifferent to it.
	TraceSystem = new FSeinMovementTraceSystem();
	Sim->RegisterSystem(TraceSystem);
}

void USeinMovementSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// System participation freezes from the descriptor registered in
	// Initialize, but custom avoidance models retain the established
	// BeginPlay-time hook where every world subsystem already exists.
	if (AvoidanceInstance)
	{
		AvoidanceInstance->OnInitialized(&InWorld);
	}
}

void USeinMovementSubsystem::Deinitialize()
{
	ReleaseModuleOwnedStateForModuleUnload();
	Super::Deinitialize();
}

void USeinMovementSubsystem::ReleaseModuleOwnedStateForModuleUnload()
{
	check(IsInGameThread());
	USeinWorldSubsystem* Sim = nullptr;
	if (UWorld* World = GetWorld())
	{
		Sim = World->GetSubsystem<USeinWorldSubsystem>();
	}
	if (Sim)
	{
		Sim->OnAuthoritativeStateRestored.RemoveAll(this);
		Sim->OnComponentPropertyLiveTuned.RemoveAll(this);
		// Core ignores ordinary world teardown. During DLL unload this stops a
		// consumed match before any registered system or UObject vtable leaves.
		Sim->TerminateAndReleaseForModuleUnload(
			TEXT("SeinARTSMovement"),
			TEXT("Movement systems and persistent policy instances are being released."));
	}

	for (TObjectIterator<USeinMoveToAction> It; It; ++It)
	{
		if (!It->HasAnyFlags(RF_ClassDefaultObject)
			&& It->GetWorld() == GetWorld())
		{
			It->Movement = nullptr;
		}
	}

	if (TraceSystem)
	{
		if (Sim) Sim->UnregisterSystem(TraceSystem);
		delete TraceSystem;
		TraceSystem = nullptr;
	}
	if (PresentationSystem)
	{
		if (Sim) Sim->UnregisterSystem(PresentationSystem);
		delete PresentationSystem;
		PresentationSystem = nullptr;
	}
	if (NavContainmentSystem)
	{
		if (Sim) Sim->UnregisterSystem(NavContainmentSystem);
		delete NavContainmentSystem;
		NavContainmentSystem = nullptr;
	}
	if (DriverSystem)
	{
		if (Sim) Sim->UnregisterSystem(DriverSystem);
		delete DriverSystem;
		DriverSystem = nullptr;
	}
	if (AvoidanceSystem)
	{
		if (Sim) Sim->UnregisterSystem(AvoidanceSystem);
		delete AvoidanceSystem;
		AvoidanceSystem = nullptr;
	}
	// AvoidanceInstance is a UPROPERTY → GC owns it; drop the ref AFTER the delegator
	// system (which holds a raw pointer to it) is gone, so the pointer never dangles.
	if (AvoidanceInstance)
	{
		AvoidanceInstance->OnDeinitialized();
	}
	AvoidanceInstance = nullptr;

	MovementInstanceMap.Empty();
	MovementInstancePool.Empty();
	MovementStateRevisions.Reset();
	RoutineRootCache.Reset();
	BumpMovementTopologyRevision();
}

void USeinMovementSubsystem::HandleAuthoritativeStateRestored()
{
	if (PresentationSystem)
	{
		PresentationSystem->ResetSamples();
	}
}

void USeinMovementSubsystem::HandleComponentPropertyLiveTuned(
	FSeinEntityHandle Entity,
	const UScriptStruct& ComponentType,
	const FSeinComponentPropertyPatch& Patch)
{
	const bool bMovementClassChanged =
		&ComponentType == FSeinMovementComponent::StaticStruct()
		&& !Patch.PropertyPath.IsEmpty()
		&& Patch.PropertyPath[0].PropertyName
			== GET_MEMBER_NAME_STRING_CHECKED(
				FSeinMovementComponent, MovementClass);
	const bool bMovementClassDataChanged =
		&ComponentType == FSeinMovementComponent::StaticStruct()
		&& !Patch.PropertyPath.IsEmpty()
		&& Patch.PropertyPath[0].PropertyName
			== GET_MEMBER_NAME_STRING_CHECKED(
				FSeinMovementComponent, MovementClassData);
	const bool bNavigationChanged =
		&ComponentType == FSeinNavigationComponent::StaticStruct();
	const bool bExtentsChanged =
		&ComponentType == FSeinExtentsComponent::StaticStruct();
	if (!bMovementClassChanged && !bMovementClassDataChanged
		&& !bNavigationChanged && !bExtentsChanged)
	{
		return;
	}

	USeinWorldSubsystem* Sim = GetWorld()
		? GetWorld()->GetSubsystem<USeinWorldSubsystem>()
		: nullptr;
	const USeinLatentActionManager* Manager =
		Sim ? Sim->GetLatentActionManager() : nullptr;
	if (!Sim || !Manager) return;

	// A persistent movement policy also ticks while idle. Keep its reflected
	// per-class tuning cache in sync before any active action refreshes cached
	// footprint/path state from it.
	if (bMovementClassDataChanged)
	{
		if (USeinMovement* Movement = FindMovementInstance(Entity))
		{
			if (const FSeinMovementComponent* MovementComponent =
				Sim->GetComponent<FSeinMovementComponent>(Entity))
			{
				Movement->HydrateTuningFromData(
					MovementComponent->MovementClassData);
				MarkMovementStateDirty(Entity);
			}
		}
	}

	// Manager order is the canonical action order used by tick/snapshot/hash.
	for (USeinLatentAction* Action : Manager->GetActiveActions())
	{
		USeinMoveToAction* MoveAction = Cast<USeinMoveToAction>(Action);
		if (MoveAction && MoveAction->OwnerEntity == Entity
			&& !MoveAction->bCompleted && !MoveAction->bCancelled)
		{
			MoveAction->RefreshAuthoredComponentTuning(
				*Sim, bMovementClassChanged,
				bMovementClassChanged || bMovementClassDataChanged
					|| bNavigationChanged || bExtentsChanged);
		}
	}
}

namespace
{
	bool NativeHierarchyTouchesModule(
		const UClass* Class,
		FName OwnerModuleId)
	{
		if (!Class || OwnerModuleId.IsNone())
		{
			return false;
		}
		const FString ScriptPackage =
			TEXT("/Script/") + OwnerModuleId.ToString();
		for (const UClass* Cursor = Class;
			Cursor;
			Cursor = Cursor->GetSuperClass())
		{
			if (Cursor->HasAnyClassFlags(CLASS_Native)
				&& Cursor->GetOutermost()
				&& Cursor->GetOutermost()->GetName()
					.Equals(
						ScriptPackage,
						ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

}

void USeinMovementSubsystem::ReleaseNativeClassStateForModuleUnload(
	FName OwnerModuleId)
{
	check(IsInGameThread());
	if (OwnerModuleId.IsNone())
	{
		return;
	}

	USeinWorldSubsystem* Sim = nullptr;
	if (UWorld* World = GetWorld())
	{
		Sim = World->GetSubsystem<USeinWorldSubsystem>();
	}
	if (Sim)
	{
		Sim->TerminateAndReleaseForModuleUnload(
			OwnerModuleId,
			FString::Printf(
				TEXT("Native movement policy module '%s' unloaded during a live world."),
				*OwnerModuleId.ToString()));
	}

	for (auto It = MovementInstanceMap.CreateIterator(); It; ++It)
	{
		if (!NativeHierarchyTouchesModule(
			It->Value ? It->Value->GetClass() : nullptr,
			OwnerModuleId))
		{
			continue;
		}
		MovementInstancePool.RemoveSingleSwap(It->Value);
		MovementStateRevisions.Remove(It->Key);
		It.RemoveCurrent();
		BumpMovementTopologyRevision();
	}

	// Active orders also hold a reflected borrowed reference. The simulation
	// is already stopped above; sever it without dispatching OnMoveEnd into
	// the implementation module that is unloading.
	for (TObjectIterator<USeinMoveToAction> It; It; ++It)
	{
		if (!It->HasAnyFlags(RF_ClassDefaultObject)
			&& It->GetWorld() == GetWorld()
			&& It->Movement
			&& NativeHierarchyTouchesModule(
				It->Movement->GetClass(),
				OwnerModuleId))
		{
			It->Movement = nullptr;
		}
	}

	if (AvoidanceInstance
		&& NativeHierarchyTouchesModule(
			AvoidanceInstance->GetClass(),
			OwnerModuleId))
	{
		if (AvoidanceSystem)
		{
			if (Sim)
			{
				Sim->UnregisterSystem(AvoidanceSystem);
			}
			delete AvoidanceSystem;
			AvoidanceSystem = nullptr;
		}
		AvoidanceInstance->OnDeinitialized();
		AvoidanceInstance = nullptr;
	}
}

UClass* USeinMovementSubsystem::ResolveMovementClass(const FSeinMovementComponent& Move)
{
	UClass* MoveClass = Move.MovementClass.IsValid()
		? Move.MovementClass.TryLoadClass<USeinMovement>()
		: nullptr;
	if (!MoveClass || MoveClass->HasAnyClassFlags(CLASS_Abstract))
	{
		MoveClass = USeinBasicMovement::StaticClass();
	}
	return MoveClass;
}

UClass* USeinMovementSubsystem::ResolveAvoidanceClass()
{
	// WYSIWYG. None/empty => avoidance is intentionally OFF: return null so Initialize skips
	// creating + registering the avoidance system. A set-but-unloadable/abstract class is a mistake,
	// not an off-switch: fall back to the shipped default with a logged error.
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	if (!Settings || Settings->AvoidanceClass.IsNull())
	{
		return nullptr;
	}
	UClass* AvoidClass = Settings->AvoidanceClass.TryLoadClass<USeinAvoidance>();
	if (!AvoidClass || AvoidClass->HasAnyClassFlags(CLASS_Abstract))
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("AvoidanceClass '%s' could not be loaded or is abstract — falling back to the shipped default."),
			*Settings->AvoidanceClass.ToString());
		AvoidClass = USeinAvoidanceDefault::StaticClass();
	}
	return AvoidClass;
}

USeinMovement* USeinMovementSubsystem::GetOrCreateMovementInstance(
	FSeinEntityHandle Handle, const FSeinMovementComponent& Move)
{
	UClass* DesiredClass = nullptr;
	if (USeinMovement** Existing = MovementInstanceMap.Find(Handle))
	{
		if (*Existing)
		{
			UClass* ExistingClass = (*Existing)->GetClass();
			// The overwhelmingly common path is a stable authored mode. Avoid a
			// soft-class load/lookup for every moving entity on every sim tick.
			if ((Move.MovementClass.IsNull()
					&& ExistingClass == USeinBasicMovement::StaticClass())
				|| Move.MovementClass.ResolveClass() == ExistingClass)
			{
				return *Existing;
			}
		}

		DesiredClass = ResolveMovementClass(Move);
		if (*Existing && (*Existing)->GetClass() == DesiredClass)
		{
			return *Existing;
		}
		// Authored MovementClass changed at runtime (effect / designer swap) —
		// retire the old instance; the new mode starts with fresh kinematic
		// state (a different mode's steer/ramp state is meaningless to carry).
		MovementInstancePool.RemoveSingleSwap(*Existing);
		MovementInstanceMap.Remove(Handle);
		MovementStateRevisions.Remove(Handle);
		BumpMovementTopologyRevision();
	}

	if (!DesiredClass)
	{
		DesiredClass = ResolveMovementClass(Move);
	}

	USeinMovement* NewInstance = NewObject<USeinMovement>(this, DesiredClass);
	if (!NewInstance) return nullptr;

	// Hydrate per-unit tuning onto the fresh instance immediately, so any virtual that reads tuning
	// (GetAltitude / GetMinTurnRadius at plan-time, TickIdle, the steering hooks) sees correct values
	// from the very first use — not just after the first OnMoveBegin. No-op when there's no tuning.
	NewInstance->HydrateTuningFromData(Move.MovementClassData);

	MovementInstanceMap.Add(Handle, NewInstance);
	MovementInstancePool.Add(NewInstance);
	MarkMovementStateDirty(Handle);
	BumpMovementTopologyRevision();
	return NewInstance;
}

void USeinMovementSubsystem::SweepStaleMovementInstances(USeinWorldSubsystem& World)
{
	for (auto It = MovementInstanceMap.CreateIterator(); It; ++It)
	{
		if (World.GetEntityPool().IsValid(It->Key)) continue;
		MovementInstancePool.RemoveSingleSwap(It->Value);
		MovementStateRevisions.Remove(It->Key);
		It.RemoveCurrent();
		BumpMovementTopologyRevision();
	}
}

void USeinMovementSubsystem::MarkMovementStateDirty(
	FSeinEntityHandle Handle)
{
	if (!Handle.IsValid())
	{
		return;
	}
	++MovementStateMutationRevision;
	if (MovementStateMutationRevision == 0)
	{
		++MovementStateMutationRevision;
	}
	MovementStateRevisions.Add(Handle, MovementStateMutationRevision);
}

void USeinMovementSubsystem::MarkAvoidanceStateDirty()
{
	++MovementStateMutationRevision;
	if (MovementStateMutationRevision == 0)
	{
		++MovementStateMutationRevision;
	}
	AvoidanceStateRevision = MovementStateMutationRevision;
}

void USeinMovementSubsystem::BumpMovementTopologyRevision()
{
	++MovementStateMutationRevision;
	if (MovementStateMutationRevision == 0)
	{
		++MovementStateMutationRevision;
	}
	++MovementStateTopologyRevision;
	if (MovementStateTopologyRevision == 0)
	{
		++MovementStateTopologyRevision;
	}
}
