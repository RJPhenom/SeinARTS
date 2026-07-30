#include "Determinism/SeinCollisionDeterminismScenario.h"

#include "Collision/SeinCollisionResolverParallel.h"
#include "Components/ActorTestSpawner.h"
#include "Components/SeinExtentsComponent.h"
#include "Components/SeinFogVisibilityComponent.h"
#include "Components/SeinLifespanData.h"
#include "Components/SeinNavigationComponent.h"
#include "Containers/Ticker.h"
#include "Core/SeinParallel.h"
#include "HAL/IConsoleManager.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinWorldSubsystem.h"

namespace
{
	constexpr int32 MovableColumns = 12;
	constexpr int32 MovableRows = 8;
	constexpr int32 StationaryCount = 4;
	constexpr uint64 FnvOffsetBasis = 14695981039346656037ull;
	constexpr uint64 FnvPrime = 1099511628211ull;

	class FScopedParallelCvars
	{
	public:
		explicit FScopedParallelCvars(bool bParallel)
		{
			IConsoleManager& Console = IConsoleManager::Get();
			Parallel = Console.FindConsoleVariable(TEXT("Sein.Sim.Parallel"));
			MinBatch = Console.FindConsoleVariable(TEXT("Sein.Sim.ParallelMinBatch"));
			if (!Parallel || !MinBatch)
			{
				return;
			}

			PreviousParallel = Parallel->GetInt();
			PreviousMinBatch = MinBatch->GetInt();
			Parallel->SetWithCurrentPriority(bParallel ? 1 : 0);
			MinBatch->SetWithCurrentPriority(1);
			bValid = true;
		}

		~FScopedParallelCvars()
		{
			if (!bValid)
			{
				return;
			}
			Parallel->SetWithCurrentPriority(PreviousParallel);
			MinBatch->SetWithCurrentPriority(PreviousMinBatch);
		}

		bool IsValid() const { return bValid; }

	private:
		IConsoleVariable* Parallel = nullptr;
		IConsoleVariable* MinBatch = nullptr;
		int32 PreviousParallel = 0;
		int32 PreviousMinBatch = 0;
		bool bValid = false;
	};

	void AddScenarioComponents(
		USeinWorldSubsystem& World,
		FSeinEntityHandle Handle,
		int32 EntityOrdinal,
		ESeinCollisionMobility Mobility,
		int32 TickCount)
	{
		FSeinExtentsShape Shape;
		Shape.Height = FFixedPoint::FromInt(120);
		if (Mobility == ESeinCollisionMobility::Stationary || EntityOrdinal % 3 == 0)
		{
			Shape.Shape = ESeinExtentsShape::Box;
			Shape.HalfExtentX = FFixedPoint::FromInt(
				Mobility == ESeinCollisionMobility::Stationary ? 70 : 60);
			Shape.HalfExtentY = FFixedPoint::FromInt(
				Mobility == ESeinCollisionMobility::Stationary ? 70 : 45);
		}
		else
		{
			Shape.Shape = ESeinExtentsShape::Capsule;
			Shape.Radius = FFixedPoint::FromInt(60);
		}

		FSeinExtentsComponent Extents;
		Extents.Shapes.Add(Shape);
		Extents.bCollisionEnabled = true;
		Extents.Mobility = Mobility;
		Extents.Mass = FFixedPoint::FromInt(50 + (EntityOrdinal % 7) * 25);
		Extents.ObjectType.Channel = FName(TEXT("Default"));
		World.AddComponent(Handle, Extents);

		World.AddComponent(Handle, FSeinNavigationComponent());
		World.AddComponent(Handle, FSeinFogVisibilityComponent());

		FSeinLifespanData Lifespan;
		Lifespan.ExpiresAtTick = TickCount + 1000;
		World.AddComponent(Handle, Lifespan);
	}

	bool CapturePoseWords(
		const USeinWorldSubsystem& World,
		const TArray<FSeinEntityHandle>& Handles,
		TArray<uint64>& OutWords,
		FString& OutFailure)
	{
		OutWords.Reset(Handles.Num() * 12);
		for (const FSeinEntityHandle Handle : Handles)
		{
			const FSeinEntity* Entity = World.GetEntity(Handle);
			if (!Entity)
			{
				OutFailure = FString::Printf(TEXT("Entity %s disappeared while capturing the trace."), *Handle.ToString());
				return false;
			}

			const FFixedTransform& Transform = Entity->Transform;
			OutWords.Add(static_cast<uint64>(Handle.Index));
			OutWords.Add(static_cast<uint64>(Handle.Generation));
			OutWords.Add(static_cast<uint64>(Transform.Location.X.Value));
			OutWords.Add(static_cast<uint64>(Transform.Location.Y.Value));
			OutWords.Add(static_cast<uint64>(Transform.Location.Z.Value));
			OutWords.Add(static_cast<uint64>(Transform.Rotation.X.Value));
			OutWords.Add(static_cast<uint64>(Transform.Rotation.Y.Value));
			OutWords.Add(static_cast<uint64>(Transform.Rotation.Z.Value));
			OutWords.Add(static_cast<uint64>(Transform.Rotation.W.Value));
			OutWords.Add(static_cast<uint64>(Transform.Scale.X.Value));
			OutWords.Add(static_cast<uint64>(Transform.Scale.Y.Value));
			OutWords.Add(static_cast<uint64>(Transform.Scale.Z.Value));
		}
		return true;
	}

