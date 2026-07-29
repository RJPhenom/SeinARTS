#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "CoverSnapshotRestoreTestTypes.h"
#include "Components/SeinCoverComponent.h"
#include "Data/SeinMatchSettings.h"
#include "Data/SeinWorldSnapshot.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSnapshotRestore.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "System/SeinCoverSubsystem.h"
#include "System/SeinCoverSystem.h"

namespace
{
	const FName CoverRestoreFixtureID(
		TEXT("SeinARTSExtensionTests.CoverSnapshotRestore"));

	struct FCoverSnapshotRestoreFixture
	{
		FActorTestSpawner SourceSpawner;
		FActorTestSpawner DestinationSpawner;
		USeinWorldSubsystem* Source = nullptr;
		USeinWorldSubsystem* Destination = nullptr;
		USeinCoverSystem* SourceCover = nullptr;
		USeinCoverSystem* DestinationCover = nullptr;
		FSeinEntityHandle FirstProvider;
		FSeinEntityHandle SecondProvider;
		FSeinWorldSnapshot Snapshot;
		FString Error;

		FCoverSnapshotRestoreFixture()
		{
			Source =
				SourceSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
			Destination =
				DestinationSpawner.GetWorld()
					.GetSubsystem<USeinWorldSubsystem>();
			USeinCoverSubsystem* SourceSubsystem =
				SourceSpawner.GetWorld().GetSubsystem<USeinCoverSubsystem>();
			USeinCoverSubsystem* DestinationSubsystem =
				DestinationSpawner.GetWorld()
					.GetSubsystem<USeinCoverSubsystem>();
			SourceCover =
				SourceSubsystem ? SourceSubsystem->GetCoverSystem() : nullptr;
			DestinationCover =
				DestinationSubsystem
					? DestinationSubsystem->GetCoverSystem()
					: nullptr;
			if (!Source || !Destination || !SourceCover || !DestinationCover)
			{
				Error = TEXT("Cover restore fixture is missing a required subsystem.");
				return;
			}

			// This fixture exercises Cover registry/order behavior, not the
			// synthetic world's navigation substrate. Keep valid authored slots
			// from being filtered by its empty dynamic-passability policy.
			Source->DynamicPassableResolver.Unbind();
			Destination->DynamicPassableResolver.Unbind();

			const auto AuthorState = [this]()
			{
				FSeinCoverComponent FirstCover;
				FirstCover.Slots.Add(FFixedVector::ZeroVector);
				FirstCover.Slots.Add(FFixedVector(
					FFixedPoint::FromInt(-80),
					FFixedPoint::Zero,
					FFixedPoint::Zero));
				FirstCover.SlotRadius = FFixedPoint::FromInt(5);

				FSeinCoverComponent SecondCover;
				SecondCover.Slots.Add(FFixedVector::ZeroVector);
				SecondCover.Slots.Add(FFixedVector(
					FFixedPoint::FromInt(80),
					FFixedPoint::Zero,
					FFixedPoint::Zero));
				SecondCover.SlotRadius = FFixedPoint::FromInt(5);

				FirstProvider = Source->SpawnAbstractEntity(
					FFixedTransform(), FSeinPlayerID::Neutral());
				SecondProvider = Source->SpawnAbstractEntity(
					FFixedTransform(), FSeinPlayerID::Neutral());
				Source->AddComponent(FirstProvider, FirstCover);
				Source->AddComponent(SecondProvider, SecondCover);

				// Deliberately register in reverse history order. The default
				// policy must canonicalize this to handle order so live and
				// restored peers resolve an equal-cost overlapping slot alike.
				SourceCover->RegisterAuthoritativeProvider(SecondProvider);
				SourceCover->RegisterAuthoritativeProvider(FirstProvider);
			};
			if (!SeinTestMatchBootstrap::Materialize(
					*Source,
					AuthorState,
					FSeinMatchSettings(),
					0x434F5652,
					CoverRestoreFixtureID,
					&Error)
				|| !SeinTestMatchBootstrap::Start(*Source, &Error))
			{
				return;
			}

			Source->StopSimulation();
			Source->CaptureSnapshot(Snapshot);
			if (Snapshot.SnapshotVersion
				!= FSeinWorldSnapshot::CurrentVersion)
			{
				Error = TEXT("Cover restore fixture snapshot capture was refused.");
			}
		}

		~FCoverSnapshotRestoreFixture()
		{
			if (Source)
			{
				Source->StopSimulation();
			}
			if (Destination)
			{
				Destination->StopSimulation();
			}
		}

		bool IsReady() const
		{
			return Source && Destination && SourceCover && DestinationCover
				&& FirstProvider.IsValid() && SecondProvider.IsValid()
				&& Error.IsEmpty()
				&& Snapshot.SnapshotVersion
					== FSeinWorldSnapshot::CurrentVersion;
		}

