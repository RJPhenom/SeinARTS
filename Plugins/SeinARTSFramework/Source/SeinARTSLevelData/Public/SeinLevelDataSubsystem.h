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

UCLASS()
class SEINARTSLEVELDATA_API USeinLevelDataSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	/** The active substrate for this world (or null pre-init). */
	USeinLevelData* GetLevelData() const { return LevelData; }

	// Static convenience accessors (mirror USeinNavigationSubsystem).
	static USeinLevelData* GetLevelDataForWorld(const UObject* WorldContextObject);
	static bool BeginBake(UWorld* World);
	static bool IsBaking(UWorld* World);
	static void RequestCancelBake(UWorld* World);

protected:
	void LoadBakedAsset(UWorld& World);

	UPROPERTY(Transient)
	TObjectPtr<USeinLevelData> LevelData;
};
