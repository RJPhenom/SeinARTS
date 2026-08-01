#include "CQTest.h"
#include "Components/ActorTestSpawner.h"
#include "Containers/Ticker.h"

#include "Data/SeinWorldSnapshot.h"
#include "Serialization/SeinCanonicalInitialStateDigest.h"
#include "Serialization/SeinCanonicalStateCodec.h"
#include "Serialization/SeinCanonicalStateRegistry.h"
#include "SeinNavigation.h"
#include "SeinNavigationAStar.h"
#include "SeinNavigationSubsystem.h"
#include "SeinPathTypes.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSnapshotRestore.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "TestTypes/SeinLevelDataTestTypes.h"
#include "TestTypes/SeinNavigationStateTestTypes.h"
#include "UObject/UnrealType.h"

namespace UE::SeinARTSTests
{
	struct FNavigationCanonicalStateTestAccess
	{
		struct FContinuationSnapshot
		{
			int32 PathRequestsThisTick = 0;
			int32 LastResetTick = -1;
			int32 LastDrainTick = -1;
			TMap<FSeinEntityHandle, FSeinPathRequest> AsyncQueue;
			TMap<
				FSeinEntityHandle,
				USeinNavigationSubsystem::FSeinAsyncPathResult>
				AsyncResults;
		};

		static void InstallDynamicBlockers(
			USeinNavigationAStar& Navigation,
			const TArray<FSeinDynamicBlocker>& Blockers)
		{
			Navigation.SetDynamicBlockers(Blockers);
		}

		/** Simulates a post-freeze gated static-grid write: the mutation
		 *  counter moves while the cached content digest stays identical. */
		static void BumpStaticGridGeneration(USeinNavigationAStar& Navigation)
		{
			++Navigation.StaticGridGeneration;
		}

		static void Seed(USeinNavigationSubsystem& Navigation)
		{
			Navigation.PathRequestsThisTick = 3;
			Navigation.LastResetTick = 0;
			Navigation.LastDrainTick = 0;

			FSeinPathRequest Queued;
			Queued.Start = FFixedVector(
				FFixedPoint::FromInt(10),
				FFixedPoint::FromInt(20),
				FFixedPoint::Zero);
			Queued.End = FFixedVector(
				FFixedPoint::FromInt(30),
				FFixedPoint::FromInt(40),
				FFixedPoint::Zero);
			Queued.Requester = FSeinEntityHandle(2, 3);
			Queued.AgentNavLayerMask = 0x04;
			Queued.AgentWallPaddingCells = 2;
			Navigation.AsyncQueue.Add(Queued.Requester, Queued);

			FSeinPathRequest ReadyRequest;
			ReadyRequest.Start = FFixedVector(
				FFixedPoint::FromInt(50),
				FFixedPoint::FromInt(60),
				FFixedPoint::Zero);
			ReadyRequest.End = FFixedVector(
				FFixedPoint::FromInt(70),
				FFixedPoint::FromInt(80),
				FFixedPoint::Zero);
			ReadyRequest.Requester = FSeinEntityHandle(5, 2);
			ReadyRequest.bAuthoritativeDestination = true;

			USeinNavigationSubsystem::FSeinAsyncPathResult Ready;
			Ready.Request = ReadyRequest;
			Ready.Path.Waypoints = {
				ReadyRequest.Start,
				ReadyRequest.End,
			};
			Ready.Path.bIsValid = true;
			Ready.Path.DeriveSegmentsFromWaypoints();
			Navigation.AsyncResults.Add(
				ReadyRequest.Requester, MoveTemp(Ready));
		}

		static void SeedRestoreSentinel(
			USeinNavigationSubsystem& Navigation)
		{
			Navigation.PathRequestsThisTick = 7;
			Navigation.LastResetTick = 0;
			Navigation.LastDrainTick = 0;
			Navigation.AsyncQueue.Reset();
			Navigation.AsyncResults.Reset();

			FSeinPathRequest Queued;
			Queued.Start = FFixedVector(
				FFixedPoint::FromInt(110),
				FFixedPoint::FromInt(120),
				FFixedPoint::FromInt(5));
			Queued.End = FFixedVector(
				FFixedPoint::FromInt(130),
				FFixedPoint::FromInt(140),
				FFixedPoint::FromInt(6));
			Queued.Requester = FSeinEntityHandle(41, 9);
			Queued.AgentNavLayerMask = 0x02;
			Queued.AgentFootprintRadius =
				FFixedPoint::FromInt(15);
			Queued.AgentWallPaddingCells = 3;
			Queued.bAuthoritativeDestination = true;
			Queued.AgentMaxSearchNodes = 99;
			Queued.GroupId = 4001;
			Navigation.AsyncQueue.Add(Queued.Requester, Queued);

			FSeinPathRequest ReadyRequest;
			ReadyRequest.Start = FFixedVector(
				FFixedPoint::FromInt(150),
				FFixedPoint::FromInt(160),
				FFixedPoint::FromInt(7));
			ReadyRequest.End = FFixedVector(
				FFixedPoint::FromInt(190),
				FFixedPoint::FromInt(200),
				FFixedPoint::FromInt(8));
			ReadyRequest.Requester = FSeinEntityHandle(42, 8);
			ReadyRequest.AgentNavLayerMask = 0x08;
			ReadyRequest.AgentFootprintRadius =
				FFixedPoint::FromInt(25);
			ReadyRequest.AgentWallPaddingCells = 1;
			ReadyRequest.AgentMaxSearchNodes = 77;
			ReadyRequest.GroupId = 5002;

			USeinNavigationSubsystem::FSeinAsyncPathResult Ready;
			Ready.Request = ReadyRequest;
			Ready.Path.Waypoints = {
				ReadyRequest.Start,
				FFixedVector(
					FFixedPoint::FromInt(170),
					FFixedPoint::FromInt(175),
					FFixedPoint::FromInt(9)),
				ReadyRequest.End,
			};
			Ready.Path.bIsValid = true;
			Ready.Path.bIsPartial = true;
			Ready.Path.DeriveSegmentsFromWaypoints();
			Navigation.AsyncResults.Add(
				ReadyRequest.Requester, MoveTemp(Ready));
		}

		static void SeedCompletePathWithWrongTerminal(
			USeinNavigationSubsystem& Navigation)
		{
			Navigation.PathRequestsThisTick = 0;
			Navigation.LastResetTick = -1;
			Navigation.LastDrainTick = -1;
			Navigation.AsyncQueue.Reset();
			Navigation.AsyncResults.Reset();

			FSeinPathRequest Request;
			Request.Start = FFixedVector(
				FFixedPoint::FromInt(10),
				FFixedPoint::FromInt(20),
				FFixedPoint::Zero);
			Request.End = FFixedVector(
				FFixedPoint::FromInt(90),
				FFixedPoint::FromInt(100),
				FFixedPoint::Zero);
			Request.Requester = FSeinEntityHandle(77, 3);

			USeinNavigationSubsystem::FSeinAsyncPathResult Ready;
			Ready.Request = Request;
			Ready.Path.Waypoints = {
				Request.Start,
				FFixedVector(
					FFixedPoint::FromInt(70),
					FFixedPoint::FromInt(80),
					FFixedPoint::Zero),
			};
			Ready.Path.bIsValid = true;
			Ready.Path.bIsPartial = false;
			Ready.Path.DeriveSegmentsFromWaypoints();
			Navigation.AsyncResults.Add(
				Request.Requester, MoveTemp(Ready));
		}

		static void SeedValidArc(
			USeinNavigationSubsystem& Navigation)
		{
			Navigation.PathRequestsThisTick = 0;
			Navigation.LastResetTick = -1;
			Navigation.LastDrainTick = -1;
			Navigation.AsyncQueue.Reset();
			Navigation.AsyncResults.Reset();

			FSeinPathRequest Request;
			Request.Start = FFixedVector(
				FFixedPoint::FromInt(100),
				FFixedPoint::Zero,
				FFixedPoint::Zero);
			Request.End = FFixedVector(
				FFixedPoint::Zero,
				FFixedPoint::FromInt(100),
				FFixedPoint::Zero);
			Request.Requester = FSeinEntityHandle(79, 3);

			USeinNavigationSubsystem::FSeinAsyncPathResult Ready;
			Ready.Request = Request;
			Ready.Path.Waypoints = {
				Request.Start,
				Request.End,
			};
			FSeinPathSegment& Arc =
				Ready.Path.Segments.AddDefaulted_GetRef();
			Arc.Type = ESeinPathSegmentType::Arc;
			Arc.From = Request.Start;
			Arc.To = Request.End;
			Arc.Center = FFixedVector::ZeroVector;
			Arc.Radius = FFixedPoint::FromInt(100);
			Arc.SweepAngle = FFixedPoint::HalfPi;
			Ready.Path.TotalCost =
				Arc.Radius * Arc.SweepAngle;
			Ready.Path.bIsValid = true;
			Ready.Path.bIsPartial = false;
			Navigation.AsyncResults.Add(
				Request.Requester, MoveTemp(Ready));
		}

		static FContinuationSnapshot CaptureContinuation(
			const USeinNavigationSubsystem& Navigation)
		{
			FContinuationSnapshot Snapshot;
			Snapshot.PathRequestsThisTick =
				Navigation.PathRequestsThisTick;
			Snapshot.LastResetTick = Navigation.LastResetTick;
			Snapshot.LastDrainTick = Navigation.LastDrainTick;
			Snapshot.AsyncQueue = Navigation.AsyncQueue;
			Snapshot.AsyncResults = Navigation.AsyncResults;
			return Snapshot;
		}

