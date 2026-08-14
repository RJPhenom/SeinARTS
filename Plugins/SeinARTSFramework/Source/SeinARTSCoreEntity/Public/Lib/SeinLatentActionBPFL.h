/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLatentActionBPFL.h
 * @brief   Blueprint control over an entity's running latent actions (moves,
 *          channels, waits, custom primitives). These are the BROAD / SELECTIVE
 *          cancels — for movement specifically, prefer the precise "Stop Movement"
 *          node (USeinMovementBPFL), which cancels only the move.
 *
 *          Sim-side: call from an ability / sim context (these mutate sim state),
 *          like the Move To node — not from render-side input directly.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Core/SeinEntityHandle.h"
#include "Abilities/SeinLatentAction.h"
#include "SeinLatentActionBPFL.generated.h"

class USeinWorldSubsystem;

UCLASS(meta = (DisplayName = "SeinARTS Latent Action Library", SeinDeterministic))
class SEINARTSCOREENTITY_API USeinLatentActionBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Cancel ALL of the entity's active latent actions — its move plus any channels /
	 *  waits / custom latent primitives. Each action's OnCancel runs. The broad "halt
	 *  everything this unit is doing." For movement only, use Stop Movement instead. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Action",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Cancel All Actions"))
	static void SeinCancelAllActions(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle);

	/** Cancel the entity's active latent actions of a specific class (or subclasses) — the
	 *  selective cancel. Pass USeinMoveToAction to cancel only movement, or a custom
	 *  latent-action class to cancel just that primitive. Null class = no-op. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Action",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Cancel Actions Of Class"))
	static void SeinCancelActionsOfClass(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, TSubclassOf<USeinLatentAction> ActionClass);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};
