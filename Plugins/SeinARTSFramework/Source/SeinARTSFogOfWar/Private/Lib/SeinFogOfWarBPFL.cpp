/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarBPFL.cpp
 */

#include "Lib/SeinFogOfWarBPFL.h"

#include "SeinFogOfWar.h"
#include "SeinFogOfWarSubsystem.h"
#include "SeinFogOfWarTypes.h"
#include "SeinARTSFogOfWarModule.h"
#include "Settings/PluginSettings.h"
#include "Data/SeinVisionLayerDefinition.h"

#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinFogVisibilityComponent.h"
#include "Types/Entity.h"

#include "Engine/Engine.h"
#include "Engine/World.h"

namespace
{
	/** Resolve a layer name to its EVNNNNNN bit index [0..7], or -1 if the
	 *  name isn't valid. "Normal" = V (1), "Explored" = E (0), anything
	 *  else matches enabled plugin-settings slots by LayerName. */
	static int32 ResolveLayerBit(FName LayerName)
	{
		if (LayerName == TEXT("Normal"))   return 1;
		if (LayerName == TEXT("Explored")) return 0;

		const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
		if (!Settings) return INDEX_NONE;
		const int32 MaxSlots = FMath::Min(Settings->VisionLayers.Num(), 6);
		for (int32 i = 0; i < MaxSlots; ++i)
		{
			const FSeinVisionLayerDefinition& Def = Settings->VisionLayers[i];
			if (Def.bEnabled && Def.LayerName == LayerName)
			{
				return 2 + i;
			}
		}
		return INDEX_NONE;
	}

}

