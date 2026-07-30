/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWar.cpp
 */

#include "SeinFogOfWar.h"

#include "SeinFogOfWarTypes.h"
#include "SeinLevelData.h"
#include "Components/SeinFogVisibilityComponent.h"
#include "Engine/World.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "SeinARTSFogOfWarLog.h"
#include "Types/Entity.h"

uint8 USeinFogOfWar::GetEntityVisibleBits(
	FSeinPlayerID Observer,
	USeinWorldSubsystem& Sim,
	FSeinEntityHandle Target) const
{
	// Single-point fallback. Subclasses override to do the volumetric
	// (extents-aware) sweep.
	const FSeinEntity* Entity = Sim.GetEntity(Target);
	if (!Entity)
	{
		return 0;
	}
	return GetCellBitfield(Observer, Entity->Transform.GetLocation());
}

bool USeinFogOfWar::IsEntityVisibleToObserver(
	FSeinPlayerID Observer,
	USeinWorldSubsystem& Sim,
	FSeinEntityHandle Target) const
{
	// Caller hasn't specified an observer → permissive (no filtering).
	if (!Observer.IsValid())
	{
		return true;
	}

	// The owner always sees its own entities.
	if (Sim.GetEntityOwner(Target) == Observer)
	{
		return true;
	}

	ESeinFogVisibilityPolicy Policy =
		ESeinFogVisibilityPolicy::VisionLayersOnly;
	uint8 EmissionMask = SEIN_FOW_BIT_NORMAL;
	if (const FSeinFogVisibilityComponent* FogVisibility =
			Sim.GetComponent<FSeinFogVisibilityComponent>(Target))
	{
		Policy = FogVisibility->FogVisibilityPolicy;
		EmissionMask = FogVisibility->FogVisibilityLayerMask;
	}

	if (Policy == ESeinFogVisibilityPolicy::AlwaysVisible)
	{
		return true;
	}
	if (Policy == ESeinFogVisibilityPolicy::VisibleOnceExplored)
	{
		EmissionMask |= SEIN_FOW_BIT_EXPLORED;
	}
	if (EmissionMask == 0)
	{
		return false;
	}

	const uint8 ObserverBits =
		GetEntityVisibleBits(Observer, Sim, Target);
	if ((ObserverBits & EmissionMask) != 0)
	{
		return true;
	}
	return Policy == ESeinFogVisibilityPolicy::VisibleOnceSeen
		&& HasObserverSeenEntity(Observer, Target);
}

void USeinFogOfWar::InitializeForWorld(UWorld* World)
{
	OwningWorld = World;
	OnFogOfWarInitialized(World);
}

void USeinFogOfWar::DeinitializeFromWorld()
{
	OnFogOfWarDeinitialized();
	OwningWorld.Reset();
}

bool USeinFogOfWar::CanMutateStaticEnvironment(
	const TCHAR* Operation,
	UWorld* RequestedWorld,
	FString& OutError) const
{
	OutError.Reset();
	UWorld* BoundWorld = OwningWorld.Get();
	if (!BoundWorld)
	{
		BoundWorld = GetWorld();
	}
	if (RequestedWorld && BoundWorld && RequestedWorld != BoundWorld)
	{
		OutError = FString::Printf(
			TEXT("%s targeted a world other than this fog implementation's owning world."),
			Operation);
		return false;
	}

	UWorld* EffectiveWorld = RequestedWorld ? RequestedWorld : BoundWorld;
	const USeinWorldSubsystem* Sim = EffectiveWorld
		? EffectiveWorld->GetSubsystem<USeinWorldSubsystem>()
		: nullptr;
	if (Sim && Sim->GetCanonicalStateContractDigest().IsValid())
	{
		OutError = FString::Printf(
			TEXT("%s is not legal after the match StateContract freezes; prepare the static fog environment before bootstrap, then restart the match/PIE session."),
			Operation);
		return false;
	}
	return true;
}

FSeinStaticEnvironmentAdoptionResult USeinFogOfWar::LoadFromSubstrate(
	const USeinLevelData& Substrate)
{
	FString Error;
	if (!CanMutateStaticEnvironment(
			TEXT("Fog substrate adoption"), nullptr, Error))
	{
		UE_LOG(LogSeinFogOfWar, Error, TEXT("%s"), *Error);
		return FSeinStaticEnvironmentAdoptionResult::Rejected(
			MoveTemp(Error));
	}
	FSeinStaticEnvironmentAdoptionResult Result =
		LoadFromSubstrateImpl(Substrate);
	if (!Result.IsAdopted()
		&& !Result.IsNotApplicable()
		&& !Result.IsRejected())
	{
		Result = FSeinStaticEnvironmentAdoptionResult::Rejected(
			FString::Printf(
				TEXT("Fog implementation '%s' returned an invalid substrate-adoption outcome."),
				*GetClass()->GetPathName()));
	}
	else if (Result.IsRejected() && Result.Detail.IsEmpty())
	{
		Result.Detail = FString::Printf(
			TEXT("Fog implementation '%s' rejected the Level Data substrate without a reason."),
			*GetClass()->GetPathName());
	}
	else if (Result.IsNotApplicable()
		&& Substrate.HasRuntimeData()
		&& GetLevelDataProvider())
	{
		Result = FSeinStaticEnvironmentAdoptionResult::Rejected(
			FString::Printf(
				TEXT("Fog implementation '%s' participates in the Level Data bake but did not adopt the prepared runtime substrate%s%s."),
				*GetClass()->GetPathName(),
				Result.Detail.IsEmpty() ? TEXT("") : TEXT(": "),
				*Result.Detail));
	}
	return Result;
}

void USeinFogOfWar::InitGridFromVolumes(UWorld* World)
{
	FString Error;
	if (!CanMutateStaticEnvironment(
			TEXT("Fog no-bake grid initialization"), World, Error))
	{
		UE_LOG(LogSeinFogOfWar, Error, TEXT("%s"), *Error);
		return;
	}
	InitGridFromVolumesImpl(World);
}
