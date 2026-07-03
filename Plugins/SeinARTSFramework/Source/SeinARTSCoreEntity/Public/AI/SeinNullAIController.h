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

/**
 * The stand-in AI controller that takes over a player's units when they drop out of a match,
 * before you have authored your own strategic AI. It does nothing on its own — the units just
 * sit idle — but it wires up the whole takeover pipeline so you can see it fire. This is the
 * one selected out of the box for the Default AI Controller Class setting.
 *
 * When a slot is dropped and the drop policy is BasicAI, the framework needs SOMETHING concrete
 * to instantiate as the takeover controller. This is that minimal-but-complete reference impl
 * that ships in the box — the same role Sein Navigation (A*) plays for navigation. Its tick is
 * a genuine no-op and it emits no commands, so the abandoned units stand still, but the full
 * lifecycle runs: the slot transitions to AITakeover, the controller registers with the World
 * Subsystem, ticks during the Command Processing phase, and unregisters cleanly if the player
 * reconnects. Replace it with your project's real strategic AI by pointing the Default AI
 * Controller Class setting at your own subclass.
 *
 * It exists as a real class rather than an empty class path on purpose: an empty default leaves
 * silent "did the takeover wire even fire?" ambiguity, whereas this concrete fallback makes the
 * registration log line and the sim tick happen unconditionally, so you can watch the takeover
 * occur even before your own AI is authored.
 */
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
