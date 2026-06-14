/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMinimapViewModel.cpp
 * @brief   Minimap view-model implementation.
 */

#include "ViewModel/SeinMinimapViewModel.h"
#include "Lib/SeinUIBPFL.h"

#include "Simulation/SeinWorldSubsystem.h"
#include "Core/SeinEntityPool.h"
#include "Core/SeinPlayerID.h"
#include "Types/Entity.h"
#include "Types/Vector.h"
#include "Player/SeinPlayerController.h"
#include "Actor/SeinActor.h"

#include "SeinLevelData.h"
#include "SeinLevelDataSubsystem.h"

#include "SeinFogOfWar.h"
#include "SeinFogOfWarSubsystem.h"
#include "SeinFogOfWarTypes.h"

#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"

void USeinMinimapViewModel::Initialize(USeinWorldSubsystem* InWorldSubsystem, UWorld* InWorld)
{
	WorldSubsystem = InWorldSubsystem;
	WorldPtr = InWorld;

	ResolveBounds();
	if (bHasBounds)
	{
		ResolveBackground();
	}
}

void USeinMinimapViewModel::Refresh()
{
	// Bounds + background resolve lazily — the level-data substrate may not be loaded
	// when the subsystem first comes up (it loads at world begin-play).
	if (!bHasBounds)
	{
		ResolveBounds();
	}
	if (!BackgroundTexture)
	{
		ResolveBackground();
	}

	RebuildBlips();

	if ((RefreshCounter++ % FMath::Max(1, FogUpdateInterval)) == 0)
	{
		UpdateFogTexture();
	}

	OnRefreshed.Broadcast();
}

void USeinMinimapViewModel::ResolveBounds()
{
	bHasBounds = false;

	UWorld* W = WorldPtr.Get();
	if (!W)
	{
		return;
	}

	USeinLevelData* LD = USeinLevelDataSubsystem::GetLevelDataForWorld(W);
	if (!LD || !LD->HasRuntimeData())
	{
		return;
	}

	const FVector OriginV = LD->GetOrigin().ToVector();
	const FIntPoint Dims = LD->GetDimensions();
	const float CellSizeF = LD->GetFinestCellSize().ToFloat();
	if (Dims.X <= 0 || Dims.Y <= 0 || CellSizeF <= 0.0f)
	{
		return;
	}

	// Match the baked grid extent exactly so the background texture, fog overlay, and
	// blips all share one coordinate basis.
	WorldBoundsMin = FVector2D(OriginV.X, OriginV.Y);
	WorldBoundsMax = FVector2D(OriginV.X + Dims.X * CellSizeF, OriginV.Y + Dims.Y * CellSizeF);
	GroundZ = OriginV.Z;
	bHasBounds = true;
}

void USeinMinimapViewModel::ResolveBackground()
{
	if (UWorld* W = WorldPtr.Get())
	{
		BackgroundTexture = USeinUIBPFL::SeinGetMinimapTextureForLevel(W);
	}
}

