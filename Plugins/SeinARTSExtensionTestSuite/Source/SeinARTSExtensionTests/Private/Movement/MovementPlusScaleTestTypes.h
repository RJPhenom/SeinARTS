#pragma once

#include "SeinLevelData.h"
#include "MovementPlusScaleTestTypes.generated.h"

/** Minimal open-field substrate local to this module (the base suite's
 *  level-data double is module-private by design). */
UCLASS()
class USeinMovementPlusScaleLevelData : public USeinLevelData
{
	GENERATED_BODY()

public:
	FIntPoint TestDimensions = FIntPoint::ZeroValue;
	FFixedPoint TestCellSize = FFixedPoint::FromInt(100);
	TMap<FName, TArray<uint8>> LayerChannels;
	TArray<FSeinLevelCellSurface> TestSurfaces;

	virtual bool HasRuntimeData() const override { return true; }
	virtual FIntPoint GetDimensions() const override { return TestDimensions; }
	virtual FFixedPoint GetFinestCellSize() const override { return TestCellSize; }
	virtual FFixedVector GetOrigin() const override { return FFixedVector::ZeroVector; }
	virtual bool GetLayerChannel(
		FName LayerId, TArray<uint8>& OutData) const override
	{
		const TArray<uint8>* Channel = LayerChannels.Find(LayerId);
		if (!Channel) return false;
		OutData = *Channel;
		return true;
	}
	virtual bool GetCellSurface(
		int32 CellIndex,
		FSeinLevelCellSurface& OutSurface) const override
	{
		if (!TestSurfaces.IsValidIndex(CellIndex)) return false;
		OutSurface = TestSurfaces[CellIndex];
		return true;
	}
};
