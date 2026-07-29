#include "CQTest.h"

#include "Serialization/SeinSimulationContentRegistry.h"

namespace UE::SeinARTSTests
{
	namespace
	{
		FSeinSimulationContentContributorDescriptor
			MakeSimulationContentDescriptor(uint32 Revision = 7401)
		{
			FSeinSimulationContentContributorDescriptor Descriptor;
			Descriptor.StableContributorId =
				TEXT("seinframeworktest.simcontent.reload");
			Descriptor.ContributorRevision = Revision;

			FSeinSimulationContentDiscoveryRoot& Root =
				Descriptor.DiscoveryRoots.AddDefaulted_GetRef();
			Root.RootClassPath =
				TEXT("/Script/SeinARTSCoreEntity.SeinCanonicalStateRecipe");
			Root.StableRecordKindId =
				FSeinSimulationContentManifestCodec::
					GetCurrentRecordKindId();
			Root.RecordRevision =
				FSeinSimulationContentManifestCodec::
					CurrentRecordRevision;
			return Descriptor;
		}

		int32 CountContributor(
			const FSeinSimulationContentRegistrySnapshot& Snapshot,
			const FString& StableId)
		{
			int32 Count = 0;
			for (const FSeinFrozenSimulationContentContributor&
				Contributor : Snapshot.Contributors)
			{
				Count += Contributor.StableContributorId
					== StableId;
			}
			return Count;
		}
	}

	TEST(SimulationContentReloadClaimsAreGenerationSafe,
		"SeinARTS.Unit.SimulationContent.Registry.Reload")
	{
		const int32 BaselineCount =
			FSeinSimulationContentRegistry::
				GetRegisteredContributorCount();
		const FSeinSimulationContentContributorDescriptor Descriptor =
			MakeSimulationContentDescriptor();
		FString Error;

		FSeinSimulationContentRegistrationHandle Previous =
			FSeinSimulationContentRegistry::RegisterContributor(
				Descriptor, &Error);
		ASSERT_THAT(IsTrue(Previous.IsValid()));
		ASSERT_THAT(IsTrue(Error.IsEmpty()));

		FSeinSimulationContentRegistrationHandle Replacement =
			FSeinSimulationContentRegistry::RegisterContributor(
				Descriptor, &Error);
		ASSERT_THAT(IsTrue(Replacement.IsValid()));
		ASSERT_THAT(IsTrue(Error.IsEmpty()));
		ASSERT_THAT(AreEqual(
			BaselineCount + 1,
			FSeinSimulationContentRegistry::
				GetRegisteredContributorCount()));

		FSeinSimulationContentRegistrySnapshot DuringOverlap;
		ASSERT_THAT(IsTrue(
			FSeinSimulationContentRegistry::CaptureSnapshot(
				DuringOverlap, Error)));
		ASSERT_THAT(AreEqual(
			1,
			CountContributor(
				DuringOverlap,
				Descriptor.StableContributorId)));

		Previous.Reset();
		ASSERT_THAT(AreEqual(
			BaselineCount + 1,
			FSeinSimulationContentRegistry::
				GetRegisteredContributorCount()));
		FSeinSimulationContentRegistrySnapshot AfterOldShutdown;
		ASSERT_THAT(IsTrue(
			FSeinSimulationContentRegistry::CaptureSnapshot(
				AfterOldShutdown, Error)));
		ASSERT_THAT(AreEqual(
			1,
			CountContributor(
				AfterOldShutdown,
				Descriptor.StableContributorId)));

		FSeinSimulationContentRegistrationHandle Conflicting =
			FSeinSimulationContentRegistry::RegisterContributor(
				MakeSimulationContentDescriptor(7402),
				&Error);
		ASSERT_THAT(IsFalse(Conflicting.IsValid()));
		ASSERT_THAT(IsTrue(
			Error.Contains(TEXT("different discovery contract"))));
		FSeinSimulationContentRegistrySnapshot PoisonedSnapshot;
		ASSERT_THAT(IsFalse(
			FSeinSimulationContentRegistry::CaptureSnapshot(
				PoisonedSnapshot, Error)));
		ASSERT_THAT(IsTrue(
			Error.Contains(TEXT("failed live module-generation"))));

		Conflicting.Reset();
		ASSERT_THAT(IsTrue(
			FSeinSimulationContentRegistry::CaptureSnapshot(
				AfterOldShutdown, Error)));

		Replacement.Reset();
		ASSERT_THAT(AreEqual(
			BaselineCount,
			FSeinSimulationContentRegistry::
				GetRegisteredContributorCount()));
	}
}