void USeinMinimapViewModel::RebuildBlips()
{
	Blips.Reset();

	USeinWorldSubsystem* Sub = WorldSubsystem.Get();
	UWorld* W = WorldPtr.Get();
	if (!Sub || !W || !bHasBounds)
	{
		return;
	}

	ASeinPlayerController* PC = Cast<ASeinPlayerController>(W->GetFirstPlayerController());
	const FSeinPlayerID LocalId = PC ? PC->SeinPlayerID : FSeinPlayerID();

	// Build the selected-entity set once for O(1) per-blip lookup.
	TSet<FSeinEntityHandle> SelectedSet;
	if (PC)
	{
		for (ASeinActor* A : PC->GetValidSelectedActors())
		{
			if (A)
			{
				SelectedSet.Add(A->GetEntityHandle());
			}
		}
	}

	// Fog is optional — when there's no active fog, nothing is culled.
	USeinFogOfWar* Fog = USeinFogOfWarSubsystem::GetFogOfWarForWorld(W);
	const bool bFogActive = Fog && Fog->HasRuntimeData();

	Sub->GetEntityPool().ForEachEntity([&](FSeinEntityHandle Handle, const FSeinEntity& Entity)
	{
		// NOTE: deliberately NOT gating on Entity.IsSelectable() — the sim's FLAG_SELECTABLE
		// isn't maintained by the spawn path (in-game selection uses actor traces, not this
		// flag), so it reads false for every unit. Draw every live entity; if non-unit
		// entities (e.g. squad-container actors) show up as stray blips, exclude them by
		// component later.

		const ESeinRelation Relation = USeinUIBPFL::SeinGetEntityRelation(W, Handle, LocalId);

		// Hide only confirmed ENEMIES the local player can't currently see. Own + allied
		// (friendly) and neutral units always show on the minimap — so unowned sandbox
		// units and neutral structures aren't silently culled.
		if (bFogActive && Relation == ESeinRelation::Enemy)
		{
			if (!Fog->IsEntityVisibleToObserver(LocalId, *Sub, Handle))
			{
				return;
			}
		}

		const FVector WorldPos = Entity.Transform.GetLocation().ToVector();

		FSeinMinimapBlip Blip;
		Blip.Entity = Handle;
		Blip.NormalizedPos = USeinUIBPFL::SeinWorldToMinimap(WorldPos, WorldBoundsMin, WorldBoundsMax);
		Blip.Relation = Relation;
		Blip.SizeClass = ESeinMinimapBlipSize::Medium; // type-based sizing is a future polish pass
		Blip.bSelected = SelectedSet.Contains(Handle);
		Blips.Add(Blip);
	});
}

void USeinMinimapViewModel::UpdateFogTexture()
{
	UWorld* W = WorldPtr.Get();
	if (!W || !bHasBounds)
	{
		FogTexture = nullptr;
		return;
	}

	USeinFogOfWar* Fog = USeinFogOfWarSubsystem::GetFogOfWarForWorld(W);
	if (!Fog || !Fog->HasRuntimeData())
	{
		// No active fog → no overlay (the whole map reads as visible).
		FogTexture = nullptr;
		return;
	}

	ASeinPlayerController* PC = Cast<ASeinPlayerController>(W->GetFirstPlayerController());
	const FSeinPlayerID Observer = PC ? PC->SeinPlayerID : FSeinPlayerID();

	const int32 Res = FMath::Clamp(FogTextureResolution, 16, 512);
	if (!FogTexture || FogTexture->GetSizeX() != Res || FogTexture->GetSizeY() != Res)
	{
		FogTexture = UTexture2D::CreateTransient(Res, Res, PF_B8G8R8A8);
		if (!FogTexture)
		{
			return;
		}
		FogTexture->SRGB = true;
		FogTexture->Filter = TF_Bilinear;
		FogTexture->AddressX = TA_Clamp;
		FogTexture->AddressY = TA_Clamp;
		FogTexture->UpdateResource();
	}

	// Sample fog state at each texel's world position (same bounds as blips → aligned).
	const FVector2D Range = WorldBoundsMax - WorldBoundsMin;
	TArray<FColor> Px;
	Px.SetNumUninitialized(Res * Res);
	for (int32 Y = 0; Y < Res; ++Y)
	{
		const float V = (Y + 0.5f) / static_cast<float>(Res);
		for (int32 X = 0; X < Res; ++X)
		{
			const float U = (X + 0.5f) / static_cast<float>(Res);
			const FVector WorldPos(WorldBoundsMin.X + U * Range.X, WorldBoundsMin.Y + V * Range.Y, GroundZ);
			const uint8 Bits = Fog->GetCellBitfield(Observer, FFixedVector::FromVector(WorldPos));

			FColor& Out = Px[Y * Res + X];
			if (Bits & SEIN_FOW_BIT_NORMAL)        { Out = FColor(0, 0, 0, 0); }     // visible: show terrain
			else if (Bits & SEIN_FOW_BIT_EXPLORED) { Out = FColor(0, 0, 0, 120); }   // explored: dim
			else                                   { Out = FColor(2, 2, 4, 255); }   // unexplored: hide
		}
	}

	if (FTexturePlatformData* PD = FogTexture->GetPlatformData())
	{
		void* Data = PD->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Data, Px.GetData(), Res * Res * sizeof(FColor));
		PD->Mips[0].BulkData.Unlock();
		FogTexture->UpdateResource();
	}
}
