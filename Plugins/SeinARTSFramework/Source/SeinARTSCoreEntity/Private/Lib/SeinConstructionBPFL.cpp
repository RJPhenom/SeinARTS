/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinConstructionBPFL.cpp
 * @author       RJ Macklem
 * @created      02 Jun 2026
 * @latest       11 Sep 2026
 * @brief        Implements authorized construction lifecycle transitions and job queries.
 * @disclaimer   This code was generated in part with the assistance of an AI language model.
 */
#include "Lib/SeinConstructionBPFL.h"
#include "Actor/SeinActor.h"
#include "Actor/SeinEntityBridgeComponent.h"
#include "Components/SeinConstructionPayload.h"
#include "Events/SeinVisualEvent.h"
#include "Lib/SeinEffectBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

namespace SeinConstructionLocal
{
	USeinWorldSubsystem* GetWorld(const UObject* Context)
	{
		UWorld* World = Context ? GEngine->GetWorldFromContextObject(Context, EGetWorldErrorMode::ReturnNull) : nullptr;
		return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	}

	void Notify(USeinWorldSubsystem& World, FSeinEntityHandle Entity,
		ESeinConstructionState OldState, FGameplayTag OldStage, const FSeinConstructionPayload& Data)
	{
		if (OldState == Data.State && OldStage == Data.Stage) return;
		FSeinVisualEvent Event = FSeinVisualEvent::MakeConstructionStateChangedEvent(
			Entity, Data.State != ESeinConstructionState::Complete);
		Event.OldConstructionState = OldState;
		Event.NewConstructionState = Data.State;
		Event.OldConstructionStage = OldStage;
		Event.NewConstructionStage = Data.Stage;
		World.EnqueueVisualEvent(Event);
	}

	ESeinConstructionResult Resolve(const UObject* Context, FSeinConstructionHandle Job,
		USeinWorldSubsystem*& World, FSeinConstructionPayload*& Data)
	{
		World = GetWorld(Context);
		Data = nullptr;
		if (!World || !World->IsEntityAlive(Job.Entity)) return ESeinConstructionResult::InvalidEntity;
		if (!World->RequireStateMutationAuthorization(TEXT("Construction")))
			return ESeinConstructionResult::NotAuthorized;
		Data = World->GetComponentMutable<FSeinConstructionPayload>(Job.Entity);
		if (!Data) return ESeinConstructionResult::MissingComponent;
		if (Job.JobID <= 0 || Job.JobID != Data->JobID) return ESeinConstructionResult::StaleJob;
		return ESeinConstructionResult::Succeeded;
	}

	ESeinConstructionResult Complete(const UObject* Context, FSeinConstructionHandle Job)
	{
		USeinWorldSubsystem* World;
		FSeinConstructionPayload* Data;
		const ESeinConstructionResult Result = Resolve(Context, Job, World, Data);
		if (Result != ESeinConstructionResult::Succeeded) return Result;
		if (Data->State == ESeinConstructionState::Complete) return ESeinConstructionResult::AlreadyComplete;

		const ESeinConstructionState OldState = Data->State;
		const FGameplayTag Stage = Data->Stage;
		const TSubclassOf<USeinEffect> Effect = Data->JobCompletionEffect;
		const bool bReleaseTag = Data->bOwnsConstructionTag;
		Data->State = ESeinConstructionState::Complete;

		Data->bOwnsConstructionTag = false;
		Notify(*World, Job.Entity, OldState, Stage, *Data);
		if (bReleaseTag) World->UngrantTag(Job.Entity, SeinARTSTags::State_UnderConstruction);
		// Publish terminal state before effects: a reentrant completion cannot apply the effect twice.
		// Never retain a storage pointer across the designer callback.
		if (Effect) USeinEffectBPFL::SeinApplyEffect(Context, Job.Entity, Effect, Job.Entity);
		return ESeinConstructionResult::Succeeded;
	}
}

