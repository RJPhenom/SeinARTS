/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarSubsystem.h
 * @brief   Thin world subsystem that owns the active USeinFogOfWar instance.
 *
 *          Reads `USeinARTSCoreSettings::FogOfWarClass` on Initialize, new's
 *          up that class, and re-exposes it to the rest of the engine (reader
 *          BPFL, cross-module LOS delegate bind). Registers the fog as the
 *          "FogOfWar" layer provider on the unified level-data substrate and
 *          adopts the baked channel at begin-play.
 *
 *          The subsystem does NOT know what a "grid" or "shadowcast" is — it
 *          only knows a USeinFogOfWar exists. All vision semantics live on
 *          the active subclass. Parallels USeinNavigationSubsystem.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinTickPhase.h"
#include "SeinStaticEnvironmentAdoption.h"
#include "Subsystems/WorldSubsystem.h"
#include "SeinFogOfWarSubsystem.generated.h"

class USeinFogOfWar;
class USeinLevelData;
class USeinLevelDataSubsystem;
class FSeinARTSFogOfWarModule;
class FSeinFogOfWarStateCodecRegistry;
struct FSeinFogOfWarCanonicalStateProvider;

UCLASS()
class SEINARTSFOGOFWAR_API USeinFogOfWarSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	/** The active fog-of-war instance. Null when FogOfWarClass is None (fog off, WYSIWYG); otherwise
	 *  valid after Initialize. All callers null-guard. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Fog Of War")
	USeinFogOfWar* GetFogOfWar() const { return FogOfWar; }

	/** Convenience accessor for BP + external callers that only have a
	 *  UObject-with-world. Returns null if the world has no fog subsystem. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Fog Of War", meta = (WorldContext = "WorldContextObject"))
	static USeinFogOfWar* GetFogOfWarForWorld(const UObject* WorldContextObject);

	/** True once initial Level Data adoption resolved to Adopted or the valid
	 *  NotApplicable/no-bake fallback. False while pending or after rejection. */
	bool IsInitialStaticEnvironmentPrepared() const
	{
		return bInitialStaticEnvironmentPrepared;
	}

	/** Exact result of the latest initial/static-environment adoption attempt. */
	const FSeinStaticEnvironmentAdoptionResult&
	GetInitialStaticEnvironmentAdoptionResult() const
	{
		return InitialStaticEnvironmentAdoptionResult;
	}

	/**
	 * Pre-unload fail-stop. Severs delegates/systems, unregisters the level
	 * provider, and destroys the active implementation while its module code
	 * is still executable. Idempotent with ordinary world teardown.
	 */
	void ReleaseModuleOwnedStateForModuleUnload();

	/** Internal tick-time exact-static-environment revalidation. */
	bool ValidateCommittedCanonicalStateBinding();

private:
	friend class FSeinARTSFogOfWarModule;
	friend class FSeinFogOfWarStateCodecRegistry;
	friend struct FSeinFogOfWarCanonicalStateProvider;

	/** The active fog-of-war for this world. Instantiated from
	 *  `USeinARTSCoreSettings::FogOfWarClass` during Initialize. */
	UPROPERTY(Transient)
	TObjectPtr<USeinFogOfWar> FogOfWar;

	/**
	 * Owned deterministic PostTick system. Running stamps inside Core's ordered
	 * sim pipeline guarantees every canonical-state observer sees the completed
	 * tick, independent of multicast delegate binding order.
	 */
	TUniquePtr<ISeinSystem> StampSystem;

	/** The shared level-data substrate (CP1.1), resolved in Initialize. Fog
	 *  registers as its "FogOfWar" layer provider and loads its runtime grid
	 *  from the baked channel when present. Weak — owned by USeinLevelDataSubsystem. */
	TWeakObjectPtr<USeinLevelData> LevelData;
	TWeakObjectPtr<USeinLevelDataSubsystem> LevelDataSubsystem;

	/** Handle for our USeinLevelData::OnLevelDataMutated subscription; removed at
	 *  Deinitialize. */
	FDelegateHandle LevelDataMutatedHandle;
	FDelegateHandle InitialLevelDataPreparedHandle;

	/** Exact concrete state-codec generation selected once for this world. */
	uint64 StateCodecToken = 0;
	bool bSimDelegatesBound = false;
	bool bFogConfigured = false;
	bool bInitialStaticEnvironmentPrepared = false;
	bool bStateBindingFrozen = false;
	FSeinStaticEnvironmentAdoptionResult
		InitialStaticEnvironmentAdoptionResult;
	FString ConfiguredFogClassPath;
	FString StateCodecFailureReason;
	FString FrozenStateBindingFrame;
	FGuid FrozenStaticEnvironmentDigest;

	/** Adopt/initialize the static fog environment when the shared Level Data
	 *  readiness barrier completes. */
	FSeinStaticEnvironmentAdoptionResult
	LoadBakedAssetIntoFogOfWar(UWorld& World);
	void HandleInitialLevelDataPrepared();
	bool PrepareInitialCanonicalStateEnvironment(FString& OutError);

	/** Re-adopt the shared substrate's fog channel when it rebakes / reloads
	 *  (CP1.1). Empty Level Data and non-participating fogs retain their valid
	 *  fallback; rejected data clears readiness until a corrected bake is
	 *  adopted. */
	void OnLevelDataChanged();

	/** If no baked data loaded, let the fog impl auto-size its grid from
	 *  the level's ASeinLevelVolumes so stamping + debug viz work before
	 *  the level has been baked. */
	void InitGridIfUnbaked(UWorld& World);

	/** Registers the ordered PostTick stamp system during subsystem initialization. */
	void RegisterStampSystem(UWorld& World);

	/** Binds cross-module delegates on USeinWorldSubsystem so sim code can
	 *  query visibility without importing fog-of-war headers. */
	void BindSimDelegates(UWorld& World);

	void UnbindSimDelegates();
	void ReleaseModuleOwnedState();

	/** Provider-only exact implementation/static-environment contract freeze. */
	bool FreezeCanonicalStateBinding(
		bool bCommit,
		FString& OutFrame,
		FGuid& OutStaticDigest,
		FString& OutError);
	bool RevalidateCanonicalStateBindingCandidate(
		const FString& ExpectedFrame,
		const FGuid& ExpectedStaticDigest,
		FString& OutError);
	void CommitCanonicalStateBinding(
		const FString& Frame,
		const FGuid& StaticDigest);
	void InvalidateCommittedCanonicalStateBinding(
		const FString& Reason);

	/** Called by codec withdrawal before the concrete module can unload. */
	void InvalidateCanonicalStateCodecLease(
		uint64 Token,
		const FString& Reason);
};
