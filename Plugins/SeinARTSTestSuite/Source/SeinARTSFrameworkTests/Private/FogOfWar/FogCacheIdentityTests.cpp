#include "CQTest.h"
#include "Components/SeinVisionComponent.h"
#include "Default/SeinFogOfWarDefault.h"
#include "TestTypes/SeinLevelDataTestTypes.h"

namespace UE::SeinARTSTests
{
	struct FFogOfWarDefaultTestAccess
	{
		static void SeedDynamicOverlay(USeinFogOfWarDefault& Fog)
		{
			for (FFixedPoint& Height : Fog.DynamicBlockerHeight)
			{
				Height = FFixedPoint::FromInt(300);
			}
			for (uint8& Mask : Fog.DynamicBlockerLayerMask)
			{
				Mask = SEIN_FOW_BIT_NORMAL;
			}
			Fog.DynamicBlockerSnapshots.AddDefaulted();
			for (int32 Index = 0; Index < Fog.DynamicBlockerHeight.Num(); ++Index)
			{
				Fog.LastDynamicBlockerCells.Add(Index);
			}
		}

		static bool IsDynamicOverlayClear(const USeinFogOfWarDefault& Fog, int32 ExpectedCells)
		{
			if (Fog.DynamicBlockerHeight.Num() != ExpectedCells
				|| Fog.DynamicBlockerLayerMask.Num() != ExpectedCells
				|| !Fog.DynamicBlockerSnapshots.IsEmpty()
				|| !Fog.LastDynamicBlockerCells.IsEmpty())
			{
				return false;
			}
			for (int32 Index = 0; Index < ExpectedCells; ++Index)
			{
				if (Fog.DynamicBlockerHeight[Index] != FFixedPoint::Zero
					|| Fog.DynamicBlockerLayerMask[Index] != 0)
				{
					return false;
				}
			}
			return true;
		}
	};

	namespace
	{
		uint32 LegacyVisionHash(const TArray<FSeinVisionStamp>& Stamps)
		{
			uint32 Hash = 0;
			for (const FSeinVisionStamp& Stamp : Stamps)
			{
				Hash ^= GetTypeHash(Stamp.Shape);
				Hash ^= static_cast<uint32>(Stamp.LayerMask);
			}
			return Hash;
		}

		void BuildEmptyFogChannel(int32 Width, int32 Height, TArray<uint8>& OutChannel)
		{
			const int32 NumCells = Width * Height;
			const int32 HeaderBytes = 2 * sizeof(int32) + 3 * sizeof(int64);
			OutChannel.SetNumZeroed(HeaderBytes + 3 * NumCells);
			uint8* Out = OutChannel.GetData();
			auto Write = [&Out](const auto& Value)
			{
				FMemory::Memcpy(Out, &Value, sizeof(Value));
				Out += sizeof(Value);
			};
			Write(Width);
			Write(Height);
			const int64 CellSize = FFixedPoint::FromInt(400).Value;
			const int64 MinHeight = 0;
			const int64 Quantum = FFixedPoint::One.Value;
			Write(CellSize);
			Write(MinHeight);
			Write(Quantum);
		}
	}

	TEST(VisionStampIdentityIsExact, "SeinARTS.Unit.FogOfWar")
	{
		FSeinVisionStamp Left;
		Left.Shape.LocalOffset.X = FFixedPoint::FromInt(-100);
		Left.LayerMask = SEIN_FOW_BIT_NORMAL;

		FSeinVisionStamp Right;
		Right.Shape.LocalOffset.X = FFixedPoint::FromInt(100);
		Right.LayerMask = static_cast<uint8>(ESeinFogOfWarLayerBit::N0);

		TArray<FSeinVisionStamp> Before;
		Before.Add(Left);
		Before.Add(Right);

		TArray<FSeinVisionStamp> After = Before;
		Swap(After[0].LayerMask, After[1].LayerMask);

		// The former XOR key cannot see which geometry owns each layer.
		ASSERT_THAT(AreEqual(LegacyVisionHash(Before), LegacyVisionHash(After)));
		ASSERT_THAT(IsFalse(Before == After));
	}

	TEST(DynamicFogBlockerIdentityIncludesPose, "SeinARTS.Unit.FogOfWar")
	{
		FSeinFogDynamicBlockerSnapshot Base;
		Base.WorldPos = FFixedVector(
			FFixedPoint::FromInt(10), FFixedPoint::FromInt(20), FFixedPoint::Zero);
		Base.Rotation = FFixedQuaternion::Identity;
		Base.Shape.Shape = ESeinStampShape::Rect;
		Base.Height = FFixedPoint::FromInt(300);
		Base.LayerMask = SEIN_FOW_BIT_NORMAL;

		FSeinFogDynamicBlockerSnapshot Rotated = Base;
		Rotated.Rotation.Z = FFixedPoint::SmallNumber;
		ASSERT_THAT(IsFalse(Base == Rotated));

		FSeinFogDynamicBlockerSnapshot SwappedPosition = Base;
		Swap(SwappedPosition.WorldPos.X, SwappedPosition.WorldPos.Y);
		ASSERT_THAT(IsFalse(Base == SwappedPosition));

		TArray<FSeinFogDynamicBlockerSnapshot> Stable;
		Stable.Add(Base);
		ASSERT_THAT(IsTrue(Stable == Stable));
	}

	TEST(SameDimensionGridReloadClearsDynamicOverlay, "SeinARTS.Unit.FogOfWar")
	{
		constexpr int32 Width = 2;
		constexpr int32 Height = 2;
		constexpr int32 NumCells = Width * Height;

		USeinFogOfWarDefault* Fog = NewObject<USeinFogOfWarDefault>();
		USeinLevelDataTestDouble* LevelData = NewObject<USeinLevelDataTestDouble>();
		ASSERT_THAT(IsNotNull(Fog));
		ASSERT_THAT(IsNotNull(LevelData));
		BuildEmptyFogChannel(Width, Height,
			LevelData->LayerChannels.FindOrAdd(TEXT("FogOfWar")));

		ASSERT_THAT(IsTrue(Fog->LoadFromSubstrate(*LevelData)));
		FFogOfWarDefaultTestAccess::SeedDynamicOverlay(*Fog);

		// Reloading the same dimensions must clear retained TArray storage before
		// the exact empty-snapshot fast path can observe it on the next tick.
		ASSERT_THAT(IsTrue(Fog->LoadFromSubstrate(*LevelData)));
		ASSERT_THAT(IsTrue(
			FFogOfWarDefaultTestAccess::IsDynamicOverlayClear(*Fog, NumCells)));
	}
}