FSeinConstructionStatus USeinConstructionBPFL::SeinGetConstructionStatus(const UObject* Context, FSeinEntityHandle Entity)
{
	FSeinConstructionStatus Status;
	USeinWorldSubsystem* World = SeinConstructionLocal::GetWorld(Context);
	if (!World || !World->IsEntityAlive(Entity)) return Status;
	const FSeinConstructionPayload* Data = World->GetComponent<FSeinConstructionPayload>(Entity);
	if (!Data) return Status;
	Status.bValid = true;
	Status.Construction.Entity = Entity;
	Status.Construction.JobID = Data->JobID;
	Status.State = Data->State;
	Status.Stage = Data->Stage;
	return Status;
}

FSeinConstructionWorkStatus USeinConstructionBPFL::SeinGetConstructionWorkStatus(const UObject* Context, FSeinEntityHandle Entity)
{
	FSeinConstructionWorkStatus Status;
	auto* World = SeinConstructionLocal::GetWorld(Context);
	if (!World || !World->IsEntityAlive(Entity)) return Status;
	const auto* Job = World->GetComponent<FSeinConstructionPayload>(Entity);
	const auto* Work = Job;
	if (!Work || Work->JobID <= 0) return Status;
	Status.bValid = true;
	Status.Progress = Work->Progress;
	Status.RequiredWork = Work->JobRequiredWork;
	Status.bWorkComplete = Work->Progress >= Work->JobRequiredWork;
	if (Status.bWorkComplete) Status.Percent = FFixedPoint::One;
	else if (Work->Progress > FFixedPoint::Zero && Work->JobRequiredWork > FFixedPoint::Zero)
		Status.Percent = Work->Progress / Work->JobRequiredWork;
	return Status;
}

bool USeinConstructionBPFL::SeinIsUnderConstruction(const UObject* Context, FSeinEntityHandle Entity)
{
	const FSeinConstructionStatus Status = SeinGetConstructionStatus(Context, Entity);
	return Status.bValid && Status.State != ESeinConstructionState::Complete;
}

FFixedPoint USeinConstructionBPFL::SeinGetConstructionPercent(const UObject* Context, FSeinEntityHandle Entity)
{
	return SeinGetConstructionWorkStatus(Context, Entity).Percent;
}

ESeinConstructionResult USeinConstructionBPFL::SeinQueueConstruction(
	const UObject* Context, FSeinEntityHandle Entity, FSeinConstructionHandle& Construction)
{
	Construction = {};
	USeinWorldSubsystem* World = SeinConstructionLocal::GetWorld(Context);
	if (!World || !World->IsEntityAlive(Entity)) return ESeinConstructionResult::InvalidEntity;
	if (!World->RequireStateMutationAuthorization(TEXT("QueueConstruction")))
		return ESeinConstructionResult::NotAuthorized;
	FSeinConstructionPayload* Data = World->GetComponentMutable<FSeinConstructionPayload>(Entity);
	if (!Data) return ESeinConstructionResult::MissingComponent;
	if (Data->State != ESeinConstructionState::Complete)
	{
		Construction.Entity = Entity;
		Construction.JobID = Data->JobID;
		return ESeinConstructionResult::Unchanged;
	}
	if (Data->JobID == MAX_int32) return ESeinConstructionResult::JobLimitReached;
	if (Data->RequiredWork < FFixedPoint::Zero) return ESeinConstructionResult::InvalidAmount;
	if (!World->GrantTag(Entity, SeinARTSTags::State_UnderConstruction))
		return ESeinConstructionResult::InvalidState;
	Data = World->GetComponentMutable<FSeinConstructionPayload>(Entity);
	const FGameplayTag OldStage = Data->Stage;
	Data->State = ESeinConstructionState::Queued;

	Data->JobCompletionEffect = Data->CompletionEffect;
	Data->Stage = FGameplayTag();
	Data->bOwnsConstructionTag = true;
	++Data->JobID;
	Data->Progress = FFixedPoint::Zero;
	Data->JobRequiredWork = Data->RequiredWork;
	Construction.Entity = Entity;
	Construction.JobID = Data->JobID;
	SeinConstructionLocal::Notify(*World, Entity, ESeinConstructionState::Complete, OldStage, *Data);
	return ESeinConstructionResult::Succeeded;
}

