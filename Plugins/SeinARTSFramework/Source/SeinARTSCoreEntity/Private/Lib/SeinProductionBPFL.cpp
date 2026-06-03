/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinProductionBPFL.cpp
 * @brief   Read-only production BPFL impl. Producer-side mutation
 *          (enqueue, rally, etc.) lives on USeinAbility — see
 *          SeinAbility.cpp for the EnqueueProduction / SetRallyPoint
 *          / SetRallyEntity / ClearRallyPoint impls.
 */

#include "Lib/SeinProductionBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinProductionComponent.h"
#include "Core/SeinPlayerState.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinProductionBPFL, Log, All);

USeinWorldSubsystem* USeinProductionBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = WorldContextObject->GetWorld();
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

bool USeinProductionBPFL::SeinGetProductionData(const UObject* WorldContextObject,
	FSeinEntityHandle EntityHandle, FSeinProductionComponent& OutData)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem)
	{
		UE_LOG(LogSeinProductionBPFL, Warning, TEXT("GetProductionData: no SeinWorldSubsystem"));
		return false;
	}
	const FSeinProductionComponent* Data = Subsystem->GetComponent<FSeinProductionComponent>(EntityHandle);
	if (!Data)
	{
		UE_LOG(LogSeinProductionBPFL, Warning,
			TEXT("GetProductionData: entity %s invalid or has no FSeinProductionComponent"),
			*EntityHandle.ToString());
		return false;
	}
	OutData = *Data;
	return true;
}

TArray<FSeinProductionComponent> USeinProductionBPFL::SeinGetProductionDataMany(const UObject* WorldContextObject,
	const TArray<FSeinEntityHandle>& EntityHandles)
{
	TArray<FSeinProductionComponent> Result;
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return Result;

	Result.Reserve(EntityHandles.Num());
	for (const FSeinEntityHandle& Handle : EntityHandles)
	{
		if (const FSeinProductionComponent* Data = Subsystem->GetComponent<FSeinProductionComponent>(Handle))
		{
			Result.Add(*Data);
		}
		else
		{
			UE_LOG(LogSeinProductionBPFL, Warning,
				TEXT("GetProductionData (batch): skipping entity %s"), *Handle.ToString());
		}
	}
	return Result;
}

bool USeinProductionBPFL::SeinPlayerHasTechTag(const UObject* WorldContextObject,
	FSeinPlayerID PlayerID, FGameplayTag TechTag)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return false;

	const FSeinPlayerState* PlayerState = Subsystem->GetPlayerState(PlayerID);
	return PlayerState && PlayerState->HasPlayerTag(TechTag);
}

FGameplayTagContainer USeinProductionBPFL::SeinGetPlayerTechTags(const UObject* WorldContextObject,
	FSeinPlayerID PlayerID)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return FGameplayTagContainer();

	const FSeinPlayerState* PlayerState = Subsystem->GetPlayerState(PlayerID);
	return PlayerState ? PlayerState->PlayerTags : FGameplayTagContainer();
}
