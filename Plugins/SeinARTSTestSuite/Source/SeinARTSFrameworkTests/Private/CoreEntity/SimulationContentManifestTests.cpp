#include "CQTest.h"

#include "Serialization/SeinSimulationContentManifest.h"

namespace UE::SeinARTSTests
{
	namespace
	{
		FSeinSimulationContentContributorRecord MakeContributor(
			const TCHAR* StableId,
			int32 Revision,
			const FGuid& DiscoveryDigest)
		{
			FSeinSimulationContentContributorRecord Contributor;
			Contributor.StableContributorId = StableId;
			Contributor.ContributorRevision = Revision;
			Contributor.DiscoveryContractDigest = DiscoveryDigest;
			return Contributor;
		}

		bool MakeRecord(
			const TCHAR* PackageName,
			uint8 Seed,
			FSeinSimulationContentRecord& OutRecord,
			FString& OutError)
		{
			TArray<uint8> SavedHash;
			SavedHash.SetNumUninitialized(
				FSeinSimulationContentManifestCodec::
					SavedPackageHashBytes);
			for (int32 Index = 0; Index < SavedHash.Num(); ++Index)
			{
				SavedHash[Index] =
					static_cast<uint8>(Seed + Index);
			}

			OutRecord = {};
			OutRecord.StableRecordKindId =
				FSeinSimulationContentManifestCodec::
					GetCurrentRecordKindId();
			OutRecord.RecordRevision =
				static_cast<int32>(
					FSeinSimulationContentManifestCodec::
						CurrentRecordRevision);
			OutRecord.CanonicalRecordId = PackageName;
			return FSeinSimulationContentManifestCodec::
				ComputeRecordDigest(
					OutRecord.StableRecordKindId,
					static_cast<uint32>(
						OutRecord.RecordRevision),
					OutRecord.CanonicalRecordId,
					SavedHash,
					OutRecord.ContentDigest,
					OutError);
		}

		FSeinSimulationContentManifestProfile MakeProfile(
			FSeinSimulationContentContributorRecord Contributor,
			FSeinSimulationContentRecord Record)
		{
			FSeinSimulationContentManifestProfile Profile;
			Profile.BuilderRevision =
				static_cast<int32>(
					FSeinSimulationContentManifestCodec::
						CurrentBuilderRevision);
			Profile.Contributors.Add(MoveTemp(Contributor));
			Profile.Records.Add(MoveTemp(Record));
			return Profile;
		}
	}

	TEST(SimulationContentManifestCanonicalizesOrderAndDuplicates,
		"SeinARTS.Unit.SimulationContent.Manifest.Canonicalization")
	{
		FString Error;
		FSeinSimulationContentRecord RecordA;
		FSeinSimulationContentRecord RecordB;
		ASSERT_THAT(IsTrue(MakeRecord(
			TEXT("/Game/SeinTests/ContentA"),
			1,
			RecordA,
			Error)));
		ASSERT_THAT(IsTrue(MakeRecord(
			TEXT("/Game/SeinTests/ContentB"),
			2,
			RecordB,
			Error)));

		const FSeinSimulationContentContributorRecord ContributorA =
			MakeContributor(
				TEXT("SeinFrameworkTest.Content.A"),
				1,
				FGuid(1, 2, 3, 4));
		const FSeinSimulationContentContributorRecord ContributorB =
			MakeContributor(
				TEXT("seinframeworktest.content.b"),
				2,
				FGuid(5, 6, 7, 8));
		const TArray<FSeinSimulationContentContributorRecord>
			ReverseContributors = { ContributorB, ContributorA };
		const TArray<FSeinSimulationContentContributorRecord>
			ForwardContributors = { ContributorA, ContributorB };
		const TArray<FSeinSimulationContentRecord> ReverseRecords = {
			RecordB, RecordA, RecordA
		};
		const TArray<FSeinSimulationContentRecord> ForwardRecords = {
			RecordA, RecordB
		};

		TArray<FSeinSimulationContentContributorRecord> CanonicalContributors;
		TArray<FSeinSimulationContentRecord> CanonicalRecords;
		ASSERT_THAT(IsTrue(
			FSeinSimulationContentManifestCodec::Canonicalize(
				ReverseContributors,
				ReverseRecords,
				CanonicalContributors,
				CanonicalRecords,
				Error)));
		ASSERT_THAT(AreEqual(2, CanonicalContributors.Num()));
		ASSERT_THAT(AreEqual(
			FString(TEXT("seinframeworktest.content.a")),
			CanonicalContributors[0].StableContributorId));
		ASSERT_THAT(AreEqual(2, CanonicalRecords.Num()));
		ASSERT_THAT(AreEqual(
			RecordA.CanonicalRecordId,
			CanonicalRecords[0].CanonicalRecordId));

		FGuid RootForward;
		FGuid RootReverse;
		ASSERT_THAT(IsTrue(
			FSeinSimulationContentManifestCodec::ComputeRootDigest(
				FSeinSimulationContentManifestCodec::
					CurrentFormatVersion,
				FSeinSimulationContentManifestCodec::
					CurrentBuilderRevision,
				ForwardContributors,
				ForwardRecords,
				RootForward,
				Error)));
		ASSERT_THAT(IsTrue(
			FSeinSimulationContentManifestCodec::ComputeRootDigest(
				FSeinSimulationContentManifestCodec::
					CurrentFormatVersion,
				FSeinSimulationContentManifestCodec::
					CurrentBuilderRevision,
				ReverseContributors,
				ReverseRecords,
				RootReverse,
				Error)));
		ASSERT_THAT(IsTrue(RootForward == RootReverse));
	}

