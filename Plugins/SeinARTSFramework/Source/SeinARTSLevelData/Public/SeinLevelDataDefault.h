/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLevelDataDefault.h
 * @brief   Default (unified single-trace grid) implementation of USeinLevelData (CP1.1).
 *
 *          Bakes ONE downward line trace per cell over the union of ASeinLevelVolume
 *          bounds → a shared height field + surface-normal·Up + in-bounds/surface flags
 *          (D13), then runs every registered layer provider to compute its channel block.
 *          The nav layer reproduces its exact bake from this shared data (slope gate from
 *          the normal·Up; it re-traces midpoints itself for the connectivity bitmask).
 *          Stores into USeinLevelDataDefaultAsset.
 *
 *          THREADING (planning/Roadmap_Multithreading.md): read queries are reentrant;
 *          BeginBake / LoadFromAsset are single-threaded. The per-cell bake loop writes
 *          results by index into pre-sized arrays (step 1) so it is trivially MT'able later.
 */

#pragma once

#include "CoreMinimal.h"
#include "SeinLevelData.h"
#include "SeinLevelDataDefaultAsset.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "SeinLevelDataDefault.generated.h"

/**
 * Bakes the level into the shared grid every other system reads from: ground height, surface
 * slope, and which cells are inside the play area. It is the level-data substrate selected out
 * of the box, and its bake is what the one "Bake Level Data" button on Sein Level Volume runs.
 *
 * Casts ONE downward line trace per grid cell over the union of all Sein Level Volume bounds,
 * producing a shared height field, a surface-normal-dot-Up value per cell (1.0 = flat ground,
 * falling toward 0 as the slope steepens), and in-bounds / valid-surface flags. It then runs
 * every registered layer provider so each can compute its own channel block from that same
 * shared trace: the Navigation and Fog of War layers both build on it, and the nav layer
 * reproduces its walkability bake from this data (its slope gate reads the stored normal-dot-Up;
 * it re-traces cell midpoints itself only for the connectivity bitmask). The grid runs at the
 * finest resolution in play (nav's cell size); results are saved into the baked Sein Level Data
 * (Default) asset and a top-down minimap background texture is synthesized from the surface
 * arrays (height shading plus slope relief plus an out-of-bounds border). Read queries are
 * reentrant; baking and asset loading are single-threaded, and the per-cell bake writes results
 * by index into pre-sized arrays so the loop can be parallelized later.
 */
UCLASS(meta = (DisplayName = "Sein Level Data (Default Grid)"))
class SEINARTSLEVELDATA_API USeinLevelDataDefault : public USeinLevelData
{
	GENERATED_BODY()

public:

	// --- USeinLevelData surface ---
	virtual bool ComputeStaticEnvironmentDigest(
		FGuid& OutDigest,
		FString& OutError) const override;
	virtual bool ComputeStateCoverageClaim(
		FSeinLevelDataStateCoverageClaim& OutClaim,
		FString& OutError) const override;
	virtual uint64 GetStaticEnvironmentGeneration() const override
	{
		return StaticEnvironmentGeneration;
	}
	virtual FFixedPoint GetFinestCellSize() const override { return CellSizeFP; }
	virtual FFixedVector GetOrigin() const override { return OriginFP; }
	virtual FIntPoint GetDimensions() const override { return FIntPoint(Width, Height); }
	virtual bool IsInBounds(const FFixedVector& WorldPos) const override;
	virtual bool GetSharedHeightAt(const FFixedVector& WorldPos, FFixedPoint& OutZ) const override;
	virtual bool GetCellSurface(int32 CellIndex, FSeinLevelCellSurface& OutSurface) const override;
	virtual bool GetLayerChannel(FName LayerId, TArray<uint8>& OutData) const override;
	virtual UTexture2D* GetMinimapTexture() const override { return MinimapTextureRuntime; }
	virtual void RegisterLayerProvider(ISeinLevelLayerProvider* Provider) override;
	virtual void UnregisterLayerProvider(ISeinLevelLayerProvider* Provider) override;
	virtual bool IsBaking() const override { return bBaking; }
	virtual void RequestCancelBake() override { bCancelRequested = true; }
	virtual bool HasRuntimeData() const override { return Width > 0 && Height > 0; }

