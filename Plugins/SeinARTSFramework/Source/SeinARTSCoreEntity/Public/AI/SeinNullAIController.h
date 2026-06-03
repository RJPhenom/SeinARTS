/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNullAIController.h
 * @brief   No-op reference impl of USeinAIController.
 *
 * Shipped as the framework default `DefaultAIControllerClass` setting so the
 * auto-spawn pipeline triggered by `Dropped → AITakeover` (when SlotDropPolicy
 * is BasicAI) has SOMETHING concrete to instantiate before designers author
 * their own AI subclass. Same role as `USeinNavigationAStar` for
 * navigation: minimal-but-complete reference impl that ships in the box.
 *
 * Behavior: nothing. Tick is a no-op; emits no commands. The dropped slot's
 * units sit idle but the framework's lifecycle/registration pipeline is
 * fully exercised (slot transitions to AITakeover, controller registered
 * with WorldSubsystem, ticked during CommandProcessing phase, properly
 * unregistered if the player reconnects). Designers replace this with their
 * project's actual strategic AI by setting `DefaultAIControllerClass` in
 * plugin settings.
 *
 * Why a real class instead of just leaving the path empty: empty defaults
 * produce silent "did the AI takeover wire fire?" ambiguity. With this
 * concrete fallback, the registration log line + sim tick on the controller
 * happen unconditionally — designers see the takeover happen even if their
 * own AI isn't authored yet.
 */

#pragma once

#include "CoreMinimal.h"
#include "AI/SeinAIController.h"
#include "SeinNullAIController.generated.h"

UCLASS(BlueprintType, meta = (DisplayName = "Null AI Controller"))
class SEINARTSCOREENTITY_API USeinNullAIController : public USeinAIController
{
	GENERATED_BODY()

public:
	// All overrides intentionally absent — defaults to the abstract base's
	// no-op `_Implementation` bodies (OnRegistered/OnUnregistered/Tick).
	// The class exists purely so the soft-class-path resolution in
	// USeinNetSubsystem has a concrete fallback target.
};