	uint64 HashPoseWords(const TArray<uint64>& Words)
	{
		uint64 Hash = FnvOffsetBasis;
		for (const uint64 Word : Words)
		{
			for (int32 ByteIndex = 0; ByteIndex < 8; ++ByteIndex)
			{
				Hash ^= (Word >> (ByteIndex * 8)) & 0xffull;
				Hash *= FnvPrime;
			}
		}
		return Hash;
	}
}

FString FSeinCollisionDeterminismFrame::ToLogPayload() const
{
	return FString::Printf(
		TEXT("tick=%03d root=%s pose=0x%016llX"),
		Tick,
		*StateRoot.ToString(EGuidFormats::Digits),
		static_cast<unsigned long long>(PoseDigest));
}

FString FSeinCollisionDeterminismTrace::Validate(bool bExpectedParallel, int32 ExpectedTicks) const
{
	if (!FailureReason.IsEmpty())
	{
		return FailureReason;
	}
	if (bRequestedParallel != bExpectedParallel)
	{
		return TEXT("Trace mode does not match the requested serial/parallel mode.");
	}
	if (!bParallelCvarsAvailable || !bParallelModeObserved)
	{
		return TEXT("The parallel cvar override was unavailable or did not take effect.");
	}
	if (!bParallelResolverSelected)
	{
		return TEXT("The transient world did not select USeinCollisionResolverParallel.");
	}
	if (bAuthoritativeDestinationResolverBound)
	{
		return TEXT("An authoritative-destination resolver was bound, which force-serializes collision resolution.");
	}
	if (ComponentStorageCount < 4)
	{
		return FString::Printf(
			TEXT("Expected at least 4 canonical component storages, found %d."),
			ComponentStorageCount);
	}
	if (SpawnedEntityCount != ExpectedEntityCount || FinalEntityCount != ExpectedEntityCount)
	{
		return FString::Printf(
			TEXT("Entity count changed or setup was incomplete (expected=%d, spawned=%d, final=%d)."),
			ExpectedEntityCount,
			SpawnedEntityCount,
			FinalEntityCount);
	}
	if (MaxDynamicEntries <= 0)
	{
		return TEXT("Collision broadphase produced no dynamic entries.");
	}
	if (!bAnyEntityMoved)
	{
		return TEXT("No entity moved; the collision workload was vacuous.");
	}
	if (Frames.Num() != ExpectedTicks)
	{
		return FString::Printf(TEXT("Expected %d trace frames, captured %d."), ExpectedTicks, Frames.Num());
	}
	for (int32 Index = 0; Index < Frames.Num(); ++Index)
	{
		if (Frames[Index].Tick != Index + 1)
		{
			return FString::Printf(
				TEXT("Trace tick sequence broke at frame %d (captured tick %d)."),
				Index + 1,
				Frames[Index].Tick);
		}
		if (!Frames[Index].StateRoot.IsValid())
		{
			return FString::Printf(
				TEXT("Canonical state root is invalid at frame %d."),
				Index + 1);
		}
	}
	return FString();
}

