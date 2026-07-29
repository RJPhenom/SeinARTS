#include "CQTest.h"

#include "Algo/Reverse.h"
#include "Serialization/SeinCanonicalStateRoot.h"

namespace UE::SeinARTSTests
{
	namespace
	{
		FGuid MakeRootDigest(uint32 Seed)
		{
			return FGuid(
				Seed,
				Seed + 0x10101010u,
				Seed + 0x20202020u,
				Seed + 0x30303030u);
		}

		FSeinCanonicalStateRootLeaf MakeRootLeaf(
			const TCHAR* SectionId,
			ESeinSnapshotSectionRole Role,
			uint32 Seed)
		{
			FSeinCanonicalStateRootLeaf Leaf;
			Leaf.SectionId = SectionId;
			Leaf.Role = Role;
			Leaf.SchemaVersion = Seed;
			Leaf.SchemaDigest = MakeRootDigest(Seed * 10);
			Leaf.DescriptorDigest = MakeRootDigest(Seed * 100);
			Leaf.PayloadBytes = Seed * 7;
			Leaf.LeafDigest = MakeRootDigest(Seed * 1000);
			return Leaf;
		}

		FSeinCanonicalStateRootIdentity MakeRootIdentity()
		{
			FSeinCanonicalStateRootIdentity Identity;
			Identity.Tick = 42;
			Identity.CommandProtocolDigest = MakeRootDigest(101);
			Identity.CompatibilityDigest = MakeRootDigest(102);
			return Identity;
		}

		TArray<FSeinCanonicalStateRootLeaf> MakeRootLeaves()
		{
			return {
				MakeRootLeaf(
					TEXT("local.camera"),
					ESeinSnapshotSectionRole::Local,
					4),
				MakeRootLeaf(
					TEXT("core.cache"),
					ESeinSnapshotSectionRole::DerivedCache,
					3),
				MakeRootLeaf(
					TEXT("core.bravo"),
					ESeinSnapshotSectionRole::Continuation,
					2),
				MakeRootLeaf(
					TEXT("core.alpha"),
					ESeinSnapshotSectionRole::Authoritative,
					1),
			};
		}

		bool ComposeRoot(
			const FSeinCanonicalStateRootIdentity& Identity,
			const TArray<FSeinCanonicalStateRootLeaf>& Leaves,
			FGuid& OutRoot,
			FString& OutError)
		{
			return FSeinCanonicalStateRootComposer::Compose(
				Identity, Leaves, OutRoot, OutError);
		}
	}

	TEST(CanonicalStateRootIsOrderIndependentAndFutureStateOnly,
		"SeinARTS.Unit.CoreEntity.CanonicalState.Root")
	{
		const FSeinCanonicalStateRootIdentity Identity =
			MakeRootIdentity();
		const TArray<FSeinCanonicalStateRootLeaf> BaselineLeaves =
			MakeRootLeaves();
		FGuid BaselineRoot;
		FString Error;
		ASSERT_THAT(IsTrue(ComposeRoot(
			Identity, BaselineLeaves, BaselineRoot, Error)));
		ASSERT_THAT(IsTrue(BaselineRoot.IsValid()));

		TArray<FSeinCanonicalStateRootLeaf> Permuted = BaselineLeaves;
		Algo::Reverse(Permuted);
		FGuid PermutedRoot;
		ASSERT_THAT(IsTrue(ComposeRoot(
			Identity, Permuted, PermutedRoot, Error)));
		ASSERT_THAT(IsTrue(PermutedRoot == BaselineRoot));

		TArray<FSeinCanonicalStateRootLeaf> NonFutureChanged =
			BaselineLeaves;
		NonFutureChanged[0].LeafDigest.D++;
		NonFutureChanged[1].SchemaVersion++;
		NonFutureChanged[1].SchemaDigest.D++;
		FGuid NonFutureRoot;
		ASSERT_THAT(IsTrue(ComposeRoot(
			Identity, NonFutureChanged, NonFutureRoot, Error)));
		ASSERT_THAT(IsTrue(NonFutureRoot == BaselineRoot));

		TArray<FSeinCanonicalStateRootLeaf> AuthoritativeChanged =
			BaselineLeaves;
		AuthoritativeChanged[3].LeafDigest.D++;
		FGuid AuthoritativeRoot;
		ASSERT_THAT(IsTrue(ComposeRoot(
			Identity, AuthoritativeChanged, AuthoritativeRoot, Error)));
		ASSERT_THAT(IsTrue(AuthoritativeRoot != BaselineRoot));

		TArray<FSeinCanonicalStateRootLeaf> ContinuationChanged =
			BaselineLeaves;
		ContinuationChanged[2].LeafDigest.D++;
		FGuid ContinuationRoot;
		ASSERT_THAT(IsTrue(ComposeRoot(
			Identity, ContinuationChanged, ContinuationRoot, Error)));
		ASSERT_THAT(IsTrue(ContinuationRoot != BaselineRoot));

		FSeinCanonicalStateRootIdentity ChangedIdentity = Identity;
		ChangedIdentity.Tick++;
		FGuid ChangedIdentityRoot;
		ASSERT_THAT(IsTrue(ComposeRoot(
			ChangedIdentity,
			BaselineLeaves,
			ChangedIdentityRoot,
			Error)));
		ASSERT_THAT(IsTrue(ChangedIdentityRoot != BaselineRoot));

		ChangedIdentity = Identity;
		ChangedIdentity.CommandProtocolDigest.D++;
		ASSERT_THAT(IsTrue(ComposeRoot(
			ChangedIdentity,
			BaselineLeaves,
			ChangedIdentityRoot,
			Error)));
		ASSERT_THAT(IsTrue(ChangedIdentityRoot != BaselineRoot));

		ChangedIdentity = Identity;
		ChangedIdentity.CompatibilityDigest.D++;
		ASSERT_THAT(IsTrue(ComposeRoot(
			ChangedIdentity,
			BaselineLeaves,
			ChangedIdentityRoot,
			Error)));
		ASSERT_THAT(IsTrue(ChangedIdentityRoot != BaselineRoot));
	}

