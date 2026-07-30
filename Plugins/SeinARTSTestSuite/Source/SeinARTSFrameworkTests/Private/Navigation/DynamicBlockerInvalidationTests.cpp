#include "CQTest.h"
#include "SeinNavigationAStar.h"
#include "TestTypes/SeinLevelDataTestTypes.h"

namespace UE::SeinARTSTests
{
	struct FNavigationAStarTestAccess
	{
		struct FStaticGridSnapshot
		{
			int32 Width = 0;
			int32 Height = 0;
			FFixedPoint CellSize = FFixedPoint::Zero;
			FFixedVector Origin = FFixedVector::ZeroVector;
			FGuid StaticGridDigest;
			TArray<uint8> CellCost;
			TArray<FFixedPoint> CellHeight;
			TArray<uint8> CellTerrainType;
			TArray<uint8> CellConnections;
			TArray<uint8> WallDistance;
			TArray<int32> CellComponent;
		};

		static void InstallDynamicBlockers(
			USeinNavigationAStar& Nav,
			const TArray<FSeinDynamicBlocker>& Blockers)
		{
			Nav.SetDynamicBlockers(Blockers);
		}

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

		static FStaticGridSnapshot CaptureStaticGrid(
			const USeinNavigationAStar& Nav)
		{
			FStaticGridSnapshot Snapshot;
			Snapshot.Width = Nav.Width;
			Snapshot.Height = Nav.Height;
			Snapshot.CellSize = Nav.CellSize;
			Snapshot.Origin = Nav.Origin;
			Snapshot.StaticGridDigest = Nav.StaticGridDigest;
			Snapshot.CellCost = Nav.CellCost;
			Snapshot.CellHeight = Nav.CellHeight;
			Snapshot.CellTerrainType = Nav.CellTerrainType;
			Snapshot.CellConnections = Nav.CellConnections;
			Snapshot.WallDistance = Nav.WallDistance;
			Snapshot.CellComponent = Nav.CellComponent;
			return Snapshot;
		}

		static bool MatchesStaticGrid(
			const USeinNavigationAStar& Nav,
			const FStaticGridSnapshot& Expected)
		{
			return Nav.Width == Expected.Width
				&& Nav.Height == Expected.Height
				&& Nav.CellSize == Expected.CellSize
				&& Nav.Origin == Expected.Origin
				&& Nav.StaticGridDigest
					== Expected.StaticGridDigest
				&& Nav.CellCost == Expected.CellCost
				&& Nav.CellHeight == Expected.CellHeight
				&& Nav.CellTerrainType
					== Expected.CellTerrainType
				&& Nav.CellConnections
					== Expected.CellConnections
				&& Nav.WallDistance == Expected.WallDistance
				&& Nav.CellComponent == Expected.CellComponent;
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
			LevelData.TestSurfaces.SetNumZeroed(NumCells);
			TArray<uint8>& Channel = LevelData.LayerChannels.FindOrAdd(TEXT("Nav"));
			Channel.SetNumZeroed(2 * NumCells);
			for (int32 Index = 0; Index < NumCells; ++Index)
			{
				Channel[Index] = 1; // passable cost; connections are irrelevant here
			}
		}
	}

