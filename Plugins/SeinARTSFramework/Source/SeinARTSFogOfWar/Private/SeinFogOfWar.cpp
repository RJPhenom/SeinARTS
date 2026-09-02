/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWar.cpp
 */

#include "SeinFogOfWar.h"

#include "SeinFogOfWarTypes.h"
#include "SeinLevelData.h"
#include "Components/SeinFogVisibilityPayload.h"
#include "Engine/World.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "SeinARTSFogOfWarLog.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "Types/Entity.h"

void USeinFogOfWar::GetEffectiveVisionSources(
	const USeinWorldSubsystem& Sim,
	FSeinPlayerID Observer,
	TArray<FSeinPlayerID>& OutSources) const
{
	OutSources.Reset();
	OutSources.Add(Observer);
	if (!Observer.IsValid() || !Sim.HasAnyPairCapabilityGrants())
	{
		return;
	}
	for (const FSeinPlayerID& Source : Sim.GetRegisteredPlayerIDs())
	{
		if (Source == Observer || !Source.IsValid())
		{
			continue;
		}
		if (Sim.HasPairCapability(
				Source,
				Observer,
				SeinARTSTags::Relationship_Capability_ShareVision))
		{
			OutSources.Add(Source);
		}
	}
}

uint8 USeinFogOfWar::GetEffectiveCellBitfield(
	const USeinWorldSubsystem& Sim,
	FSeinPlayerID Observer,
	const FFixedVector& WorldPos) const
{
	if (!Sim.HasAnyPairCapabilityGrants())
	{
		return GetCellBitfield(Observer, WorldPos);
	}
	TArray<FSeinPlayerID> Sources;
	GetEffectiveVisionSources(Sim, Observer, Sources);
	uint8 Bits = 0;
	for (const FSeinPlayerID& Source : Sources)
	{
		Bits |= GetCellBitfield(Source, WorldPos);
		if (Bits == 0xFF)
		{
			break;
		}
	}
	return Bits;
}

bool USeinFogOfWar::GetEffectiveObserverGrid(
	const USeinWorldSubsystem& Sim,
	FSeinPlayerID Observer,
	TArray<uint8>& OutCells,
	FFixedVector& OutOrigin,
	FFixedPoint& OutCellSize,
	int32& OutWidth,
	int32& OutHeight) const
{
	if (!Sim.HasAnyPairCapabilityGrants())
	{
		return GetObserverGrid(
			Observer, OutCells, OutOrigin, OutCellSize, OutWidth, OutHeight);
	}
	TArray<FSeinPlayerID> Sources;
	GetEffectiveVisionSources(Sim, Observer, Sources);
	if (!GetObserverGrid(
			Sources[0], OutCells, OutOrigin, OutCellSize, OutWidth, OutHeight))
	{
		return false;
	}
	TArray<uint8> SourceCells;
	for (int32 Index = 1; Index < Sources.Num(); ++Index)
	{
		FFixedVector SourceOrigin = FFixedVector::ZeroVector;
		FFixedPoint SourceCellSize = FFixedPoint::Zero;
		int32 SourceWidth = 0;
		int32 SourceHeight = 0;
		if (!GetObserverGrid(
				Sources[Index], SourceCells, SourceOrigin, SourceCellSize,
				SourceWidth, SourceHeight)
			|| SourceWidth != OutWidth
			|| SourceHeight != OutHeight
			|| SourceCells.Num() != OutCells.Num())
		{
			continue;
		}
		for (int32 Cell = 0; Cell < OutCells.Num(); ++Cell)
		{
			OutCells[Cell] |= SourceCells[Cell];
		}
	}
	return true;
}

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
	if (const FSeinFogVisibilityPayload* FogVisibility =
			Sim.GetComponent<FSeinFogVisibilityPayload>(Target))
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

	// ShareVision consumer: the observer's effective vision is the union of
	// its own VisionGroup and every group a directional A -> Observer
	// ShareVision grant exposes. B consumes A's VISION (stamped bits and
	// seen latches), not A's owner omniscience — an A-owned entity with a
	// zero emission mask stays hidden to B even though A's owner shortcut
	// shows it to A. Zero grants = single-source, identical to the legacy
	// behavior and cost.
	TArray<FSeinPlayerID> Sources;
	GetEffectiveVisionSources(Sim, Observer, Sources);
	uint8 ObserverBits = 0;
	for (const FSeinPlayerID& Source : Sources)
	{
		ObserverBits |= GetEntityVisibleBits(Source, Sim, Target);
	}
	if ((ObserverBits & EmissionMask) != 0)
	{
		return true;
	}
	if (Policy != ESeinFogVisibilityPolicy::VisibleOnceSeen)
	{
		return false;
	}
	for (const FSeinPlayerID& Source : Sources)
	{
		if (HasObserverSeenEntity(Source, Target))
		{
			return true;
		}
	}
	return false;
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
