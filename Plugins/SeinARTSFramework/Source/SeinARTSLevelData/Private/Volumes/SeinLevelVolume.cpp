/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLevelVolume.cpp
 */

#include "Volumes/SeinLevelVolume.h"
#include "SeinLevelDataSubsystem.h"
#include "SeinLayerConfig.h"
#include "Settings/PluginSettings.h"

#include "Components/BrushComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"

ASeinLevelVolume::ASeinLevelVolume(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;

	// Match ANavMeshBoundsVolume: NoCollision keeps the brush wireframe
	// rendering in editor while guaranteeing no physics/query.
	if (UBrushComponent* BrushComp = GetBrushComponent())
	{
		BrushComp->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		BrushComp->Mobility = EComponentMobility::Static;
	}

	BrushColor = FColor(100, 220, 120, 255); // green — level-data bounds (distinct from nav orange / FoW teal)
	bColored = true;
}

FBox ASeinLevelVolume::GetVolumeWorldBounds() const
{
	if (UBrushComponent* BrushComp = GetBrushComponent())
	{
		return BrushComp->Bounds.GetBox();
	}
	return FBox(ForceInit);
}

#if WITH_EDITOR
void ASeinLevelVolume::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);

	const FBox WorldBounds = GetVolumeWorldBounds();
	if (WorldBounds.IsValid)
	{
		// Editor-process FromFloat → serialized to the .umap; all client platforms
		// load identical bytes (no per-arch drift). Cross-platform-deterministic.
		PlacedBoundsMin = FFixedVector(
			FFixedPoint::FromFloat(WorldBounds.Min.X),
			FFixedPoint::FromFloat(WorldBounds.Min.Y),
			FFixedPoint::FromFloat(WorldBounds.Min.Z));
		PlacedBoundsMax = FFixedVector(
			FFixedPoint::FromFloat(WorldBounds.Max.X),
			FFixedPoint::FromFloat(WorldBounds.Max.Y),
			FFixedPoint::FromFloat(WorldBounds.Max.Z));
		bBoundsBaked = true;

		if (bFinished)
		{
			MarkPackageDirty();
		}
	}
}
#endif

FFixedPoint ASeinLevelVolume::GetResolvedCellSize() const
{
	if (bOverrideCellSize) return CellSize;
	if (const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>())
	{
		return Settings->CellSize;
	}
	return FFixedPoint::FromInt(100);
}

FFixedPoint ASeinLevelVolume::GetResolvedMaxStepHeight() const
{
	if (bOverrideMaxStepHeight) return MaxStepHeight;
	if (const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>())
	{
		return Settings->MaxStepHeight;
	}
	return FFixedPoint::FromInt(50);
}

FFixedPoint ASeinLevelVolume::GetResolvedVisionCellSize() const
{
	if (bOverrideVisionCellSize) return VisionCellSize;
	if (const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>())
	{
		return Settings->VisionCellSize;
	}
	return FFixedPoint::FromInt(400);
}

void ASeinLevelVolume::BakeLevelData()
{
	if (UWorld* World = GetWorld())
	{
		USeinLevelDataSubsystem::BeginBake(World);
	}
}

// ----------------------------------------------------------------------------
// Debug-viz component registry
// ----------------------------------------------------------------------------

namespace
{
	/** Module-registered debug component classes (nav cell viz, fog cell viz).
	 *  Raw UClass* is safe: entries live for their owning module's lifetime and
	 *  are removed in ShutdownModule. */
	TArray<UClass*> GSeinLevelVolumeDebugComponentClasses;

	/** Module-registered USeinLayerConfig subclasses (one editable instance per
	 *  volume). Same lifetime/safety as the debug-component registry above. */
	TArray<UClass*> GSeinLevelVolumeLayerConfigClasses;
}

void ASeinLevelVolume::RegisterDebugComponentClass(UClass* ComponentClass)
{
	if (ComponentClass)
	{
		GSeinLevelVolumeDebugComponentClasses.AddUnique(ComponentClass);
	}
}

void ASeinLevelVolume::UnregisterDebugComponentClass(UClass* ComponentClass)
{
	GSeinLevelVolumeDebugComponentClasses.Remove(ComponentClass);
}

void ASeinLevelVolume::RegisterLayerConfigClass(UClass* ConfigClass)
{
	if (ConfigClass)
	{
		GSeinLevelVolumeLayerConfigClasses.AddUnique(ConfigClass);
	}
}

void ASeinLevelVolume::UnregisterLayerConfigClass(UClass* ConfigClass)
{
	GSeinLevelVolumeLayerConfigClasses.Remove(ConfigClass);
}

USeinLayerConfig* ASeinLevelVolume::GetLayerConfig(TSubclassOf<USeinLayerConfig> ConfigClass) const
{
	if (!ConfigClass)
	{
		return nullptr;
	}
	for (const TObjectPtr<USeinLayerConfig>& Config : LayerConfigs)
	{
		if (Config && Config->IsA(ConfigClass))
		{
			return Config;
		}
	}
	return nullptr;
}

#if WITH_EDITOR
void ASeinLevelVolume::ReconcileLayerConfigs()
{
	// Additive: drop only nulls (a config whose class unloaded), then add one
	// instance of each registered subclass not already present. Never removes a
	// live, edited config for a transiently-unregistered class.
	bool bChanged = LayerConfigs.RemoveAll([](const TObjectPtr<USeinLayerConfig>& C) { return C == nullptr; }) > 0;
	for (UClass* ConfigClass : GSeinLevelVolumeLayerConfigClasses)
	{
		if (!ConfigClass)
		{
			continue;
		}
		const bool bHas = LayerConfigs.ContainsByPredicate(
			[ConfigClass](const TObjectPtr<USeinLayerConfig>& C) { return C && C->GetClass() == ConfigClass; });
		if (!bHas)
		{
			if (USeinLayerConfig* NewCfg = NewObject<USeinLayerConfig>(this, ConfigClass))
			{
				LayerConfigs.Add(NewCfg);
				bChanged = true;
			}
		}
	}
	if (bChanged)
	{
		MarkPackageDirty();
	}
}
#endif

void ASeinLevelVolume::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();

#if !UE_BUILD_SHIPPING
	// Attach one transient instance of each registered debug component class.
	// Re-entrant (map reload, RerunConstructionScripts): skip classes already
	// present on the actor. Editor-idle + PIE both pass through here.
	if (IsTemplate())
	{
		return; // never mutate the CDO / archetypes
	}
	for (UClass* ComponentClass : GSeinLevelVolumeDebugComponentClasses)
	{
		if (!ComponentClass || FindComponentByClass(ComponentClass))
		{
			continue;
		}
		UActorComponent* Comp = NewObject<UActorComponent>(this, ComponentClass, NAME_None, RF_Transient);
		if (!Comp)
		{
			continue;
		}
		if (USceneComponent* AsScene = Cast<USceneComponent>(Comp))
		{
			AsScene->SetupAttachment(GetBrushComponent());
		}
		AddInstanceComponent(Comp);
		Comp->RegisterComponent();
	}
#endif

#if WITH_EDITOR
	// Per-volume custom-layer config: ensure an editable instance of each registered
	// USeinLayerConfig subclass is present (additive). Editor-only authoring concern.
	if (!IsTemplate())
	{
		ReconcileLayerConfigs();
	}
#endif
}