		static TArray<FSeinCoverSlotCandidate> Query(USeinCoverSystem& Cover)
		{
			return Cover.FindNearbySlots(
				FFixedVector::ZeroVector,
				FFixedPoint::FromInt(100),
				FSeinPlayerID());
		}
	};
}

namespace UE::SeinARTSTests
{
	TEST(CoverProviderRegistryRebuildsCanonicallyForFreshAndSameWorldRestore,
		"SeinARTS.Unit.Cover.SnapshotRestore")
	{
		FCoverSnapshotRestoreFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.IsReady()));
		ASSERT_THAT(IsTrue(
			Fixture.FirstProvider < Fixture.SecondProvider));

		const TArray<FSeinCoverSlotCandidate> LiveCandidates =
			FCoverSnapshotRestoreFixture::Query(*Fixture.SourceCover);
		ASSERT_THAT(AreEqual(3, LiveCandidates.Num()));
		ASSERT_THAT(IsTrue(
			LiveCandidates[0].ProviderHandle == Fixture.FirstProvider));
		ASSERT_THAT(IsTrue(
			LiveCandidates[1].ProviderHandle == Fixture.FirstProvider));
		ASSERT_THAT(IsTrue(
			LiveCandidates[2].ProviderHandle == Fixture.SecondProvider));

		ASSERT_THAT(IsTrue(
			SeinTestSnapshotRestore::RestoreTrusted(
				*Fixture.Source, Fixture.Snapshot, &Fixture.Error)));
		const TArray<FSeinCoverSlotCandidate> SameWorldCandidates =
			FCoverSnapshotRestoreFixture::Query(*Fixture.SourceCover);
		ASSERT_THAT(AreEqual(LiveCandidates.Num(),
			SameWorldCandidates.Num()));
		for (int32 Index = 0; Index < LiveCandidates.Num(); ++Index)
		{
			ASSERT_THAT(IsTrue(
				SameWorldCandidates[Index].ProviderHandle
					== LiveCandidates[Index].ProviderHandle));
			ASSERT_THAT(IsTrue(
				SameWorldCandidates[Index].WorldPosition
					== LiveCandidates[Index].WorldPosition));
		}

		ASSERT_THAT(AreEqual(
			0,
			FCoverSnapshotRestoreFixture::Query(
				*Fixture.DestinationCover).Num()));
		const FSeinSnapshotRestoreOptions ResyncOptions(
			ESeinSnapshotLocalStateRestorePolicy::PreserveCurrent,
			ESeinSnapshotResumePolicy::RemainStopped);
		ASSERT_THAT(IsTrue(
			SeinTestSnapshotRestore::RestoreTrusted(
				*Fixture.Destination,
				Fixture.Snapshot,
				ResyncOptions,
				&Fixture.Error)));
		ASSERT_THAT(IsFalse(
			Fixture.Destination->IsSimulationRunning()));
		const TArray<FSeinCoverSlotCandidate> FreshWorldCandidates =
			FCoverSnapshotRestoreFixture::Query(*Fixture.DestinationCover);
		ASSERT_THAT(AreEqual(LiveCandidates.Num(),
			FreshWorldCandidates.Num()));
		for (int32 Index = 0; Index < LiveCandidates.Num(); ++Index)
		{
			ASSERT_THAT(IsTrue(
				FreshWorldCandidates[Index].ProviderHandle
					== LiveCandidates[Index].ProviderHandle));
			ASSERT_THAT(IsTrue(
				FreshWorldCandidates[Index].WorldPosition
					== LiveCandidates[Index].WorldPosition));
		}
	}

	TEST(CoverBaseRebuildReplacesLegacySubclassRegistry,
		"SeinARTS.Unit.Cover.SnapshotRestore")
	{
		USeinCoverRegistryCompatibilityTestSystem* Cover =
			NewObject<USeinCoverRegistryCompatibilityTestSystem>();
		ASSERT_THAT(IsNotNull(Cover));
		Cover->OnCoverSystemInitialized(nullptr);

		const FSeinEntityHandle First(1, 1);
		const FSeinEntityHandle Second(2, 1);
		const FSeinEntityHandle Third(3, 1);
		Cover->RegisterAuthoritativeProvider(Second);
		Cover->RegisterAuthoritativeProvider(First);
		Cover->RegisterAuthoritativeProvider(Second);
		ASSERT_THAT(AreEqual(2, Cover->GetProviders().Num()));
		ASSERT_THAT(IsTrue(Cover->GetProviders()[0] == First));
		ASSERT_THAT(IsTrue(Cover->GetProviders()[1] == Second));

		Cover->RebuildProviderRegistry({ Third, First, Third });
		ASSERT_THAT(AreEqual(2, Cover->GetProviders().Num()));
		ASSERT_THAT(IsTrue(Cover->GetProviders()[0] == First));
		ASSERT_THAT(IsTrue(Cover->GetProviders()[1] == Third));
	}
}
