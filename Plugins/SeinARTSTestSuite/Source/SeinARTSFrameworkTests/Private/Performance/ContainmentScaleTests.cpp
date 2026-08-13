#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Components/SeinContainmentData.h"
#include "Components/SeinContainmentMemberData.h"
#include "Data/SeinWorldSnapshot.h"
#include "HAL/PlatformTime.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSimContext.h"
#include "Simulation/SeinWorldSubsystem.h"

namespace UE::SeinARTSTests
{
	namespace ContainmentScaleTestLocal
	{
		constexpr int32 TimedSamples = 7;

		bool MeasurePopulation(
			int32 Population,
			double& OutRootMedianMilliseconds,
			double& OutInvalidatedCaptureMedianMilliseconds,
			double& OutWarmCaptureMedianMilliseconds,
			FString& OutError)
		{
			FActorTestSpawner Spawner;
			USeinWorldSubsystem* World =
				Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
			if (!World)
			{
				OutError = TEXT(
					"Fresh world has no Sein simulation subsystem.");
				return false;
			}

			FSeinEntityHandle ContainerHandle;
			FSeinEntityHandle FirstMutationMemberHandle;
			FSeinEntityHandle SecondMutationMemberHandle;
			bool bAuthoringSucceeded = true;
			const auto AuthorState = [&]()
			{
				ContainerHandle = World->SpawnAbstractEntity(
					FFixedTransform(), FSeinPlayerID::Neutral());
				if (!ContainerHandle.IsValid())
				{
					bAuthoringSucceeded = false;
					return;
				}

				FSeinContainmentData Container;
				Container.TotalCapacity = Population;
				Container.CurrentLoad = Population;
				Container.bTracksVisualSlots = true;
				Container.Occupants.Reserve(Population);
				Container.VisualSlotAssignments.Reserve(Population);
				for (int32 Index = 0; Index < Population; ++Index)
				{
					const FSeinEntityHandle Member =
						World->SpawnAbstractEntity(
							FFixedTransform(), FSeinPlayerID::Neutral());
					if (!Member.IsValid())
					{
						bAuthoringSucceeded = false;
						return;
					}
					FSeinContainmentMemberData MemberData;
					MemberData.CurrentContainer = ContainerHandle;
					MemberData.VisualSlotIndex = Index;
					World->AddComponent(Member, MemberData);
					if (Index == 0)
					{
						FirstMutationMemberHandle = Member;
					}
					else if (Index == 1)
					{
						SecondMutationMemberHandle = Member;
					}
					Container.Occupants.Add(Member);
					Container.VisualSlotAssignments.Add(Member);
				}
				World->AddComponent(ContainerHandle, Container);
			};

			if (!SeinTestMatchBootstrap::Materialize(
					*World,
					AuthorState,
					FSeinMatchSettings(),
					0x434F4E53,
					TEXT("Containment.Scale"),
					&OutError)
				|| !bAuthoringSucceeded
				|| !SeinTestMatchBootstrap::Start(*World, &OutError))
			{
				if (OutError.IsEmpty())
				{
					OutError = TEXT(
						"Could not materialize containment scale workload.");
				}
				return false;
			}

			FGuid InitialRoot;
			if (!World->ComputeCanonicalStateRoot(InitialRoot, OutError)
				|| World->GetAllNestedOccupants(ContainerHandle).Num()
					!= Population)
			{
				if (OutError.IsEmpty())
				{
					OutError = TEXT(
						"Containment scale fixture was structurally incomplete.");
				}
				World->StopSimulation();
				return false;
			}

			TArray<double> RootSamples;
			TArray<double> InvalidatedCaptureSamples;
			TArray<double> WarmCaptureSamples;
			RootSamples.Reserve(TimedSamples);
			InvalidatedCaptureSamples.Reserve(TimedSamples);
			WarmCaptureSamples.Reserve(TimedSamples);
			for (int32 Sample = -1; Sample < TimedSamples; ++Sample)
			{
				const FSeinContainmentMemberData* FirstSource =
					World->GetComponent<FSeinContainmentMemberData>(
						FirstMutationMemberHandle);
				const FSeinContainmentMemberData* SecondSource =
					World->GetComponent<FSeinContainmentMemberData>(
						SecondMutationMemberHandle);
				const FSeinContainmentData* ContainerSource =
					World->GetComponent<FSeinContainmentData>(ContainerHandle);
				if (!FirstSource || !SecondSource || !ContainerSource
					|| ContainerSource->VisualSlotAssignments.Num() < 2)
				{
					OutError = TEXT(
						"Containment scale mutation target disappeared.");
					World->StopSimulation();
					return false;
				}
				FSeinContainmentMemberData FirstMember = *FirstSource;
				FSeinContainmentMemberData SecondMember = *SecondSource;
				FSeinContainmentData Container = *ContainerSource;
				const bool bSwap = (Sample + 1) % 2 == 0;
				FirstMember.VisualSlotIndex = bSwap ? 1 : 0;
				SecondMember.VisualSlotIndex = bSwap ? 0 : 1;
				Container.VisualSlotAssignments[0] = bSwap
					? SecondMutationMemberHandle
					: FirstMutationMemberHandle;
				Container.VisualSlotAssignments[1] = bSwap
					? FirstMutationMemberHandle
					: SecondMutationMemberHandle;
				{
					auto SimScope = FSeinSimContextTestAccess::Enter(*World);
					World->AddComponent(FirstMutationMemberHandle, FirstMember);
					World->AddComponent(SecondMutationMemberHandle, SecondMember);
					World->AddComponent(ContainerHandle, Container);
				}

				FGuid Root;
				const double RootStartedAt = FPlatformTime::Seconds();
				const bool bRootComputed =
					World->ComputeCanonicalStateRoot(Root, OutError);
				const double RootMilliseconds =
					(FPlatformTime::Seconds() - RootStartedAt) * 1000.0;
				FGuid VerifiedRoot;
				if (!bRootComputed
					|| !World->ComputeCanonicalStateRoot(
						VerifiedRoot, OutError)
					|| Root != VerifiedRoot)
				{
					if (OutError.IsEmpty())
					{
						OutError = TEXT(
							"Containment scale root changed without mutation.");
					}
					World->StopSimulation();
					return false;
				}

				FSeinWorldSnapshot InvalidatedSnapshot;
				const double CaptureStartedAt = FPlatformTime::Seconds();
				World->CaptureSnapshot(InvalidatedSnapshot);
				const double InvalidatedCaptureMilliseconds =
					(FPlatformTime::Seconds() - CaptureStartedAt) * 1000.0;
				FSeinWorldSnapshot WarmSnapshot;
				const double WarmCaptureStartedAt = FPlatformTime::Seconds();
				World->CaptureSnapshot(WarmSnapshot);
				const double WarmCaptureMilliseconds =
					(FPlatformTime::Seconds() - WarmCaptureStartedAt) * 1000.0;
				if (InvalidatedSnapshot.SnapshotVersion
						!= FSeinWorldSnapshot::CurrentVersion
					|| WarmSnapshot.SnapshotVersion
						!= FSeinWorldSnapshot::CurrentVersion
					|| InvalidatedSnapshot.Entities.Num() != Population + 1
					|| WarmSnapshot.Entities.Num() != Population + 1)
				{
					OutError = TEXT(
						"Containment scale checkpoint was incomplete.");
					World->StopSimulation();
					return false;
				}

				if (Sample >= 0)
				{
					RootSamples.Add(RootMilliseconds);
					InvalidatedCaptureSamples.Add(
						InvalidatedCaptureMilliseconds);
					WarmCaptureSamples.Add(WarmCaptureMilliseconds);
				}
			}
			World->StopSimulation();

			RootSamples.Sort();
			InvalidatedCaptureSamples.Sort();
			WarmCaptureSamples.Sort();
			OutRootMedianMilliseconds =
				RootSamples[RootSamples.Num() / 2];
			OutInvalidatedCaptureMedianMilliseconds =
				InvalidatedCaptureSamples[
					InvalidatedCaptureSamples.Num() / 2];
			OutWarmCaptureMedianMilliseconds =
				WarmCaptureSamples[WarmCaptureSamples.Num() / 2];
			return true;
		}
	}

