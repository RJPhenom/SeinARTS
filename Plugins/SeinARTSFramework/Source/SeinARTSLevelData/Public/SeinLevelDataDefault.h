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

UCLASS(meta = (DisplayName = "Sein Level Data (Default Grid)"))
class SEINARTSLEVELDATA_API USeinLevelDataDefault : public USeinLevelData
{
	GENERATED_BODY()

public:

	// --- USeinLevelData surface ---
	virtual FFixedPoint GetFinestCellSize() const override { return CellSizeFP; }
	virtual FFixedVector GetOrigin() const override { return OriginFP; }
	virtual FIntPoint GetDimensions() const override { return FIntPoint(Width, Height); }
	virtual bool IsInBounds(const FFixedVector& WorldPos) const override;
	virtual bool GetSharedHeightAt(const FFixedVector& WorldPos, FFixedPoint& OutZ) const override;
	virtual bool GetCellSurface(int32 CellIndex, FSeinLevelCellSurface& OutSurface) const override;
	virtual bool GetLayerChannel(FName LayerId, TArray<uint8>& OutData) const override;
	virtual void RegisterLayerProvider(ISeinLevelLayerProvider* Provider) override;
	virtual void UnregisterLayerProvider(ISeinLevelLayerProvider* Provider) override;
	virtual bool BeginBake(UWorld* World) override;
	virtual bool IsBaking() const override { return bBaking; }
	virtual void RequestCancelBake() override { bCancelRequested = true; }
	virtual void LoadFromAsset(USeinLevelDataAsset* Asset) override;
	virtual bool HasRuntimeData() const override { return Width > 0 && Height > 0; }

	/** Surface normal · Up at a world XY (the nav layer's slope gate reads this).
	 *  Reentrant. Returns false off-grid / no bake. */
	bool GetSharedNormalZAt(const FFixedVector& WorldPos, FFixedPoint& OutNormalZ) const;

	/** Row-major finest-cell index for a world XY, or INDEX_NONE off-grid. Mirrors
	 *  nav's WorldToGrid: `(Local / CellSize).ToInt()` (deterministic floor). Reentrant. */
	int32 WorldToCellIndex(const FFixedVector& WorldPos) const;

protected:

	bool DoSyncBake(UWorld* World, USeinLevelDataDefaultAsset*& OutAsset);
	void ApplyAssetData(const USeinLevelDataDefaultAsset* Asset);

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

	/** Per-layer baked channel blocks (nav, FoW, …), copied from the asset at load
	 *  so consumers can read their channel via GetLayerChannel. */
	TArray<FSeinLevelChannelBlock> RuntimeChannels;

	// Registered layer providers (non-owning; registered at module startup).
	TArray<ISeinLevelLayerProvider*> Providers;

	bool bBaking = false;
	bool bCancelRequested = false;
};
