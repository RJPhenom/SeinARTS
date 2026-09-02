#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Components/SeinAbilityPayload.h"
#include "Components/SeinExtentsPayload.h"
#include "Components/SeinMovementPayload.h"
#include "Components/SeinNavigationPayload.h"
#include "Data/SeinWorldSnapshot.h"
#include "HAL/PlatformTime.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Serialization/SeinSnapshotTransfer.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinWorldSubsystem.h"

namespace UE::SeinARTSTests
{
	namespace ReplayCheckpointScaleTestLocal
	{
		constexpr int32 TimedSamples = 5;

		bool HaveSameComponentStorageBlobs(
			const FSeinWorldSnapshot& A,
			const FSeinWorldSnapshot& B)
		{
			if (A.ComponentStorageBlobs.Num() != B.ComponentStorageBlobs.Num())
			{
				return false;
			}
			for (const auto& Pair : A.ComponentStorageBlobs)
			{
				const FSeinSnapshotComponentStorageBlob* Other =
					B.ComponentStorageBlobs.Find(Pair.Key);
				if (!Other
					|| Other->EntryCount != Pair.Value.EntryCount
					|| Other->Bytes != Pair.Value.Bytes)
				{
					return false;
				}
			}
			return true;
		}

		bool MeasurePopulation(
			int32 Population,
			double& OutCaptureMedianMilliseconds,
			double& OutSerializeMedianMilliseconds,
			double& OutEncodeMedianMilliseconds,
			int32& OutEnvelopeBytes,
			int64& OutComponentBlobBytes,
			int32& OutPoolSlots,
			FString& OutError)
		{
			FActorTestSpawner Spawner;
			USeinWorldSubsystem* World =
				Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
			if (!World)
			{
				OutError = TEXT("Fresh world has no Sein simulation subsystem.");
				return false;
			}

			bool bAuthoringSucceeded = true;
			TArray<FSeinEntityHandle> Handles;
			Handles.Reserve(Population);
			const auto AuthorState = [&]()
			{
				for (int32 Index = 0; Index < Population; ++Index)
				{
					const FFixedVector Position(
						FFixedPoint::FromInt((Index % 50) * 125),
						FFixedPoint::FromInt((Index / 50) * 125),
						FFixedPoint::Zero);
					const FSeinEntityHandle Handle = World->SpawnAbstractEntity(
						FFixedTransform(Position), FSeinPlayerID::Neutral());
					if (!Handle.IsValid())
					{
						bAuthoringSucceeded = false;
						return;
					}
					Handles.Add(Handle);

					FSeinExtentsShape Shape;
					Shape.Radius = FFixedPoint::FromInt(50);
					Shape.Height = FFixedPoint::FromInt(180);
					FSeinExtentsPayload Extents;
					Extents.Shapes.Add(Shape);
					Extents.bCollisionEnabled = true;
					Extents.Mobility = ESeinCollisionMobility::Movable;
					Extents.Mass = FFixedPoint::FromInt(100);
					Extents.ObjectType.Channel = FName(TEXT("Default"));
					World->AddComponent(Handle, Extents);

					FSeinMovementPayload Movement;
					Movement.Velocity = FFixedVector(
						FFixedPoint::FromInt(Index % 7),
						FFixedPoint::FromInt(-(Index % 5)),
						FFixedPoint::Zero);
					Movement.HomePos = Position;
					Movement.bHomeSeeded = true;
					World->AddComponent(Handle, Movement);
					World->AddComponent(Handle, FSeinNavigationPayload());
					World->AddComponent(Handle, FSeinAbilityPayload());
				}
			};

			if (!SeinTestMatchBootstrap::Materialize(
					*World,
					AuthorState,
					FSeinMatchSettings(),
					0,
					TEXT("SeinARTS.ReplayCheckpointScale"),
					&OutError)
				|| !bAuthoringSucceeded
				|| !SeinTestMatchBootstrap::Start(*World, &OutError))
			{
				if (OutError.IsEmpty())
				{
					OutError = TEXT("Could not materialize checkpoint scale workload.");
				}
				return false;
			}
			FSeinWorldSnapshot FirstSnapshot;
			FSeinWorldSnapshot CachedSnapshot;
			const int64 CacheHitsBefore =
				World->GetComponentStorageSnapshotCacheHitCountForTests();
			const int64 CacheMissesBefore =
				World->GetComponentStorageSnapshotCacheMissCountForTests();
			World->CaptureSnapshot(FirstSnapshot);
			const int64 CacheHitsAfterFirst =
				World->GetComponentStorageSnapshotCacheHitCountForTests();
			const int64 CacheMissesAfterFirst =
				World->GetComponentStorageSnapshotCacheMissCountForTests();
			World->CaptureSnapshot(CachedSnapshot);
			if (FirstSnapshot.SnapshotVersion != FSeinWorldSnapshot::CurrentVersion
				|| !HaveSameComponentStorageBlobs(FirstSnapshot, CachedSnapshot)
				|| CacheHitsAfterFirst != CacheHitsBefore
				|| CacheMissesAfterFirst - CacheMissesBefore
					!= FirstSnapshot.ComponentStorageBlobs.Num()
				|| World->GetComponentStorageSnapshotCacheHitCountForTests()
					- CacheHitsAfterFirst
					!= CachedSnapshot.ComponentStorageBlobs.Num()
				|| World->GetComponentStorageSnapshotCacheMissCountForTests()
					!= CacheMissesAfterFirst
				|| World->GetComponentStorageSnapshotCacheBytesForTests()
					> World->GetDefaultComponentStorageSnapshotCacheBudgetForTests())
			{
				OutError = TEXT(
					"An unchanged checkpoint did not reuse every exact component blob.");
				World->StopSimulation();
				return false;
			}
			const FString MovementStoragePath =
				FSeinMovementPayload::StaticStruct()->GetPathName();
			const FSeinSnapshotComponentStorageBlob* PreviousMovementBlob =
				CachedSnapshot.ComponentStorageBlobs.Find(MovementStoragePath);
			if (!PreviousMovementBlob)
			{
				OutError = TEXT("Checkpoint scale fixture has no movement storage blob.");
				World->StopSimulation();
				return false;
			}
			TArray<uint8> PreviousMovementBytes = PreviousMovementBlob->Bytes;
			World->ResetComponentStorageSnapshotCacheForTests(0);
			FSeinWorldSnapshot ZeroBudgetSnapshot;
			World->CaptureSnapshot(ZeroBudgetSnapshot);
			if (!HaveSameComponentStorageBlobs(
					CachedSnapshot, ZeroBudgetSnapshot)
				|| World->GetComponentStorageSnapshotCacheBytesForTests() != 0
				|| World->GetComponentStorageSnapshotCacheEntryCountForTests() != 0)
			{
				OutError = TEXT(
					"Zero cache budget admitted bytes or changed checkpoint output.");
				World->StopSimulation();
				return false;
			}
			World->ResetComponentStorageSnapshotCacheForTests(
				World->GetDefaultComponentStorageSnapshotCacheBudgetForTests());
			FSeinWorldSnapshot RefilledCacheSnapshot;
			World->CaptureSnapshot(RefilledCacheSnapshot);
			if (!HaveSameComponentStorageBlobs(
					CachedSnapshot, RefilledCacheSnapshot)
				|| World->GetComponentStorageSnapshotCacheEntryCountForTests()
					!= RefilledCacheSnapshot.ComponentStorageBlobs.Num())
			{
				OutError = TEXT("Restored cache budget did not admit every storage.");
				World->StopSimulation();
				return false;
			}
			FGuid TopologyRootBefore;
			const FSeinMovementPayload* MovementToRemove =
				World->GetComponent<FSeinMovementPayload>(Handles.Last());
			ISeinComponentStorage* MovementStorage =
				World->GetComponentStorageMutable(
					FSeinMovementPayload::StaticStruct());
			if (!MovementToRemove || !MovementStorage
				|| !World->ComputeCanonicalStateRoot(TopologyRootBefore, OutError))
			{
				OutError = OutError.IsEmpty()
					? TEXT("Checkpoint topology invalidation fixture was unavailable.")
					: OutError;
				World->StopSimulation();
				return false;
			}
			const FSeinMovementPayload RemovedMovement = *MovementToRemove;
			MovementStorage->RemoveComponent(Handles.Last());
			FSeinWorldSnapshot RemovedSnapshot;
			World->CaptureSnapshot(RemovedSnapshot);
			const FSeinSnapshotComponentStorageBlob* RemovedMovementBlob =
				RemovedSnapshot.ComponentStorageBlobs.Find(MovementStoragePath);
			if (!RemovedMovementBlob
				|| RemovedMovementBlob->EntryCount != Population - 1
				|| RemovedMovementBlob->Bytes == PreviousMovementBytes)
			{
				OutError = TEXT(
					"A removed component did not invalidate its cached storage topology.");
				World->StopSimulation();
				return false;
			}
			MovementStorage->AddComponent(Handles.Last(), &RemovedMovement);
			FSeinWorldSnapshot RestoredTopologySnapshot;
			World->CaptureSnapshot(RestoredTopologySnapshot);
			const FSeinSnapshotComponentStorageBlob* RestoredMovementBlob =
				RestoredTopologySnapshot.ComponentStorageBlobs.Find(
					MovementStoragePath);
			FGuid TopologyRootAfter;
			if (!RestoredMovementBlob
				|| RestoredMovementBlob->EntryCount != Population
				|| RestoredMovementBlob->Bytes != PreviousMovementBytes
				|| !World->ComputeCanonicalStateRoot(TopologyRootAfter, OutError)
				|| TopologyRootAfter != TopologyRootBefore)
			{
				OutError = OutError.IsEmpty()
					? TEXT("Restoring component topology did not restore exact state.")
					: OutError;
				World->StopSimulation();
				return false;
			}
			FSeinMovementPayload* RetainedMovementPointer =
				World->GetComponentMutable<FSeinMovementPayload>(Handles[0]);
			if (!RetainedMovementPointer)
			{
				OutError = TEXT("Retained-pointer invalidation fixture was unavailable.");
				World->StopSimulation();
				return false;
			}
			const uint64 CacheBytesBeforeRetainedPointer =
				World->GetComponentStorageSnapshotCacheBytesForTests();
			RetainedMovementPointer->Velocity.X = FFixedPoint::FromInt(1001);
			FSeinWorldSnapshot FirstRetainedPointerSnapshot;
			World->CaptureSnapshot(FirstRetainedPointerSnapshot);
			RetainedMovementPointer->Velocity.X = FFixedPoint::FromInt(1002);
			FSeinWorldSnapshot SecondRetainedPointerSnapshot;
			World->CaptureSnapshot(SecondRetainedPointerSnapshot);
			const FSeinSnapshotComponentStorageBlob* FirstRetainedBlob =
				FirstRetainedPointerSnapshot.ComponentStorageBlobs.Find(
					MovementStoragePath);
			const FSeinSnapshotComponentStorageBlob* SecondRetainedBlob =
				SecondRetainedPointerSnapshot.ComponentStorageBlobs.Find(
					MovementStoragePath);
			if (!FirstRetainedBlob || !SecondRetainedBlob
				|| FirstRetainedBlob->Bytes == SecondRetainedBlob->Bytes
				|| World->HasComponentStorageSnapshotCacheEntryForTests(
					FSeinMovementPayload::StaticStruct())
				|| World->GetComponentStorageSnapshotCacheBytesForTests()
					!= CacheBytesBeforeRetainedPointer
						- static_cast<uint64>(PreviousMovementBytes.Num()))
			{
				OutError = TEXT(
					"A retained mutable pointer produced a stale cached checkpoint blob.");
				World->StopSimulation();
				return false;
			}
			PreviousMovementBytes = SecondRetainedBlob->Bytes;

			TArray<double> CaptureSamples;
			TArray<double> SerializeSamples;
			TArray<double> EncodeSamples;
			CaptureSamples.Reserve(TimedSamples);
			SerializeSamples.Reserve(TimedSamples);
			EncodeSamples.Reserve(TimedSamples);
			for (int32 Sample = -1; Sample < TimedSamples; ++Sample)
			{
				for (int32 Index = 0; Index < Handles.Num(); ++Index)
				{
					FSeinMovementPayload* Movement =
						World->GetComponentMutable<FSeinMovementPayload>(Handles[Index]);
					if (!Movement)
					{
						OutError = TEXT("Checkpoint scale movement mutation failed.");
						World->StopSimulation();
						return false;
					}
					Movement->Velocity.X = FFixedPoint::FromInt(
						((Index + Sample + 2) % 17) + 1);
				}
				FGuid RootBefore;
				if (!World->ComputeCanonicalStateRoot(RootBefore, OutError))
				{
					World->StopSimulation();
					return false;
				}

				FSeinWorldSnapshot Snapshot;
				const double CaptureStartedAt = FPlatformTime::Seconds();
				World->CaptureSnapshot(Snapshot);
				const double CaptureMilliseconds =
					(FPlatformTime::Seconds() - CaptureStartedAt) * 1000.0;
				if (Snapshot.SnapshotVersion != FSeinWorldSnapshot::CurrentVersion
					|| Snapshot.Entities.Num() != Population)
				{
					OutError = TEXT("Checkpoint scale capture was incomplete.");
					World->StopSimulation();
					return false;
				}

				TArray<uint8> SerializedPayload;
				const double SerializeStartedAt = FPlatformTime::Seconds();
				{
					FSeinWorldSnapshotReferenceGuard SnapshotGCGuard(Snapshot);
					FMemoryWriter MemWriter(
						SerializedPayload, /*bIsPersistent*/ true);
					FObjectAndNameAsStringProxyArchive Writer(
						MemWriter, /*bInLoadIfFindFails*/ false);
					FSeinWorldSnapshot::StaticStruct()->SerializeItem(
						Writer, &Snapshot, nullptr);
					if (Writer.IsError() || Writer.IsCriticalError()
						|| MemWriter.IsError() || MemWriter.IsCriticalError()
						|| MemWriter.Tell() != SerializedPayload.Num())
					{
						OutError = TEXT(
							"Checkpoint payload diagnostic serialization failed.");
						World->StopSimulation();
						return false;
					}
				}
				const double SerializeMilliseconds =
					(FPlatformTime::Seconds() - SerializeStartedAt) * 1000.0;

				TArray<uint8> Envelope;
				FSeinSnapshotEnvelopeMetadata Metadata;
				const double EncodeStartedAt = FPlatformTime::Seconds();
				const bool bEncoded = SeinSnapshotTransfer::EncodeCheckpointEnvelope(
					Snapshot, Envelope, Metadata, OutError);
				const double EncodeMilliseconds =
					(FPlatformTime::Seconds() - EncodeStartedAt) * 1000.0;
				if (!bEncoded
					|| Metadata.SnapshotTick != Snapshot.CurrentTick
					|| Envelope.IsEmpty())
				{
					if (OutError.IsEmpty())
					{
						OutError = TEXT("Checkpoint scale envelope was invalid.");
					}
					World->StopSimulation();
					return false;
				}
				const FSeinSnapshotComponentStorageBlob* MovementBlob =
					Snapshot.ComponentStorageBlobs.Find(MovementStoragePath);
				if (!MovementBlob || MovementBlob->Bytes == PreviousMovementBytes)
				{
					OutError = TEXT(
						"A mutated movement storage reused a stale checkpoint blob.");
					World->StopSimulation();
					return false;
				}
				PreviousMovementBytes = MovementBlob->Bytes;

				FGuid RootAfter;
				if (!World->ComputeCanonicalStateRoot(RootAfter, OutError)
					|| RootAfter != RootBefore)
				{
					OutError = TEXT(
						"Checkpoint capture or encoding changed canonical state.");
					World->StopSimulation();
					return false;
				}

				OutEnvelopeBytes = Envelope.Num();
				OutPoolSlots = Snapshot.EntityPoolState.Slots.Num();
				OutComponentBlobBytes = 0;
				for (const auto& Pair : Snapshot.ComponentStorageBlobs)
				{
					OutComponentBlobBytes += Pair.Value.Bytes.Num();
				}
				if (Sample >= 0)
				{
					CaptureSamples.Add(CaptureMilliseconds);
					SerializeSamples.Add(SerializeMilliseconds);
					EncodeSamples.Add(EncodeMilliseconds);
				}
			}

			World->StopSimulation();

			CaptureSamples.Sort();
			SerializeSamples.Sort();
			EncodeSamples.Sort();
			OutCaptureMedianMilliseconds =
				CaptureSamples[CaptureSamples.Num() / 2];
			OutSerializeMedianMilliseconds =
				SerializeSamples[SerializeSamples.Num() / 2];
			OutEncodeMedianMilliseconds =
				EncodeSamples[EncodeSamples.Num() / 2];
			return true;
		}
	}

