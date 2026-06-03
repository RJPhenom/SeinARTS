/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarVolume.cpp
 */

#include "Volumes/SeinFogOfWarVolume.h"
#include "Debug/SeinFogOfWarDebugComponent.h"
#include "Settings/PluginSettings.h"

#include "Components/BrushComponent.h"
#include "Engine/CollisionProfile.h"

ASeinFogOfWarVolume::ASeinFogOfWarVolume(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;

	if (UBrushComponent* BrushComp = GetBrushComponent())
	{
		BrushComp->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		BrushComp->Mobility = EComponentMobility::Static;
	}

	// Distinct color from the nav volume's orange so the two are visually
	// distinguishable when both sit on the same level. Teal / cyan reads as
	// "vision-related" against the terrain palette.
	BrushColor = FColor(0, 200, 200, 255);
	bColored = true;

#if UE_ENABLE_DEBUG_DRAWING
	// Scene-proxy debug viz. Visible in editor pre-PIE when the FogOfWar
	// show flag is on; null + stripped in shipping.
	DebugComponent = CreateDefaultSubobject<USeinFogOfWarDebugComponent>(TEXT("FogOfWarDebugRenderer"));
	if (DebugComponent && GetBrushComponent())
	{
		DebugComponent->SetupAttachment(GetBrushComponent());
	}
#endif
}

FBox ASeinFogOfWarVolume::GetVolumeWorldBounds() const
{
	if (UBrushComponent* BrushComp = GetBrushComponent())
	{
		return BrushComp->Bounds.GetBox();
	}
	return FBox(ForceInit);
}

#if WITH_EDITOR
void ASeinFogOfWarVolume::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);

	const FBox WorldBounds = GetVolumeWorldBounds();
	if (WorldBounds.IsValid)
	{
		// Editor-process conversion. All clients later load these int64
		// bits from disk — no per-platform FromFloat at runtime.
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

FFixedPoint ASeinFogOfWarVolume::GetResolvedCellSize() const
{
	if (bOverrideCellSize) return CellSize;
	if (const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>())
	{
		return Settings->VisionCellSize;
	}
	return FFixedPoint::FromInt(400);
}
