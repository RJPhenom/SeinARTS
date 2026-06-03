/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFaction.h
 * @brief   UDataAsset defining a playable faction: its identity, resource kit,
 *          available entity classes, and starting entities.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Core/SeinFactionID.h"
#include "Types/FixedPoint.h"
#include "Actor/SeinActor.h"
#include "Data/SeinResourceTypes.h"
#include "SeinFaction.generated.h"

/**
 * Data asset defining a playable faction.
 * Designers create one per faction to configure identity, economy, and roster.
 */
UCLASS(BlueprintType)
class SEINARTSCOREENTITY_API USeinFaction : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Display name of this faction */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Faction")
	FText FactionName;

	/** Unique faction identifier used by the simulation */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Faction")
	FSeinFactionID FactionID;

	/** Blueprint classes this faction can build/produce */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Faction")
	TArray<TSubclassOf<ASeinActor>> AvailableUnitClasses;

	/**
	 * This faction's resource kit — which catalog resources are available to
	 * players of this faction, plus optional per-faction overrides (starting
	 * value, cap). Only catalog entries referenced here are usable by this
	 * faction; unreferenced resources are ignored.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Economy",
		meta = (TitleProperty = "ResourceTag"))
	TArray<FSeinFactionResourceEntry> ResourceKit;

	/** Entity classes to spawn on game start (e.g., HQ building) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Faction")
	TArray<TSubclassOf<ASeinActor>> StartingEntityClasses;
};
