/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLevelDataSubsystem.h
 * @brief   Per-world owner of the level-data substrate (CP1.1). Instantiates the
 *          USeinLevelData impl from USeinARTSCoreSettings::LevelDataClass (default
 *          USeinLevelDataDefault), loads the baked asset at begin-play, and exposes
 *          the bake drive + a GetLevelDataForWorld accessor. Mirrors
 *          USeinNavigationSubsystem. Supports the Editor world so the volume's
 *          "Bake Level Data" button works pre-PIE.
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SeinLevelDataSubsystem.generated.h"

class USeinLevelData;
class USeinLevelDataAsset;

/** Fired exactly once after the world has either adopted its initial baked
 *  substrate or deliberately resolved to an empty/disabled substrate.
 *  Static-environment consumers use this as the pre-bootstrap preparation
 *  barrier; every listener has already initialized before BeginPlay dispatch. */
DECLARE_MULTICAST_DELEGATE(FSeinOnInitialLevelDataPrepared);

UCLASS()
class SEINARTSLEVELDATA_API USeinLevelDataSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	/** Release the active substrate and its provider graph before module unload. */
	void ReleaseModuleOwnedStateForModuleUnload();

	/**
	 * Resolve the initial baked substrate once, before any match StateContract
	 * freezes. Idempotent. A world with no baked asset (or with Level Data
	 * disabled) is still a successfully prepared, intentionally empty world.
	 */
	bool EnsureInitialRuntimeDataPrepared(UWorld& World);
	bool IsInitialRuntimeDataPrepared() const
	{
		return bInitialRuntimeDataPrepared;
	}

	/** Internal StateContract binding. Validates the active implementation's
	 * exact mutable-state claim and static substrate digest, then latches local
	 * mutation evidence only for the committing bootstrap transaction. */
	bool FreezeCanonicalStateBinding(
		bool bCommit,
		FString& OutFrame,
		FString& OutError);

	FSeinOnInitialLevelDataPrepared OnInitialLevelDataPrepared;

	/** The active substrate for this world (or null pre-init). */
	USeinLevelData* GetLevelData() const { return LevelData; }

	// Static convenience accessors (mirror USeinNavigationSubsystem).
	static USeinLevelData* GetLevelDataForWorld(const UObject* WorldContextObject);
	static bool BeginBake(UWorld* World);
	static bool IsBaking(UWorld* World);
	static void RequestCancelBake(UWorld* World);

protected:
	bool LoadBakedAsset(UWorld& World);

	UPROPERTY(Transient)
	TObjectPtr<USeinLevelData> LevelData;

	bool bInitialRuntimeDataPrepared = false;
	bool bStateBindingFrozen = false;
	FString FrozenStateBindingFrame;
	FGuid FrozenStaticEnvironmentDigest;
	uint64 FrozenStaticEnvironmentGeneration = 0;
};
