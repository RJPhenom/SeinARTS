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
};