ESeinConstructionResult USeinConstructionBPFL::SeinStartConstruction(const UObject* Context, FSeinConstructionHandle Job)
{
	USeinWorldSubsystem* World;
	FSeinConstructionPayload* Data;
	const ESeinConstructionResult Result = SeinConstructionLocal::Resolve(Context, Job, World, Data);
	if (Result != ESeinConstructionResult::Succeeded) return Result;
	if (Data->State == ESeinConstructionState::Complete) return ESeinConstructionResult::AlreadyComplete;

	if (Data->State == ESeinConstructionState::Building) return ESeinConstructionResult::Unchanged;
	if (Data->State != ESeinConstructionState::Queued && Data->State != ESeinConstructionState::Paused)
		return ESeinConstructionResult::InvalidState;
	const ESeinConstructionState OldState = Data->State;
	Data->State = ESeinConstructionState::Building;
	SeinConstructionLocal::Notify(*World, Job.Entity, OldState, Data->Stage, *Data);
	const auto Work = SeinGetConstructionWorkStatus(Context, Job.Entity);
	return Work.bValid && Work.bWorkComplete ? ESeinConstructionResult::ReadyToComplete : ESeinConstructionResult::Succeeded;
}

ESeinConstructionResult USeinConstructionBPFL::SeinAdvanceConstruction(
	const UObject* Context, FSeinConstructionHandle Job, FFixedPoint Amount)
{
	USeinWorldSubsystem* World;
	FSeinConstructionPayload* Data;
	const ESeinConstructionResult Result = SeinConstructionLocal::Resolve(Context, Job, World, Data);
	if (Result != ESeinConstructionResult::Succeeded) return Result;
	if (Amount <= FFixedPoint::Zero) return ESeinConstructionResult::InvalidAmount;
	if (Data->State == ESeinConstructionState::Complete) return ESeinConstructionResult::AlreadyComplete;

	if (Data->State != ESeinConstructionState::Building) return ESeinConstructionResult::InvalidState;
	auto* Work = Data;
	if (Work->Progress < FFixedPoint::Zero || Work->JobRequiredWork < Work->Progress)
		return ESeinConstructionResult::InvalidAmount;
	const FFixedPoint Remaining = Work->JobRequiredWork - Work->Progress;
	if (Amount >= Remaining)
	{
		Work->Progress = Work->JobRequiredWork;
		return ESeinConstructionResult::ReadyToComplete;
	}
	Work->Progress += Amount;
	return ESeinConstructionResult::Succeeded;
}
ESeinConstructionResult USeinConstructionBPFL::SeinPauseConstruction(const UObject* Context, FSeinConstructionHandle Job)
{
	USeinWorldSubsystem* World;
	FSeinConstructionPayload* Data;
	const ESeinConstructionResult Result = SeinConstructionLocal::Resolve(Context, Job, World, Data);
	if (Result != ESeinConstructionResult::Succeeded) return Result;
	if (Data->State == ESeinConstructionState::Paused) return ESeinConstructionResult::Unchanged;
	if (Data->State != ESeinConstructionState::Building) return ESeinConstructionResult::InvalidState;
	Data->State = ESeinConstructionState::Paused;
	SeinConstructionLocal::Notify(*World, Job.Entity, ESeinConstructionState::Building, Data->Stage, *Data);
	return ESeinConstructionResult::Succeeded;
}

ESeinConstructionResult USeinConstructionBPFL::SeinCompleteConstruction(const UObject* Context, FSeinConstructionHandle Job)
{
	return SeinConstructionLocal::Complete(Context, Job);
}

ESeinConstructionResult USeinConstructionBPFL::SeinForceCompleteConstruction(const UObject* Context, FSeinConstructionHandle Job)
{
	return SeinConstructionLocal::Complete(Context, Job);
}