bool USeinFogOfWarBPFL::SeinIsCellVisible(const UObject* WorldContextObject,
	FSeinPlayerID Observer, const FVector& WorldPos, FName LayerName)
{
	if (!WorldContextObject) return false;
	UWorld* World = GEngine->GetWorldFromContextObject(
		WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (!World) return false;

	USeinFogOfWar* Fog = USeinFogOfWarSubsystem::GetFogOfWarForWorld(WorldContextObject);
	if (!Fog) return true;
	if (!Fog->HasRuntimeData()) return true; // no data = permit (matches LOS resolver)

	const int32 Bit = ResolveLayerBit(LayerName);
	if (Bit < 0) return false;

	const uint8 Mask = static_cast<uint8>(1u << Bit);
	const FFixedVector FixedPos = FFixedVector::FromVector(WorldPos);
	// Shared-vision aware when a sim is available; plain per-observer read
	// otherwise (no sim = no ledger to consume).
	if (const USeinWorldSubsystem* Sim =
			World->GetSubsystem<USeinWorldSubsystem>())
	{
		return (Fog->GetEffectiveCellBitfield(*Sim, Observer, FixedPos)
			& Mask) != 0;
	}
	return Fog->IsCellVisible(Observer, FixedPos, Mask);
}

bool USeinFogOfWarBPFL::SeinIsCellExplored(const UObject* WorldContextObject,
	FSeinPlayerID Observer, const FVector& WorldPos)
{
	USeinFogOfWar* Fog = USeinFogOfWarSubsystem::GetFogOfWarForWorld(WorldContextObject);
	if (!Fog) return false;
	const FFixedVector FixedPos = FFixedVector::FromVector(WorldPos);
	UWorld* World = GEngine->GetWorldFromContextObject(
		WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (const USeinWorldSubsystem* Sim =
			World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr)
	{
		return (Fog->GetEffectiveCellBitfield(*Sim, Observer, FixedPos)
			& SEIN_FOW_BIT_EXPLORED) != 0;
	}
	return Fog->IsCellExplored(Observer, FixedPos);
}

uint8 USeinFogOfWarBPFL::SeinGetCellBitfield(const UObject* WorldContextObject,
	FSeinPlayerID Observer, const FVector& WorldPos)
{
	USeinFogOfWar* Fog = USeinFogOfWarSubsystem::GetFogOfWarForWorld(WorldContextObject);
	if (!Fog) return 0;
	const FFixedVector FixedPos = FFixedVector::FromVector(WorldPos);
	UWorld* World = GEngine->GetWorldFromContextObject(
		WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (const USeinWorldSubsystem* Sim =
			World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr)
	{
		return Fog->GetEffectiveCellBitfield(*Sim, Observer, FixedPos);
	}
	return Fog->GetCellBitfield(Observer, FixedPos);
}

bool USeinFogOfWarBPFL::SeinIsEntityVisible(const UObject* WorldContextObject,
	FSeinPlayerID Observer, FSeinEntityHandle Target)
{
	if (!Target.IsValid()) return false;
	if (!WorldContextObject) return false;
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (!World) return false;

	USeinWorldSubsystem* Sim = World->GetSubsystem<USeinWorldSubsystem>();
	if (!Sim) return false;
	if (!Sim->GetEntity(Target)) return false;

	USeinFogOfWar* Fog = USeinFogOfWarSubsystem::GetFogOfWarForWorld(WorldContextObject);
	if (!Fog) return true;
	if (!Fog->HasRuntimeData()) return true;

	return Fog->IsEntityVisibleToObserver(Observer, *Sim, Target);
}

FSeinPlayerID USeinFogOfWarBPFL::SeinGetLocalObserver(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return FSeinPlayerID();
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	return UE::SeinARTSFogOfWar::ResolveLocalObserverPlayerID(World);
}

// ============================================================================
// Runtime mutation — emission mask
// ============================================================================

namespace
{
	static USeinWorldSubsystem* FindFogWorld(
		const UObject* WorldContextObject,
		FSeinEntityHandle Entity)
	{
		if (!WorldContextObject || !Entity.IsValid()) return nullptr;
		UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
		if (!World) return nullptr;
		return World->GetSubsystem<USeinWorldSubsystem>();
	}

	static FSeinFogVisibilityComponent* FindMutableFogVisibility(
		const UObject* WorldContextObject,
		FSeinEntityHandle Entity,
		const TCHAR* MutationOperation)
	{
		USeinWorldSubsystem* Sim =
			FindFogWorld(WorldContextObject, Entity);
		if (!Sim) return nullptr;
		if (!Sim->RequireStateMutationAuthorization(
			MutationOperation))
		{
			return nullptr;
		}
		return Sim->GetComponentMutable<
			FSeinFogVisibilityComponent>(Entity);
	}
}

int32 USeinFogOfWarBPFL::SeinGetEntityEmissionMask(const UObject* WorldContextObject,
	FSeinEntityHandle Entity)
{
	const USeinWorldSubsystem* Sim =
		FindFogWorld(WorldContextObject, Entity);
	const FSeinFogVisibilityComponent* FogVis = Sim
		? Sim->GetComponent<FSeinFogVisibilityComponent>(Entity)
		: nullptr;
	return FogVis ? static_cast<int32>(FogVis->FogVisibilityLayerMask) : 0;
}

void USeinFogOfWarBPFL::SeinSetEntityEmissionMask(const UObject* WorldContextObject,
	FSeinEntityHandle Entity, int32 NewMask)
{
	if (FSeinFogVisibilityComponent* FogVis = FindMutableFogVisibility(
		WorldContextObject, Entity, TEXT("SetEntityEmissionMask")))
	{
		FogVis->FogVisibilityLayerMask = static_cast<uint8>(NewMask & 0xFF);
	}
}

void USeinFogOfWarBPFL::SeinAddEntityEmissionLayers(const UObject* WorldContextObject,
	FSeinEntityHandle Entity, int32 LayersToAdd)
{
	if (FSeinFogVisibilityComponent* FogVis = FindMutableFogVisibility(
		WorldContextObject, Entity, TEXT("AddEntityEmissionLayers")))
	{
		FogVis->FogVisibilityLayerMask |= static_cast<uint8>(LayersToAdd & 0xFF);
	}
}

void USeinFogOfWarBPFL::SeinRemoveEntityEmissionLayers(const UObject* WorldContextObject,
	FSeinEntityHandle Entity, int32 LayersToRemove)
{
	if (FSeinFogVisibilityComponent* FogVis = FindMutableFogVisibility(
		WorldContextObject, Entity, TEXT("RemoveEntityEmissionLayers")))
	{
		FogVis->FogVisibilityLayerMask &= ~static_cast<uint8>(LayersToRemove & 0xFF);
	}
}
