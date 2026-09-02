#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Components/SeinCoverPayload.h"
#include "Components/SeinExtentsPayload.h"
#include "Data/SeinMatchSettings.h"
#include "HAL/PlatformTime.h"
#include "Lib/SeinCommandBrokerBPFL.h"
#include "Resolvers/SeinCoverAwareDefaultBrokerResolver.h"
#include "Settings/PluginSettings.h"
#include "Settings/SeinARTSCoverSettings.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "System/SeinCoverDefault.h"
#include "System/SeinCoverSubsystem.h"
#include "System/SeinCoverSystem.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "Tags/SeinCoverGameplayTags.h"

namespace UE::SeinARTSExtensionTests
{
	namespace CoverFormationPreviewPerfLocal
	{
		constexpr int32 MaximumMembers = 128;
		constexpr int32 TimedSamples = 11;
		constexpr int32 ProviderColumns = 4;
		constexpr int32 ProviderCount = 16;
		constexpr int32 SlotsPerProvider = 8;
		const FFixedVector DenseTarget(
			FFixedPoint::FromInt(10000),
			FFixedPoint::FromInt(10000),
			FFixedPoint::Zero);
		const FFixedVector CoverlessTarget(
			FFixedPoint::FromInt(-10000),
			FFixedPoint::FromInt(-10000),
			FFixedPoint::Zero);

		struct FScopedPreviewPolicy
		{
			FScopedPreviewPolicy()
				: Core(GetMutableDefault<USeinARTSCoreSettings>())
				, Cover(GetMutableDefault<USeinARTSCoverSettings>())
				, SavedResolver(Core->DefaultBrokerResolverClass)
				, SavedCoverSystem(Cover->CoverSystemClass)
				, SavedCoverSnapRadius(Cover->CoverSnapRadius)
			{
				Core->DefaultBrokerResolverClass = FSoftClassPath(
					USeinCoverAwareDefaultBrokerResolver::StaticClass());
				Cover->CoverSystemClass = FSoftClassPath(
					USeinCoverDefault::StaticClass());
				Cover->CoverSnapRadius = FFixedPoint::FromInt(500);
			}

			~FScopedPreviewPolicy()
			{
				Core->DefaultBrokerResolverClass = SavedResolver;
				Cover->CoverSystemClass = SavedCoverSystem;
				Cover->CoverSnapRadius = SavedCoverSnapRadius;
			}

			USeinARTSCoreSettings* Core;
			USeinARTSCoverSettings* Cover;
			TSoftClassPtr<USeinCommandBrokerResolver> SavedResolver;
			FSoftClassPath SavedCoverSystem;
			FFixedPoint SavedCoverSnapRadius;
		};

		struct FMeasurement
		{
			double MedianMilliseconds = 0.0;
			double P95Milliseconds = 0.0;
			int32 CandidateCount = 0;
			int32 SnappedCount = 0;
		};

		bool LayoutsEqual(
			const FSeinFormationLayout& A,
			const FSeinFormationLayout& B)
		{
			return A.Positions == B.Positions
				&& A.Radii == B.Radii
				&& A.Facing == B.Facing
				&& A.Facings == B.Facings;
		}

		int32 CountSnappedPositions(
			const FSeinFormationLayout& Layout,
			const TArray<FSeinCoverSlotCandidate>& Candidates)
		{
			int32 Count = 0;
			for (const FFixedVector& Position : Layout.Positions)
			{
				if (Candidates.ContainsByPredicate(
					[&Position](const FSeinCoverSlotCandidate& Candidate)
					{
						return Candidate.WorldPosition == Position;
					}))
				{
					++Count;
				}
			}
			return Count;
		}

		struct FPreviewFixture
		{
			FScopedPreviewPolicy Policy;
			FActorTestSpawner Spawner;
			USeinWorldSubsystem* World = nullptr;
			USeinCoverSystem* Cover = nullptr;
			TArray<FSeinEntityHandle> Members;
			FString Error;

