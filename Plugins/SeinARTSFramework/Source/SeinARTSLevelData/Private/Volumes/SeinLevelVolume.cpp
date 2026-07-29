/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLevelVolume.cpp
 */

#include "Volumes/SeinLevelVolume.h"
#include "SeinLevelDataSubsystem.h"
#include "SeinLayerConfig.h"
#include "Settings/PluginSettings.h"

#include "Components/ActorComponent.h"
#include "Components/BrushComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "RenderingThread.h"
#include "UObject/UObjectIterator.h"

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
	 *  are removed in PreUnloadCallback (and idempotently at shutdown). */
	TArray<UClass*> GSeinLevelVolumeDebugComponentClasses;

	/** Module-registered USeinLayerConfig subclasses (one editable instance per
	 *  volume). Same lifetime/safety as the debug-component registry above. */
	TArray<UClass*> GSeinLevelVolumeLayerConfigClasses;

#if !UE_BUILD_SHIPPING
	bool IsUsableDebugComponentClass(const UClass* ComponentClass)
	{
		return ComponentClass
			&& ComponentClass->IsChildOf(UActorComponent::StaticClass())
			&& !ComponentClass->HasAnyClassFlags(
				CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists);
	}

	bool IsLiveLevelVolume(const ASeinLevelVolume* Volume)
	{
		return IsValid(Volume) && !Volume->IsTemplate();
	}

	bool HasExactDebugComponent(
		const ASeinLevelVolume& Volume,
		const UClass* ComponentClass)
	{
		TInlineComponentArray<UActorComponent*> Components(&Volume);
		return Components.ContainsByPredicate(
			[ComponentClass](const UActorComponent* Component)
			{
				return IsValid(Component)
					&& Component->GetClass() == ComponentClass;
			});
	}

	void AttachDebugComponent(
		ASeinLevelVolume& Volume,
		UClass* ComponentClass)
	{
		if (HasExactDebugComponent(Volume, ComponentClass))
		{
			return;
		}

		UActorComponent* Component = NewObject<UActorComponent>(
			&Volume, ComponentClass, NAME_None, RF_Transient);
		if (!Component)
		{
			return;
		}

		if (USceneComponent* SceneComponent =
			Cast<USceneComponent>(Component))
		{
			SceneComponent->SetupAttachment(Volume.GetBrushComponent());
		}
		Volume.AddInstanceComponent(Component);
		if (Volume.GetWorld())
		{
			Component->RegisterComponent();
		}
	}

	int32 DestroyDebugComponents(UClass* ComponentClass)
	{
		TArray<UActorComponent*> ComponentsToDestroy;
		for (TObjectIterator<UActorComponent> It; It; ++It)
		{
			UActorComponent* Component = *It;
			if (!IsValid(Component)
				|| Component->IsTemplate()
				|| Component->GetClass() != ComponentClass)
			{
				continue;
			}

			ASeinLevelVolume* Volume =
				Cast<ASeinLevelVolume>(Component->GetOwner());
			if (IsLiveLevelVolume(Volume))
			{
				ComponentsToDestroy.Add(Component);
			}
		}

		int32 NumRemoved = ComponentsToDestroy.Num();
		for (UActorComponent* Component : ComponentsToDestroy)
		{
			if (!IsValid(Component))
			{
				continue;
			}

			// DestroyComponent removes instance ownership too, but do it first
			// and explicitly so even a partially torn-down component cannot
			// leave a stale module-class reference in InstanceComponents.
			if (ASeinLevelVolume* Volume =
				Cast<ASeinLevelVolume>(Component->GetOwner()))
			{
				Volume->RemoveInstanceComponent(Component);
			}
			if (!Component->IsBeingDestroyed())
			{
				Component->DestroyComponent();
			}
		}

		// Scrub any exact-class instance entry missed by the object snapshot
		// (including an invalid or synchronously re-created component).
		for (TObjectIterator<ASeinLevelVolume> It; It; ++It)
		{
			ASeinLevelVolume* Volume = *It;
			if (!IsLiveLevelVolume(Volume))
			{
				continue;
			}

			const TArray<UActorComponent*> InstanceComponents =
				Volume->GetInstanceComponents();
			for (UActorComponent* Component : InstanceComponents)
			{
				if (Component && Component->GetClass() == ComponentClass)
				{
					Volume->RemoveInstanceComponent(Component);
					if (IsValid(Component)
						&& !Component->IsBeingDestroyed())
					{
						Component->DestroyComponent();
					}
					++NumRemoved;
				}
			}
		}

		return NumRemoved;
	}
#endif
}

void ASeinLevelVolume::RegisterDebugComponentClass(UClass* ComponentClass)
{
	check(IsInGameThread());
	if (!ComponentClass
		|| !ComponentClass->IsChildOf(UActorComponent::StaticClass())
		|| ComponentClass->HasAnyClassFlags(
			CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		return;
	}

	GSeinLevelVolumeDebugComponentClasses.AddUnique(ComponentClass);

#if !UE_BUILD_SHIPPING
	if (!IsUsableDebugComponentClass(ComponentClass))
	{
		return;
	}
	for (TObjectIterator<ASeinLevelVolume> It; It; ++It)
	{
		ASeinLevelVolume* Volume = *It;
		if (IsLiveLevelVolume(Volume))
		{
			AttachDebugComponent(*Volume, ComponentClass);
		}
	}
#endif
}

void ASeinLevelVolume::UnregisterDebugComponentClass(UClass* ComponentClass)
{
	check(IsInGameThread());
	const bool bWasRegistered =
		GSeinLevelVolumeDebugComponentClasses.Remove(ComponentClass) > 0;

#if !UE_BUILD_SHIPPING
	if (!ComponentClass
		|| !ComponentClass->IsChildOf(UActorComponent::StaticClass()))
	{
		return;
	}

	const int32 NumRemoved = DestroyDebugComponents(ComponentClass);
	if ((bWasRegistered || NumRemoved > 0)
		&& ComponentClass->IsChildOf(UPrimitiveComponent::StaticClass()))
	{
		// Primitive scene-proxy destruction is queued to the render thread.
		// Drain it while the component's owning module and vtable are callable.
		FlushRenderingCommands();
	}
#endif
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
		if (!IsUsableDebugComponentClass(ComponentClass))
		{
			continue;
		}
		AttachDebugComponent(*this, ComponentClass);
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
