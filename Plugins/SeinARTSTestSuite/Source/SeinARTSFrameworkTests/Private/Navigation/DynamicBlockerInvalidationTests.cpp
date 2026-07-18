#include "CQTest.h"
#include "SeinNavigationAStar.h"
#include "TestTypes/SeinLevelDataTestTypes.h"

namespace UE::SeinARTSTests
{
	struct FNavigationAStarTestAccess
	{
		static void SeedSerialOverlay(USeinNavigationAStar& Nav, int32 NumCells)
		{
			Nav.MainScratch.DynamicBlocked.SetNumZeroed(NumCells);
			Nav.MainScratch.DynamicBlocked.Last() = 1;
			Nav.MainScratch.LastOverlayDirtyRect = FIntRect(1, 1, 2, 2);
			Nav.MainScratch.bOverlayReuseValid = true;
		}

		static bool IsSerialOverlayReset(const USeinNavigationAStar& Nav)
		{
			const FIntRect& Dirty = Nav.MainScratch.LastOverlayDirtyRect;
			return Nav.MainScratch.DynamicBlocked.IsEmpty()
				&& Dirty.Min.X > Dirty.Max.X
				&& !Nav.MainScratch.bOverlayReuseValid;
		}
	};

	namespace
	{
		uint32 LegacyBlockerHash(const TArray<FSeinDynamicBlocker>& Blockers)
		{
			uint32 Hash = 0;
			for (const FSeinDynamicBlocker& Blocker : Blockers)
			{
				Hash ^= GetTypeHash(Blocker.Owner.Index);
				Hash ^= GetTypeHash(Blocker.EntityCenter.X.Value);
				Hash ^= GetTypeHash(Blocker.EntityCenter.Y.Value);
				Hash ^= GetTypeHash(Blocker.EntityCenter.Z.Value);
				Hash ^= GetTypeHash(Blocker.EntityRotation.X.Value);
				Hash ^= GetTypeHash(Blocker.EntityRotation.Y.Value);
				Hash ^= GetTypeHash(Blocker.EntityRotation.Z.Value);
				Hash ^= GetTypeHash(Blocker.EntityRotation.W.Value);
				Hash ^= GetTypeHash(Blocker.Shape);
				Hash ^= static_cast<uint32>(Blocker.BlockedNavLayerMask);
			}
			return Hash;
		}

		FSeinDynamicBlocker MakeBlocker()
		{
			FSeinDynamicBlocker Blocker;
			Blocker.Owner = FSeinEntityHandle(7, 1);
			Blocker.EntityCenter = FFixedVector(
				FFixedPoint::FromInt(10), FFixedPoint::FromInt(20), FFixedPoint::Zero);
			Blocker.EntityRotation = FFixedQuaternion::Identity;
			Blocker.Shape.Shape = ESeinStampShape::Rect;
			Blocker.Shape.HalfExtentX = FFixedPoint::FromInt(150);
			Blocker.Shape.HalfExtentY = FFixedPoint::FromInt(50);
			Blocker.BlockedNavLayerMask = 0x03;
			return Blocker;
		}

		void ConfigureNavGrid(USeinLevelDataTestDouble& LevelData, FIntPoint Dimensions)
		{
			LevelData.TestDimensions = Dimensions;
			const int32 NumCells = Dimensions.X * Dimensions.Y;
			TArray<uint8>& Channel = LevelData.LayerChannels.FindOrAdd(TEXT("Nav"));
			Channel.SetNumZeroed(2 * NumCells);
			for (int32 Index = 0; Index < NumCells; ++Index)
			{
				Channel[Index] = 1; // passable cost; connections are irrelevant here
			}
		}
	}

	TEST(DynamicBlockersInvalidateExactly, "SeinARTS.Unit.Navigation")
	{
		USeinNavigationAStar* Nav = NewObject<USeinNavigationAStar>();
		ASSERT_THAT(IsTrue(Nav != nullptr));

		int32 MutationCount = 0;
		const FDelegateHandle MutationHandle = Nav->OnNavigationMutated.AddLambda(
			[&MutationCount]() { ++MutationCount; });

		TArray<FSeinDynamicBlocker> Blockers;
		Blockers.Add(MakeBlocker());
		Nav->SetDynamicBlockers(Blockers);
		ASSERT_THAT(AreEqual(1, MutationCount));

		Nav->SetDynamicBlockers(Blockers);
		ASSERT_THAT(AreEqual(1, MutationCount));

		Blockers[0].EntityCenter.X += FFixedPoint::SmallNumber;
		Nav->SetDynamicBlockers(Blockers);
		ASSERT_THAT(AreEqual(2, MutationCount));

		Blockers[0].EntityRotation.Z = FFixedPoint::SmallNumber;
		Nav->SetDynamicBlockers(Blockers);
		ASSERT_THAT(AreEqual(3, MutationCount));

		Blockers[0].Shape.HalfExtentX += FFixedPoint::SmallNumber;
		Nav->SetDynamicBlockers(Blockers);
		ASSERT_THAT(AreEqual(4, MutationCount));

		Blockers[0].Owner.Generation += 1;
		Nav->SetDynamicBlockers(Blockers);
		ASSERT_THAT(AreEqual(5, MutationCount));

		TArray<FSeinDynamicBlocker> XY;
		XY.Add(MakeBlocker());
		TArray<FSeinDynamicBlocker> YX = XY;
		Swap(YX[0].EntityCenter.X, YX[0].EntityCenter.Y);
		ASSERT_THAT(AreEqual(LegacyBlockerHash(XY), LegacyBlockerHash(YX)));

		Nav->SetDynamicBlockers(XY);
		Nav->SetDynamicBlockers(YX);
		ASSERT_THAT(AreEqual(7, MutationCount));

		Blockers.Reset();
		Nav->SetDynamicBlockers(Blockers);
		ASSERT_THAT(AreEqual(8, MutationCount));
		Nav->SetDynamicBlockers(Blockers);
		ASSERT_THAT(AreEqual(8, MutationCount));

		Nav->OnNavigationMutated.Remove(MutationHandle);
	}

	TEST(EqualCellCountGridReloadDropsOverlayCoordinates, "SeinARTS.Unit.Navigation")
	{
		USeinNavigationAStar* Nav = NewObject<USeinNavigationAStar>();
		USeinLevelDataTestDouble* LevelData = NewObject<USeinLevelDataTestDouble>();
		ASSERT_THAT(IsNotNull(Nav));
		ASSERT_THAT(IsNotNull(LevelData));

		ConfigureNavGrid(*LevelData, FIntPoint(2, 3));
		ASSERT_THAT(IsTrue(Nav->LoadFromSubstrate(*LevelData)));
		FNavigationAStarTestAccess::SeedSerialOverlay(*Nav, 6);

		// The linear count remains six, but the row stride and dirty-rect
		// coordinate space change. Retained bytes must not enter the new grid.
		ConfigureNavGrid(*LevelData, FIntPoint(3, 2));
		ASSERT_THAT(IsTrue(Nav->LoadFromSubstrate(*LevelData)));
		ASSERT_THAT(IsTrue(FNavigationAStarTestAccess::IsSerialOverlayReset(*Nav)));
	}
}