		static bool MatchesContinuation(
			const USeinNavigationSubsystem& Navigation,
			const FContinuationSnapshot& Expected)
		{
			if (Navigation.PathRequestsThisTick
					!= Expected.PathRequestsThisTick
				|| Navigation.LastResetTick
					!= Expected.LastResetTick
				|| Navigation.LastDrainTick
					!= Expected.LastDrainTick
				|| Navigation.AsyncQueue.Num()
					!= Expected.AsyncQueue.Num()
				|| Navigation.AsyncResults.Num()
					!= Expected.AsyncResults.Num())
			{
				return false;
			}

			for (const TPair<
				FSeinEntityHandle,
				FSeinPathRequest>& Pair : Expected.AsyncQueue)
			{
				const FSeinPathRequest* Actual =
					Navigation.AsyncQueue.Find(Pair.Key);
				if (!Actual
					|| !PathRequestsEqual(*Actual, Pair.Value))
				{
					return false;
				}
			}

			for (const TPair<
				FSeinEntityHandle,
				USeinNavigationSubsystem::FSeinAsyncPathResult>&
				Pair : Expected.AsyncResults)
			{
				const USeinNavigationSubsystem::
					FSeinAsyncPathResult* Actual =
						Navigation.AsyncResults.Find(Pair.Key);
				if (!Actual
					|| !PathRequestsEqual(
						Actual->Request, Pair.Value.Request)
					|| !PathsEqual(
						Actual->Path, Pair.Value.Path))
				{
					return false;
				}
			}
			return true;
		}

		static bool MatchesSeed(
			const USeinNavigationSubsystem& Navigation)
		{
			const FSeinPathRequest* Queued =
				Navigation.AsyncQueue.Find(FSeinEntityHandle(2, 3));
			const USeinNavigationSubsystem::FSeinAsyncPathResult* Ready =
				Navigation.AsyncResults.Find(FSeinEntityHandle(5, 2));
			return Navigation.PathRequestsThisTick == 3
				&& Navigation.LastResetTick == 0
				&& Navigation.LastDrainTick == 0
				&& Navigation.AsyncQueue.Num() == 1
				&& Navigation.AsyncResults.Num() == 1
				&& Queued
				&& Queued->End.X == FFixedPoint::FromInt(30)
				&& Queued->AgentNavLayerMask == 0x04
				&& Queued->AgentWallPaddingCells == 2
				&& Ready
				&& Ready->Request.bAuthoritativeDestination
				&& Ready->Path.bIsValid
				&& Ready->Path.Waypoints.Num() == 2
				&& Ready->Path.Waypoints.Last().Y
					== FFixedPoint::FromInt(80);
		}

		static void SignalSubstrateMutation(
			USeinNavigationSubsystem& Navigation,
			USeinLevelData& Substrate)
		{
			Navigation.LevelData = &Substrate;
			Navigation.OnLevelDataChanged();
		}

	private:
		static bool PathRequestsEqual(
			const FSeinPathRequest& A,
			const FSeinPathRequest& B)
		{
			return A.Start == B.Start
				&& A.End == B.End
				&& A.Requester == B.Requester
				&& A.BlockedTerrainTags
					== B.BlockedTerrainTags
				&& A.AgentNavLayerMask
					== B.AgentNavLayerMask
				&& A.AgentFootprintRadius
					== B.AgentFootprintRadius
				&& A.AgentWallPaddingCells
					== B.AgentWallPaddingCells
				&& A.bAuthoritativeDestination
					== B.bAuthoritativeDestination
				&& A.AgentMaxSearchNodes
					== B.AgentMaxSearchNodes
				&& A.GroupId == B.GroupId;
		}

		static bool PathSegmentsEqual(
			const FSeinPathSegment& A,
			const FSeinPathSegment& B)
		{
			return A.Type == B.Type
				&& A.From == B.From
				&& A.To == B.To
				&& A.Center == B.Center
				&& A.Radius == B.Radius
				&& A.SweepAngle == B.SweepAngle
				&& A.bReverse == B.bReverse;
		}

		static bool PathsEqual(
			const FSeinPath& A,
			const FSeinPath& B)
		{
			if (A.Waypoints != B.Waypoints
				|| A.Segments.Num() != B.Segments.Num()
				|| A.TotalCost != B.TotalCost
				|| A.bIsValid != B.bIsValid
				|| A.bIsPartial != B.bIsPartial)
			{
				return false;
			}
			for (int32 Index = 0;
				Index < A.Segments.Num();
				++Index)
			{
				if (!PathSegmentsEqual(
					A.Segments[Index], B.Segments[Index]))
				{
					return false;
				}
			}
			return true;
		}
	};

	namespace
	{
		struct FScopedNavigationClassOverride
		{
			explicit FScopedNavigationClassOverride(
				const UClass* NavigationClass)
				: Settings(
					GetMutableDefault<USeinARTSCoreSettings>())
				, SavedNavigationClass(
					Settings
						? Settings->NavigationClass
						: FSoftClassPath())
			{
				check(Settings);
				check(NavigationClass);
				Settings->NavigationClass =
					FSoftClassPath(NavigationClass);
			}

			~FScopedNavigationClassOverride()
			{
				Settings->NavigationClass =
					SavedNavigationClass;
			}

			USeinARTSCoreSettings* Settings = nullptr;
			FSoftClassPath SavedNavigationClass;
		};

		bool StartNavigationStateWorld(
			USeinWorldSubsystem& World,
			FName FixtureId,
			FString& OutError)
		{
			return SeinTestMatchBootstrap::Materialize(
				World,
				FSeinMatchSettings(),
				0x4E415653,
				FixtureId,
				&OutError)
				&& SeinTestMatchBootstrap::Start(World, &OutError);
		}

		USeinLevelDataTestDouble* MakeGrid(
			UWorld& World,
			uint8 Variant)
		{
			USeinLevelDataTestDouble* Grid =
				NewObject<USeinLevelDataTestDouble>(&World);
			Grid->TestDimensions = FIntPoint(2, 2);
			Grid->TestCellSize = FFixedPoint::FromInt(100);
			Grid->TestOrigin = FFixedVector(
				FFixedPoint::FromInt(-100),
				FFixedPoint::FromInt(-100),
				FFixedPoint::Zero);
			Grid->TestSurfaces.SetNum(4);

			TArray<uint8> NavChannel = {
				1, Variant, 1, 1,
				0, 0, 0, 0,
			};
			Grid->LayerChannels.Add(
				FName(TEXT("Nav")), MoveTemp(NavChannel));
			return Grid;
		}

		USeinNavigationAStar* ConfigureGrid(
			UWorld& World,
			USeinNavigationSubsystem& NavigationSubsystem,
			uint8 Variant)
		{
			USeinNavigationAStar* Navigation =
				Cast<USeinNavigationAStar>(
					NavigationSubsystem.GetNavigation());
			if (!Navigation
				|| !Navigation->LoadFromSubstrate(
					*MakeGrid(World, Variant)).IsAdopted())
			{
				return nullptr;
			}
			return Navigation;
		}

		FSeinStructWireLimits MakeWireLimits(
			const FSeinCanonicalStateLimits& StateLimits)
		{
			FSeinStructWireLimits Limits;
			Limits.MaxBytes = StateLimits.MaxEncodedBytes;
			Limits.MaxAggregateElements =
				StateLimits.MaxAggregateElements;
			Limits.MaxStringBytes = FMath::Min(
				StateLimits.MaxEncodedBytes,
				1024 * 1024);
			Limits.MaxRecursionDepth =
				StateLimits.MaxRecursionDepth;
			Limits.MaxNativeAllocationBytes =
				StateLimits.MaxEncodedBytes;
			return Limits;
		}

		bool CaptureValidArcSnapshot(
			FSeinWorldSnapshot& OutSnapshot,
			FString& OutError)
		{
			FActorTestSpawner SourceSpawner;
			UWorld& SourceUnrealWorld =
				SourceSpawner.GetWorld();
			USeinWorldSubsystem* Source =
				SourceUnrealWorld.GetSubsystem<
					USeinWorldSubsystem>();
			USeinNavigationSubsystem* SourceNavigation =
				SourceUnrealWorld.GetSubsystem<
					USeinNavigationSubsystem>();
			if (!Source || !SourceNavigation)
			{
				OutError =
					TEXT("Valid-arc fixture could not resolve its world subsystems.");
				return false;
			}
			if (!StartNavigationStateWorld(
					*Source,
					TEXT("NavigationState.ValidArcSnapshot"),
					OutError))
			{
				return false;
			}

			FNavigationCanonicalStateTestAccess::SeedValidArc(
				*SourceNavigation);
			Source->CaptureSnapshot(OutSnapshot);
			Source->StopSimulation();
			if (OutSnapshot.SnapshotVersion
				!= FSeinWorldSnapshot::CurrentVersion)
			{
				OutError =
					TEXT("Valid-arc fixture did not produce a checkpoint.");
				return false;
			}
			return true;
		}