			FPreviewFixture()
			{
				World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
				USeinCoverSubsystem* CoverSubsystem =
					Spawner.GetWorld().GetSubsystem<USeinCoverSubsystem>();
				Cover = CoverSubsystem ? CoverSubsystem->GetCoverSystem() : nullptr;
				if (!World || !Cover)
				{
					Error = TEXT("Formation preview fixture is missing a required subsystem.");
					return;
				}

				World->NavProjectResolver.Unbind();
				World->DynamicPassableResolver.Unbind();
				const FSeinPlayerID Player(1);
				bool bAuthored = true;
				const auto AuthorState = [&]()
				{
					World->RegisterPlayer(Player, FSeinFactionID(1));
					Members.Reserve(MaximumMembers);
					for (int32 Index = 0; Index < MaximumMembers; ++Index)
					{
						const FFixedVector Position(
							FFixedPoint::FromInt((Index % 16) * 100),
							FFixedPoint::FromInt((Index / 16) * 100),
							FFixedPoint::Zero);
						const FSeinEntityHandle Member = World->SpawnAbstractEntity(
							FFixedTransform(Position), Player);
						if (!Member.IsValid())
						{
							bAuthored = false;
							return;
						}

						FSeinExtentsShape Shape;
						Shape.Shape = ESeinExtentsShape::Capsule;
						Shape.Radius = FFixedPoint::FromInt(40);
						FSeinExtentsPayload Extents;
						Extents.Shapes.Add(Shape);
						World->AddComponent(Member, Extents);
						bAuthored = World->GrantTag(
							Member, SeinCoverTags::Cover_UsesCover) && bAuthored;
						Members.Add(Member);
					}

					const FFixedVector SlotOffsets[SlotsPerProvider] = {
						FFixedVector(FFixedPoint::FromInt(-60), FFixedPoint::FromInt(-60), FFixedPoint::Zero),
						FFixedVector(FFixedPoint::Zero, FFixedPoint::FromInt(-60), FFixedPoint::Zero),
						FFixedVector(FFixedPoint::FromInt(60), FFixedPoint::FromInt(-60), FFixedPoint::Zero),
						FFixedVector(FFixedPoint::FromInt(-60), FFixedPoint::Zero, FFixedPoint::Zero),
						FFixedVector(FFixedPoint::FromInt(60), FFixedPoint::Zero, FFixedPoint::Zero),
						FFixedVector(FFixedPoint::FromInt(-60), FFixedPoint::FromInt(60), FFixedPoint::Zero),
						FFixedVector(FFixedPoint::Zero, FFixedPoint::FromInt(60), FFixedPoint::Zero),
						FFixedVector(FFixedPoint::FromInt(60), FFixedPoint::FromInt(60), FFixedPoint::Zero)
					};
					for (int32 Index = 0; Index < ProviderCount; ++Index)
					{
						const int32 X = (Index % ProviderColumns) * 180 - 270;
						const int32 Y = (Index / ProviderColumns) * 180 - 270;
						const FFixedVector ProviderPosition = DenseTarget + FFixedVector(
							FFixedPoint::FromInt(X),
							FFixedPoint::FromInt(Y),
							FFixedPoint::Zero);
						const FSeinEntityHandle Provider = World->SpawnAbstractEntity(
							FFixedTransform(ProviderPosition), Player);
						if (!Provider.IsValid())
						{
							bAuthored = false;
							return;
						}

						FSeinCoverPayload CoverData;
						CoverData.QualityTag = SeinCoverTags::Cover_Light;
						CoverData.SlotRadius = FFixedPoint::FromInt(20);
						CoverData.Slots.Append(SlotOffsets, SlotsPerProvider);
						World->AddComponent(Provider, CoverData);
						Cover->RegisterAuthoritativeProvider(Provider);
					}
				};

				if (!SeinTestMatchBootstrap::Materialize(
						*World,
						AuthorState,
						FSeinMatchSettings(),
						0x50525657,
						TEXT("SeinARTS.Cover.FormationPreviewPerformance"),
						&Error)
					|| !bAuthored
					|| Members.Num() != MaximumMembers
					|| !SeinTestMatchBootstrap::Start(*World, &Error))
				{
					if (Error.IsEmpty())
					{
						Error = TEXT("Could not materialize the formation preview workload.");
					}
				}
			}

			~FPreviewFixture()
			{
				if (World) World->StopSimulation();
			}

			bool IsReady() const
			{
				return World && Cover && Error.IsEmpty()
					&& World->IsSimulationRunning()
					&& World->IsExecutionTopologyValid();
			}