FSeinCollisionDeterminismTrace SeinRunCollisionDeterminismScenario(bool bParallel, int32 TickCount)
{
	FSeinCollisionDeterminismTrace Trace;
	Trace.bRequestedParallel = bParallel;
	Trace.ExpectedEntityCount = MovableColumns * MovableRows + StationaryCount;
	if (TickCount <= 0)
	{
		Trace.FailureReason = TEXT("TickCount must be positive.");
		return Trace;
	}

	FScopedParallelCvars ParallelCvars(bParallel);
	Trace.bParallelCvarsAvailable = ParallelCvars.IsValid();
	if (!Trace.bParallelCvarsAvailable)
	{
		Trace.FailureReason = TEXT("Sein parallel cvars were not registered.");
		return Trace;
	}

	FActorTestSpawner Spawner;
	USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
	if (!World)
	{
		Trace.FailureReason = TEXT("The transient world did not create USeinWorldSubsystem.");
		return Trace;
	}
	Trace.bParallelModeObserved =
		SeinSimParallelEnabled() == bParallel && SeinSimParallelMinBatch() == 1;
	Trace.bParallelResolverSelected =
		Cast<USeinCollisionResolverParallel>(World->GetCollisionResolver()) != nullptr;
	// The optional Cover extension binds this delegate for authoritative slot
	// delivery, which deliberately routes collision through its serial seam.
	// This isolated kernel workload owns a fresh world and has no cover state,
	// so detach the unrelated extension hook before proving the parallel path.
	World->AuthoritativeDestinationResolver.Unbind();
	Trace.bAuthoritativeDestinationResolverBound =
		World->AuthoritativeDestinationResolver.IsBound();
	if (!Trace.bParallelModeObserved || !Trace.bParallelResolverSelected
		|| Trace.bAuthoritativeDestinationResolverBound)
	{
		Trace.FailureReason = TEXT("The transient world could not enter the requested collision execution mode.");
		return Trace;
	}

	TArray<FSeinEntityHandle> Handles;
	Handles.Reserve(Trace.ExpectedEntityCount);
	int32 EntityOrdinal = 0;
	bool bAuthoringSucceeded = true;
	const auto AuthorState = [&]()
	{
		for (int32 Row = 0; Row < MovableRows; ++Row)
		{
			for (int32 Column = 0; Column < MovableColumns; ++Column)
			{
				const int32 X = Column * 90 + ((Row & 1) != 0 ? 35 : 0);
				const int32 Y = Row * 80;
				const FFixedVector Position(
					FFixedPoint::FromInt(X),
					FFixedPoint::FromInt(Y),
					FFixedPoint::Zero);
				const FSeinEntityHandle Handle = World->SpawnAbstractEntity(
					FFixedTransform(Position), FSeinPlayerID::Neutral());
				if (!Handle.IsValid())
				{
					Trace.FailureReason =
						TEXT("Failed to spawn a movable collision entity.");
					bAuthoringSucceeded = false;
					return;
				}
				AddScenarioComponents(
					*World, Handle, EntityOrdinal,
					ESeinCollisionMobility::Movable, TickCount);
				Handles.Add(Handle);
				++EntityOrdinal;
			}
		}

		const FIntPoint AnchorPositions[StationaryCount] = {
			FIntPoint(250, 200),
			FIntPoint(780, 200),
			FIntPoint(250, 520),
			FIntPoint(780, 520),
		};
		for (const FIntPoint Position : AnchorPositions)
		{
			const FSeinEntityHandle Handle = World->SpawnAbstractEntity(
				FFixedTransform(FFixedVector(
					FFixedPoint::FromInt(Position.X),
					FFixedPoint::FromInt(Position.Y),
					FFixedPoint::Zero)),
				FSeinPlayerID::Neutral());
			if (!Handle.IsValid())
			{
				Trace.FailureReason =
					TEXT("Failed to spawn a stationary collision anchor.");
				bAuthoringSucceeded = false;
				return;
			}
			AddScenarioComponents(
				*World, Handle, EntityOrdinal,
				ESeinCollisionMobility::Stationary, TickCount);
			Handles.Add(Handle);
			++EntityOrdinal;
		}
	};
	FString BootstrapError;
	if (!SeinTestMatchBootstrap::Materialize(
			*World,
			AuthorState,
			FSeinMatchSettings(),
			0,
			TEXT("SeinARTS.CollisionDeterminism"),
			&BootstrapError)
		|| !bAuthoringSucceeded)
	{
		if (Trace.FailureReason.IsEmpty())
		{
			Trace.FailureReason = BootstrapError.IsEmpty()
				? TEXT("The collision scenario could not materialize tick zero.")
				: BootstrapError;
		}
		return Trace;
	}

	Trace.SpawnedEntityCount = World->GetEntityPool().GetActiveCount();
	Trace.ComponentStorageCount =
		World->GetComponentStorageCount();

	TArray<uint64> InitialPoseWords;
	if (!CapturePoseWords(*World, Handles, InitialPoseWords, Trace.FailureReason))
	{
		return Trace;
	}

	Trace.Frames.Reserve(TickCount);
	if (!SeinTestMatchBootstrap::Start(*World))
	{
		Trace.FailureReason = TEXT("The collision scenario bootstrap could not authorize tick zero.");
		return Trace;
	}
	for (int32 FrameIndex = 0; FrameIndex < TickCount; ++FrameIndex)
	{
		const int32 TickBefore = World->GetCurrentTick();
		FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds());
		if (World->GetCurrentTick() != TickBefore + 1)
		{
			Trace.FailureReason = FString::Printf(
				TEXT("Expected one sim tick at frame %d, advanced from %d to %d."),
				FrameIndex + 1,
				TickBefore,
				World->GetCurrentTick());
			break;
		}

		FSeinCollisionDeterminismFrame Frame;
		Frame.Tick = World->GetCurrentTick();
		if (!World->ComputeCanonicalStateRoot(
				Frame.StateRoot, Trace.FailureReason))
		{
			break;
		}
		if (!CapturePoseWords(*World, Handles, Frame.PoseWords, Trace.FailureReason))
		{
			break;
		}
		Frame.PoseDigest = HashPoseWords(Frame.PoseWords);
		Trace.bAnyEntityMoved |= Frame.PoseWords != InitialPoseWords;
		Trace.MaxDynamicEntries = FMath::Max(
			Trace.MaxDynamicEntries,
			World->GetCollisionSpatialHash().NumDynamicEntries());
		Trace.Frames.Add(MoveTemp(Frame));
	}
	World->StopSimulation();

	Trace.FinalEntityCount = World->GetEntityPool().GetActiveCount();
	return Trace;
}