	/** Surface normal · Up at a world XY (the nav layer's slope gate reads this).
	 *  Reentrant. Returns false off-grid / no bake. */
	bool GetSharedNormalZAt(const FFixedVector& WorldPos, FFixedPoint& OutNormalZ) const;

	/** Row-major finest-cell index for a world XY, or INDEX_NONE off-grid. Mirrors
	 *  nav's WorldToGrid: `(Local / CellSize).ToInt()` (deterministic floor). Reentrant. */
	int32 WorldToCellIndex(const FFixedVector& WorldPos) const;

protected:
	/** Reusable only by a native subclass that explicitly re-asserts this
	 * implementation's exact stateless contract from its own override. */
	bool ComputeDefaultStateCoverageClaim(
		FSeinLevelDataStateCoverageClaim& OutClaim,
		FString& OutError) const;
	bool ComputeDefaultStaticEnvironmentDigest(
		FGuid& OutDigest,
		FString& OutError) const;
	void MarkStaticEnvironmentMutated();

	virtual void OnDeinitialized() override;
	virtual bool BeginBakeImpl(UWorld* World) override;
	virtual bool LoadFromAssetImpl(USeinLevelDataAsset* Asset) override;

	bool DoSyncBake(UWorld* World, USeinLevelDataDefaultAsset*& OutAsset);
	bool ApplyAssetData(const USeinLevelDataDefaultAsset* Asset);

	/** Synthesize (or refresh) the asset's top-down minimap background texture from the
	 *  current runtime surface arrays (Width/Height/SharedHeight/SharedNormalZ/CellFlags).
	 *  Height shading + slope relief + an out-of-bounds border. Editor bakes a persistent
	 *  Source texture (subobject of the asset); runtime bakes a transient texture. */
	void BuildOrUpdateMinimapTexture(USeinLevelDataDefaultAsset* Asset) const;

#if WITH_EDITOR
	USeinLevelDataDefaultAsset* CreateOrLoadAsset(UWorld* World, const FString& AssetName) const;
	bool SaveAssetToDisk(USeinLevelDataDefaultAsset* Asset) const;
#endif

	// Runtime grid state (from the baked asset).
	int32 Width = 0;
	int32 Height = 0;
	FFixedPoint CellSizeFP = FFixedPoint::FromInt(100);
	FFixedVector OriginFP = FFixedVector::ZeroVector;
	TArray<FFixedPoint> SharedHeight;
	TArray<FFixedPoint> SharedNormalZ;
	TArray<uint8> CellFlags;

	/** Per-cell terrain-type index (0 = Default), loaded from the baked asset. Surfaced
	 *  through GetCellSurface so layer providers (nav cost) and the Cover extension read
	 *  the shared classification. Empty/all-zero when the asset predates terrain types. */
	TArray<uint8> CellTerrainType;

	/** Baked minimap background texture, cached from the loaded asset (GC-rooted via
	 *  this UPROPERTY). Returned by GetMinimapTexture. */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> MinimapTextureRuntime;

	/** Per-layer baked channel blocks (nav, FoW, …), copied from the asset at load
	 *  so consumers can read their channel via GetLayerChannel. */
	TArray<FSeinLevelChannelBlock> RuntimeChannels;

	// Registered layer providers (non-owning; registered at module startup).
	TArray<ISeinLevelLayerProvider*> Providers;

	bool bBaking = false;
	bool bCancelRequested = false;
	FGuid StaticEnvironmentDigest;
	uint64 StaticEnvironmentGeneration = 0;
};
