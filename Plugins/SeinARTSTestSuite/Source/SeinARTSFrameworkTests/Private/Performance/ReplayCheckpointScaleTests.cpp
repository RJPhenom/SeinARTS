#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Components/SeinAbilityComponent.h"
#include "Components/SeinExtentsComponent.h"
#include "Components/SeinMovementComponent.h"
#include "Components/SeinNavigationComponent.h"
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

					FSeinExtentsShape Shape;
					Shape.Radius = FFixedPoint::FromInt(50);
					Shape.Height = FFixedPoint::FromInt(180);
					FSeinExtentsComponent Extents;
					Extents.Shapes.Add(Shape);
					Extents.bCollisionEnabled = true;
					Extents.Mobility = ESeinCollisionMobility::Movable;
					Extents.Mass = FFixedPoint::FromInt(100);
					Extents.ObjectType.Channel = FName(TEXT("Default"));
					World->AddComponent(Handle, Extents);

					FSeinMovementComponent Movement;
					Movement.Velocity = FFixedVector(
						FFixedPoint::FromInt(Index % 7),
						FFixedPoint::FromInt(-(Index % 5)),
						FFixedPoint::Zero);
					Movement.HomePos = Position;
					Movement.bHomeSeeded = true;
					World->AddComponent(Handle, Movement);
					World->AddComponent(Handle, FSeinNavigationComponent());
					World->AddComponent(Handle, FSeinAbilityComponent());
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
			FGuid RootBefore;
			if (!World->ComputeCanonicalStateRoot(RootBefore, OutError))
			{
				World->StopSimulation();
				return false;
			}

			TArray<double> CaptureSamples;
			TArray<double> SerializeSamples;
			TArray<double> EncodeSamples;
			CaptureSamples.Reserve(TimedSamples);
			SerializeSamples.Reserve(TimedSamples);
			EncodeSamples.Reserve(TimedSamples);
			for (int32 Sample = -1; Sample < TimedSamples; ++Sample)
			{
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

			FGuid RootAfter;
			if (!World->ComputeCanonicalStateRoot(RootAfter, OutError)
				|| RootAfter != RootBefore)
			{
				OutError = TEXT("Checkpoint capture or encoding changed canonical state.");
				World->StopSimulation();
				return false;
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