		bool MutateNavigationReadyPath(
			FSeinWorldSnapshot& Snapshot,
			TFunctionRef<void(
				FSeinPathRequest&,
				FSeinPath&)> Mutation,
			FString& OutError)
		{
			const FName DomainId(
				TEXT("seinarts.navigation"));
			const FName ContributorId(
				TEXT("async-path-continuation"));
			FSeinCanonicalStateContributorRecord* Record =
				Snapshot.NativeCanonicalStateRecords.FindByPredicate(
					[&](
						const FSeinCanonicalStateContributorRecord&
							Candidate)
					{
						return Candidate.Key.StableDomainId
								== DomainId
							&& Candidate.Key.StableContributorId
								== ContributorId;
					});
			if (!Record)
			{
				OutError =
					TEXT("Navigation continuation record is absent.");
				return false;
			}

			FSeinCanonicalStateSchemaSnapshot Schema =
				FSeinCanonicalStateRegistry::
					CaptureSchemaSnapshot(&OutError);
			if (!Schema.IsValid())
			{
				return false;
			}
			const FSeinFrozenCanonicalStateContributor*
				Contributor = nullptr;
			for (const FSeinFrozenCanonicalStateContributor&
				Candidate : Schema.GetContributors())
			{
				if (Candidate.Descriptor.Key.StableDomainId
						== DomainId
					&& Candidate.Descriptor.Key
						.StableContributorId
						== ContributorId)
				{
					Contributor = &Candidate;
					break;
				}
			}
			if (!Contributor
				|| !Contributor->Descriptor.PayloadStruct)
			{
				OutError =
					TEXT("Navigation continuation descriptor is unavailable.");
				return false;
			}

			FInstancedStruct Payload;
			Payload.InitializeAs(
				Contributor->Descriptor.PayloadStruct);
			const FSeinStructWireLimits Limits =
				MakeWireLimits(
					Contributor->Descriptor.Limits);
			if (!FSeinCanonicalStateCodec::Decode(
					Record->PayloadBytes,
					Contributor->Descriptor.PayloadStruct,
					Payload.GetMutableMemory(),
					{
						Contributor->Descriptor
							.DynamicPayloadStructs,
						Contributor->Descriptor.AllowedNames,
					},
					Limits,
					OutError))
			{
				return false;
			}

			FArrayProperty* ReadyResultsProperty =
				FindFProperty<FArrayProperty>(
					Contributor->Descriptor.PayloadStruct,
					TEXT("ReadyResults"));
			FStructProperty* ResultProperty =
				ReadyResultsProperty
					? CastField<FStructProperty>(
						ReadyResultsProperty->Inner)
					: nullptr;
			if (!ReadyResultsProperty || !ResultProperty)
			{
				OutError =
					TEXT("Navigation continuation result schema is unavailable.");
				return false;
			}
			FScriptArrayHelper ReadyResults(
				ReadyResultsProperty,
				ReadyResultsProperty->
					ContainerPtrToValuePtr<void>(
						Payload.GetMutableMemory()));
			if (ReadyResults.Num() != 1)
			{
				OutError =
					TEXT("Valid-arc fixture has an unexpected result count.");
				return false;
			}

			void* ResultMemory = ReadyResults.GetRawPtr(0);
			FStructProperty* RequestProperty =
				FindFProperty<FStructProperty>(
					ResultProperty->Struct,
					TEXT("Request"));
			FStructProperty* PathProperty =
				FindFProperty<FStructProperty>(
					ResultProperty->Struct,
					TEXT("Path"));
			if (!RequestProperty || !PathProperty
				|| RequestProperty->Struct
					!= FSeinPathRequest::StaticStruct()
				|| PathProperty->Struct
					!= FSeinPath::StaticStruct())
			{
				OutError =
					TEXT("Navigation continuation result fields do not match the frozen schema.");
				return false;
			}

			FSeinPathRequest* Request =
				RequestProperty->
					ContainerPtrToValuePtr<
						FSeinPathRequest>(
						ResultMemory);
			FSeinPath* Path =
				PathProperty->
					ContainerPtrToValuePtr<FSeinPath>(
						ResultMemory);
			if (!Request || !Path
				|| Path->Segments.Num() != 1
				|| Path->Segments[0].Type
					!= ESeinPathSegmentType::Arc)
			{
				OutError =
					TEXT("Valid-arc fixture decoded to an unexpected path.");
				return false;
			}
			Mutation(*Request, *Path);

			TArray<uint8> MutatedBytes;
			if (!FSeinCanonicalStateCodec::Encode(
					Contributor->Descriptor.PayloadStruct,
					Payload.GetMemory(),
					{
						Contributor->Descriptor
							.DynamicPayloadStructs,
						Contributor->Descriptor.AllowedNames,
					},
					Limits,
					MutatedBytes,
					OutError))
			{
				return false;
			}

			FGuid MutatedLeaf;
			FSeinCanonicalDigestWriter Writer(
				TEXT("SeinARTS.CanonicalState.Leaf"),
				1);
			if (!Writer.WriteGuid(
					Record->DescriptorDigest)
				|| !Writer.WriteBytes(MutatedBytes)
				|| !Writer.Finalize(
					MutatedLeaf, OutError))
			{
				return false;
			}
			Record->PayloadBytes = MoveTemp(MutatedBytes);
			Record->LeafDigest = MutatedLeaf;
			return true;
		}
	}

	TEST(AStarStaticEnvironmentTracksGridButNotDynamicBlockers,
		"SeinARTS.Unit.Navigation.CanonicalState")
	{
		FActorTestSpawner Spawner;
		UWorld& UnrealWorld = Spawner.GetWorld();
		USeinNavigationSubsystem* NavigationSubsystem =
			UnrealWorld.GetSubsystem<USeinNavigationSubsystem>();
		ASSERT_THAT(IsNotNull(NavigationSubsystem));
		USeinNavigationAStar* Navigation = ConfigureGrid(
			UnrealWorld, *NavigationSubsystem, 1);
		ASSERT_THAT(IsNotNull(Navigation));

		FString Error;
		FGuid GridA;
		ASSERT_THAT(IsTrue(
			Navigation->ComputeStaticEnvironmentDigest(
				GridA, Error)));
		ASSERT_THAT(IsTrue(GridA.IsValid()));

		TArray<FSeinDynamicBlocker> DynamicBlockers;
		DynamicBlockers.AddDefaulted();
		FNavigationCanonicalStateTestAccess::InstallDynamicBlockers(
			*Navigation,
			DynamicBlockers);
		FGuid WithDynamicBlocker;
		ASSERT_THAT(IsTrue(
			Navigation->ComputeStaticEnvironmentDigest(
				WithDynamicBlocker, Error)));
		ASSERT_THAT(IsTrue(WithDynamicBlocker == GridA));

		ASSERT_THAT(IsNotNull(ConfigureGrid(
			UnrealWorld, *NavigationSubsystem, 2)));
		FGuid GridB;
		ASSERT_THAT(IsTrue(
			Navigation->ComputeStaticEnvironmentDigest(
				GridB, Error)));
		ASSERT_THAT(IsTrue(GridB.IsValid()));
		ASSERT_THAT(IsTrue(GridB != GridA));
	}

	TEST(AStarStaticEnvironmentDigestCoversEveryQueryInput,
		"SeinARTS.Unit.Navigation.CanonicalState")
	{
		FActorTestSpawner Spawner;
		UWorld& UnrealWorld = Spawner.GetWorld();
		USeinNavigationSubsystem* NavigationSubsystem =
			UnrealWorld.GetSubsystem<USeinNavigationSubsystem>();
		ASSERT_THAT(IsNotNull(NavigationSubsystem));
		USeinNavigationAStar* Navigation = ConfigureGrid(
			UnrealWorld, *NavigationSubsystem, 1);
		ASSERT_THAT(IsNotNull(Navigation));

		FString Error;
		FGuid Baseline;
		ASSERT_THAT(IsTrue(
			Navigation->ComputeStaticEnvironmentDigest(
				Baseline, Error)));
		ASSERT_THAT(IsTrue(Baseline.IsValid()));

		const auto AdoptAndDigest =
			[Navigation](
				USeinLevelDataTestDouble& Grid,
				FGuid& OutDigest,
				FString& OutError)
			{
				OutError.Reset();
				return Navigation->LoadFromSubstrate(Grid)
						.IsAdopted()
					&& Navigation->ComputeStaticEnvironmentDigest(
						OutDigest, OutError);
			};

		USeinLevelDataTestDouble* Same =
			MakeGrid(UnrealWorld, 1);
		FGuid SameDigest;
		ASSERT_THAT(IsTrue(AdoptAndDigest(
			*Same, SameDigest, Error)));
		ASSERT_THAT(IsTrue(SameDigest == Baseline));

		USeinLevelDataTestDouble* Dimensions =
			MakeGrid(UnrealWorld, 1);
		Dimensions->TestDimensions = FIntPoint(4, 1);
		FGuid DimensionsDigest;
		ASSERT_THAT(IsTrue(AdoptAndDigest(
			*Dimensions, DimensionsDigest, Error)));
		ASSERT_THAT(IsTrue(DimensionsDigest != Baseline));

		USeinLevelDataTestDouble* CellSize =
			MakeGrid(UnrealWorld, 1);
		CellSize->TestCellSize = FFixedPoint::FromInt(101);
		FGuid CellSizeDigest;
		ASSERT_THAT(IsTrue(AdoptAndDigest(
			*CellSize, CellSizeDigest, Error)));
		ASSERT_THAT(IsTrue(CellSizeDigest != Baseline));

		USeinLevelDataTestDouble* Origin =
			MakeGrid(UnrealWorld, 1);
		Origin->TestOrigin.X += FFixedPoint::One;
		FGuid OriginDigest;
		ASSERT_THAT(IsTrue(AdoptAndDigest(
			*Origin, OriginDigest, Error)));
		ASSERT_THAT(IsTrue(OriginDigest != Baseline));

		USeinLevelDataTestDouble* Cost =
			MakeGrid(UnrealWorld, 2);
		FGuid CostDigest;
		ASSERT_THAT(IsTrue(AdoptAndDigest(
			*Cost, CostDigest, Error)));
		ASSERT_THAT(IsTrue(CostDigest != Baseline));

		USeinLevelDataTestDouble* Height =
			MakeGrid(UnrealWorld, 1);
		Height->TestSurfaces[0].Height =
			FFixedPoint::FromInt(1);
		FGuid HeightDigest;
		ASSERT_THAT(IsTrue(AdoptAndDigest(
			*Height, HeightDigest, Error)));
		ASSERT_THAT(IsTrue(HeightDigest != Baseline));

		USeinLevelDataTestDouble* Terrain =
			MakeGrid(UnrealWorld, 1);
		Terrain->TestSurfaces[0].TerrainTypeIndex = 1;
		FGuid TerrainDigest;
		ASSERT_THAT(IsTrue(AdoptAndDigest(
			*Terrain, TerrainDigest, Error)));
		ASSERT_THAT(IsTrue(TerrainDigest != Baseline));

		USeinLevelDataTestDouble* Connections =
			MakeGrid(UnrealWorld, 1);
		Connections->LayerChannels.FindChecked(
			FName(TEXT("Nav")))[4] = 1;
		FGuid ConnectionsDigest;
		ASSERT_THAT(IsTrue(AdoptAndDigest(
			*Connections, ConnectionsDigest, Error)));
		ASSERT_THAT(IsTrue(ConnectionsDigest != Baseline));

		USeinNavigationAStar* EmptyNavigation =
			NewObject<USeinNavigationAStar>(&UnrealWorld);
		FGuid EmptyDigest;
		ASSERT_THAT(IsTrue(
			EmptyNavigation->ComputeStaticEnvironmentDigest(
				EmptyDigest, Error)));
		ASSERT_THAT(IsTrue(EmptyDigest != Baseline));

		USeinClaimedAStarNavigationTestDouble*
			ClassVariantNavigation =
				NewObject<
					USeinClaimedAStarNavigationTestDouble>(
						&UnrealWorld);
		FGuid ClassVariantDigest;
		ASSERT_THAT(IsTrue(
			ClassVariantNavigation->
				ComputeStaticEnvironmentDigest(
					ClassVariantDigest, Error)));
		ASSERT_THAT(IsTrue(
			ClassVariantDigest != EmptyDigest));

		ASSERT_THAT(IsTrue(AdoptAndDigest(
			*Same, SameDigest, Error)));
		ASSERT_THAT(IsTrue(SameDigest == Baseline));

		const int32 SavedHeuristic =
			Navigation->AStarHeuristicWeightPercent;
		Navigation->AStarHeuristicWeightPercent =
			SavedHeuristic + 1;
		FGuid HeuristicDigest;
		ASSERT_THAT(IsTrue(
			Navigation->ComputeStaticEnvironmentDigest(
				HeuristicDigest, Error)));
		Navigation->AStarHeuristicWeightPercent =
			SavedHeuristic;
		ASSERT_THAT(IsTrue(HeuristicDigest != Baseline));

		const int32 SavedMaxIterations =
			Navigation->AStarMaxIterations;
		Navigation->AStarMaxIterations =
			SavedMaxIterations + 1;
		FGuid MaxIterationsDigest;
		ASSERT_THAT(IsTrue(
			Navigation->ComputeStaticEnvironmentDigest(
				MaxIterationsDigest, Error)));
		Navigation->AStarMaxIterations =
			SavedMaxIterations;
		ASSERT_THAT(IsTrue(
			MaxIterationsDigest != Baseline));
	}

