/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinConstructionBPFL.cpp
 * @brief   Construction-over-time BPFL implementation. All mutations route
 *          through USeinWorldSubsystem so component storage and the
 *          EntityTagIndex stay consistent.
 */

#include "Lib/SeinConstructionBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinConstructionComponent.h"
#include "Events/SeinVisualEvent.h"
#include "Lib/SeinEffectBPFL.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "Engine/Engine.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinConstruction, Log, All);

namespace
{
	USeinWorldSubsystem* GetSubsystem(const UObject* WorldContextObject)
	{
		if (!WorldContextObject) return nullptr;
		UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
		return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	}
}

bool USeinConstructionBPFL::SeinIsUnderConstruction(const UObject* WorldContextObject, FSeinEntityHandle Entity)
{
	USeinWorldSubsystem* Sub = GetSubsystem(WorldContextObject);
	if (!Sub) return false;
	const FSeinConstructionComponent* Data = Sub->GetComponent<FSeinConstructionComponent>(Entity);
	if (!Data) return false;
	// Component present + threshold not yet crossed = active construction.
	// Once Progress >= TimeToCompletion, AddProgress's auto-finish removes
	// the component, so this check naturally returns false post-completion.
	return Data->Progress < Data->TimeToCompletion;
}

FFixedPoint USeinConstructionBPFL::SeinGetConstructionPercent(const UObject* WorldContextObject, FSeinEntityHandle Entity)
{
	USeinWorldSubsystem* Sub = GetSubsystem(WorldContextObject);
	if (!Sub) return FFixedPoint::Zero;
	const FSeinConstructionComponent* Data = Sub->GetComponent<FSeinConstructionComponent>(Entity);
	if (!Data) return FFixedPoint::Zero;
	if (Data->TimeToCompletion <= FFixedPoint::Zero) return FFixedPoint::One;
	const FFixedPoint Ratio = Data->Progress / Data->TimeToCompletion;
	// Clamp to [0, 1] — Progress can briefly exceed BuildTime in the same tick
	// the threshold is crossed, before AddProgress's auto-finish fires.
	if (Ratio < FFixedPoint::Zero) return FFixedPoint::Zero;
	if (Ratio > FFixedPoint::One) return FFixedPoint::One;
	return Ratio;
}

bool USeinConstructionBPFL::SeinAddConstructionProgress(const UObject* WorldContextObject,
	FSeinEntityHandle Entity, FFixedPoint Amount)
{
	USeinWorldSubsystem* Sub = GetSubsystem(WorldContextObject);
	if (!Sub
		|| !Sub->RequireStateMutationAuthorization(
			TEXT("AddConstructionProgress")))
	{
		return false;
	}

	FSeinConstructionComponent* Data = Sub->GetComponent<FSeinConstructionComponent>(Entity);
	if (!Data)
	{
		// No active construction. Caller's BA_Construct should validate this
		// in CanActivate, but the BPFL is defensive — return false silently.
		return false;
	}

	// If already past threshold (e.g. another worker's tick same frame raced
	// us), AddProgress is a no-op. We don't double-fire CompletionEffect.
	if (Data->Progress >= Data->TimeToCompletion)
	{
		return false;
	}

	const FFixedPoint NewProgress = Data->Progress + Amount;
	Data->Progress = NewProgress;

	UE_LOG(LogSeinConstruction, Verbose,
		TEXT("AddProgress[%s]: +%.3f → %.3f / %.3f"),
		*Entity.ToString(), Amount.ToFloat(), NewProgress.ToFloat(), Data->TimeToCompletion.ToFloat());

	if (NewProgress >= Data->TimeToCompletion)
	{
		// Threshold crossed THIS call — auto-finish.
		SeinFinishConstruction(WorldContextObject, Entity);
		return true;
	}
	return false;
}

void USeinConstructionBPFL::SeinFinishConstruction(const UObject* WorldContextObject, FSeinEntityHandle Entity)
{
	USeinWorldSubsystem* Sub = GetSubsystem(WorldContextObject);
	if (!Sub
		|| !Sub->RequireStateMutationAuthorization(TEXT("FinishConstruction")))
	{
		return;
	}

	// Snapshot completion-effect class BEFORE we remove the component, so the
	// effect application happens against valid data. The remove + ungrant +
	// effect order matters: applying the effect last lets a designer-authored
	// effect inspect the entity in its just-finished state (no longer "under
	// construction" — abilities are unblocked, mesh-swap can fire, etc.).
	TSubclassOf<USeinEffect> CompletionEffect = nullptr;
	if (FSeinConstructionComponent* Data = Sub->GetComponent<FSeinConstructionComponent>(Entity))
	{
		CompletionEffect = Data->CompletionEffect;
	}
	else
	{
		// Already finished or never had the component — nothing to do.
		return;
	}

	UE_LOG(LogSeinConstruction, Verbose, TEXT("FinishConstruction[%s]: effect=%s"),
		*Entity.ToString(), *GetNameSafe(CompletionEffect));

	// 1. Ungrant the UnderConstruction state tag (refcounted — only fully
	//    drops if no other source granted it). Designer-authored buildings
	//    granted this in BaseTags or via the placement ability flow.
	Sub->UngrantTag(Entity, SeinARTSTags::State_UnderConstruction);

	// 2. Remove the construction component itself. The entity stops being
	//    "under construction" — IsUnderConstruction now returns false.
	Sub->RemoveComponent<FSeinConstructionComponent>(Entity);

	// 3. Notify the render layer — the entity's USeinConstructionComponent
	//    consumes this and reverses the placement-visual swap (destroys the
	//    spawned placement primitive + decal, restores main mesh visibility).
	//    Symmetric with the spawn-time MakeConstructionStateChangedEvent.
	Sub->EnqueueVisualEvent(FSeinVisualEvent::MakeConstructionStateChangedEvent(Entity, /*bUnderConstruction=*/false));

	// 4. Apply the completion effect last (entity is now in finished state).
	//    Effect is designer-authored (additional mesh swaps beyond placement,
	//    grant operational abilities, fire bespoke VFX visual events, register
	//    with capture-point system, etc.). The framework's mesh-swap-back is
	//    already handled by step 3's event — this is for game-specific extras.
	if (CompletionEffect)
	{
		USeinEffectBPFL::SeinApplyEffect(WorldContextObject, Entity, CompletionEffect, Entity);
	}
}
