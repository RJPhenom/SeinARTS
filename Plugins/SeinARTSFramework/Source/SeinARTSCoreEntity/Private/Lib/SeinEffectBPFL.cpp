/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinEffectBPFL.cpp
 * @brief   Implementation of effect system Blueprint nodes. Thin wrappers over
 *          `USeinWorldSubsystem::ApplyEffect` / `RemoveInstanceEffect` /
 *          `RemoveInstanceEffectsWithTag`.
 */

#include "Lib/SeinEffectBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinActiveEffectsComponent.h"

USeinWorldSubsystem* USeinEffectBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

int32 USeinEffectBPFL::SeinApplyEffect(const UObject* WorldContextObject, FSeinEntityHandle TargetHandle,
	TSubclassOf<USeinEffect> EffectClass, FSeinEntityHandle SourceHandle)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return 0;
	return static_cast<int32>(Subsystem->ApplyEffect(TargetHandle, EffectClass, SourceHandle));
}

void USeinEffectBPFL::SeinRemoveEffect(const UObject* WorldContextObject, FSeinEntityHandle TargetHandle, int32 EffectInstanceID)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return;
	Subsystem->RemoveInstanceEffect(TargetHandle, static_cast<uint32>(EffectInstanceID), /*bByExpiration=*/false);
}

void USeinEffectBPFL::SeinRemoveEffectsWithTag(const UObject* WorldContextObject, FSeinEntityHandle TargetHandle, FGameplayTag Tag)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return;
	Subsystem->RemoveInstanceEffectsWithTag(TargetHandle, Tag);
}

bool USeinEffectBPFL::SeinHasEffectWithTag(const UObject* WorldContextObject, FSeinEntityHandle TargetHandle, FGameplayTag Tag)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return false;

	const FSeinActiveEffectsComponent* EffectsComp = Subsystem->GetComponent<FSeinActiveEffectsComponent>(TargetHandle);
	return EffectsComp ? EffectsComp->HasEffectWithTag(Tag) : false;
}

int32 USeinEffectBPFL::SeinGetEffectStacks(const UObject* WorldContextObject, FSeinEntityHandle TargetHandle, FGameplayTag EffectTag)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return 0;

	const FSeinActiveEffectsComponent* EffectsComp = Subsystem->GetComponent<FSeinActiveEffectsComponent>(TargetHandle);
	return EffectsComp ? EffectsComp->GetStackCountForTag(EffectTag) : 0;
}
