/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLobbyMapEntry.h
 * @brief   One playable-map entry for the lobby's map dropdown.
 *
 * Designers list these in `USeinARTSCoreSettings::AvailableMaps`. The
 * lobby UI populates its map combobox from the list, and the host's
 * selection drives:
 *   - The lobby's slot count (resizes to `SlotCount` on selection).
 *   - The destination of `ServerStartMatch`'s ServerTravel.
 *   - The grey-out state for smaller-map options (when a host can't
 *     shrink without losing a claimed slot).
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPtr.h"
#include "Engine/Texture2D.h"
#include "SeinLobbyMapEntry.generated.h"

USTRUCT(BlueprintType, meta = (DisplayName = "Sein Lobby Map Entry"))
struct SEINARTSCOREENTITY_API FSeinLobbyMapEntry
{
	GENERATED_BODY()

	/** The gameplay map asset. Soft pointer — not loaded until ServerTravel. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sein Lobby Map Entry")
	TSoftObjectPtr<UWorld> Map;

	/** UI label in the map dropdown ("1v1 Skirmish", "Coastal Assault 4P", ...). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sein Lobby Map Entry")
	FText DisplayName;

	/** Number of slots this map supports. Designer-declared (matches the count
	 *  of `SeinPlayerStart` actors with `PlayerSlot > 0` in the level). The
	 *  lobby resizes its slot array to this on selection. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sein Lobby Map Entry",
		meta = (ClampMin = "1", ClampMax = "16"))
	int32 SlotCount = 2;

	/** Number of distinct teams the map's mode supports (e.g. 2 for 1v1 / 2v2,
	 *  4 for FFA-with-teams). Drives the per-slot team picker — the lobby UI
	 *  populates the team combobox with values 1..TeamCount. Framework treats
	 *  TeamID as opaque; this is purely a UI hint to designers and does NOT
	 *  validate or clamp slots' assigned TeamIDs server-side (that's a
	 *  designer-policy decision, not a framework one). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sein Lobby Map Entry",
		meta = (ClampMin = "1", ClampMax = "16"))
	int32 TeamCount = 2;

	/** Optional preview image shown in the lobby UI. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sein Lobby Map Entry")
	TObjectPtr<UTexture2D> Thumbnail;
};