	TEST(ContainmentValidationHasMeasuredPopulationCurve,
		"SeinARTS.Perf.Containment")
	{
		using namespace ContainmentScaleTestLocal;
		const int32 Populations[] = {100, 500, 1000};
		double RootMedians[UE_ARRAY_COUNT(Populations)] = {};
		double InvalidatedCaptureMedians[UE_ARRAY_COUNT(Populations)] = {};
		double WarmCaptureMedians[UE_ARRAY_COUNT(Populations)] = {};
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Populations); ++Index)
		{
			FString Error;
			const bool bMeasured = MeasurePopulation(
				Populations[Index],
				RootMedians[Index],
				InvalidatedCaptureMedians[Index],
				WarmCaptureMedians[Index],
				Error);
			if (!bMeasured)
			{
				UE_LOG(LogTemp, Error,
					TEXT("Containment scale fixture failed: %s"),
					*Error);
			}
			ASSERT_THAT(IsTrue(bMeasured));
			UE_LOG(LogTemp, Display,
				TEXT("Containment at %d occupants: canonical root %.3f ms, invalidated checkpoint %.3f ms, warm checkpoint %.3f ms"),
				Populations[Index],
				RootMedians[Index],
				InvalidatedCaptureMedians[Index],
				WarmCaptureMedians[Index]);
		}

		ASSERT_THAT(IsTrue(RootMedians[2] < 50.0));
		ASSERT_THAT(IsTrue(InvalidatedCaptureMedians[2] < 100.0));
		ASSERT_THAT(IsTrue(WarmCaptureMedians[2] < 100.0));
		ASSERT_THAT(IsTrue(
			RootMedians[2] < FMath::Max(1.0, RootMedians[0] * 20.0)));
	}
}
