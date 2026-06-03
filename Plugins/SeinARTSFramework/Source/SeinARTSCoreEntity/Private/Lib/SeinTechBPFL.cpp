/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinTechBPFL.cpp
 * @brief   Tech availability + query implementation.
 */

#include "Lib/SeinTechBPFL.h"
#include "Core/SeinPlayerState.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Engine/World.h"

USeinWorldSubsystem* USeinTechBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

FSeinTechAvailability USeinTechBPFL::SeinGetTechAvailability(
	const UObject* WorldContextObject,
	FSeinPlayerID PlayerID,
	TSubclassOf<USeinEffect> TechClass,
	const FGameplayTagContainer& PrerequisiteTags)
{
	FSeinTechAvailability Out;
	Out.TechClass = TechClass;
	if (!TechClass) return Out;

	const USeinEffect* CDO = GetDefault<USeinEffect>(TechClass);
	if (!CDO) return Out;
	Out.TechTag = CDO->EffectTag;

	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return Out;

	const FSeinPlayerState* PlayerState = Subsystem->GetPlayerState(PlayerID);
	if (!PlayerState) return Out;

	if (Out.TechTag.IsValid())
	{
		Out.bAlreadyResearched = PlayerState->HasPlayerTag(Out.TechTag);
	}

	Out.bPrerequisitesMet = PlayerState->HasAllPlayerTags(PrerequisiteTags);
	Out.bForbiddenPresent = CDO->ForbiddenPrerequisiteTags.IsEmpty()
		? false
		: PlayerState->PlayerTags.HasAny(CDO->ForbiddenPrerequisiteTags);

	USeinEffect* MutableCDO = Cast<USeinEffect>(TechClass->GetDefaultObject());
	Out.bCustomCheckPassed = MutableCDO ? MutableCDO->CanResearch(PlayerID) : true;

	return Out;
}

bool USeinTechBPFL::SeinIsTechResearched(const UObject* WorldContextObject, FSeinPlayerID PlayerID, TSubclassOf<USeinEffect> TechClass)
{
	if (!TechClass) return false;
	const USeinEffect* CDO = GetDefault<USeinEffect>(TechClass);
	if (!CDO || !CDO->EffectTag.IsValid()) return false;

	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return false;

	const FSeinPlayerState* PlayerState = Subsystem->GetPlayerState(PlayerID);
	return PlayerState && PlayerState->HasPlayerTag(CDO->EffectTag);
}

bool USeinTechBPFL::SeinPlayerHasAllTags(const UObject* WorldContextObject, FSeinPlayerID PlayerID, const FGameplayTagContainer& Tags)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return false;

	const FSeinPlayerState* PlayerState = Subsystem->GetPlayerState(PlayerID);
	return PlayerState && PlayerState->HasAllPlayerTags(Tags);
}

bool USeinTechBPFL::SeinPlayerHasAnyTag(const UObject* WorldContextObject, FSeinPlayerID PlayerID, const FGameplayTagContainer& Tags)
{
	if (Tags.IsEmpty()) return false;

	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return false;

	const FSeinPlayerState* PlayerState = Subsystem->GetPlayerState(PlayerID);
	return PlayerState && PlayerState->PlayerTags.HasAny(Tags);
}
