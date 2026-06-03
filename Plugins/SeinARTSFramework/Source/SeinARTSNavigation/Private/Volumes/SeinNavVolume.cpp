/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavVolume.cpp
 */

#include "Volumes/SeinNavVolume.h"
#include "Settings/PluginSettings.h"
#include "Debug/SeinNavDebugComponent.h"

#include "Components/BrushComponent.h"
#include "Engine/CollisionProfile.h"

ASeinNavVolume::ASeinNavVolume(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;

	// Match ANavMeshBoundsVolume: NoCollision profile keeps the FBrushSceneProxy
	// orange-wireframe rendering in editor while guaranteeing no physics/query.
	if (UBrushComponent* BrushComp = GetBrushComponent())
	{
		BrushComp->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		BrushComp->Mobility = EComponentMobility::Static;
	}

	BrushColor = FColor(255, 165, 0, 255); // orange
	bColored = true;

#if UE_ENABLE_DEBUG_DRAWING
	// Scene-proxy debug viz. Visible in editor pre-PIE when the Navigation
	// show flag is on; null + stripped in shipping.
	DebugComponent = CreateDefaultSubobject<USeinNavDebugComponent>(TEXT("NavDebugRenderer"));
	if (DebugComponent && GetBrushComponent())
	{
		DebugComponent->SetupAttachment(GetBrushComponent());
	}
#endif
}

FBox ASeinNavVolume::GetVolumeWorldBounds() const
{
	if (UBrushComponent* BrushComp = GetBrushComponent())
	{
		return BrushComp->Bounds.GetBox();
	}
	return FBox(ForceInit);
}

#if WITH_EDITOR
void ASeinNavVolume::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);

	const FBox WorldBounds = GetVolumeWorldBounds();
	if (WorldBounds.IsValid)
	{
		// Editor-process FromFloat conversion. Result is serialized to the
		// .umap; all client platforms load identical bytes — no per-arch
		// drift at level load. Cross-platform-deterministic.
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

FFixedPoint ASeinNavVolume::GetResolvedCellSize() const
{
	if (bOverrideCellSize) return CellSize;
	if (const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>())
	{
		return Settings->CellSize;
	}
	return FFixedPoint::FromInt(100);
}

FFixedPoint ASeinNavVolume::GetResolvedMaxStepHeight() const
{
	if (bOverrideMaxStepHeight) return MaxStepHeight;
	if (const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>())
	{
		return Settings->MaxStepHeight;
	}
	return FFixedPoint::FromInt(50);
}
