/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinPlayerViewModel.h
 * @brief   Generic ViewModel providing a read-only lens into a player's sim
 *          state (resources, tech, elimination). Widgets bind to this for
 *          resource displays, tech tree UI, etc.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinPlayerID.h"
#include "GameplayTagContainer.h"
#include "SeinPlayerViewModel.generated.h"

class USeinWorldSubsystem;

/** Broadcast when the player ViewModel has been refreshed with new sim data. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPlayerViewModelRefreshed);

/**
 * Generic ViewModel for a player's sim state.
 *
 * Created and cached by USeinUISubsystem. Automatically refreshed each sim tick.
 * All resource values are exposed as float for display purposes.
 */
UCLASS(BlueprintType)
class SEINARTSUITOOLKIT_API USeinPlayerViewModel : public UObject
{
	GENERATED_BODY()

public:
	// ========== Identity ==========

	/** The player ID this ViewModel tracks. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Player")
	FSeinPlayerID PlayerID;

	/** Whether this player has been eliminated. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Player")
	bool bIsEliminated = false;

	// ========== Resource Access ==========

	/** Get the current amount of a resource by tag. Returns 0 if the resource isn't present. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Player", meta = (Categories = "SeinARTS.Resource"))
	float GetResource(FGameplayTag ResourceTag) const;

	/** Get all resources as a tag→float map. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Player")
	TMap<FGameplayTag, float> GetAllResources() const;

	/** Get the tags of all resources this player has. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Player")
	TArray<FGameplayTag> GetResourceTags() const;

	/** Check if the player can afford a set of costs (float values). Plain balance comparison —
	 *  does not consult catalog CostDirection; use USeinResourceBPFL::SeinCanAfford for that. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Player", meta = (Categories = "SeinARTS.Resource"))
	bool CanAfford(const TMap<FGameplayTag, float>& Cost) const;

	// ========== Tech Access ==========

	/** Check if the player has a specific tech tag. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Player")
	bool HasTechTag(FGameplayTag Tag) const;

	/** Get all unlocked tech tags. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Player")
	FGameplayTagContainer GetUnlockedTechTags() const;

	// ========== Lifecycle ==========

	/** Fired after Refresh() updates cached data. */
	UPROPERTY(BlueprintAssignable, Category = "SeinARTS|UI|Player")
	FOnPlayerViewModelRefreshed OnRefreshed;

	/** Initialize with a player ID and world subsystem reference. */
	void Initialize(FSeinPlayerID InPlayerID, USeinWorldSubsystem* InWorldSubsystem);

	/** Refresh cached data from the simulation. Called each sim tick. */
	void Refresh();

private:
	/** Cached world subsystem for sim data access. */
	UPROPERTY()
	TWeakObjectPtr<USeinWorldSubsystem> WorldSubsystem;

	/** Cached resource values (float for display). Updated on Refresh(). */
	TMap<FGameplayTag, float> CachedResources;

	/** Cached tech tags. Updated on Refresh(). */
	FGameplayTagContainer CachedTechTags;
};