	TEST(RejectedGridAdoptionPreservesPriorTopology,
		"SeinARTS.Unit.Navigation")
	{
		USeinNavigationAStar* Nav =
			NewObject<USeinNavigationAStar>();
		USeinLevelDataTestDouble* Valid =
			NewObject<USeinLevelDataTestDouble>();
		ASSERT_THAT(IsNotNull(Nav));
		ASSERT_THAT(IsNotNull(Valid));

		ConfigureNavGrid(*Valid, FIntPoint(2, 3));
		ASSERT_THAT(IsTrue(
			Nav->LoadFromSubstrate(*Valid).IsAdopted()));
		const FNavigationAStarTestAccess::FStaticGridSnapshot
			Baseline =
				FNavigationAStarTestAccess::CaptureStaticGrid(
					*Nav);

		USeinLevelDataTestDouble* Missing =
			NewObject<USeinLevelDataTestDouble>();
		ASSERT_THAT(IsNotNull(Missing));
		Missing->TestDimensions = FIntPoint(2, 3);
		Missing->TestSurfaces.SetNumZeroed(6);
		const FSeinStaticEnvironmentAdoptionResult MissingResult =
			Nav->LoadFromSubstrate(*Missing);
		ASSERT_THAT(IsTrue(MissingResult.IsRejected()));
		ASSERT_THAT(IsTrue(
			MissingResult.Detail.Contains(
				TEXT("missing the required Nav channel"))));
		ASSERT_THAT(IsTrue(
			FNavigationAStarTestAccess::MatchesStaticGrid(
				*Nav, Baseline)));

		USeinLevelDataTestDouble* Malformed =
			NewObject<USeinLevelDataTestDouble>();
		ASSERT_THAT(IsNotNull(Malformed));
		Malformed->TestDimensions = FIntPoint(2, 3);
		Malformed->TestSurfaces.SetNumZeroed(6);
		Malformed->LayerChannels.Add(
			TEXT("Nav"), { 0x01 });
		const FSeinStaticEnvironmentAdoptionResult MalformedResult =
			Nav->LoadFromSubstrate(*Malformed);
		ASSERT_THAT(IsTrue(MalformedResult.IsRejected()));
		ASSERT_THAT(IsTrue(
			MalformedResult.Detail.Contains(
				TEXT("Nav channel is malformed"))));
		ASSERT_THAT(IsTrue(
			FNavigationAStarTestAccess::MatchesStaticGrid(
				*Nav, Baseline)));
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
		FNavigationAStarTestAccess::InstallDynamicBlockers(*Nav, Blockers);
		ASSERT_THAT(AreEqual(1, MutationCount));

		FNavigationAStarTestAccess::InstallDynamicBlockers(*Nav, Blockers);
		ASSERT_THAT(AreEqual(1, MutationCount));

		Blockers[0].EntityCenter.X += FFixedPoint::SmallNumber;
		FNavigationAStarTestAccess::InstallDynamicBlockers(*Nav, Blockers);
		ASSERT_THAT(AreEqual(2, MutationCount));

		Blockers[0].EntityRotation.Z = FFixedPoint::SmallNumber;
		FNavigationAStarTestAccess::InstallDynamicBlockers(*Nav, Blockers);
		ASSERT_THAT(AreEqual(3, MutationCount));

		Blockers[0].Shape.HalfExtentX += FFixedPoint::SmallNumber;
		FNavigationAStarTestAccess::InstallDynamicBlockers(*Nav, Blockers);
		ASSERT_THAT(AreEqual(4, MutationCount));

		Blockers[0].Owner.Generation += 1;
		FNavigationAStarTestAccess::InstallDynamicBlockers(*Nav, Blockers);
		ASSERT_THAT(AreEqual(5, MutationCount));

		TArray<FSeinDynamicBlocker> XY;
		XY.Add(MakeBlocker());
		TArray<FSeinDynamicBlocker> YX = XY;
		Swap(YX[0].EntityCenter.X, YX[0].EntityCenter.Y);
		ASSERT_THAT(AreEqual(LegacyBlockerHash(XY), LegacyBlockerHash(YX)));

		FNavigationAStarTestAccess::InstallDynamicBlockers(*Nav, XY);
		FNavigationAStarTestAccess::InstallDynamicBlockers(*Nav, YX);
		ASSERT_THAT(AreEqual(7, MutationCount));

		Blockers.Reset();
		FNavigationAStarTestAccess::InstallDynamicBlockers(*Nav, Blockers);
		ASSERT_THAT(AreEqual(8, MutationCount));
		FNavigationAStarTestAccess::InstallDynamicBlockers(*Nav, Blockers);
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
		ASSERT_THAT(IsTrue(
			Nav->LoadFromSubstrate(*LevelData).IsAdopted()));
		FNavigationAStarTestAccess::SeedSerialOverlay(*Nav, 6);

		// The linear count remains six, but the row stride and dirty-rect
		// coordinate space change. Retained bytes must not enter the new grid.
		ConfigureNavGrid(*LevelData, FIntPoint(3, 2));
		ASSERT_THAT(IsTrue(
			Nav->LoadFromSubstrate(*LevelData).IsAdopted()));
		ASSERT_THAT(IsTrue(FNavigationAStarTestAccess::IsSerialOverlayReset(*Nav)));
	}
}
