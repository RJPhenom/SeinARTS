/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinTerrainVolume.cpp
 */

#include "Volumes/SeinTerrainVolume.h"

#include "Components/BrushComponent.h"
#include "Engine/CollisionProfile.h"

ASeinTerrainVolume::ASeinTerrainVolume(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;

	// NoCollision static brush — same as ASeinLevelVolume: wireframe in editor,
	// no physics/query (the bake reads the brush shape directly via EncompassesPoint).
	if (UBrushComponent* BrushComp = GetBrushComponent())
	{
		BrushComp->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		BrushComp->Mobility = EComponentMobility::Static;
	}

	BrushColor = FColor(210, 150, 60, 255); // amber — terrain-type region (distinct from level-volume green)
	bColored = true;
}

FBox ASeinTerrainVolume::GetVolumeWorldBounds() const
{
	if (UBrushComponent* BrushComp = GetBrushComponent())
	{
		return BrushComp->Bounds.GetBox();
	}
	return FBox(ForceInit);
}
