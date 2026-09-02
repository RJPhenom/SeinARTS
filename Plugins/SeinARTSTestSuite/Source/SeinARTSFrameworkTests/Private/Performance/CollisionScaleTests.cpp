#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Collision/SeinCollisionResolverDefault.h"
#include "Components/SeinExtentsPayload.h"
#include "HAL/PlatformTime.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSimContext.h"
#include "Simulation/SeinWorldSubsystem.h"

struct FSeinWorldSubsystemTestAccess
{
	static bool TickSimulation(USeinWorldSubsystem& World, float DeltaTime)
	{
		return World.TickSimulation(DeltaTime);
	}
};

namespace UE::SeinARTSTests
{
	namespace CollisionScaleTestLocal
	{
		constexpr int32 TimedSamples = 7;
		constexpr int32 Radius = 50;
		constexpr int32 Spacing = 75;

		struct FScopedDefaultCollisionResolver
		{
			FScopedDefaultCollisionResolver()
			{
				Settings = GetMutableDefault<USeinARTSCoreSettings>();
				check(Settings);
				PreviousClass = Settings->CollisionResolverClass;
				Settings->CollisionResolverClass = FSoftClassPath(
					USeinCollisionResolverDefault::StaticClass());
			}

			~FScopedDefaultCollisionResolver()
			{
				Settings->CollisionResolverClass = PreviousClass;
			}

			USeinARTSCoreSettings* Settings = nullptr;
			FSoftClassPath PreviousClass;
		};

		FFixedVector PackedPosition(int32 Index, int32 Columns)
		{
			return FFixedVector(
				FFixedPoint::FromInt((Index % Columns) * Spacing),
				FFixedPoint::FromInt((Index / Columns) * Spacing),
				FFixedPoint::Zero);
		}

		bool MeasurePopulation(
			int32 Population,
			double& OutMedianMilliseconds,
			FString& OutError)
		{
			FScopedDefaultCollisionResolver ResolverScope;
			FActorTestSpawner Spawner;
			USeinWorldSubsystem* World =
				Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
			if (!World
				|| !Cast<USeinCollisionResolverDefault>(
					World->GetCollisionResolver()))
			{
				OutError = TEXT("Fresh world did not select the default collision resolver.");
				return false;
			}

			const int32 Columns = FMath::CeilToInt(FMath::Sqrt(
				static_cast<double>(Population)));
			TArray<FSeinEntityHandle> Handles;
			Handles.Reserve(Population);
			bool bAuthoringSucceeded = true;
			const auto AuthorState = [&]()
			{
				for (int32 Index = 0; Index < Population; ++Index)
				{
					const FSeinEntityHandle Handle = World->SpawnAbstractEntity(
						FFixedTransform(PackedPosition(Index, Columns)),
						FSeinPlayerID::Neutral());
					if (!Handle.IsValid())
					{
						bAuthoringSucceeded = false;
						return;
					}

					FSeinExtentsShape Shape;
					Shape.Shape = ESeinExtentsShape::Capsule;
					Shape.Radius = FFixedPoint::FromInt(Radius);
					Shape.Height = FFixedPoint::FromInt(100);
					FSeinExtentsPayload Extents;
					Extents.Shapes.Add(Shape);
					Extents.bCollisionEnabled = true;
					Extents.Mobility = ESeinCollisionMobility::Movable;
					Extents.Mass = FFixedPoint::FromInt(100);
					Extents.ObjectType.Channel = FName(TEXT("Default"));
					World->AddComponent(Handle, Extents);
					Handles.Add(Handle);
				}
			};

			if (!SeinTestMatchBootstrap::Materialize(
					*World,
					AuthorState,
					FSeinMatchSettings(),
					0,
					TEXT("SeinARTS.CollisionScale"),
					&OutError)
				|| !bAuthoringSucceeded
				|| Handles.Num() != Population
				|| !SeinTestMatchBootstrap::Start(*World, &OutError))
			{
				if (OutError.IsEmpty())
				{
					OutError = TEXT("Could not materialize the collision scale workload.");
				}
				return false;
			}

			TArray<double> Samples;
			Samples.Reserve(TimedSamples);
			for (int32 Sample = -1; Sample < TimedSamples; ++Sample)
			{
				{
					auto SimScope = FSeinSimContextTestAccess::Enter(*World);
					for (int32 Index = 0; Index < Handles.Num(); ++Index)
					{
						FSeinEntity* Entity =
							World->GetEntityMutable(Handles[Index]);
						if (!Entity)
						{
							OutError = TEXT("Collision scale entity disappeared.");
							World->StopSimulation();
							return false;
						}
						Entity->Transform.SetLocation(
							PackedPosition(Index, Columns));
					}
				}

				const int32 TickBefore = World->GetCurrentTick();
				const double StartedAt = FPlatformTime::Seconds();
				const bool bSchedulerRetained =
					FSeinWorldSubsystemTestAccess::TickSimulation(
						*World, World->GetFixedDeltaTimeSeconds());
				const double ElapsedMilliseconds =
					(FPlatformTime::Seconds() - StartedAt) * 1000.0;
				if (!bSchedulerRetained
					|| World->GetCurrentTick() != TickBefore + 1)
				{
					OutError = TEXT("Collision scale sample did not advance exactly one tick.");
					World->StopSimulation();
					return false;
				}
				if (Sample == -1)
				{
					const FSeinCollisionSpatialHash& Broadphase =
						World->GetCollisionSpatialHash();
					if (Broadphase.NumDynamicEntries() < Population)
					{
						OutError = TEXT("Collision scale broadphase omitted population entries.");
						World->StopSimulation();
						return false;
					}
					TArray<FSeinEntityHandle> Nearby;
					for (int32 Index = 0; Index < Handles.Num(); ++Index)
					{
						Nearby.Reset();
						// The broadphase is rebuilt in PreTick and collision resolution
						// moves entities later in PostTick. Query the positive-radius
						// pre-resolution location that this broadphase represents.
						Broadphase.QueryRadius(
							PackedPosition(Index, Columns),
							FFixedPoint::One,
							Nearby);
						if (!Nearby.Contains(Handles[Index]))
						{
							OutError = FString::Printf(
								TEXT("Collision scale broadphase omitted handle %s."),
								*Handles[Index].ToString());
							World->StopSimulation();
							return false;
						}
					}
				}
				const FSeinEntity* Probe = World->GetEntity(Handles[0]);
				if (!Probe
					|| Probe->Transform.GetLocation() == PackedPosition(0, Columns))
				{
					OutError = TEXT("Collision scale workload was vacuous.");
					World->StopSimulation();
					return false;
				}
				if (Sample >= 0)
				{
					Samples.Add(ElapsedMilliseconds);
				}
			}
			World->StopSimulation();

			Samples.Sort();
			OutMedianMilliseconds = Samples[Samples.Num() / 2];
			return true;
		}
	}

	TEST(DenseCollisionFixedTickHasMeasuredPopulationCurve,
		"SeinARTS.Perf.Collision.Scale")
	{
		using namespace CollisionScaleTestLocal;
		const int32 Populations[] = {64, 128, 256};
		double Medians[UE_ARRAY_COUNT(Populations)] = {};
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Populations); ++Index)
		{
			FString Error;
			ASSERT_THAT(IsTrue(MeasurePopulation(
				Populations[Index], Medians[Index], Error)));
			UE_LOG(LogTemp, Display,
				TEXT("Dense collision fixed tick median at %d movers: %.3f ms"),
				Populations[Index], Medians[Index]);
		}

		ASSERT_THAT(IsTrue(Medians[2] < 25.0));
	}
}