	TEST(SimulationContentManifestSelectsOnlyTheExactContributorProfile,
		"SeinARTS.Unit.SimulationContent.Manifest.ProfileSelection")
	{
		FString Error;
		FSeinSimulationContentRecord RecordA;
		FSeinSimulationContentRecord RecordB;
		ASSERT_THAT(IsTrue(MakeRecord(
			TEXT("/Game/SeinTests/ProfileA"),
			11,
			RecordA,
			Error)));
		ASSERT_THAT(IsTrue(MakeRecord(
			TEXT("/Game/SeinTests/ProfileB"),
			21,
			RecordB,
			Error)));

		const FSeinSimulationContentContributorRecord ContributorA =
			MakeContributor(
				TEXT("seinframeworktest.profile.a"),
				1,
				FGuid(11, 12, 13, 14));
		const FSeinSimulationContentContributorRecord ContributorB =
			MakeContributor(
				TEXT("seinframeworktest.profile.b"),
				1,
				FGuid(21, 22, 23, 24));
		FSeinSimulationContentManifestProfile ProfileA =
			MakeProfile(ContributorA, RecordA);
		FSeinSimulationContentManifestProfile ProfileB =
			MakeProfile(ContributorB, RecordB);

		USeinSimulationContentManifest* Manifest =
			NewObject<USeinSimulationContentManifest>();
		ASSERT_THAT(IsNotNull(Manifest));
		ASSERT_THAT(IsTrue(
			FSeinSimulationContentManifestCodec::UpsertProfile(
				*Manifest,
				ProfileB,
				Error)));
		ASSERT_THAT(IsTrue(
			FSeinSimulationContentManifestCodec::UpsertProfile(
				*Manifest,
				ProfileA,
				Error)));
		ASSERT_THAT(AreEqual(2, Manifest->Profiles.Num()));
		ASSERT_THAT(IsTrue(Manifest->Validate(Error)));

		FSeinSimulationContentManifestProfile Selected;
		const TArray<FSeinSimulationContentContributorRecord>
			ActiveContributors = { ContributorA };
		ASSERT_THAT(IsTrue(
			FSeinSimulationContentManifestCodec::SelectExactProfile(
				*Manifest,
				FSeinSimulationContentManifestCodec::
					CurrentBuilderRevision,
				ActiveContributors,
				Selected,
				Error)));
		ASSERT_THAT(AreEqual(1, Selected.Contributors.Num()));
		ASSERT_THAT(AreEqual(
			FString(TEXT("seinframeworktest.profile.a")),
			Selected.Contributors[0].StableContributorId));
		ASSERT_THAT(AreEqual(
			RecordA.CanonicalRecordId,
			Selected.Records[0].CanonicalRecordId));

		ASSERT_THAT(IsFalse(
			FSeinSimulationContentManifestCodec::SelectExactProfile(
				*Manifest,
				FSeinSimulationContentManifestCodec::
					CurrentBuilderRevision + 1,
				ActiveContributors,
				Selected,
				Error)));
		ASSERT_THAT(IsTrue(
			Error.Contains(TEXT("header or profile count"))));
	}

	/** A records-free profile is how the runtime plays when no manifest is
	 *  configured (synthesized mode): sealing must accept it, reproduce the same
	 *  digest for the same contributor set, and keep that digest distinct from a
	 *  baked profile carrying package records. */
	TEST(SimulationContentSealsRecordFreeSynthesizedProfile,
		"SeinARTS.Unit.CoreEntity.SimulationContent")
	{
		const FGuid DiscoveryDigest(1u, 2u, 3u, 4u);
		FString Error;

		FSeinSimulationContentManifestProfile Synthesized;
		Synthesized.BuilderRevision =
			static_cast<int32>(
				FSeinSimulationContentManifestCodec::
					CurrentBuilderRevision);
		Synthesized.Contributors.Add(MakeContributor(
			TEXT("seinframeworktest.synth.a"), 1, DiscoveryDigest));
		ASSERT_THAT(IsTrue(
			FSeinSimulationContentManifestCodec::SealProfile(
				FSeinSimulationContentManifestCodec::
					CurrentFormatVersion,
				Synthesized,
				Error)));
		ASSERT_THAT(IsTrue(Synthesized.RootDigest.IsValid()));
		ASSERT_THAT(IsTrue(
			FSeinSimulationContentManifestCodec::ValidateProfile(
				FSeinSimulationContentManifestCodec::
					CurrentFormatVersion,
				Synthesized,
				Error)));

		FSeinSimulationContentManifestProfile Again;
		Again.BuilderRevision = Synthesized.BuilderRevision;
		Again.Contributors.Add(MakeContributor(
			TEXT("seinframeworktest.synth.a"), 1, DiscoveryDigest));
		ASSERT_THAT(IsTrue(
			FSeinSimulationContentManifestCodec::SealProfile(
				FSeinSimulationContentManifestCodec::
					CurrentFormatVersion,
				Again,
				Error)));
		ASSERT_THAT(IsTrue(Again.RootDigest == Synthesized.RootDigest));

		FSeinSimulationContentRecord Record;
		ASSERT_THAT(IsTrue(MakeRecord(
			TEXT("/Game/SeinFrameworkTest/SynthDelta"),
			7,
			Record,
			Error)));
		FSeinSimulationContentManifestProfile Baked = MakeProfile(
			MakeContributor(
				TEXT("seinframeworktest.synth.a"), 1, DiscoveryDigest),
			MoveTemp(Record));
		ASSERT_THAT(IsTrue(
			FSeinSimulationContentManifestCodec::SealProfile(
				FSeinSimulationContentManifestCodec::
					CurrentFormatVersion,
				Baked,
				Error)));
		ASSERT_THAT(IsFalse(Baked.RootDigest == Synthesized.RootDigest));
	}
}
