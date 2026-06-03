/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinActionSlotData.h
 * @brief   Generic data struct representing an activatable action slot (ability,
 *          production item, or custom action) with state for UI display.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "SeinActionSlotData.generated.h"

class UTexture2D;

/** Current state of an action slot. Drives visual presentation (greyed out, cooldown sweep, etc.). */
UENUM(BlueprintType)
enum class ESeinActionSlotState : uint8
{
	/** No action assigned to this slot. */
	Empty,
	/** Action is available for activation. */
	Available,
	/** Action is cooling down. */
	OnCooldown,
	/** Action is disabled (prerequisites not met, blocked by tags, etc.). */
	Disabled,
	/** Action cannot be afforded (insufficient resources). */
	Unaffordable,
	/** Action is currently active/executing. */
	Active
};

/**
 * Data describing a single action slot for UI display.
 * This is a generic, game-agnostic representation — works for abilities,
 * production items, research items, or any custom activatable action.
 */
USTRUCT(BlueprintType)
struct SEINARTSUITOOLKIT_API FSeinActionSlotData
{
	GENERATED_BODY()

	/** Display name of this action. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Action")
	FText Name;

	/** Tooltip text (typically a longer description). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Action")
	FText Tooltip;

	/** Icon texture for the button. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Action")
	TObjectPtr<UTexture2D> Icon = nullptr;

	/** Gameplay tag identifying this action (ability tag, identity tag, etc.). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Action")
	FGameplayTag ActionTag;

	/** Current state of the action. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Action")
	ESeinActionSlotState State = ESeinActionSlotState::Empty;

	/** Cooldown progress (0 = ready, 1 = full cooldown remaining). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Action")
	float CooldownPercent = 0.0f;

	/** Resource cost to activate, keyed by resource tag (float for display). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Action", meta = (Categories = "SeinARTS.Resource"))
	TMap<FGameplayTag, float> ResourceCost;

	/** Slot index within the panel (for input mapping). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Action")
	int32 SlotIndex = -1;

	/** Hotkey display label (e.g., "Q", "W", "1"). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Action")
	FText HotkeyLabel;
};
