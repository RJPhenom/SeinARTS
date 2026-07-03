/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFactionService.h
 * @brief   Pluggable faction-discovery subsystem.
 *
 * Default impl scans the AssetRegistry for `USeinFaction` data assets at
 * Initialize time and exposes them to the lobby UI + claim validation.
 * Pluggable via `USeinARTSCoreSettings::FactionServiceClass` so designers
 * can override to layer in player-designed factions, modded folder scans,
 * or network-imported definitions.
 *
 * Lifetime: GameInstance scope — survives map travel between lobby and
 * gameplay maps so the cached faction list stays warm. Cache invalidates
 * on any `RegisterRuntimeFaction` / `UnregisterRuntimeFaction` call.
 *
 * Lookup paths:
 *   GetAvailableFactions() — every known faction (asset + runtime-registered)
 *   IsFactionValid(ID)     — fast O(N) scan; lobby slot-claim validation
 *   ResolveFaction(ID)     — soft pointer back to the underlying USeinFaction
 *
 * For lobby UI filtering against a preset's AllowedFactions allowlist, use
 * `GetAvailableFactionsFiltered`.
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UObject/SoftObjectPtr.h"
#include "Core/SeinFactionID.h"
#include "SeinFactionService.generated.h"

class USeinFaction;

/**
 * Finds the playable factions in your project and hands them to the lobby so players can pick a
 * side, and validates the faction a player claims in a lobby slot. This is the faction source
 * selected out of the box; swap in your own to add player-designed, modded, or downloaded factions.
 *
 * On startup it scans the AssetRegistry for every Sein Faction data asset and caches them as soft
 * references (name and icon load only when the UI displays them). It re-scans when the AssetRegistry
 * finishes loading (cooked builds can finish after this subsystem starts) and whenever you call
 * Refresh From Asset Registry. Alongside the discovered assets you can register or unregister
 * runtime factions (player-designed, modded, or network-imported); asset factions can only be
 * removed by deleting the asset. Any change fires On Factions Changed so faction-picker widgets
 * rebuild. It lives at GameInstance scope, so the cached list survives travel between the lobby and
 * gameplay maps and stays warm.
 *
 * Lookups: Get Available Factions returns everything known (assets plus runtime-registered);
 * Get Available Factions Filtered narrows that to a preset's allowlist (an empty allowlist means
 * no restriction, so the full list is returned); Is Faction Valid is a linear scan the lobby uses
 * to reject slot claims with unknown faction IDs; Resolve Faction maps a faction ID back to its
 * underlying Sein Faction (or a null soft pointer if there is no match) so the UI can show its
 * name and icon.
 */
UCLASS(BlueprintType, Blueprintable)
class SEINARTSCOREENTITY_API USeinFactionService : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// UGameInstanceSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** True iff this subsystem class should be the one instantiated by the
	 *  GameInstance — false on the base class when the project has overridden
	 *  via `USeinARTSCoreSettings::FactionServiceClass`. Engine calls this
	 *  on every candidate; only the project-resolved class returns true. */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	// ========== Public API ==========

	/** All known factions (AssetRegistry-discovered + runtime-registered).
	 *  Default impl returns the cached list; override to mix in additional
	 *  sources (modded folder, player-designed factions saved to disk, etc.). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Lobby|Factions")
	virtual TArray<TSoftObjectPtr<USeinFaction>> GetAvailableFactions() const;

	/** O(N) validity check — used by `USeinLobbySubsystem::ServerHandleSlotClaim`
	 *  to reject claims with unknown faction IDs. Designer overrides may
	 *  short-circuit via a TMap if N grows large. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Lobby|Factions")
	virtual bool IsFactionValid(FSeinFactionID ID) const;

	/** Soft pointer back to the `USeinFaction` whose `FactionID` matches.
	 *  Returns null soft pointer if not found. The lobby UI uses this to look
	 *  up display name + icon for a given claimed faction. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Lobby|Factions")
	virtual TSoftObjectPtr<USeinFaction> ResolveFaction(FSeinFactionID ID) const;

	/** Filtered subset: only factions whose soft path appears in `Allowlist`.
	 *  Empty allowlist returns the full list (preset's "no restriction"
	 *  semantics). Used by the lobby's faction-picker widget when a preset
	 *  with an `AllowedFactions` allowlist is selected. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Lobby|Factions")
	virtual TArray<TSoftObjectPtr<USeinFaction>> GetAvailableFactionsFiltered(
		const TArray<TSoftObjectPtr<USeinFaction>>& Allowlist) const;

	/** Add a runtime-allocated faction (player-designed, modded, network-
	 *  imported). The faction must have a stable `FactionID` (designer
	 *  responsibility — collisions overwrite older entries). Triggers
	 *  `OnFactionsChanged`. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Lobby|Factions")
	virtual void RegisterRuntimeFaction(USeinFaction* Faction);

	/** Remove a previously-registered runtime faction. AssetRegistry-discovered
	 *  factions cannot be unregistered through this path — use the asset
	 *  delete flow. Triggers `OnFactionsChanged`. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Lobby|Factions")
	virtual void UnregisterRuntimeFaction(USeinFaction* Faction);

	/** Force a re-scan of the AssetRegistry. Cheap; called on Initialize, on
	 *  AssetRegistry's `OnFilesLoaded` (cooked builds may complete loading
	 *  after subsystem init), and explicitly by designer code that authored
	 *  new faction assets at runtime (PIE-only — cooked builds can't add
	 *  assets). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Lobby|Factions")
	virtual void RefreshFromAssetRegistry();

	// ========== Events ==========

	/** Multicast fired whenever the faction list changes (initial scan
	 *  completes, runtime register/unregister, manual refresh). UI binds
	 *  to refresh faction-picker widgets. */
	DECLARE_MULTICAST_DELEGATE(FOnFactionsChanged);
	FOnFactionsChanged OnFactionsChanged;

	/** BP-bindable variant. */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnFactionsChangedDynamic);
	UPROPERTY(BlueprintAssignable, Category = "SeinARTS|Lobby|Factions")
	FOnFactionsChangedDynamic OnFactionsChangedBP;

protected:
	/** Cached AssetRegistry-discovered factions (soft refs to keep memory
	 *  low until UI wants to display name/icon). Re-populated by
	 *  RefreshFromAssetRegistry. */
	UPROPERTY()
	TArray<TSoftObjectPtr<USeinFaction>> CachedAssetFactions;

	/** Runtime-registered factions kept as hard refs (the project's
	 *  responsibility to keep alive — typically held by a save-game
	 *  object or a faction-editor subsystem). */
	UPROPERTY()
	TArray<TObjectPtr<USeinFaction>> RuntimeFactions;

	/** Internal: fire both delegate variants. */
	void BroadcastFactionsChanged();

private:
	/** AssetRegistry's "files loaded" hook — cooked builds may finish loading
	 *  after our Initialize. Bound in Initialize, unbound in Deinitialize. */
	FDelegateHandle FilesLoadedHandle;
};
