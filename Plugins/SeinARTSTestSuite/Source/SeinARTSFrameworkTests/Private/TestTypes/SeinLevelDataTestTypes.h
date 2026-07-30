#pragma once

#include "SeinLevelData.h"
#include "SeinLevelDataTestTypes.generated.h"

/** Minimal runtime substrate shared by focused layer-adoption tests. */
UCLASS()
class USeinLevelDataTestDouble : public USeinLevelData
{
	GENERATED_BODY()

public:
	FIntPoint TestDimensions = FIntPoint::ZeroValue;
	FFixedPoint TestCellSize = FFixedPoint::FromInt(100);
	FFixedVector TestOrigin = FFixedVector::ZeroVector;
	TMap<FName, TArray<uint8>> LayerChannels;
	TArray<FSeinLevelCellSurface> TestSurfaces;
	int32 LoadCallCount = 0;
	int32 BakeCallCount = 0;

	virtual bool HasRuntimeData() const override { return true; }
	virtual FIntPoint GetDimensions() const override { return TestDimensions; }
	virtual FFixedPoint GetFinestCellSize() const override { return TestCellSize; }
	virtual FFixedVector GetOrigin() const override { return TestOrigin; }
	virtual bool GetLayerChannel(FName LayerId, TArray<uint8>& OutData) const override
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
		if (!TestSurfaces.IsValidIndex(CellIndex))
		{
			return false;
		}
		OutSurface = TestSurfaces[CellIndex];
		return true;
	}

protected:
	virtual bool LoadFromAssetImpl(
		USeinLevelDataAsset* /*Asset*/) override
	{
		++LoadCallCount;
		OnLevelDataMutated.Broadcast();
		return true;
	}

	virtual bool BeginBakeImpl(UWorld* /*World*/) override
	{
		++BakeCallCount;
		return true;
	}
};
