#include "CQTest.h"
#include "Components/SeinMovementPayload.h"
#include "Components/SeinNavigationPayload.h"
#include "Movement/SeinBasicUnitMovement.h"
#include "Movement/SeinMovement.h"
#include "SeinNavigationAStar.h"
#include "Settings/PluginSettings.h"
#include "Stamping/SeinStampUtils.h"
#include "TestTypes/SeinLevelDataTestTypes.h"
#include "Types/Entity.h"

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

		static int32 ReachabilityProfileCacheSize(
			const USeinNavigationAStar& Nav)
		{
			return Nav.ReachabilityProfileCache.Num();
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

		void ConfigureOpenConnectedNavGrid(
			USeinLevelDataTestDouble& LevelData,
			FIntPoint Dimensions)
		{
			ConfigureNavGrid(LevelData, Dimensions);
			TArray<uint8>& Channel =
				LevelData.LayerChannels.FindChecked(TEXT("Nav"));
			const int32 NumCells = Dimensions.X * Dimensions.Y;
			static const int32 DX[8] =
				{ 1, -1, 0, 0, 1, 1, -1, -1 };
			static const int32 DY[8] =
				{ 0, 0, 1, -1, 1, -1, 1, -1 };
			for (int32 Y = 0; Y < Dimensions.Y; ++Y)
			{
				for (int32 X = 0; X < Dimensions.X; ++X)
				{
					uint8 Connections = 0;
					for (int32 Direction = 0;
						Direction < 8;
						++Direction)
					{
						const int32 NX = X + DX[Direction];
						const int32 NY = Y + DY[Direction];
						if (NX >= 0 && NX < Dimensions.X
							&& NY >= 0 && NY < Dimensions.Y)
						{
							Connections |= (1 << Direction);
						}
					}
					Channel[NumCells + Y * Dimensions.X + X]
						= Connections;
				}
			}
		}

		FFixedVector CellCenter(int32 X, int32 Y)
		{
			return FFixedVector(
				FFixedPoint::FromInt(X * 100 + 50),
				FFixedPoint::FromInt(Y * 100 + 50),
				FFixedPoint::Zero);
		}

		bool ReferenceDynamicCellBlocked(
			const TArray<FSeinDynamicBlocker>& Blockers,
			int32 QueryX,
			int32 QueryY,
			uint8 AgentMask,
			FSeinEntityHandle Exclude,
			const TSet<FSeinEntityHandle>* IgnoredOwners = nullptr)
		{
			for (const FSeinDynamicBlocker& Blocker : Blockers)
			{
				if (Blocker.Owner == Exclude
					|| (IgnoredOwners
						&& IgnoredOwners->Contains(Blocker.Owner))
					|| (Blocker.BlockedNavLayerMask & AgentMask) == 0)
				{
					continue;
				}
				bool bCovered = false;
				SeinStampUtils::ForEachCoveredCell(
					Blocker.Shape,
					Blocker.EntityCenter,
					Blocker.EntityRotation,
					FFixedPoint::FromInt(100),
					FFixedVector::ZeroVector,
					9,
					9,
					[&](int32 X, int32 Y)
					{
						bCovered = bCovered
							|| (X == QueryX && Y == QueryY);
					});
				if (bCovered) return true;
			}
			return false;
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

	TEST(DynamicBlockerCellIndexMatchesExactStampCoverage,
		"SeinARTS.Unit.Navigation")
	{
		USeinNavigationAStar* Nav =
			NewObject<USeinNavigationAStar>();
		USeinLevelDataTestDouble* LevelData =
			NewObject<USeinLevelDataTestDouble>();
		ASSERT_THAT(IsNotNull(Nav));
		ASSERT_THAT(IsNotNull(LevelData));
		ConfigureOpenConnectedNavGrid(*LevelData, FIntPoint(9, 9));
		ASSERT_THAT(IsTrue(
			Nav->LoadFromSubstrate(*LevelData).IsAdopted()));

		FSeinDynamicBlocker Radial;
		Radial.Owner = FSeinEntityHandle(10, 1);
		Radial.EntityCenter = CellCenter(2, 2);
		Radial.EntityRotation = FFixedQuaternion::Identity;
		Radial.Shape.Shape = ESeinStampShape::Radial;
		Radial.Shape.Radius = FFixedPoint::FromInt(125);
		Radial.Shape.LocalOffset = FFixedVector(
			FFixedPoint::FromInt(20), FFixedPoint::FromInt(-15),
			FFixedPoint::Zero);
		Radial.BlockedNavLayerMask = 0x01;

		FSeinDynamicBlocker Rect;
		Rect.Owner = FSeinEntityHandle(11, 2);
		Rect.EntityCenter = CellCenter(4, 4);
		Rect.EntityRotation = FFixedQuaternion::Identity;
		Rect.Shape.Shape = ESeinStampShape::Rect;
		Rect.Shape.HalfExtentX = FFixedPoint::FromInt(145);
		Rect.Shape.HalfExtentY = FFixedPoint::FromInt(65);
		Rect.Shape.YawOffsetDegrees = FFixedPoint::FromInt(35);
		Rect.BlockedNavLayerMask = 0x02;

		FSeinDynamicBlocker Cone;
		Cone.Owner = FSeinEntityHandle(12, 3);
		Cone.EntityCenter = CellCenter(6, 6);
		Cone.EntityRotation = FFixedQuaternion::Identity;
		Cone.Shape.Shape = ESeinStampShape::Conical;
		Cone.Shape.ConeAngleDegrees = FFixedPoint::FromInt(80);
		Cone.Shape.ConeLength = FFixedPoint::FromInt(225);
		Cone.Shape.YawOffsetDegrees = FFixedPoint::FromInt(-40);
		Cone.Shape.bConeRoundEdge = false;
		Cone.BlockedNavLayerMask = 0x03;

		const TArray<FSeinDynamicBlocker> Blockers = {
			Radial, Rect, Cone,
		};
		FNavigationAStarTestAccess::InstallDynamicBlockers(
			*Nav, Blockers);

		TSet<FSeinEntityHandle> GroupIgnored;
		GroupIgnored.Add(Rect.Owner);
		GroupIgnored.Add(FSeinEntityHandle(Cone.Owner.Index, 99));
		for (const uint8 Mask : { uint8(0x01), uint8(0x02), uint8(0x03) })
		{
			for (int32 Y = 0; Y < 9; ++Y)
			{
				for (int32 X = 0; X < 9; ++X)
				{
					const FFixedVector Position = CellCenter(X, Y);
					const bool bOrdinaryExpected =
						!ReferenceDynamicCellBlocked(
							Blockers, X, Y, Mask, FSeinEntityHandle());
					ASSERT_THAT(AreEqual(
						bOrdinaryExpected,
						Nav->IsWorldPositionClear(Position, Mask)));

					FSeinNavAgentProfile Agent;
					Agent.Requester = Radial.Owner;
					Agent.AgentNavLayerMask = Mask;
					const bool bGroupExpected =
						!ReferenceDynamicCellBlocked(
							Blockers, X, Y, Mask, Agent.Requester,
							&GroupIgnored);
					ASSERT_THAT(AreEqual(
						bGroupExpected,
						Nav->IsFootprintClearForAgentIgnoringDynamicBlockers(
							Position, Agent, GroupIgnored)));
				}
			}
		}

		ASSERT_THAT(IsTrue(
			Nav->LoadFromSubstrate(*LevelData).IsAdopted()));
		ASSERT_THAT(IsFalse(Nav->IsWorldPositionClear(
			CellCenter(2, 2), 0x01)));
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
		Nav->WarmAgentProfile(FSeinNavAgentProfile());
		ASSERT_THAT(AreEqual(1,
			FNavigationAStarTestAccess::
				ReachabilityProfileCacheSize(*Nav)));

		// The linear count remains six, but the row stride and dirty-rect
		// coordinate space change. Retained bytes must not enter the new grid.
		ConfigureNavGrid(*LevelData, FIntPoint(3, 2));
		ASSERT_THAT(IsTrue(
			Nav->LoadFromSubstrate(*LevelData).IsAdopted()));
		ASSERT_THAT(IsTrue(FNavigationAStarTestAccess::IsSerialOverlayReset(*Nav)));
		ASSERT_THAT(AreEqual(0,
			FNavigationAStarTestAccess::
				ReachabilityProfileCacheSize(*Nav)));
	}

	TEST(AgentTerrainPolicyOwnsPathAndReachabilityTopology,
		"SeinARTS.Unit.Navigation")
	{
		const FGameplayTag TerrainTag =
			FGameplayTag::RequestGameplayTag(
				TEXT("Test"), false);
		ASSERT_THAT(IsTrue(TerrainTag.IsValid()));

		USeinNavigationAStar* Nav =
			NewObject<USeinNavigationAStar>();
		USeinLevelDataTestDouble* LevelData =
			NewObject<USeinLevelDataTestDouble>();
		ASSERT_THAT(IsNotNull(Nav));
		ASSERT_THAT(IsNotNull(LevelData));
		ConfigureOpenConnectedNavGrid(
			*LevelData, FIntPoint(5, 3));
		for (int32 Y = 0; Y < 3; ++Y)
		{
			LevelData->TestSurfaces[Y * 5 + 2]
				.TerrainTypeIndex = 1;
		}
		ASSERT_THAT(IsTrue(
			Nav->LoadFromSubstrate(*LevelData).IsAdopted()));

		FSeinNavAgentProfile GroundAgent;
		GroundAgent.BlockedTerrainTags.AddTag(TerrainTag);
		ASSERT_THAT(IsFalse(Nav->IsReachableForAgent(
			CellCenter(0, 1), CellCenter(4, 1), GroundAgent)));
		ASSERT_THAT(AreEqual(1,
			FNavigationAStarTestAccess::
				ReachabilityProfileCacheSize(*Nav)));
		ASSERT_THAT(IsFalse(Nav->IsReachableForAgent(
			CellCenter(0, 1), CellCenter(4, 1), GroundAgent)));
		ASSERT_THAT(AreEqual(1,
			FNavigationAStarTestAccess::
				ReachabilityProfileCacheSize(*Nav)));

		FSeinNavAgentProfile UnrestrictedAgent;
		ASSERT_THAT(IsTrue(Nav->IsReachableForAgent(
			CellCenter(0, 1),
			CellCenter(4, 1),
			UnrestrictedAgent)));

		FSeinPathRequest Request;
		Request.Start = CellCenter(0, 1);
		Request.End = CellCenter(4, 1);
		Request.BlockedTerrainTags.AddTag(TerrainTag);
		FSeinPath Path;
		ASSERT_THAT(IsTrue(Nav->FindPath(Request, Path)));
		ASSERT_THAT(IsTrue(Path.bIsPartial));
	}

	TEST(DynamicBlockersAffectRoutesNotFundamentalReachability,
		"SeinARTS.Unit.Navigation")
	{
		USeinNavigationAStar* Nav =
			NewObject<USeinNavigationAStar>();
		USeinLevelDataTestDouble* LevelData =
			NewObject<USeinLevelDataTestDouble>();
		ASSERT_THAT(IsNotNull(Nav));
		ASSERT_THAT(IsNotNull(LevelData));
		ConfigureOpenConnectedNavGrid(
			*LevelData, FIntPoint(5, 3));
		ASSERT_THAT(IsTrue(
			Nav->LoadFromSubstrate(*LevelData).IsAdopted()));

		FSeinDynamicBlocker Wall;
		Wall.Owner = FSeinEntityHandle(9, 1);
		Wall.EntityCenter = CellCenter(2, 1);
		Wall.EntityRotation = FFixedQuaternion::Identity;
		Wall.Shape.Shape = ESeinStampShape::Rect;
		Wall.Shape.HalfExtentX = FFixedPoint::FromInt(49);
		Wall.Shape.HalfExtentY = FFixedPoint::FromInt(149);
		Wall.BlockedNavLayerMask = 0x01;
		FNavigationAStarTestAccess::InstallDynamicBlockers(
			*Nav, { Wall });

		FSeinNavAgentProfile BlockedLayerAgent;
		BlockedLayerAgent.AgentNavLayerMask = 0x01;
		ASSERT_THAT(IsTrue(Nav->IsReachableForAgent(
			CellCenter(0, 1),
			CellCenter(4, 1),
			BlockedLayerAgent)));

		FSeinPathRequest BlockedRequest;
		BlockedRequest.Start = CellCenter(0, 1);
		BlockedRequest.End = CellCenter(4, 1);
		BlockedRequest.AgentNavLayerMask = 0x01;
		FSeinPath BlockedPath;
		ASSERT_THAT(IsTrue(Nav->FindPath(
			BlockedRequest, BlockedPath)));
		ASSERT_THAT(IsTrue(BlockedPath.bIsPartial));

		FSeinPathRequest IgnoringRequest = BlockedRequest;
		IgnoringRequest.AgentNavLayerMask = 0x02;
		FSeinPath IgnoringPath;
		ASSERT_THAT(IsTrue(Nav->FindPath(
			IgnoringRequest, IgnoringPath)));
		ASSERT_THAT(IsFalse(IgnoringPath.bIsPartial));
	}

	TEST(FormationProjectionIgnoresExactGroupDynamicBlockersOnly,
		"SeinARTS.Unit.Navigation")
	{
		USeinNavigationAStar* Nav =
			NewObject<USeinNavigationAStar>();
		USeinLevelDataTestDouble* LevelData =
			NewObject<USeinLevelDataTestDouble>();
		ASSERT_THAT(IsNotNull(Nav));
		ASSERT_THAT(IsNotNull(LevelData));
		ConfigureOpenConnectedNavGrid(
			*LevelData, FIntPoint(7, 7));
		TArray<uint8>& Channel =
			LevelData->LayerChannels.FindChecked(TEXT("Nav"));
		Channel[1 * 7 + 1] = 255;
		ASSERT_THAT(IsTrue(
			Nav->LoadFromSubstrate(*LevelData).IsAdopted()));

		const FSeinEntityHandle BlockerOwner(9, 1);
		FSeinDynamicBlocker Blocker;
		Blocker.Owner = BlockerOwner;
		Blocker.EntityCenter = CellCenter(3, 3);
		Blocker.EntityRotation = FFixedQuaternion::Identity;
		Blocker.Shape.Shape = ESeinStampShape::Rect;
		Blocker.Shape.HalfExtentX = FFixedPoint::FromInt(49);
		Blocker.Shape.HalfExtentY = FFixedPoint::FromInt(49);
		Blocker.BlockedNavLayerMask = 0x01;
		FNavigationAStarTestAccess::InstallDynamicBlockers(
			*Nav, { Blocker });

		FSeinNavAgentProfile Agent;
		Agent.Requester = FSeinEntityHandle(1, 1);
		Agent.AgentNavLayerMask = 0x01;
		const FFixedVector BlockedPosition = CellCenter(3, 3);
		ASSERT_THAT(IsFalse(Nav->IsFootprintClearForAgent(
			BlockedPosition, Agent)));

		TSet<FSeinEntityHandle> WrongGeneration;
		WrongGeneration.Add(FSeinEntityHandle(9, 2));
		ASSERT_THAT(IsFalse(
			Nav->IsFootprintClearForAgentIgnoringDynamicBlockers(
				BlockedPosition, Agent, WrongGeneration)));

		TSet<FSeinEntityHandle> ExactOwner;
		ExactOwner.Add(BlockerOwner);
		ASSERT_THAT(IsTrue(
			Nav->IsFootprintClearForAgentIgnoringDynamicBlockers(
				BlockedPosition, Agent, ExactOwner)));
		ASSERT_THAT(IsFalse(
			Nav->IsFootprintClearForAgentIgnoringDynamicBlockers(
				CellCenter(1, 1), Agent, ExactOwner)));

		const TArray<FFixedVector> AvoidCentres;
		const TArray<FFixedPoint> AvoidRadii;
		FFixedVector OrdinaryProjection;
		ASSERT_THAT(IsTrue(Nav->ProjectPointToNavFreeForAgent(
			BlockedPosition, Agent, AvoidCentres, AvoidRadii,
			OrdinaryProjection)));
		ASSERT_THAT(IsTrue(OrdinaryProjection != BlockedPosition));

		FFixedVector GroupProjection;
		ASSERT_THAT(IsTrue(
			Nav->ProjectPointToNavFreeForAgentIgnoringDynamicBlockers(
				BlockedPosition, Agent, ExactOwner,
				AvoidCentres, AvoidRadii, GroupProjection)));
		ASSERT_THAT(IsTrue(GroupProjection == BlockedPosition));

		const TArray<FFixedVector> SaturatedAvoidCentres = {
			BlockedPosition,
		};
		const TArray<FFixedPoint> SaturatedAvoidRadii = {
			FFixedPoint::FromInt(100000),
		};
		FFixedVector GroupFallbackProjection;
		ASSERT_THAT(IsTrue(
			Nav->ProjectPointToNavFreeForAgentIgnoringDynamicBlockers(
				BlockedPosition, Agent, ExactOwner,
				SaturatedAvoidCentres, SaturatedAvoidRadii,
				GroupFallbackProjection)));
		ASSERT_THAT(IsTrue(
			GroupFallbackProjection == BlockedPosition));
	}

	TEST(AgentTerrainParticipatesInFootprintClearance,
		"SeinARTS.Unit.Navigation")
	{
		const FGameplayTag TerrainTag =
			FGameplayTag::RequestGameplayTag(
				TEXT("Test"), false);
		ASSERT_THAT(IsTrue(TerrainTag.IsValid()));

		USeinNavigationAStar* Nav =
			NewObject<USeinNavigationAStar>();
		USeinLevelDataTestDouble* LevelData =
			NewObject<USeinLevelDataTestDouble>();
		ASSERT_THAT(IsNotNull(Nav));
		ASSERT_THAT(IsNotNull(LevelData));
		ConfigureOpenConnectedNavGrid(
			*LevelData, FIntPoint(7, 7));
		LevelData->TestSurfaces[3 * 7 + 3]
			.TerrainTypeIndex = 1;
		ASSERT_THAT(IsTrue(
			Nav->LoadFromSubstrate(*LevelData).IsAdopted()));

		FSeinPathRequest Request;
		Request.Start = CellCenter(0, 3);
		Request.End = CellCenter(6, 3);
		Request.AgentFootprintRadius =
			FFixedPoint::FromInt(60);
		Request.BlockedTerrainTags.AddTag(TerrainTag);
		FSeinPath Path;
		ASSERT_THAT(IsTrue(Nav->FindPath(Request, Path)));
		ASSERT_THAT(IsFalse(Path.bIsPartial));

		bool bRoutedOutsideAdjacentRows = false;
		for (const FFixedVector& Waypoint : Path.Waypoints)
		{
			const int32 Row = (Waypoint.Y
				/ FFixedPoint::FromInt(100)).ToInt();
			if (Row <= 1 || Row >= 5)
			{
				bRoutedOutsideAdjacentRows = true;
				break;
			}
		}
		ASSERT_THAT(IsTrue(bRoutedOutsideAdjacentRows));
	}

	TEST(AgentProjectionMatchesTerrainWallPadding,
		"SeinARTS.Unit.Navigation")
	{
		const FGameplayTag TerrainTag =
			FGameplayTag::RequestGameplayTag(
				TEXT("Test"), false);
		ASSERT_THAT(IsTrue(TerrainTag.IsValid()));

		USeinNavigationAStar* Nav =
			NewObject<USeinNavigationAStar>();
		USeinLevelDataTestDouble* LevelData =
			NewObject<USeinLevelDataTestDouble>();
		ASSERT_THAT(IsNotNull(Nav));
		ASSERT_THAT(IsNotNull(LevelData));
		ConfigureOpenConnectedNavGrid(
			*LevelData, FIntPoint(5, 3));
		LevelData->TestSurfaces[1 * 5 + 2]
			.TerrainTypeIndex = 1;
		ASSERT_THAT(IsTrue(
			Nav->LoadFromSubstrate(*LevelData).IsAdopted()));

		FSeinNavAgentProfile Agent;
		Agent.BlockedTerrainTags.AddTag(TerrainTag);
		Agent.AgentWallPaddingCells = 2;
		FFixedVector Projected;
		ASSERT_THAT(IsTrue(Nav->ProjectPointToNavForAgent(
			CellCenter(1, 1), Agent, Projected)));
		ASSERT_THAT(IsTrue(Projected != CellCenter(1, 1)));
		const int32 ProjectedX =
			(Projected.X / FFixedPoint::FromInt(100)).ToInt();
		const int32 ProjectedY =
			(Projected.Y / FFixedPoint::FromInt(100)).ToInt();
		ASSERT_THAT(IsTrue(
			FMath::Max(
				FMath::Abs(ProjectedX - 2),
				FMath::Abs(ProjectedY - 1)) >= 2));
	}

	TEST(AuthoritativeDestinationCannotOverrideAgentHazards,
		"SeinARTS.Unit.Navigation")
	{
		const FGameplayTag TerrainTag =
			FGameplayTag::RequestGameplayTag(
				TEXT("Test"), false);
		ASSERT_THAT(IsTrue(TerrainTag.IsValid()));

		USeinNavigationAStar* Nav =
			NewObject<USeinNavigationAStar>();
		USeinLevelDataTestDouble* LevelData =
			NewObject<USeinLevelDataTestDouble>();
		ASSERT_THAT(IsNotNull(Nav));
		ASSERT_THAT(IsNotNull(LevelData));
		ConfigureOpenConnectedNavGrid(
			*LevelData, FIntPoint(3, 3));
		LevelData->TestSurfaces[1 * 3 + 1]
			.TerrainTypeIndex = 1;
		ASSERT_THAT(IsTrue(
			Nav->LoadFromSubstrate(*LevelData).IsAdopted()));

		FSeinPathRequest Request;
		Request.Start = CellCenter(0, 1);
		Request.End = CellCenter(1, 1);
		Request.BlockedTerrainTags.AddTag(TerrainTag);
		Request.bAuthoritativeDestination = true;
		FSeinPath Path;
		ASSERT_THAT(IsTrue(Nav->FindPath(Request, Path)));
		ASSERT_THAT(IsTrue(Path.bIsPartial));
		ASSERT_THAT(IsTrue(Path.Waypoints.Last()
			!= Request.End));
	}

	TEST(AuthoritativeFinalStepRejectsBlockerAddedAfterPlanning,
		"SeinARTS.Unit.Navigation")
	{
		USeinNavigationAStar* Nav =
			NewObject<USeinNavigationAStar>();
		USeinLevelDataTestDouble* LevelData =
			NewObject<USeinLevelDataTestDouble>();
		USeinBasicUnitMovement* Movement =
			NewObject<USeinBasicUnitMovement>();
		ASSERT_THAT(IsNotNull(Nav));
		ASSERT_THAT(IsNotNull(LevelData));
		ASSERT_THAT(IsNotNull(Movement));
		ConfigureOpenConnectedNavGrid(
			*LevelData, FIntPoint(5, 3));
		ASSERT_THAT(IsTrue(
			Nav->LoadFromSubstrate(*LevelData).IsAdopted()));

		const FFixedVector Start = CellCenter(0, 1);
		const FFixedVector NearDestination = CellCenter(2, 1);
		const FFixedVector Destination = CellCenter(3, 1);
		const FSeinEntityHandle Requester(1, 1);
		const FFixedPoint FootprintRadius = FFixedPoint::FromInt(20);
		const FFixedPoint AcceptanceRadius = FFixedPoint::FromInt(60);

		FSeinPathRequest Request;
		Request.Start = Start;
		Request.End = Destination;
		Request.Requester = Requester;
		Request.AgentNavLayerMask = 0x01;
		Request.AgentFootprintRadius = FootprintRadius;
		Request.bAuthoritativeDestination = true;
		FSeinPath Path;
		ASSERT_THAT(IsTrue(Nav->FindPath(Request, Path)));
		ASSERT_THAT(IsFalse(Path.bIsPartial));
		ASSERT_THAT(IsTrue(Path.Waypoints.Last() == Destination));

		FSeinEntity Entity;
		Entity.Transform.SetLocation(NearDestination);
		FSeinMovementPayload MovementData;
		FSeinNavigationPayload NavigationData;
		NavigationData.NavLayerMask = 0x01;
		NavigationData.FallbackFootprintRadius = FootprintRadius;
		int32 CurrentWaypointIndex = Path.Waypoints.Num() - 1;
		FSeinMovementContext Context{
			Entity,
			&MovementData,
			&NavigationData,
			Path,
			CurrentWaypointIndex,
			FFixedVector::SquareSaturated(AcceptanceRadius),
			FFixedPoint::FromInt(1) / FFixedPoint::FromInt(30),
			Nav,
			nullptr,
			Requester
		};
		Context.bAuthoritativeDestination = true;
		Context.ExactAcceptanceRadius = AcceptanceRadius;
		Movement->CacheFootprintFromContext(Context);

		FSeinDynamicBlocker Blocker;
		Blocker.Owner = FSeinEntityHandle(9, 1);
		Blocker.EntityCenter = Destination;
		Blocker.EntityRotation = FFixedQuaternion::Identity;
		Blocker.Shape.Shape = ESeinStampShape::Rect;
		Blocker.Shape.HalfExtentX = FFixedPoint::FromInt(49);
		Blocker.Shape.HalfExtentY = FFixedPoint::FromInt(49);
		Blocker.BlockedNavLayerMask = 0x01;
		FNavigationAStarTestAccess::InstallDynamicBlockers(
			*Nav, { Blocker });

		ASSERT_THAT(IsFalse(
			Movement->TryFinalizeAuthoritativeArrival(Context)));
		ASSERT_THAT(IsTrue(
			Entity.Transform.GetLocation() == NearDestination));

		FNavigationAStarTestAccess::InstallDynamicBlockers(*Nav, {});
		ASSERT_THAT(IsTrue(
			Movement->TryFinalizeAuthoritativeArrival(Context)));
		ASSERT_THAT(IsTrue(
			Entity.Transform.GetLocation() == Destination));
	}
}