	TEST(CanonicalStateRootRejectsMalformedEvidenceTransactionally,
		"SeinARTS.Unit.CoreEntity.CanonicalState.Root.Security")
	{
		const FSeinCanonicalStateRootIdentity Identity =
			MakeRootIdentity();
		const TArray<FSeinCanonicalStateRootLeaf> BaselineLeaves =
			MakeRootLeaves();
		const FGuid Sentinel = MakeRootDigest(999);
		FGuid Root = Sentinel;
		FString Error;

		auto Rejects = [&Identity, &Root, &Sentinel, &Error](
			const TArray<FSeinCanonicalStateRootLeaf>& Leaves)
		{
			Root = Sentinel;
			Error.Reset();
			return !ComposeRoot(Identity, Leaves, Root, Error)
				&& Root == Sentinel
				&& !Error.IsEmpty();
		};

		TArray<FSeinCanonicalStateRootLeaf> Invalid =
			BaselineLeaves;
		Invalid[0].SectionId = TEXT("Uppercase.invalid");
		ASSERT_THAT(IsTrue(Rejects(Invalid)));

		Invalid = BaselineLeaves;
		Invalid[1].SectionId = Invalid[0].SectionId;
		ASSERT_THAT(IsTrue(Rejects(Invalid)));

		Invalid = BaselineLeaves;
		Invalid[2].SchemaVersion = 0;
		ASSERT_THAT(IsTrue(Rejects(Invalid)));

		Invalid = BaselineLeaves;
		Invalid[2].SchemaDigest.Invalidate();
		ASSERT_THAT(IsTrue(Rejects(Invalid)));

		Invalid = BaselineLeaves;
		Invalid[2].DescriptorDigest.Invalidate();
		ASSERT_THAT(IsTrue(Rejects(Invalid)));

		Invalid = BaselineLeaves;
		Invalid[2].LeafDigest.Invalidate();
		ASSERT_THAT(IsTrue(Rejects(Invalid)));

		Invalid = BaselineLeaves;
		Invalid[2].Role =
			static_cast<ESeinSnapshotSectionRole>(255);
		ASSERT_THAT(IsTrue(Rejects(Invalid)));

		Invalid = BaselineLeaves;
		Invalid[2].Codec =
			static_cast<ESeinSnapshotSectionCodec>(255);
		ASSERT_THAT(IsTrue(Rejects(Invalid)));

		Invalid = BaselineLeaves;
		Invalid[2].PayloadBytes =
			FSeinCanonicalStateRootComposer::MaxSectionPayloadBytes + 1;
		ASSERT_THAT(IsTrue(Rejects(Invalid)));

		FSeinCanonicalStateRootIdentity InvalidIdentity = Identity;
		InvalidIdentity.Tick = -1;
		Root = Sentinel;
		ASSERT_THAT(IsFalse(ComposeRoot(
			InvalidIdentity, BaselineLeaves, Root, Error)));
		ASSERT_THAT(IsTrue(Root == Sentinel && !Error.IsEmpty()));

		InvalidIdentity = Identity;
		InvalidIdentity.CommandProtocolDigest.Invalidate();
		Root = Sentinel;
		ASSERT_THAT(IsFalse(ComposeRoot(
			InvalidIdentity, BaselineLeaves, Root, Error)));
		ASSERT_THAT(IsTrue(Root == Sentinel && !Error.IsEmpty()));

		InvalidIdentity = Identity;
		InvalidIdentity.CompatibilityDigest.Invalidate();
		Root = Sentinel;
		ASSERT_THAT(IsFalse(ComposeRoot(
			InvalidIdentity, BaselineLeaves, Root, Error)));
		ASSERT_THAT(IsTrue(Root == Sentinel && !Error.IsEmpty()));
	}
}