ESeinConstructionResult USeinConstructionBPFL::SeinSetConstructionStage(
	const UObject* Context, FSeinConstructionHandle Job, FGameplayTag Stage)
{
	USeinWorldSubsystem* World;
	FSeinConstructionPayload* Data;
	const ESeinConstructionResult Result = SeinConstructionLocal::Resolve(Context, Job, World, Data);
	if (Result != ESeinConstructionResult::Succeeded) return Result;
	if (Data->State == ESeinConstructionState::Complete) return ESeinConstructionResult::InvalidState;
	if (Data->Stage == Stage) return ESeinConstructionResult::Unchanged;
	const FGameplayTag OldStage = Data->Stage;
	Data->Stage = Stage;
	SeinConstructionLocal::Notify(*World, Job.Entity, Data->State, OldStage, *Data);
	return ESeinConstructionResult::Succeeded;
}

FSeinEntityHandle USeinConstructionBPFL::SeinSpawnConstructionSite(
	const UObject* Context, TSubclassOf<ASeinActor> ActorClass, const FFixedTransform& Transform,
	FSeinPlayerID Player, ESeinConstructionInitialState InitialState, FSeinConstructionHandle& Construction)
{
	Construction = {};
	USeinWorldSubsystem* World = SeinConstructionLocal::GetWorld(Context);
	if (!World || !ActorClass || !World->RequireStateMutationAuthorization(TEXT("SpawnConstructionSite")))
		return FSeinEntityHandle::Invalid();
	const FSeinEntityHandle Entity = World->SpawnEntityWithConstruction(ActorClass, Transform, Player, &InitialState);
	const FSeinConstructionStatus Status = SeinGetConstructionStatus(Context, Entity);
	Construction = Status.Construction;
	return Entity;
}

void USeinConstructionBPFL::InitializeAtSpawn(USeinWorldSubsystem& World, FSeinEntityHandle Entity,
	const ESeinConstructionInitialState* Override)
{
	FSeinConstructionPayload* Data = World.GetComponentMutable<FSeinConstructionPayload>(Entity);
	if (!Data) return;
	const bool bQueue = Override || Data->bQueueConstructionOnSpawn;
	Data->State = ESeinConstructionState::Complete;
	Data->Stage = {};
	Data->Progress = FFixedPoint::Zero;
	Data->JobRequiredWork = FFixedPoint::Zero;
	Data->JobID = 0;
	Data->JobCompletionEffect = nullptr;
	Data->bOwnsConstructionTag = false;

	if (bQueue)
	{
		FSeinConstructionHandle Job;
		if (SeinQueueConstruction(&World, Entity, Job) != ESeinConstructionResult::Succeeded)
		{
			World.InvalidateDeterministicExecutionContract(TEXT("Construction initialization could not queue the authored site."));
			return;
		}
		if (Override && *Override == ESeinConstructionInitialState::Building)
			SeinStartConstruction(&World, Job);
	}
}

bool USeinConstructionBPFL::SeinAddConstructionProgress(const UObject* Context, FSeinEntityHandle Entity, FFixedPoint Amount)
{
	const auto Status = SeinGetConstructionWorkStatus(Context, Entity);
	if (!Status.bValid) return false;
	// Retain the legacy arithmetic rejection while migrating old graphs to explicit jobs.
	if (Status.Progress.Value > MAX_int64 - (Amount > FFixedPoint::Zero ? Amount.Value : 0)) return false;
	if (SeinAdvanceConstruction(Context, SeinGetConstructionStatus(Context, Entity).Construction, Amount) != ESeinConstructionResult::ReadyToComplete) return false;
	return SeinCompleteConstruction(Context, SeinGetConstructionStatus(Context, Entity).Construction) == ESeinConstructionResult::Succeeded;
}

void USeinConstructionBPFL::SeinFinishConstruction(const UObject* Context, FSeinEntityHandle Entity)
{
	SeinForceCompleteConstruction(Context, SeinGetConstructionStatus(Context, Entity).Construction);
}