	TEST(ReplayCheckpointCaptureAndEncodingHaveMeasuredPopulationCurve,
		"SeinARTS.Perf.Replay.Checkpoint")
	{
		using namespace ReplayCheckpointScaleTestLocal;
		const int32 Populations[] = {100, 500, 1000};
		double CaptureMedians[UE_ARRAY_COUNT(Populations)] = {};
		double SerializeMedians[UE_ARRAY_COUNT(Populations)] = {};
		double EncodeMedians[UE_ARRAY_COUNT(Populations)] = {};
		int32 EnvelopeBytes[UE_ARRAY_COUNT(Populations)] = {};
		int64 ComponentBlobBytes[UE_ARRAY_COUNT(Populations)] = {};
		int32 PoolSlots[UE_ARRAY_COUNT(Populations)] = {};
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Populations); ++Index)
		{
			FString Error;
			const bool bMeasured = MeasurePopulation(
				Populations[Index],
				CaptureMedians[Index],
				SerializeMedians[Index],
				EncodeMedians[Index],
				EnvelopeBytes[Index],
				ComponentBlobBytes[Index],
				PoolSlots[Index],
				Error);
			if (!bMeasured)
			{
				UE_LOG(LogTemp, Error,
					TEXT("Replay checkpoint scale fixture failed: %s"), *Error);
			}
			ASSERT_THAT(IsTrue(bMeasured));
			UE_LOG(LogTemp, Display,
				TEXT("Replay checkpoint at %d entities: capture %.3f ms, serialize-only %.3f ms, encode-total %.3f ms, framing-estimate %.3f ms, envelope %d bytes, component blobs %lld bytes, pool slots %d"),
				Populations[Index],
				CaptureMedians[Index],
				SerializeMedians[Index],
				EncodeMedians[Index],
				FMath::Max(0.0,
					EncodeMedians[Index] - SerializeMedians[Index]),
				EnvelopeBytes[Index],
				ComponentBlobBytes[Index],
				PoolSlots[Index]);
		}

		// Periodic replay encoding runs on the worker pool. This local sentinel
		// protects the only synchronous game-thread stage at production scale;
		// the second ceiling catches gross worker-throughput regressions.
		ASSERT_THAT(IsTrue(CaptureMedians[2] < 50.0));
		ASSERT_THAT(IsTrue(EncodeMedians[2] < 250.0));
	}
}