	TEST(CustomNavigationWithoutStaticCoverageFailsClosed,
		"SeinARTS.Unit.Navigation.CanonicalState")
	{
		FActorTestSpawner Spawner;
		UWorld& UnrealWorld = Spawner.GetWorld();
		FString Error;
		FGuid Digest;

		USeinOpaqueRuntimeNavigationTestDouble* Opaque =
			NewObject<USeinOpaqueRuntimeNavigationTestDouble>(
				&UnrealWorld);
		ASSERT_THAT(IsFalse(
			Opaque->ComputeStaticEnvironmentDigest(
				Digest, Error)));
		ASSERT_THAT(IsTrue(Error.Contains(
			TEXT("does not provide an explicit exact static-environment digest"))));

		USeinInheritedAStarNavigationTestDouble* Inherited =
			NewObject<USeinInheritedAStarNavigationTestDouble>(
				&UnrealWorld);
		Error.Reset();
		ASSERT_THAT(IsFalse(
			Inherited->ComputeStaticEnvironmentDigest(
				Digest, Error)));
		ASSERT_THAT(IsTrue(Error.Contains(
			TEXT("must override ComputeStaticEnvironmentDigest"))));

		USeinClaimedAStarNavigationTestDouble* Claimed =
			NewObject<USeinClaimedAStarNavigationTestDouble>(
				&UnrealWorld);
		Error.Reset();
		ASSERT_THAT(IsTrue(
			Claimed->ComputeStaticEnvironmentDigest(
				Digest, Error)));
		ASSERT_THAT(IsTrue(Digest.IsValid()));
	}

	TEST(BootstrapRejectsCustomNavigationWithoutExactStateClaim,
		"SeinARTS.Unit.Navigation.CanonicalState")
	{
		FScopedNavigationClassOverride NavigationClass(
			USeinStaticDigestOnlyNavigationTestDouble::
				StaticClass());
		FActorTestSpawner Spawner;
		UWorld& UnrealWorld = Spawner.GetWorld();
		USeinWorldSubsystem* World =
			UnrealWorld.GetSubsystem<USeinWorldSubsystem>();
		USeinNavigationSubsystem* Navigation =
			UnrealWorld.GetSubsystem<USeinNavigationSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsNotNull(Navigation));
		ASSERT_THAT(IsNotNull(Cast<
			USeinStaticDigestOnlyNavigationTestDouble>(
				Navigation->GetNavigation())));