			bool Measure(
				int32 MemberCount,
				FFixedVector Target,
				bool bExpectCover,
				const FGuid& ExpectedRoot,
				FMeasurement& OutMeasurement)
			{
				TArray<FSeinEntityHandle> WorkloadMembers;
				WorkloadMembers.Append(Members.GetData(), MemberCount);
				const TArray<FFixedVector> GuidePoints;
				const FSeinFormationLayout Warmup =
					USeinCommandBrokerBPFL::SeinComputeFormationPreview(
						&Spawner.GetWorld(),
						WorkloadMembers,
						Target,
						GuidePoints,
						SeinARTSTags::Formation_Blob);
				if (Warmup.Positions.Num() != MemberCount
					|| Warmup.Radii.Num() != MemberCount)
				{
					Error = TEXT("Public formation preview returned an incomplete layout.");
					return false;
				}

				const TArray<FSeinCoverSlotCandidate> Candidates =
					Cover->FindNearbySlots(
						Target,
						GetDefault<USeinARTSCoverSettings>()->CoverSnapRadius,
						FSeinPlayerID(1));
				OutMeasurement.CandidateCount = Candidates.Num();
				OutMeasurement.SnappedCount =
					CountSnappedPositions(Warmup, Candidates);
				if (bExpectCover
					? (Candidates.Num() == 0 || OutMeasurement.SnappedCount == 0)
					: (Candidates.Num() != 0 || OutMeasurement.SnappedCount != 0))
				{
					Error = TEXT("Formation preview cover workload was not representative.");
					return false;
				}

				TArray<double> Samples;
				Samples.Reserve(TimedSamples);
				for (int32 Sample = 0; Sample < TimedSamples; ++Sample)
				{
					const double StartedAt = FPlatformTime::Seconds();
					const FSeinFormationLayout Layout =
						USeinCommandBrokerBPFL::SeinComputeFormationPreview(
							&Spawner.GetWorld(),
							WorkloadMembers,
							Target,
							GuidePoints,
							SeinARTSTags::Formation_Blob);
					Samples.Add(
						(FPlatformTime::Seconds() - StartedAt) * 1000.0);
					if (!LayoutsEqual(Layout, Warmup))
					{
						Error = TEXT("Repeated formation preview changed deterministic output.");
						return false;
					}
				}

				FGuid RootAfterPreview;
				if (!World->ComputeCanonicalStateRoot(RootAfterPreview, Error)
					|| RootAfterPreview != ExpectedRoot)
				{
					if (Error.IsEmpty())
					{
						Error = TEXT("Formation preview changed the canonical state root.");
					}
					return false;
				}

				Samples.Sort();
				OutMeasurement.MedianMilliseconds = Samples[Samples.Num() / 2];
				OutMeasurement.P95Milliseconds = Samples[
					FMath::Min(Samples.Num() - 1,
						FMath::CeilToInt(Samples.Num() * 0.95) - 1)];
				return true;
			}
		};
	}

	TEST(PublicFormationPreviewHasMeasuredCoverlessAndDenseCoverCurve,
		"SeinARTS.Perf.Cover.FormationPreview")
	{
		using namespace CoverFormationPreviewPerfLocal;
		FPreviewFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.IsReady()));

		FGuid InitialRoot;
		ASSERT_THAT(IsTrue(Fixture.World->ComputeCanonicalStateRoot(
			InitialRoot, Fixture.Error)));
		const int32 MemberCounts[] = {64, 128};
		for (const int32 MemberCount : MemberCounts)
		{
			FMeasurement Coverless;
			ASSERT_THAT(IsTrue(Fixture.Measure(
				MemberCount,
				CoverlessTarget,
				false,
				InitialRoot,
				Coverless)));
			UE_LOG(LogTemp, Display,
				TEXT("Public formation preview coverless: members=%d, candidates=%d, snapped=%d, median=%.3f ms, p95=%.3f ms"),
				MemberCount,
				Coverless.CandidateCount,
				Coverless.SnappedCount,
				Coverless.MedianMilliseconds,
				Coverless.P95Milliseconds);

			FMeasurement Dense;
			ASSERT_THAT(IsTrue(Fixture.Measure(
				MemberCount,
				DenseTarget,
				true,
				InitialRoot,
				Dense)));
			UE_LOG(LogTemp, Display,
				TEXT("Public formation preview dense cover: members=%d, providers=%d, authored_slots=%d, candidates=%d, snapped=%d, median=%.3f ms, p95=%.3f ms"),
				MemberCount,
				ProviderCount,
				ProviderCount * SlotsPerProvider,
				Dense.CandidateCount,
				Dense.SnappedCount,
				Dense.MedianMilliseconds,
				Dense.P95Milliseconds);

			if (MemberCount == MaximumMembers)
			{
				ASSERT_THAT(IsTrue(Coverless.P95Milliseconds < 50.0));
				ASSERT_THAT(IsTrue(Dense.P95Milliseconds < 100.0));
			}
		}
	}
}