		TestRunner->AddExpectedError(
			TEXT("does not explicitly claim exact mutable-state coverage"),
			EAutomationExpectedErrorFlags::Contains,
			2,
			false);
		TestRunner->AddExpectedError(
			TEXT("SeinMatchBootstrap: transaction closed (failed)"),
			EAutomationExpectedErrorFlags::Contains,
			1,
			false);
		FString Error;
		ASSERT_THAT(IsFalse(StartNavigationStateWorld(
			*World,
			TEXT("NavigationState.UnclaimedBootstrap"),
			Error)));
		ASSERT_THAT(IsTrue(Error.Contains(
			TEXT("does not explicitly claim exact mutable-state coverage"))));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(
				ESeinMatchBootstrapState::Failed),
			static_cast<uint8>(
				World->GetMatchBootstrapState())));
		ASSERT_THAT(IsFalse(World->IsSimulationRunning()));
	}

	TEST(BootstrapAcceptsExplicitStatelessNavigationClaim,
		"SeinARTS.Unit.Navigation.CanonicalState")
	{
		FScopedNavigationClassOverride NavigationClass(
			USeinStatelessClaimedNavigationTestDouble::
				StaticClass());
		FActorTestSpawner Spawner;
		UWorld& UnrealWorld = Spawner.GetWorld();
		USeinWorldSubsystem* World =
			UnrealWorld.GetSubsystem<USeinWorldSubsystem>();
		USeinNavigationSubsystem* Navigation =
			UnrealWorld.GetSubsystem<USeinNavigationSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsNotNull(Navigation));
		ASSERT_THAT(IsNotNull(Cast<
			USeinStatelessClaimedNavigationTestDouble>(
				Navigation->GetNavigation())));

		FString Error;
		ASSERT_THAT(IsTrue(StartNavigationStateWorld(
			*World,
			TEXT("NavigationState.StatelessBootstrap"),
			Error)));
		ASSERT_THAT(IsTrue(
			World->GetCanonicalStateContractDigest().IsValid()));
		ASSERT_THAT(IsTrue(World->IsSimulationRunning()));
		World->StopSimulation();
	}

	TEST(BootstrapRejectsMissingNavigationStateContributor,
		"SeinARTS.Unit.Navigation.CanonicalState")
	{
		FScopedNavigationClassOverride NavigationClass(
			USeinMissingSupplementalNavigationTestDouble::
				StaticClass());
		FActorTestSpawner Spawner;
		UWorld& UnrealWorld = Spawner.GetWorld();
		USeinWorldSubsystem* World =
			UnrealWorld.GetSubsystem<USeinWorldSubsystem>();
		USeinNavigationSubsystem* Navigation =
			UnrealWorld.GetSubsystem<USeinNavigationSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsNotNull(Navigation));
		ASSERT_THAT(IsNotNull(Cast<
			USeinMissingSupplementalNavigationTestDouble>(
				Navigation->GetNavigation())));

		TestRunner->AddExpectedError(
			TEXT("requires missing authoritative canonical-state contributor"),
			EAutomationExpectedErrorFlags::Contains,
			2,
			false);
		TestRunner->AddExpectedError(
			TEXT("SeinMatchBootstrap: transaction closed (failed)"),
			EAutomationExpectedErrorFlags::Contains,
			1,
			false);
		FString Error;
		ASSERT_THAT(IsFalse(StartNavigationStateWorld(
			*World,
			TEXT("NavigationState.MissingSupplementalBootstrap"),
			Error)));
		ASSERT_THAT(IsTrue(Error.Contains(
			TEXT("requires missing authoritative canonical-state contributor"))));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(
				ESeinMatchBootstrapState::Failed),
			static_cast<uint8>(
				World->GetMatchBootstrapState())));
		ASSERT_THAT(IsFalse(World->IsSimulationRunning()));
	}

	TEST(BootstrapRejectsClaimedNavigationWithInvalidDigest,
		"SeinARTS.Unit.Navigation.CanonicalState")
	{
		FScopedNavigationClassOverride NavigationClass(
			USeinInvalidDigestClaimedNavigationTestDouble::
				StaticClass());
		FActorTestSpawner Spawner;
		UWorld& UnrealWorld = Spawner.GetWorld();
		USeinWorldSubsystem* World =
			UnrealWorld.GetSubsystem<USeinWorldSubsystem>();
		USeinNavigationSubsystem* Navigation =
			UnrealWorld.GetSubsystem<USeinNavigationSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsNotNull(Navigation));
		ASSERT_THAT(IsNotNull(Cast<
			USeinInvalidDigestClaimedNavigationTestDouble>(
				Navigation->GetNavigation())));

		TestRunner->AddExpectedError(
			TEXT("returned an invalid static-environment digest"),
			EAutomationExpectedErrorFlags::Contains,
			2,
			false);
		TestRunner->AddExpectedError(
			TEXT("SeinMatchBootstrap: transaction closed (failed)"),
			EAutomationExpectedErrorFlags::Contains,
			1,
			false);
		FString Error;
		ASSERT_THAT(IsFalse(StartNavigationStateWorld(
			*World,
			TEXT("NavigationState.InvalidDigestBootstrap"),
			Error)));
		ASSERT_THAT(IsTrue(Error.Contains(
			TEXT("returned an invalid static-environment digest"))));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(
				ESeinMatchBootstrapState::Failed),
			static_cast<uint8>(
				World->GetMatchBootstrapState())));
		ASSERT_THAT(IsFalse(World->IsSimulationRunning()));
	}

	TEST(DisabledNavigationHasStableExplicitWorldBinding,
		"SeinARTS.Unit.Navigation.CanonicalState")
	{
		USeinARTSCoreSettings* Settings =
			GetMutableDefault<USeinARTSCoreSettings>();
		ASSERT_THAT(IsNotNull(Settings));
		const FSoftClassPath SavedNavigationClass =
			Settings->NavigationClass;
		Settings->NavigationClass.Reset();
		ON_SCOPE_EXIT
		{
			Settings->NavigationClass =
				SavedNavigationClass;
		};

		FActorTestSpawner Spawner;
		UWorld& UnrealWorld = Spawner.GetWorld();
		USeinWorldSubsystem* World =
			UnrealWorld.GetSubsystem<USeinWorldSubsystem>();
		USeinNavigationSubsystem* Navigation =
			UnrealWorld.GetSubsystem<USeinNavigationSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsNotNull(Navigation));
		ASSERT_THAT(IsNull(Navigation->GetNavigation()));

		FString Error;
		ASSERT_THAT(IsTrue(StartNavigationStateWorld(
			*World,
			TEXT("NavigationState.DisabledBinding"),
			Error)));
		ASSERT_THAT(IsTrue(
			World->GetCanonicalStateContractDigest().IsValid()));
		FGuid FirstRoot;
		FGuid SecondRoot;
		ASSERT_THAT(IsTrue(
			World->ComputeCanonicalStateRoot(
				FirstRoot, Error)));
		ASSERT_THAT(IsTrue(
			World->ComputeCanonicalStateRoot(
				SecondRoot, Error)));
		ASSERT_THAT(IsTrue(FirstRoot == SecondRoot));
		World->StopSimulation();
	}

	TEST(NavigationGridChangesMatchStateContract,
		"SeinARTS.Unit.Navigation.CanonicalState")
	{
		FGuid GridAContract;
		{
			FActorTestSpawner Spawner;
			UWorld& UnrealWorld = Spawner.GetWorld();
			USeinWorldSubsystem* World =
				UnrealWorld.GetSubsystem<USeinWorldSubsystem>();
			USeinNavigationSubsystem* Navigation =
				UnrealWorld.GetSubsystem<USeinNavigationSubsystem>();
			ASSERT_THAT(IsNotNull(World));
			ASSERT_THAT(IsNotNull(Navigation));
			ASSERT_THAT(IsNotNull(ConfigureGrid(
				UnrealWorld, *Navigation, 1)));
			FString Error;
			ASSERT_THAT(IsTrue(StartNavigationStateWorld(
				*World,
				TEXT("NavigationState.StaticContract"),
				Error)));
			GridAContract =
				World->GetCanonicalStateContractDigest();
			ASSERT_THAT(IsTrue(GridAContract.IsValid()));
			World->StopSimulation();
		}

		FActorTestSpawner Spawner;
		UWorld& UnrealWorld = Spawner.GetWorld();
		USeinWorldSubsystem* World =
			UnrealWorld.GetSubsystem<USeinWorldSubsystem>();
		USeinNavigationSubsystem* Navigation =
			UnrealWorld.GetSubsystem<USeinNavigationSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsNotNull(Navigation));
		ASSERT_THAT(IsNotNull(ConfigureGrid(
			UnrealWorld, *Navigation, 2)));
		FString Error;
		ASSERT_THAT(IsTrue(StartNavigationStateWorld(
			*World,
			TEXT("NavigationState.StaticContract"),
			Error)));
		const FGuid GridBContract =
			World->GetCanonicalStateContractDigest();
		ASSERT_THAT(IsTrue(GridBContract.IsValid()));
		ASSERT_THAT(IsTrue(GridBContract != GridAContract));
		World->StopSimulation();
	}

	TEST(PostFreezeNavigationAdoptionIsRejectedBeforeWrite,
		"SeinARTS.Unit.Navigation.CanonicalState")
	{
		FActorTestSpawner Spawner;
		UWorld& UnrealWorld = Spawner.GetWorld();
		USeinWorldSubsystem* World =
			UnrealWorld.GetSubsystem<USeinWorldSubsystem>();
		USeinNavigationSubsystem* NavigationSubsystem =
			UnrealWorld.GetSubsystem<USeinNavigationSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsNotNull(NavigationSubsystem));
		USeinNavigationAStar* Navigation = ConfigureGrid(
			UnrealWorld, *NavigationSubsystem, 1);
		ASSERT_THAT(IsNotNull(Navigation));

		FString Error;
		ASSERT_THAT(IsTrue(StartNavigationStateWorld(
			*World,
			TEXT("NavigationState.GuardedAdoption"),
			Error)));
		FGuid Before;
		ASSERT_THAT(IsTrue(
			Navigation->ComputeStaticEnvironmentDigest(
				Before, Error)));

		TestRunner->AddExpectedError(
			TEXT("Navigation substrate adoption is not legal"),
			EAutomationExpectedErrorFlags::Contains,
			1,
			false);
		ASSERT_THAT(IsTrue(Navigation->LoadFromSubstrate(
			*MakeGrid(UnrealWorld, 2)).IsRejected()));

		FGuid After;
		ASSERT_THAT(IsTrue(
			Navigation->ComputeStaticEnvironmentDigest(
				After, Error)));
		ASSERT_THAT(IsTrue(After == Before));
		FGuid Root;
		ASSERT_THAT(IsTrue(
			World->ComputeCanonicalStateRoot(Root, Error)));
		ASSERT_THAT(IsTrue(World->IsSimulationRunning()));
		World->StopSimulation();
	}

	TEST(PostFreezeAStarTuningMutationFailStopsBeforePathQueries,
		"SeinARTS.Unit.Navigation.CanonicalState")
	{
		FActorTestSpawner Spawner;
		UWorld& UnrealWorld = Spawner.GetWorld();
		USeinWorldSubsystem* World =
			UnrealWorld.GetSubsystem<USeinWorldSubsystem>();
		USeinNavigationSubsystem* NavigationSubsystem =
			UnrealWorld.GetSubsystem<USeinNavigationSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsNotNull(NavigationSubsystem));
		USeinNavigationAStar* Navigation = ConfigureGrid(
			UnrealWorld, *NavigationSubsystem, 1);
		ASSERT_THAT(IsNotNull(Navigation));

		FString Error;
		ASSERT_THAT(IsTrue(StartNavigationStateWorld(
			*World,
			TEXT("NavigationState.TuningFailStop"),
			Error)));
		FGuid FrozenDigest;
		ASSERT_THAT(IsTrue(
			Navigation->ComputeStaticEnvironmentDigest(
				FrozenDigest, Error)));

		const int32 SavedHeuristic =
			Navigation->AStarHeuristicWeightPercent;
		ON_SCOPE_EXIT
		{
			Navigation->AStarHeuristicWeightPercent =
				SavedHeuristic;
		};
		Navigation->AStarHeuristicWeightPercent =
			SavedHeuristic + 1;

		FGuid DriftedDigest;
		ASSERT_THAT(IsTrue(
			Navigation->ComputeStaticEnvironmentDigest(
				DriftedDigest, Error)));
		ASSERT_THAT(IsTrue(DriftedDigest != FrozenDigest));

		TestRunner->AddExpectedError(
			TEXT("Navigation implementation or static environment changed"),
			EAutomationExpectedErrorFlags::Contains,
			2,
			false);
		FTSTicker::GetCoreTicker().Tick(
			World->GetFixedDeltaTimeSeconds());

		ASSERT_THAT(IsFalse(World->IsSimulationRunning()));
		ASSERT_THAT(IsFalse(
			World->IsExecutionTopologyValid()));
		ASSERT_THAT(IsTrue(
			World->GetExecutionTopologyFailureReason().Contains(
				TEXT("Navigation implementation or static environment changed"))));
	}

	TEST(PostFreezeInPlaceGridMutationFailStopsDespiteCachedDigest,
		"SeinARTS.Unit.Navigation.CanonicalState")
	{
		FActorTestSpawner Spawner;
		UWorld& UnrealWorld = Spawner.GetWorld();
		USeinWorldSubsystem* World =
			UnrealWorld.GetSubsystem<USeinWorldSubsystem>();
		USeinNavigationSubsystem* NavigationSubsystem =
			UnrealWorld.GetSubsystem<USeinNavigationSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsNotNull(NavigationSubsystem));
		USeinNavigationAStar* Navigation = ConfigureGrid(
			UnrealWorld, *NavigationSubsystem, 1);
		ASSERT_THAT(IsNotNull(Navigation));

		FString Error;
		ASSERT_THAT(IsTrue(StartNavigationStateWorld(
			*World,
			TEXT("NavigationState.GenerationFailStop"),
			Error)));
		FGuid FrozenDigest;
		ASSERT_THAT(IsTrue(
			Navigation->ComputeStaticEnvironmentDigest(
				FrozenDigest, Error)));

		// An in-place cell write leaves array lengths AND the cached content
		// digest untouched — only the mutation counter can expose it.
		FNavigationCanonicalStateTestAccess::
			BumpStaticGridGeneration(*Navigation);
		FGuid AfterMutation;
		ASSERT_THAT(IsTrue(
			Navigation->ComputeStaticEnvironmentDigest(
				AfterMutation, Error)));
		ASSERT_THAT(IsTrue(AfterMutation == FrozenDigest));

		TestRunner->AddExpectedError(
			TEXT("Navigation static topology mutated in place"),
			EAutomationExpectedErrorFlags::Contains,
			2,
			false);
		FTSTicker::GetCoreTicker().Tick(
			World->GetFixedDeltaTimeSeconds());

		ASSERT_THAT(IsFalse(World->IsSimulationRunning()));
		ASSERT_THAT(IsFalse(
			World->IsExecutionTopologyValid()));
		ASSERT_THAT(IsTrue(
			World->GetExecutionTopologyFailureReason().Contains(
				TEXT("Navigation static topology mutated in place"))));
	}

	TEST(UnguardedSubstrateMutationFailStopsFrozenNavigation,
		"SeinARTS.Unit.Navigation.CanonicalState")
	{
		FActorTestSpawner Spawner;
		UWorld& UnrealWorld = Spawner.GetWorld();
		USeinWorldSubsystem* World =
			UnrealWorld.GetSubsystem<USeinWorldSubsystem>();
		USeinNavigationSubsystem* NavigationSubsystem =
			UnrealWorld.GetSubsystem<USeinNavigationSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsNotNull(NavigationSubsystem));
		USeinNavigationAStar* Navigation = ConfigureGrid(
			UnrealWorld, *NavigationSubsystem, 1);
		ASSERT_THAT(IsNotNull(Navigation));

		FString Error;
		ASSERT_THAT(IsTrue(StartNavigationStateWorld(
			*World,
			TEXT("NavigationState.FailStop"),
			Error)));
		FGuid Before;
		ASSERT_THAT(IsTrue(
			Navigation->ComputeStaticEnvironmentDigest(
				Before, Error)));

		TestRunner->AddExpectedError(
			TEXT("shared level-data substrate mutated"),
			EAutomationExpectedErrorFlags::Contains,
			2,
			false);
		USeinLevelDataTestDouble* ChangedSubstrate =
			MakeGrid(UnrealWorld, 2);
		FNavigationCanonicalStateTestAccess::
			SignalSubstrateMutation(
				*NavigationSubsystem,
				*ChangedSubstrate);

		FGuid After;
		ASSERT_THAT(IsTrue(
			Navigation->ComputeStaticEnvironmentDigest(
				After, Error)));
		ASSERT_THAT(IsTrue(After == Before));
		ASSERT_THAT(IsFalse(World->IsSimulationRunning()));
		ASSERT_THAT(IsFalse(
			World->IsExecutionTopologyValid()));

		FGuid Root;
		ASSERT_THAT(IsFalse(
			World->ComputeCanonicalStateRoot(Root, Error)));
		ASSERT_THAT(IsFalse(Error.IsEmpty()));
	}

	TEST(NavigationContinuationChangesCanonicalWorldRoot,
		"SeinARTS.Unit.Navigation.CanonicalState")
	{
		FActorTestSpawner Spawner;
		UWorld& UnrealWorld = Spawner.GetWorld();
		USeinWorldSubsystem* World =
			UnrealWorld.GetSubsystem<USeinWorldSubsystem>();
		USeinNavigationSubsystem* Navigation =
			UnrealWorld.GetSubsystem<USeinNavigationSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsNotNull(Navigation));

		FString Error;
		ASSERT_THAT(IsTrue(StartNavigationStateWorld(
			*World, TEXT("NavigationState.Root"), Error)));

		FGuid EmptyRoot;
		ASSERT_THAT(IsTrue(
			World->ComputeCanonicalStateRoot(EmptyRoot, Error)));
		FNavigationCanonicalStateTestAccess::Seed(*Navigation);

		FGuid SeededRoot;
		ASSERT_THAT(IsTrue(
			World->ComputeCanonicalStateRoot(SeededRoot, Error)));
		ASSERT_THAT(IsTrue(SeededRoot.IsValid()));
		ASSERT_THAT(IsTrue(SeededRoot != EmptyRoot));

		World->StopSimulation();
	}

	TEST(NavigationCaptureAcceptsCompletePathWithNudgedTerminal,
		"SeinARTS.Unit.Navigation.CanonicalState")
	{
		// Regression (2026-08 PIE session kill): the exact final waypoint is
		// pipeline-internal, NOT a canonical invariant — a complete ready
		// path legitimately ends near-but-not-at Request.End whenever
		// PushWaypointsAwayFromWalls nudges a terminal clicked near a wall
		// edge (only authoritative cover-slot destinations restore the exact
		// End). Capture must accept such a path; asserting terminal equality
		// killed healthy live sessions at the first gossip checkpoint.
		FActorTestSpawner Spawner;
		UWorld& UnrealWorld = Spawner.GetWorld();
		USeinWorldSubsystem* World =
			UnrealWorld.GetSubsystem<USeinWorldSubsystem>();
		USeinNavigationSubsystem* Navigation =
			UnrealWorld.GetSubsystem<USeinNavigationSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsNotNull(Navigation));

		FString Error;
		ASSERT_THAT(IsTrue(StartNavigationStateWorld(
			*World,
			TEXT("NavigationState.WrongReadyTerminal"),
			Error)));
		FNavigationCanonicalStateTestAccess::
			SeedCompletePathWithWrongTerminal(*Navigation);

		FSeinWorldSnapshot Snapshot;
		World->CaptureSnapshot(Snapshot);
		ASSERT_THAT(IsTrue(Snapshot.SnapshotVersion != 0));
		ASSERT_THAT(IsTrue(World->IsSimulationRunning()));
		World->StopSimulation();
	}

	TEST(NavigationRestoreRejectsArcWithMalformedFromRadius,
		"SeinARTS.Unit.Navigation.CanonicalState")
	{
		FSeinWorldSnapshot Snapshot;
		FString Error;
		ASSERT_THAT(IsTrue(CaptureValidArcSnapshot(
			Snapshot, Error)));
		ASSERT_THAT(IsTrue(MutateNavigationReadyPath(
			Snapshot,
			[](
				FSeinPathRequest& Request,
				FSeinPath& Path)
			{
				const FFixedVector BadFrom(
					FFixedPoint::FromInt(125),
					FFixedPoint::Zero,
					FFixedPoint::Zero);
				Request.Start = BadFrom;
				Path.Waypoints[0] = BadFrom;
				Path.Segments[0].From = BadFrom;
			},
			Error)));

		FActorTestSpawner DestinationSpawner;
		UWorld& DestinationUnrealWorld =
			DestinationSpawner.GetWorld();
		USeinWorldSubsystem* Destination =
			DestinationUnrealWorld.GetSubsystem<
				USeinWorldSubsystem>();
		USeinNavigationSubsystem* DestinationNavigation =
			DestinationUnrealWorld.GetSubsystem<
				USeinNavigationSubsystem>();
		ASSERT_THAT(IsNotNull(Destination));
		ASSERT_THAT(IsNotNull(DestinationNavigation));
		FNavigationCanonicalStateTestAccess::SeedRestoreSentinel(
			*DestinationNavigation);
		const FNavigationCanonicalStateTestAccess::
			FContinuationSnapshot Sentinel =
				FNavigationCanonicalStateTestAccess::
					CaptureContinuation(
						*DestinationNavigation);

		TestRunner->AddExpectedError(
			TEXT("arc From endpoint outside its declared planar radius"),
			EAutomationExpectedErrorFlags::Contains,
			1,
			false);
		ASSERT_THAT(IsFalse(
			SeinTestSnapshotRestore::RestoreTrusted(
				*Destination, Snapshot)));
		ASSERT_THAT(IsFalse(
			Destination->IsSimulationRunning()));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(
				ESeinMatchBootstrapState::Awaiting),
			static_cast<uint8>(
				Destination->GetMatchBootstrapState())));
		ASSERT_THAT(AreEqual(
			0, Destination->GetCurrentTick()));
		ASSERT_THAT(IsTrue(
			Destination->IsExecutionTopologyValid()));
		ASSERT_THAT(IsTrue(
			FNavigationCanonicalStateTestAccess::
				MatchesContinuation(
					*DestinationNavigation,
					Sentinel)));
	}

	TEST(NavigationRestoreRejectsArcWithMalformedToRadius,
		"SeinARTS.Unit.Navigation.CanonicalState")
	{
		FSeinWorldSnapshot Snapshot;
		FString Error;
		ASSERT_THAT(IsTrue(CaptureValidArcSnapshot(
			Snapshot, Error)));
		ASSERT_THAT(IsTrue(MutateNavigationReadyPath(
			Snapshot,
			[](
				FSeinPathRequest& Request,
				FSeinPath& Path)
			{
				const FFixedVector BadTo(
					FFixedPoint::Zero,
					FFixedPoint::FromInt(125),
					FFixedPoint::Zero);
				Request.End = BadTo;
				Path.Waypoints.Last() = BadTo;
				Path.Segments[0].To = BadTo;
			},
			Error)));

		FActorTestSpawner DestinationSpawner;
		UWorld& DestinationUnrealWorld =
			DestinationSpawner.GetWorld();
		USeinWorldSubsystem* Destination =
			DestinationUnrealWorld.GetSubsystem<
				USeinWorldSubsystem>();
		USeinNavigationSubsystem* DestinationNavigation =
			DestinationUnrealWorld.GetSubsystem<
				USeinNavigationSubsystem>();
		ASSERT_THAT(IsNotNull(Destination));
		ASSERT_THAT(IsNotNull(DestinationNavigation));
		FNavigationCanonicalStateTestAccess::SeedRestoreSentinel(
			*DestinationNavigation);
		const FNavigationCanonicalStateTestAccess::
			FContinuationSnapshot Sentinel =
				FNavigationCanonicalStateTestAccess::
					CaptureContinuation(
						*DestinationNavigation);

		TestRunner->AddExpectedError(
			TEXT("arc To endpoint outside its declared planar radius"),
			EAutomationExpectedErrorFlags::Contains,
			1,
			false);
		ASSERT_THAT(IsFalse(
			SeinTestSnapshotRestore::RestoreTrusted(
				*Destination, Snapshot)));
		ASSERT_THAT(IsFalse(
			Destination->IsSimulationRunning()));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(
				ESeinMatchBootstrapState::Awaiting),
			static_cast<uint8>(
				Destination->GetMatchBootstrapState())));
		ASSERT_THAT(AreEqual(
			0, Destination->GetCurrentTick()));
		ASSERT_THAT(IsTrue(
			Destination->IsExecutionTopologyValid()));
		ASSERT_THAT(IsTrue(
			FNavigationCanonicalStateTestAccess::
				MatchesContinuation(
					*DestinationNavigation,
					Sentinel)));
	}

	TEST(NavigationRestoreRejectsArcWhoseSignedSweepMissesEndpoint,
		"SeinARTS.Unit.Navigation.CanonicalState")
	{
		FSeinWorldSnapshot Snapshot;
		FString Error;
		ASSERT_THAT(IsTrue(CaptureValidArcSnapshot(
			Snapshot, Error)));
		ASSERT_THAT(IsTrue(MutateNavigationReadyPath(
			Snapshot,
			[](
				FSeinPathRequest&,
				FSeinPath& Path)
			{
				Path.Segments[0].SweepAngle =
					-FFixedPoint::HalfPi;
			},
			Error)));

		FActorTestSpawner DestinationSpawner;
		UWorld& DestinationUnrealWorld =
			DestinationSpawner.GetWorld();
		USeinWorldSubsystem* Destination =
			DestinationUnrealWorld.GetSubsystem<
				USeinWorldSubsystem>();
		USeinNavigationSubsystem* DestinationNavigation =
			DestinationUnrealWorld.GetSubsystem<
				USeinNavigationSubsystem>();
		ASSERT_THAT(IsNotNull(Destination));
		ASSERT_THAT(IsNotNull(DestinationNavigation));
		FNavigationCanonicalStateTestAccess::SeedRestoreSentinel(
			*DestinationNavigation);
		const FNavigationCanonicalStateTestAccess::
			FContinuationSnapshot Sentinel =
				FNavigationCanonicalStateTestAccess::
					CaptureContinuation(
						*DestinationNavigation);

		TestRunner->AddExpectedError(
			TEXT("arc signed sweep that does not reach its declared endpoint"),
			EAutomationExpectedErrorFlags::Contains,
			1,
			false);
		ASSERT_THAT(IsFalse(
			SeinTestSnapshotRestore::RestoreTrusted(
				*Destination, Snapshot)));
		ASSERT_THAT(IsFalse(
			Destination->IsSimulationRunning()));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(
				ESeinMatchBootstrapState::Awaiting),
			static_cast<uint8>(
				Destination->GetMatchBootstrapState())));
		ASSERT_THAT(AreEqual(
			0, Destination->GetCurrentTick()));
		ASSERT_THAT(IsTrue(
			Destination->IsExecutionTopologyValid()));
		ASSERT_THAT(IsTrue(
			FNavigationCanonicalStateTestAccess::
				MatchesContinuation(
					*DestinationNavigation,
					Sentinel)));
	}

	TEST(NavigationRestoreRejectsNonArcDistanceOverflow,
		"SeinARTS.Unit.Navigation.CanonicalState")
	{
		FSeinWorldSnapshot Snapshot;
		FString Error;
		ASSERT_THAT(IsTrue(CaptureValidArcSnapshot(
			Snapshot, Error)));
		ASSERT_THAT(IsTrue(MutateNavigationReadyPath(
			Snapshot,
			[](
				FSeinPathRequest& Request,
				FSeinPath& Path)
			{
				const FFixedVector Start =
					FFixedVector::ZeroVector;
				const FFixedVector End(
					FFixedPoint::FromInt(65536),
					FFixedPoint::Zero,
					FFixedPoint::Zero);
				Request.Start = Start;
				Request.End = End;
				Path.Waypoints = { Start, End };
				FSeinPathSegment& Segment =
					Path.Segments[0];
				Segment.Type =
					ESeinPathSegmentType::Straight;
				Segment.From = Start;
				Segment.To = End;
				Segment.Center =
					FFixedVector::ZeroVector;
				Segment.Radius =
					FFixedPoint::Zero;
				Segment.SweepAngle =
					FFixedPoint::Zero;
				// The legacy unchecked square wraps this exact
				// 65536-unit span to zero in 32.32.
				Path.TotalCost = FFixedPoint::Zero;
			},
			Error)));

		FActorTestSpawner DestinationSpawner;
		UWorld& DestinationUnrealWorld =
			DestinationSpawner.GetWorld();
		USeinWorldSubsystem* Destination =
			DestinationUnrealWorld.GetSubsystem<
				USeinWorldSubsystem>();
		USeinNavigationSubsystem* DestinationNavigation =
			DestinationUnrealWorld.GetSubsystem<
				USeinNavigationSubsystem>();
		ASSERT_THAT(IsNotNull(Destination));
		ASSERT_THAT(IsNotNull(DestinationNavigation));
		FNavigationCanonicalStateTestAccess::
			SeedRestoreSentinel(
				*DestinationNavigation);
		const FNavigationCanonicalStateTestAccess::
			FContinuationSnapshot Sentinel =
				FNavigationCanonicalStateTestAccess::
					CaptureContinuation(
						*DestinationNavigation);

		TestRunner->AddExpectedError(
			TEXT("segment coordinates outside the representable distance domain"),
			EAutomationExpectedErrorFlags::Contains,
			1,
			false);
		ASSERT_THAT(IsFalse(
			SeinTestSnapshotRestore::RestoreTrusted(
				*Destination, Snapshot)));
		ASSERT_THAT(IsFalse(
			Destination->IsSimulationRunning()));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(
				ESeinMatchBootstrapState::Awaiting),
			static_cast<uint8>(
				Destination->GetMatchBootstrapState())));
		ASSERT_THAT(AreEqual(
			0, Destination->GetCurrentTick()));
		ASSERT_THAT(IsTrue(
			Destination->IsExecutionTopologyValid()));
		ASSERT_THAT(IsTrue(
			FNavigationCanonicalStateTestAccess::
				MatchesContinuation(
					*DestinationNavigation,
					Sentinel)));
	}

	TEST(NavigationRestoreAcceptsPartialFlagOnTerminalPath,
		"SeinARTS.Unit.Navigation.CanonicalState")
	{
		// Companion to NavigationCaptureAcceptsCompletePathWithNudgedTerminal:
		// the terminal waypoint's relation to Request.End is pipeline-internal
		// (wall-push nudging, same-cell partial upgrades), not a canonical
		// invariant — the shared validator must accept it on restore exactly
		// as it does on capture.
		FSeinWorldSnapshot Snapshot;
		FString Error;
		ASSERT_THAT(IsTrue(CaptureValidArcSnapshot(
			Snapshot, Error)));
		ASSERT_THAT(IsTrue(MutateNavigationReadyPath(
			Snapshot,
			[](
				FSeinPathRequest&,
				FSeinPath& Path)
			{
				Path.bIsPartial = true;
			},
			Error)));

		FActorTestSpawner DestinationSpawner;
		UWorld& DestinationUnrealWorld =
			DestinationSpawner.GetWorld();
		USeinWorldSubsystem* Destination =
			DestinationUnrealWorld.GetSubsystem<
				USeinWorldSubsystem>();
		USeinNavigationSubsystem* DestinationNavigation =
			DestinationUnrealWorld.GetSubsystem<
				USeinNavigationSubsystem>();
		ASSERT_THAT(IsNotNull(Destination));
		ASSERT_THAT(IsNotNull(DestinationNavigation));
		FNavigationCanonicalStateTestAccess::SeedRestoreSentinel(
			*DestinationNavigation);

		ASSERT_THAT(IsTrue(
			SeinTestSnapshotRestore::RestoreTrusted(
				*Destination, Snapshot)));
		Destination->StopSimulation();
	}

	TEST(NavigationContinuationRoundTripsThroughSnapshot,
		"SeinARTS.Determinism.Navigation.CanonicalState")
	{
		FActorTestSpawner SourceSpawner;
		UWorld& SourceUnrealWorld = SourceSpawner.GetWorld();
		USeinWorldSubsystem* Source =
			SourceUnrealWorld.GetSubsystem<USeinWorldSubsystem>();
		USeinNavigationSubsystem* SourceNavigation =
			SourceUnrealWorld.GetSubsystem<USeinNavigationSubsystem>();
		ASSERT_THAT(IsNotNull(Source));
		ASSERT_THAT(IsNotNull(SourceNavigation));

		FString Error;
		ASSERT_THAT(IsTrue(StartNavigationStateWorld(
			*Source, TEXT("NavigationState.RoundTrip"), Error)));
		FNavigationCanonicalStateTestAccess::Seed(*SourceNavigation);

		FGuid SourceRoot;
		ASSERT_THAT(IsTrue(
			Source->ComputeCanonicalStateRoot(SourceRoot, Error)));
		FSeinWorldSnapshot Snapshot;
		Source->CaptureSnapshot(Snapshot);
		ASSERT_THAT(AreEqual(
			FSeinWorldSnapshot::CurrentVersion,
			Snapshot.SnapshotVersion));
		Source->StopSimulation();

		FActorTestSpawner DestinationSpawner;
		UWorld& DestinationUnrealWorld = DestinationSpawner.GetWorld();
		USeinWorldSubsystem* Destination =
			DestinationUnrealWorld.GetSubsystem<USeinWorldSubsystem>();
		USeinNavigationSubsystem* DestinationNavigation =
			DestinationUnrealWorld.GetSubsystem<USeinNavigationSubsystem>();
		ASSERT_THAT(IsNotNull(Destination));
		ASSERT_THAT(IsNotNull(DestinationNavigation));
		ASSERT_THAT(IsTrue(
			SeinTestSnapshotRestore::RestoreTrusted(
				*Destination, Snapshot)));
		ASSERT_THAT(IsTrue(
			FNavigationCanonicalStateTestAccess::MatchesSeed(
				*DestinationNavigation)));

		FGuid DestinationRoot;
		ASSERT_THAT(IsTrue(
			Destination->ComputeCanonicalStateRoot(
				DestinationRoot, Error)));
		ASSERT_THAT(IsTrue(SourceRoot == DestinationRoot));

		Destination->StopSimulation();
	}

	TEST(NavigationLazyBudgetStateRoundTripsAfterIdleTick,
		"SeinARTS.Determinism.Navigation.CanonicalState")
	{
		FSeinWorldSnapshot Snapshot;
		const int32 RequestTick = 0;
		int32 CaptureTick = INDEX_NONE;
		{
			FActorTestSpawner SourceSpawner;
			UWorld& SourceUnrealWorld =
				SourceSpawner.GetWorld();
			USeinWorldSubsystem* Source =
				SourceUnrealWorld.GetSubsystem<
					USeinWorldSubsystem>();
			USeinNavigationSubsystem* SourceNavigation =
				SourceUnrealWorld.GetSubsystem<
					USeinNavigationSubsystem>();
			ASSERT_THAT(IsNotNull(Source));
			ASSERT_THAT(IsNotNull(SourceNavigation));

			FString Error;
			ASSERT_THAT(IsTrue(StartNavigationStateWorld(
				*Source,
				TEXT("NavigationState.LazyBudget"),
				Error)));
			ASSERT_THAT(AreEqual(
				RequestTick, Source->GetCurrentTick()));

			FSeinPathRequest Request;
			Request.Start = FFixedVector::ZeroVector;
			Request.End = FFixedVector(
				FFixedPoint::FromInt(100),
				FFixedPoint::Zero,
				FFixedPoint::Zero);
			FSeinPath IgnoredPath;
			SourceNavigation->RequestPath(
				Request, IgnoredPath);
			FNavigationCanonicalStateTestAccess::
				FContinuationSnapshot AtRequest =
					FNavigationCanonicalStateTestAccess::
						CaptureContinuation(
							*SourceNavigation);
			ASSERT_THAT(AreEqual(
				1, AtRequest.PathRequestsThisTick));
			ASSERT_THAT(AreEqual(
				RequestTick, AtRequest.LastResetTick));

			FTSTicker::GetCoreTicker().Tick(
				Source->GetFixedDeltaTimeSeconds());
			CaptureTick = Source->GetCurrentTick();
			ASSERT_THAT(IsTrue(
				CaptureTick > RequestTick));
			const FNavigationCanonicalStateTestAccess::
				FContinuationSnapshot BeforeCapture =
					FNavigationCanonicalStateTestAccess::
						CaptureContinuation(
							*SourceNavigation);
			ASSERT_THAT(AreEqual(
				1,
				BeforeCapture.PathRequestsThisTick));
			ASSERT_THAT(AreEqual(
				RequestTick,
				BeforeCapture.LastResetTick));

			Source->CaptureSnapshot(Snapshot);
			ASSERT_THAT(AreEqual(
				FSeinWorldSnapshot::CurrentVersion,
				Snapshot.SnapshotVersion));
			Source->StopSimulation();
		}

		FActorTestSpawner DestinationSpawner;
		UWorld& DestinationUnrealWorld =
			DestinationSpawner.GetWorld();
		USeinWorldSubsystem* Destination =
			DestinationUnrealWorld.GetSubsystem<
				USeinWorldSubsystem>();
		USeinNavigationSubsystem* DestinationNavigation =
			DestinationUnrealWorld.GetSubsystem<
				USeinNavigationSubsystem>();
		ASSERT_THAT(IsNotNull(Destination));
		ASSERT_THAT(IsNotNull(DestinationNavigation));
		ASSERT_THAT(IsTrue(
			SeinTestSnapshotRestore::RestoreTrusted(
				*Destination, Snapshot)));
		ASSERT_THAT(AreEqual(
			CaptureTick, Destination->GetCurrentTick()));

		const FNavigationCanonicalStateTestAccess::
			FContinuationSnapshot Restored =
				FNavigationCanonicalStateTestAccess::
					CaptureContinuation(
						*DestinationNavigation);
		ASSERT_THAT(AreEqual(
			1, Restored.PathRequestsThisTick));
		ASSERT_THAT(AreEqual(
			RequestTick, Restored.LastResetTick));

		FSeinPathRequest NextRequest;
		NextRequest.Start = FFixedVector::ZeroVector;
		NextRequest.End = FFixedVector(
			FFixedPoint::FromInt(200),
			FFixedPoint::Zero,
			FFixedPoint::Zero);
		FSeinPath IgnoredPath;
		DestinationNavigation->RequestPath(
			NextRequest, IgnoredPath);
		const FNavigationCanonicalStateTestAccess::
			FContinuationSnapshot AfterLazyReset =
				FNavigationCanonicalStateTestAccess::
					CaptureContinuation(
						*DestinationNavigation);
		ASSERT_THAT(AreEqual(
			1, AfterLazyReset.PathRequestsThisTick));
		ASSERT_THAT(AreEqual(
			CaptureTick,
			AfterLazyReset.LastResetTick));
		Destination->StopSimulation();
	}

	TEST(NavigationSnapshotRejectsDifferentStaticGridTransactionally,
		"SeinARTS.Determinism.Navigation.CanonicalState")
	{
		FSeinWorldSnapshot Snapshot;
		FGuid SourceRoot;
		{
			FActorTestSpawner SourceSpawner;
			UWorld& SourceUnrealWorld =
				SourceSpawner.GetWorld();
			USeinWorldSubsystem* Source =
				SourceUnrealWorld.GetSubsystem<
					USeinWorldSubsystem>();
			USeinNavigationSubsystem* SourceNavigation =
				SourceUnrealWorld.GetSubsystem<
					USeinNavigationSubsystem>();
			ASSERT_THAT(IsNotNull(Source));
			ASSERT_THAT(IsNotNull(SourceNavigation));
			ASSERT_THAT(IsNotNull(ConfigureGrid(
				SourceUnrealWorld, *SourceNavigation, 1)));

			FString Error;
			ASSERT_THAT(IsTrue(StartNavigationStateWorld(
				*Source,
				TEXT("NavigationState.GridMismatch"),
				Error)));
			FNavigationCanonicalStateTestAccess::Seed(
				*SourceNavigation);
			ASSERT_THAT(IsTrue(
				Source->ComputeCanonicalStateRoot(
					SourceRoot, Error)));
			Source->CaptureSnapshot(Snapshot);
			ASSERT_THAT(AreEqual(
				FSeinWorldSnapshot::CurrentVersion,
				Snapshot.SnapshotVersion));
			Source->StopSimulation();
		}

		FActorTestSpawner DestinationSpawner;
		UWorld& DestinationUnrealWorld =
			DestinationSpawner.GetWorld();
		USeinWorldSubsystem* Destination =
			DestinationUnrealWorld.GetSubsystem<
				USeinWorldSubsystem>();
		USeinNavigationSubsystem* DestinationSubsystem =
			DestinationUnrealWorld.GetSubsystem<
				USeinNavigationSubsystem>();
		ASSERT_THAT(IsNotNull(Destination));
		ASSERT_THAT(IsNotNull(DestinationSubsystem));
		USeinNavigationAStar* DestinationNavigation =
			ConfigureGrid(
				DestinationUnrealWorld,
				*DestinationSubsystem,
				2);
		ASSERT_THAT(IsNotNull(DestinationNavigation));
		FNavigationCanonicalStateTestAccess::
			SeedRestoreSentinel(*DestinationSubsystem);
		const FNavigationCanonicalStateTestAccess::
			FContinuationSnapshot DestinationSentinel =
				FNavigationCanonicalStateTestAccess::
					CaptureContinuation(
						*DestinationSubsystem);
		ASSERT_THAT(IsTrue(
			FNavigationCanonicalStateTestAccess::
				MatchesContinuation(
					*DestinationSubsystem,
					DestinationSentinel)));

		FString Error;
		FGuid Before;
		ASSERT_THAT(IsTrue(
			DestinationNavigation->
				ComputeStaticEnvironmentDigest(
					Before, Error)));

		TestRunner->AddExpectedError(
			TEXT("checkpoint contract is invalid"),
			EAutomationExpectedErrorFlags::Contains,
			1,
			false);
		ASSERT_THAT(IsFalse(
			SeinTestSnapshotRestore::RestoreTrusted(
				*Destination, Snapshot)));

		FGuid After;
		ASSERT_THAT(IsTrue(
			DestinationNavigation->
				ComputeStaticEnvironmentDigest(
					After, Error)));
		ASSERT_THAT(IsTrue(After == Before));
		ASSERT_THAT(IsFalse(
			Destination->IsSimulationRunning()));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(
				ESeinMatchBootstrapState::Awaiting),
			static_cast<uint8>(
				Destination->GetMatchBootstrapState())));
		ASSERT_THAT(AreEqual(0, Destination->GetCurrentTick()));
		ASSERT_THAT(IsFalse(
			Destination->GetCanonicalStateContractDigest()
				.IsValid()));
		ASSERT_THAT(IsTrue(
			Destination->IsExecutionTopologyValid()));
		ASSERT_THAT(IsTrue(
			FNavigationCanonicalStateTestAccess::
				MatchesContinuation(
					*DestinationSubsystem,
					DestinationSentinel)));

		ASSERT_THAT(IsNotNull(ConfigureGrid(
			DestinationUnrealWorld,
			*DestinationSubsystem,
			1)));
		ASSERT_THAT(IsTrue(
			FNavigationCanonicalStateTestAccess::
				MatchesContinuation(
					*DestinationSubsystem,
					DestinationSentinel)));
		ASSERT_THAT(IsTrue(
			SeinTestSnapshotRestore::RestoreTrusted(
				*Destination, Snapshot)));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(
				ESeinMatchBootstrapState::Consumed),
			static_cast<uint8>(
				Destination->GetMatchBootstrapState())));
		ASSERT_THAT(IsTrue(
			FNavigationCanonicalStateTestAccess::MatchesSeed(
				*DestinationSubsystem)));
		ASSERT_THAT(IsFalse(
			FNavigationCanonicalStateTestAccess::
				MatchesContinuation(
					*DestinationSubsystem,
					DestinationSentinel)));

		FGuid DestinationRoot;
		ASSERT_THAT(IsTrue(
			Destination->ComputeCanonicalStateRoot(
				DestinationRoot, Error)));
		ASSERT_THAT(IsTrue(DestinationRoot == SourceRoot));
		Destination->StopSimulation();
	}

	TEST(ModulePreUnloadReleaseSeversLiveNavigationSystems,
		"SeinARTS.Unit.Navigation.CanonicalState")
	{
		FActorTestSpawner Spawner;
		UWorld& UnrealWorld = Spawner.GetWorld();
		USeinWorldSubsystem* World =
			UnrealWorld.GetSubsystem<USeinWorldSubsystem>();
		USeinNavigationSubsystem* Navigation =
			UnrealWorld.GetSubsystem<USeinNavigationSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsNotNull(Navigation));
		ASSERT_THAT(IsNotNull(Navigation->GetNavigation()));

		FString Error;
		ASSERT_THAT(IsTrue(StartNavigationStateWorld(
			*World, TEXT("NavigationState.ModulePreUnload"), Error)));
		ASSERT_THAT(IsTrue(World->IsSimulationRunning()));

		TestRunner->AddExpectedError(
			TEXT("withdrew live state"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		Navigation->ReleaseModuleOwnedStateForModuleUnload();

		ASSERT_THAT(IsFalse(World->IsSimulationRunning()));
		ASSERT_THAT(IsFalse(World->IsExecutionTopologyValid()));
		ASSERT_THAT(IsNull(Navigation->GetNavigation()));

		// ModuleManager may invoke ordinary world teardown after its pre-unload
		// pass. The second release must not touch the dead system pointer.
		Navigation->ReleaseModuleOwnedStateForModuleUnload();
	}
}
