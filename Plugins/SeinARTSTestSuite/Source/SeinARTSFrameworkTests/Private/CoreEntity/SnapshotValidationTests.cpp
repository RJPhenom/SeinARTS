#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Abilities/SeinAbility.h"
#include "Abilities/Actions/SeinWaitAction.h"
#include "Abilities/SeinLatentActionManager.h"
#include "Brokers/SeinCommandBrokerResolver.h"
#include "Brokers/SeinDefaultCommandBrokerResolver.h"
#include "Components/SeinAbilityComponent.h"
#include "Data/SeinWorldSnapshot.h"
#include "Lib/SeinAbilityBPFL.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Serialization/SeinCanonicalStateRegistry.h"
#include "Serialization/SeinLatentActionCodecRegistry.h"
#include "Serialization/SeinPoolObjectCodecRegistry.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSnapshotRestore.h"
#include "Simulation/SeinTestSimContext.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "TestTypes/SeinEffectMutationTestTypes.h"
#include "TestTypes/SeinSnapshotValidationTestTypes.h"
#include "UObject/StrongObjectPtr.h"

USeinSnapshotPassiveTestAbility::USeinSnapshotPassiveTestAbility()
{
	bIsPassive = true;
}

struct FSeinSnapshotPoolTestAccess
{
	static bool RewriteAbilityState(
		USeinWorldSubsystem& World,
		FSeinSnapshotPoolInstanceRecord& Record,
		TFunctionRef<void(USeinAbility&)> Mutator)
	{
		FString Error;
		TStrongObjectPtr<UObject> Scratch(
			FSeinPoolObjectCodecRegistry::MaterializeObject(
				World.PoolObjectCodecManifest,
				Record,
				ESeinPoolObjectKind::Ability,
				World,
				Error));
		USeinAbility* Ability = Cast<USeinAbility>(Scratch.Get());
		if (!Ability)
		{
			return false;
		}

		Mutator(*Ability);
		FSeinSnapshotPoolInstanceRecord Rewritten;
		if (!FSeinPoolObjectCodecRegistry::CaptureObject(
				World.PoolObjectCodecManifest,
				*Ability,
				ESeinPoolObjectKind::Ability,
				Record.PoolID,
				Rewritten,
				Error))
		{
			return false;
		}
		Record = MoveTemp(Rewritten);
		return true;
	}
};

namespace
{
	struct FSnapshotAbilityFixture
	{
		USeinWorldSubsystem* World = nullptr;
		FSeinEntityHandle Entity;
		int32 OrdinaryAbilityID = INDEX_NONE;
		int32 PassiveAbilityID = INDEX_NONE;
		int32 WithinAbilityID = INDEX_NONE;
		int32 ResolverID = INDEX_NONE;
		int32 WithinResolverID = INDEX_NONE;
		TArray<int32> ExpectedAbilityFreeList;
		TArray<int32> ExpectedResolverFreeList;
		bool bExactPoolTopologyAuthored = false;
		bool bBootstrapConsumed = false;

		explicit FSnapshotAbilityFixture(FActorTestSpawner& Spawner)
		{
			World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
			check(World);
			const auto AuthorState = [this]()
			{
				Entity = World->SpawnAbstractEntity(
					FFixedTransform(), FSeinPlayerID::Neutral());
				World->AddComponent(Entity, FSeinAbilityComponent());
				OrdinaryAbilityID = USeinAbilityBPFL::SeinGrantAbility(
					World, Entity, USeinSnapshotTestAbility::StaticClass());
				PassiveAbilityID = USeinAbilityBPFL::SeinGrantAbility(
					World, Entity, USeinSnapshotPassiveTestAbility::StaticClass());
				WithinAbilityID = USeinAbilityBPFL::SeinGrantAbility(
					World, Entity, USeinSnapshotWithinTestAbility::StaticClass());
				ResolverID = World->RegisterCommandBrokerResolver(
					NewObject<USeinDefaultCommandBrokerResolver>(World));
				WithinResolverID = World->RegisterCommandBrokerResolver(
					NewObject<USeinSnapshotWithinTestResolver>(World));

				const int32 ReleasedAbilityA = World->RegisterAbilityInstance(
					NewObject<USeinSnapshotTestAbility>(World));
				const int32 ReleasedAbilityB = World->RegisterAbilityInstance(
					NewObject<USeinSnapshotTestAbility>(World));
				const int32 ReleasedResolverA =
					World->RegisterCommandBrokerResolver(
						NewObject<USeinDefaultCommandBrokerResolver>(World));
				const int32 ReleasedResolverB =
					World->RegisterCommandBrokerResolver(
						NewObject<USeinSnapshotWithinTestResolver>(World));
				bExactPoolTopologyAuthored =
					ReleasedAbilityA != INDEX_NONE
					&& ReleasedAbilityB != INDEX_NONE
					&& ReleasedResolverA != INDEX_NONE
					&& ReleasedResolverB != INDEX_NONE;
				if (bExactPoolTopologyAuthored)
				{
					World->UnregisterAbilityInstance(ReleasedAbilityB);
					World->UnregisterAbilityInstance(ReleasedAbilityA);
					World->UnregisterCommandBrokerResolver(ReleasedResolverB);
					World->UnregisterCommandBrokerResolver(ReleasedResolverA);
					ExpectedAbilityFreeList = {
						ReleasedAbilityB, ReleasedAbilityA};
					ExpectedResolverFreeList = {
						ReleasedResolverB, ReleasedResolverA};
				}
			};
			if (!SeinTestMatchBootstrap::Materialize(
				*World,
				AuthorState,
				FSeinMatchSettings(),
				0,
				TEXT("SeinARTS.SnapshotValidation")))
			{
				return;
			}
			bBootstrapConsumed = SeinTestMatchBootstrap::Start(*World);
			if (bBootstrapConsumed)
			{
				World->StopSimulation();
			}
		}
	};

	struct FAbilityBlobEntry
	{
		int32 Slot = 0;
		int32 Generation = 0;
		FSeinAbilityComponent Component;
	};

	bool RewriteAbilityComponent(FSeinWorldSnapshot& Snapshot, int32 TargetSlot,
		TFunctionRef<void(FSeinAbilityComponent&)> Mutator,
		int32 GenerationDelta = 0)
	{
		FSeinSnapshotComponentStorageBlob* Blob =
			Snapshot.ComponentStorageBlobs.Find(
				FSeinAbilityComponent::StaticStruct()->GetPathName());
		if (!Blob) return false;

		TArray<uint8> SourceBytes = Blob->Bytes;
		FMemoryReader MemoryReader(SourceBytes, /*bIsPersistent=*/true);
		FObjectAndNameAsStringProxyArchive Reader(
			MemoryReader, /*bInLoadIfFindFails=*/true);
		int32 EntryCount = 0;
		Reader << EntryCount;
		if (Reader.IsError() || EntryCount != Blob->EntryCount || EntryCount < 0)
		{
			return false;
		}

		bool bFound = false;
		TArray<FAbilityBlobEntry> Entries;
		Entries.Reserve(EntryCount);
		for (int32 Index = 0; Index < EntryCount; ++Index)
		{
			FAbilityBlobEntry& Entry = Entries.AddDefaulted_GetRef();
			Reader << Entry.Slot;
			Reader << Entry.Generation;
			FSeinAbilityComponent::StaticStruct()->SerializeBin(
				Reader, &Entry.Component);
			if (Reader.IsError()) return false;
			if (Entry.Slot == TargetSlot)
			{
				Mutator(Entry.Component);
				Entry.Generation += GenerationDelta;
				bFound = true;
			}
		}
		if (!bFound || MemoryReader.Tell() != SourceBytes.Num()) return false;

		TArray<uint8> RewrittenBytes;
		FMemoryWriter MemoryWriter(RewrittenBytes, /*bIsPersistent=*/true);
		FObjectAndNameAsStringProxyArchive Writer(
			MemoryWriter, /*bInLoadIfFindFails=*/false);
		Writer << EntryCount;
		for (FAbilityBlobEntry& Entry : Entries)
		{
			Writer << Entry.Slot;
			Writer << Entry.Generation;
			FSeinAbilityComponent::StaticStruct()->SerializeBin(
				Writer, &Entry.Component);
		}
		if (Writer.IsError()) return false;
		Blob->Bytes = MoveTemp(RewrittenBytes);
		return true;
	}
}

namespace UE::SeinARTSTests
{
	TEST(SnapshotRejectsAbstractAndDeprecatedPoolClassesWithoutMutation,
		"SeinARTS.Unit.Snapshot")
	{
		FActorTestSpawner Spawner;
		FSnapshotAbilityFixture Fixture(Spawner);
		ASSERT_THAT(IsNotNull(Fixture.World));
		ASSERT_THAT(IsTrue(Fixture.bBootstrapConsumed));
		ASSERT_THAT(IsTrue(Fixture.OrdinaryAbilityID != INDEX_NONE));
		ASSERT_THAT(IsTrue(Fixture.WithinAbilityID != INDEX_NONE));
		ASSERT_THAT(IsTrue(Fixture.ResolverID != INDEX_NONE));
		ASSERT_THAT(IsTrue(Fixture.WithinResolverID != INDEX_NONE));

		FSeinWorldSnapshot Valid;
		Fixture.World->CaptureSnapshot(Valid);
		const int32 HashBefore = Fixture.World->ComputeStateHash();
		USeinAbility* const LiveAbility =
			Fixture.World->GetAbilityInstance(Fixture.OrdinaryAbilityID);

		auto ExpectRejectedWithoutMutation = [&](FSeinWorldSnapshot& Bad)
		{
			Assert.ExpectError(TEXT(
				"failed provider materialization"));
			ASSERT_THAT(IsFalse(
				SeinTestSnapshotRestore::RestoreTrusted(
					*Fixture.World, Bad)));
			ASSERT_THAT(AreEqual(HashBefore, Fixture.World->ComputeStateHash()));
			ASSERT_THAT(AreEqual(LiveAbility,
				Fixture.World->GetAbilityInstance(Fixture.OrdinaryAbilityID)));
		};

		FSeinWorldSnapshot AbstractAbility = Valid;
		AbstractAbility.AbilityPoolRecords[Fixture.OrdinaryAbilityID].ClassPath =
			USeinAbility::StaticClass()->GetPathName();
		ExpectRejectedWithoutMutation(AbstractAbility);

		FSeinWorldSnapshot DeprecatedAbility = Valid;
		DeprecatedAbility.AbilityPoolRecords[Fixture.OrdinaryAbilityID].ClassPath =
			UDEPRECATED_SeinSnapshotObsoleteTestAbility::StaticClass()->GetPathName();
		ExpectRejectedWithoutMutation(DeprecatedAbility);

		FSeinWorldSnapshot AbstractResolver = Valid;
		AbstractResolver.ResolverPoolRecords[Fixture.ResolverID].ClassPath =
			USeinCommandBrokerResolver::StaticClass()->GetPathName();
		ExpectRejectedWithoutMutation(AbstractResolver);

		FSeinWorldSnapshot DeprecatedResolver = Valid;
		DeprecatedResolver.ResolverPoolRecords[Fixture.ResolverID].ClassPath =
			UDEPRECATED_SeinSnapshotObsoleteTestResolver::StaticClass()->GetPathName();
		ExpectRejectedWithoutMutation(DeprecatedResolver);
	}

	TEST(SnapshotRejectsMalformedPoolStateBytesWithoutMutation,
		"SeinARTS.Unit.Snapshot")
	{
		FActorTestSpawner Spawner;
		FSnapshotAbilityFixture Fixture(Spawner);
		ASSERT_THAT(IsTrue(Fixture.bBootstrapConsumed));
		FSeinWorldSnapshot Valid;
		Fixture.World->CaptureSnapshot(Valid);
		const int32 HashBefore = Fixture.World->ComputeStateHash();

		auto ExpectRejectedWithoutMutation = [&](FSeinWorldSnapshot& Bad)
		{
			Assert.ExpectError(TEXT(
				"failed provider materialization"));
			ASSERT_THAT(IsFalse(
				SeinTestSnapshotRestore::RestoreTrusted(
					*Fixture.World, Bad)));
			ASSERT_THAT(AreEqual(HashBefore, Fixture.World->ComputeStateHash()));
		};

		FSeinWorldSnapshot TrailingAbility = Valid;
		TrailingAbility.AbilityPoolRecords[Fixture.OrdinaryAbilityID]
			.StateBytes.Add(0xA5);
		ExpectRejectedWithoutMutation(TrailingAbility);

		FSeinWorldSnapshot TruncatedAbility = Valid;
		TArray<uint8>& TruncatedBytes =
			TruncatedAbility.AbilityPoolRecords[Fixture.OrdinaryAbilityID].StateBytes;
		ASSERT_THAT(IsTrue(TruncatedBytes.Num() > 1));
		TruncatedBytes.SetNum(TruncatedBytes.Num() / 2);
		ExpectRejectedWithoutMutation(TruncatedAbility);

		FSeinWorldSnapshot TrailingResolver = Valid;
		TrailingResolver.ResolverPoolRecords[Fixture.ResolverID].StateBytes.Add(0x5A);
		ExpectRejectedWithoutMutation(TrailingResolver);
	}

	TEST(SnapshotRejectsAbilityOwnerAndIndexRoleContradictionsWithoutMutation,
		"SeinARTS.Unit.Snapshot")
	{
		FActorTestSpawner Spawner;
		FSnapshotAbilityFixture Fixture(Spawner);
		ASSERT_THAT(IsTrue(Fixture.bBootstrapConsumed));
		FSeinWorldSnapshot Valid;
		Fixture.World->CaptureSnapshot(Valid);
		const int32 HashBefore = Fixture.World->ComputeStateHash();
		const FSeinEntityHandle LiveOwner = Fixture.World
			->GetAbilityInstance(Fixture.OrdinaryAbilityID)->OwnerEntity;

		auto ExpectRejectedWithoutMutation = [&](FSeinWorldSnapshot& Bad)
		{
			Assert.ExpectError(TEXT(
				"RestoreSnapshot: authoritative sim state failed structural preflight."));
			ASSERT_THAT(IsFalse(
				SeinTestSnapshotRestore::RestoreTrusted(
					*Fixture.World, Bad)));
			ASSERT_THAT(AreEqual(HashBefore, Fixture.World->ComputeStateHash()));
			ASSERT_THAT(AreEqual(LiveOwner, Fixture.World
				->GetAbilityInstance(Fixture.OrdinaryAbilityID)->OwnerEntity));
		};

		FSeinWorldSnapshot WrongOwner = Valid;
		ASSERT_THAT(IsTrue(FSeinSnapshotPoolTestAccess::RewriteAbilityState(
			*Fixture.World,
			WrongOwner.AbilityPoolRecords[Fixture.OrdinaryAbilityID],
			[&](USeinAbility& Ability)
			{
				Ability.OwnerEntity = FSeinEntityHandle(
					Fixture.Entity.Index, Fixture.Entity.Generation + 1);
			})));
		ExpectRejectedWithoutMutation(WrongOwner);

		FSeinWorldSnapshot PassiveAsPrimary = Valid;
		ASSERT_THAT(IsTrue(RewriteAbilityComponent(
			PassiveAsPrimary, Fixture.Entity.Index,
			[&](FSeinAbilityComponent& Component)
			{
				Component.ActiveAbilityID = Fixture.PassiveAbilityID;
			})));
		ExpectRejectedWithoutMutation(PassiveAsPrimary);

		FSeinWorldSnapshot NonPassiveInPassiveList = Valid;
		ASSERT_THAT(IsTrue(RewriteAbilityComponent(
			NonPassiveInPassiveList, Fixture.Entity.Index,
			[&](FSeinAbilityComponent& Component)
			{
				Component.ActivePassiveIDs.Add(Fixture.OrdinaryAbilityID);
			})));
		ExpectRejectedWithoutMutation(NonPassiveInPassiveList);

		FSeinWorldSnapshot InactiveIndexedPassive = Valid;
		ASSERT_THAT(IsTrue(FSeinSnapshotPoolTestAccess::RewriteAbilityState(
			*Fixture.World,
			InactiveIndexedPassive.AbilityPoolRecords[Fixture.PassiveAbilityID],
			[](USeinAbility& Ability)
			{
				Ability.bIsActive = false;
			})));
		ExpectRejectedWithoutMutation(InactiveIndexedPassive);

	}

	TEST(SnapshotRejectsUnindexedActivePrimaryWithoutMutation,
		"SeinARTS.Unit.Snapshot")
	{
		FActorTestSpawner Spawner;
		FSnapshotAbilityFixture Fixture(Spawner);
		ASSERT_THAT(IsTrue(Fixture.bBootstrapConsumed));
		USeinAbility* Ordinary = Fixture.World->GetAbilityInstance(
			Fixture.OrdinaryAbilityID);
		ASSERT_THAT(IsNotNull(Ordinary));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*Fixture.World)));
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
			ASSERT_THAT(IsTrue(Ordinary->ActivateAbility(
				FSeinEntityHandle::Invalid(), FFixedVector::ZeroVector)));
		}

		FSeinWorldSnapshot Bad;
		Fixture.World->CaptureSnapshot(Bad);
		const int32 HashBefore = Fixture.World->ComputeStateHash();
		ASSERT_THAT(IsTrue(RewriteAbilityComponent(
			Bad, Fixture.Entity.Index,
			[](FSeinAbilityComponent& Component)
			{
				Component.ActiveAbilityID = INDEX_NONE;
			})));

		Assert.ExpectError(TEXT(
			"RestoreSnapshot: authoritative sim state failed structural preflight."));
		ASSERT_THAT(IsFalse(SeinTestSnapshotRestore::RestoreTrusted(
			*Fixture.World, Bad)));
		ASSERT_THAT(AreEqual(HashBefore, Fixture.World->ComputeStateHash()));
		ASSERT_THAT(IsTrue(Ordinary->bIsActive));
		ASSERT_THAT(AreEqual(Fixture.OrdinaryAbilityID,
			Fixture.World->GetComponent<FSeinAbilityComponent>(Fixture.Entity)
				->ActiveAbilityID));
	}

	TEST(SnapshotRejectsComponentGenerationMismatchWithoutMutation,
		"SeinARTS.Unit.Snapshot")
	{
		FActorTestSpawner Spawner;
		FSnapshotAbilityFixture Fixture(Spawner);
		ASSERT_THAT(IsTrue(Fixture.bBootstrapConsumed));
		FSeinWorldSnapshot Bad;
		Fixture.World->CaptureSnapshot(Bad);
		const int32 HashBefore = Fixture.World->ComputeStateHash();
		ASSERT_THAT(IsTrue(RewriteAbilityComponent(
			Bad,
			Fixture.Entity.Index,
			[](FSeinAbilityComponent&) {},
			1)));

		Assert.ExpectError(TEXT(
			"RestoreSnapshot: authoritative sim state failed structural preflight."));
		ASSERT_THAT(IsFalse(
			SeinTestSnapshotRestore::RestoreTrusted(
				*Fixture.World, Bad)));
		ASSERT_THAT(AreEqual(HashBefore, Fixture.World->ComputeStateHash()));
	}

	TEST(SnapshotRejectsInvalidAbilityActivationIdentityWithoutMutation,
		"SeinARTS.Unit.Snapshot.Latent")
	{
		FActorTestSpawner Spawner;
		FSnapshotAbilityFixture Fixture(Spawner);
		ASSERT_THAT(IsTrue(Fixture.bBootstrapConsumed));
		const USeinAbility* Passive =
			Fixture.World->GetAbilityInstance(
				Fixture.PassiveAbilityID);
		ASSERT_THAT(IsNotNull(Passive));
		const int64 ExistingActivationID =
			Passive->GetActivationID();
		ASSERT_THAT(IsTrue(ExistingActivationID > 0));

		FSeinWorldSnapshot Valid;
		Fixture.World->CaptureSnapshot(Valid);
		const int32 HashBefore =
			Fixture.World->ComputeStateHash();
		auto ExpectStructuralRejection =
			[&](FSeinWorldSnapshot& Bad)
			{
				Assert.ExpectError(TEXT(
					"RestoreSnapshot: authoritative sim state failed structural preflight."));
				ASSERT_THAT(IsFalse(
					SeinTestSnapshotRestore::RestoreTrusted(
						*Fixture.World, Bad)));
				ASSERT_THAT(AreEqual(
					HashBefore,
					Fixture.World->ComputeStateHash()));
			};

		FSeinWorldSnapshot Duplicate = Valid;
		ASSERT_THAT(IsTrue(FSeinSnapshotPoolTestAccess::RewriteAbilityState(
			*Fixture.World,
			Duplicate.AbilityPoolRecords[
				Fixture.OrdinaryAbilityID],
			[ExistingActivationID](USeinAbility& Ability)
			{
				Ability.AbilityActivationID =
					ExistingActivationID;
			})));
		ExpectStructuralRejection(Duplicate);

		FSeinWorldSnapshot ActiveZero = Valid;
		ASSERT_THAT(IsTrue(FSeinSnapshotPoolTestAccess::RewriteAbilityState(
			*Fixture.World,
			ActiveZero.AbilityPoolRecords[
				Fixture.OrdinaryAbilityID],
			[](USeinAbility& Ability)
			{
				Ability.bIsActive = true;
				Ability.AbilityActivationID = 0;
			})));
		ExpectStructuralRejection(ActiveZero);

		FSeinWorldSnapshot CursorBehindLiveID = Valid;
		CursorBehindLiveID.NextAbilityActivationID =
			ExistingActivationID;
		ExpectStructuralRejection(CursorBehindLiveID);
	}

	TEST(SnapshotRejectsOversizedEntityTopologyBeforeCommit,
		"SeinARTS.Unit.Snapshot")
	{
		FActorTestSpawner Spawner;
		FSnapshotAbilityFixture Fixture(Spawner);
		ASSERT_THAT(IsTrue(Fixture.bBootstrapConsumed));
		FSeinWorldSnapshot Bad;
		Fixture.World->CaptureSnapshot(Bad);
		ASSERT_THAT(IsTrue(Bad.Entities.Num() > 0));
		const int32 HashBefore = Fixture.World->ComputeStateHash();
		Bad.Entities[0].SlotIndex =
			FSeinWorldSnapshot::MaxSupportedEntitySlotIndex + 1;

		Assert.ExpectError(TEXT(
			"RestoreSnapshot: authoritative sim state failed structural preflight."));
		ASSERT_THAT(IsFalse(
			SeinTestSnapshotRestore::RestoreTrusted(
				*Fixture.World, Bad)));
		ASSERT_THAT(AreEqual(HashBefore, Fixture.World->ComputeStateHash()));
	}

	TEST(SnapshotRejectsMalformedExactPoolTopologyWithoutMutation,
		"SeinARTS.Unit.Snapshot")
	{
		FActorTestSpawner Spawner;
		FSnapshotAbilityFixture Fixture(Spawner);
		ASSERT_THAT(IsTrue(Fixture.bBootstrapConsumed));
		ASSERT_THAT(IsTrue(Fixture.bExactPoolTopologyAuthored));
		FSeinWorldSnapshot Valid;
		Fixture.World->CaptureSnapshot(Valid);
		const int32 HashBefore = Fixture.World->ComputeStateHash();
		USeinAbility* const LiveAbility =
			Fixture.World->GetAbilityInstance(Fixture.OrdinaryAbilityID);

		auto ExpectRejectedWithoutMutation = [&](FSeinWorldSnapshot& Bad)
		{
			Assert.ExpectError(TEXT(
				"RestoreSnapshot: authoritative sim state failed structural preflight."));
			ASSERT_THAT(IsFalse(
				SeinTestSnapshotRestore::RestoreTrusted(
					*Fixture.World, Bad)));
			ASSERT_THAT(AreEqual(HashBefore, Fixture.World->ComputeStateHash()));
			ASSERT_THAT(AreEqual(LiveAbility,
				Fixture.World->GetAbilityInstance(Fixture.OrdinaryAbilityID)));
		};

		FSeinWorldSnapshot MissingEntityFreeSlot = Valid;
		ASSERT_THAT(IsTrue(
			!MissingEntityFreeSlot.EntityPoolState.FreeList.IsEmpty()));
		MissingEntityFreeSlot.EntityPoolState.FreeList.Pop(
			EAllowShrinking::No);
		ExpectRejectedWithoutMutation(MissingEntityFreeSlot);

		FSeinWorldSnapshot DuplicateAbilityFreeSlot = Valid;
		ASSERT_THAT(IsTrue(
			!DuplicateAbilityFreeSlot.AbilityPoolFreeList.IsEmpty()));
		const int32 DuplicateFreeSlot =
			DuplicateAbilityFreeSlot.AbilityPoolFreeList[0];
		DuplicateAbilityFreeSlot.AbilityPoolFreeList.Add(
			DuplicateFreeSlot);
		ExpectRejectedWithoutMutation(DuplicateAbilityFreeSlot);

		FSeinWorldSnapshot LiveResolverListedFree = Valid;
		LiveResolverListedFree.ResolverPoolFreeList.Add(Fixture.ResolverID);
		ExpectRejectedWithoutMutation(LiveResolverListedFree);
	}

	TEST(SnapshotRestoreRetainsPassiveIndexTypeCoherence,
		"SeinARTS.Unit.Snapshot")
	{
		FActorTestSpawner Spawner;
		FSnapshotAbilityFixture Fixture(Spawner);
		ASSERT_THAT(IsTrue(Fixture.bBootstrapConsumed));
		ASSERT_THAT(AreEqual(USeinWorldSubsystem::StaticClass(),
			USeinSnapshotWithinTestAbility::StaticClass()->ClassWithin));
		ASSERT_THAT(AreEqual(USeinWorldSubsystem::StaticClass(),
			USeinSnapshotWithinTestResolver::StaticClass()->ClassWithin));
		FSeinWorldSnapshot First;
		Fixture.World->CaptureSnapshot(First);
		ASSERT_THAT(IsTrue(
			SeinTestSnapshotRestore::RestoreTrusted(
				*Fixture.World, First)));
		Fixture.World->StopSimulation();

		const USeinAbility* RestoredPassive =
			Fixture.World->GetAbilityInstance(Fixture.PassiveAbilityID);
		const FSeinAbilityComponent* RestoredComponent =
			Fixture.World->GetComponent<FSeinAbilityComponent>(Fixture.Entity);
		ASSERT_THAT(IsNotNull(RestoredPassive));
		ASSERT_THAT(IsNotNull(RestoredComponent));
		ASSERT_THAT(IsTrue(RestoredPassive->bIsPassive));
		ASSERT_THAT(IsTrue(RestoredComponent->ActivePassiveIDs.Contains(
			Fixture.PassiveAbilityID)));

		FSeinWorldSnapshot Second;
		Fixture.World->CaptureSnapshot(Second);
		ASSERT_THAT(IsTrue(
			SeinTestSnapshotRestore::RestoreTrusted(
				*Fixture.World, Second)));
		Fixture.World->StopSimulation();
	}

	TEST(SnapshotRestorePreservesExactPoolTopologyAndNextAllocation,
		"SeinARTS.Unit.Snapshot")
	{
		FActorTestSpawner Spawner;
		FSnapshotAbilityFixture Fixture(Spawner);
		ASSERT_THAT(IsTrue(Fixture.bBootstrapConsumed));
		ASSERT_THAT(IsTrue(Fixture.bExactPoolTopologyAuthored));

		FSeinWorldSnapshot First;
		Fixture.World->CaptureSnapshot(First);
		ASSERT_THAT(IsTrue(
			First.EntityPoolState.Slots.Num()
				== First.EntityPoolState.Capacity + 1));
		ASSERT_THAT(IsTrue(
			First.AbilityPoolFreeList == Fixture.ExpectedAbilityFreeList));
		ASSERT_THAT(IsTrue(
			First.ResolverPoolFreeList == Fixture.ExpectedResolverFreeList));
		const int32 ExpectedNextAbilityID = First.AbilityPoolFreeList.Last();
		const int32 ExpectedNextResolverID = First.ResolverPoolFreeList.Last();

		ASSERT_THAT(IsTrue(
			SeinTestSnapshotRestore::RestoreTrusted(
				*Fixture.World, First)));

		int32 ReusedAbilityID = INDEX_NONE;
		int32 ReusedResolverID = INDEX_NONE;
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
			ReusedAbilityID = Fixture.World->RegisterAbilityInstance(
				NewObject<USeinSnapshotTestAbility>(Fixture.World));
			ReusedResolverID = Fixture.World->RegisterCommandBrokerResolver(
				NewObject<USeinDefaultCommandBrokerResolver>(Fixture.World));
			Fixture.World->UnregisterAbilityInstance(ReusedAbilityID);
			Fixture.World->UnregisterCommandBrokerResolver(ReusedResolverID);
		}
		Fixture.World->StopSimulation();
		ASSERT_THAT(AreEqual(ExpectedNextAbilityID, ReusedAbilityID));
		ASSERT_THAT(AreEqual(ExpectedNextResolverID, ReusedResolverID));

		FSeinWorldSnapshot Second;
		Fixture.World->CaptureSnapshot(Second);
		ASSERT_THAT(IsTrue(
			Second.EntityPoolState == First.EntityPoolState));
		ASSERT_THAT(IsTrue(
			Second.AbilityPoolFreeList == First.AbilityPoolFreeList));
		ASSERT_THAT(IsTrue(
			Second.ResolverPoolFreeList == First.ResolverPoolFreeList));
		ASSERT_THAT(IsTrue(
			SeinTestSnapshotRestore::RestoreTrusted(
				*Fixture.World, Second)));
		Fixture.World->StopSimulation();
	}

	TEST(SnapshotRestorePreservesCentralRegistriesAndVisibleOrder,
		"SeinARTS.Unit.Snapshot")
	{
		FActorTestSpawner Spawner;
		FSnapshotAbilityFixture Fixture(Spawner);
		ASSERT_THAT(IsTrue(Fixture.bBootstrapConsumed));

		const FGameplayTag EntityTag =
			SeinARTSTags::State_UnderConstruction;
		const FGameplayTag VoteTag =
			SeinARTSTags::Command_Type_StartVote;
		const FName EntityName(TEXT("SnapshotRegistryTarget"));
		const FSeinPlayerID PlayerOne(1);
		const FSeinPlayerID PlayerTwo(2);
		FSeinEntityHandle FirstInBucket;
		Fixture.World->StartSimulation();
		{
			auto SimScope =
				FSeinSimContextTestAccess::Enter(*Fixture.World);
			Fixture.World->RegisterPlayer(
				PlayerOne, FSeinFactionID(), 1);
			Fixture.World->RegisterPlayer(
				PlayerTwo, FSeinFactionID(), 2);
			FirstInBucket = Fixture.World->SpawnAbstractEntity(
				FFixedTransform(), PlayerOne);
			ASSERT_THAT(IsTrue(FirstInBucket.IsValid()));
			ASSERT_THAT(IsTrue(
				Fixture.World->GrantTag(FirstInBucket, EntityTag)));
			ASSERT_THAT(IsTrue(
				Fixture.World->GrantTag(Fixture.Entity, EntityTag)));
			ASSERT_THAT(IsTrue(
				Fixture.World->GrantTag(Fixture.Entity, EntityTag)));
			Fixture.World->RegisterNamedEntity(
				EntityName, Fixture.Entity);
			Fixture.World->StartVote(
				VoteTag,
				ESeinVoteResolution::Majority,
				2,
				100,
				PlayerOne);
			Fixture.World->CastVote(VoteTag, PlayerOne, 1);
		}

		FSeinWorldSnapshot Snapshot;
		Fixture.World->CaptureSnapshot(Snapshot);
		ASSERT_THAT(AreEqual(
			FSeinWorldSnapshot::CurrentVersion,
			Snapshot.SnapshotVersion));

		const FSeinSnapshotEntityTagState* SavedTagState =
			Snapshot.EntityTagStates.FindByPredicate(
				[&](const FSeinSnapshotEntityTagState& State)
				{
					return State.Entity == Fixture.Entity;
				});
		ASSERT_THAT(IsNotNull(SavedTagState));
		ASSERT_THAT(AreEqual(1, SavedTagState->RefCounts.Num()));
		ASSERT_THAT(AreEqual(
			2, SavedTagState->RefCounts[0].RefCount));

		const FSeinSnapshotTagIndexBucket* SavedBucket =
			Snapshot.EntityTagIndex.FindByPredicate(
				[&](const FSeinSnapshotTagIndexBucket& Bucket)
				{
					return Bucket.Tag == EntityTag;
				});
		ASSERT_THAT(IsNotNull(SavedBucket));
		ASSERT_THAT(AreEqual(2, SavedBucket->Entities.Num()));
		ASSERT_THAT(IsTrue(
			SavedBucket->Entities[0] == FirstInBucket));
		ASSERT_THAT(IsTrue(
			SavedBucket->Entities[1] == Fixture.Entity));
		ASSERT_THAT(AreEqual(1, Snapshot.NamedEntities.Num()));
		ASSERT_THAT(AreEqual(EntityName, Snapshot.NamedEntities[0].Name));
		ASSERT_THAT(AreEqual(1, Snapshot.ActiveVotes.Num()));
		ASSERT_THAT(AreEqual(1, Snapshot.ActiveVotes[0].Votes.Num()));
		ASSERT_THAT(IsTrue(
			Snapshot.ActiveVotes[0].Votes[0].Voter == PlayerOne));

		const int32 SnapshotHash = Fixture.World->ComputeStateHash();
		{
			auto SimScope =
				FSeinSimContextTestAccess::Enter(*Fixture.World);
			Fixture.World->UngrantTag(Fixture.Entity, EntityTag);
			Fixture.World->UngrantTag(Fixture.Entity, EntityTag);
			Fixture.World->UngrantTag(FirstInBucket, EntityTag);
			Fixture.World->UnregisterNamedEntity(EntityName);
			Fixture.World->CastVote(VoteTag, PlayerTwo, 0);
		}
		ASSERT_THAT(IsFalse(
			Fixture.World->ComputeStateHash() == SnapshotHash));

		ASSERT_THAT(IsTrue(
			SeinTestSnapshotRestore::RestoreTrusted(
				*Fixture.World, Snapshot)));
		ASSERT_THAT(AreEqual(
			SnapshotHash, Fixture.World->ComputeStateHash()));
		ASSERT_THAT(IsTrue(
			Fixture.World->LookupNamedEntity(EntityName)
				== Fixture.Entity));
		const TArray<FSeinEntityHandle>* RestoredBucket =
			Fixture.World->FindEntitiesWithTag(EntityTag);
		ASSERT_THAT(IsNotNull(RestoredBucket));
		ASSERT_THAT(AreEqual(2, RestoredBucket->Num()));
		ASSERT_THAT(IsTrue((*RestoredBucket)[0] == FirstInBucket));
		ASSERT_THAT(IsTrue((*RestoredBucket)[1] == Fixture.Entity));
		const TArray<FSeinVoteState> RestoredVotes =
			Fixture.World->GetActiveVotes();
		ASSERT_THAT(AreEqual(1, RestoredVotes.Num()));
		ASSERT_THAT(AreEqual(1, RestoredVotes[0].Votes.Num()));

		{
			auto SimScope =
				FSeinSimContextTestAccess::Enter(*Fixture.World);
			Fixture.World->UngrantTag(Fixture.Entity, EntityTag);
		}
		Fixture.World->StopSimulation();
		ASSERT_THAT(IsTrue(
			Fixture.World->HasTag(Fixture.Entity, EntityTag)));
	}

	TEST(SnapshotRestoreAbandonsOldLatentActionsWithoutCallbacks,
		"SeinARTS.Unit.Snapshot")
	{
		FActorTestSpawner Spawner;
		FSnapshotAbilityFixture Fixture(Spawner);
		ASSERT_THAT(IsTrue(Fixture.bBootstrapConsumed));

		FSeinWorldSnapshot Snapshot;
		Fixture.World->CaptureSnapshot(Snapshot);
		ASSERT_THAT(AreEqual(
			FSeinWorldSnapshot::CurrentVersion,
			Snapshot.SnapshotVersion));

		USeinAbility* OldAbility =
			Fixture.World->GetAbilityInstance(
				Fixture.OrdinaryAbilityID);
		ASSERT_THAT(IsNotNull(OldAbility));
		ASSERT_THAT(IsTrue(Fixture.World->StartSimulation()));
		{
			auto SimScope =
				FSeinSimContextTestAccess::Enter(*Fixture.World);
			ASSERT_THAT(IsTrue(OldAbility->ActivateAbility(
				FSeinEntityHandle::Invalid(),
				FFixedVector::ZeroVector)));
		}
		USeinLatentMutationTestAction* OldTimelineAction =
			NewObject<USeinLatentMutationTestAction>(
				Fixture.World->LatentActionManager);
		ASSERT_THAT(IsNotNull(OldTimelineAction));
		OldTimelineAction->OwningAbility = OldAbility;
		OldTimelineAction->OwnerEntity = Fixture.Entity;
		ASSERT_THAT(IsTrue(
			Fixture.World->LatentActionManager->RegisterAction(
				OldTimelineAction)));
		ASSERT_THAT(AreEqual(
			1,
			Fixture.World->LatentActionManager
				->GetActiveActionCount()));

		ASSERT_THAT(IsTrue(
			SeinTestSnapshotRestore::RestoreTrusted(
				*Fixture.World, Snapshot)));
		Fixture.World->StopSimulation();
		ASSERT_THAT(AreEqual(0, OldTimelineAction->CancelCount));
		ASSERT_THAT(IsFalse(OldTimelineAction->bCancelled));
		ASSERT_THAT(IsNull(OldAbility->WorldSubsystem));
		ASSERT_THAT(AreEqual(
			INDEX_NONE, OldAbility->GetRuntimePoolID()));
		ASSERT_THAT(IsFalse(OldAbility->ActivateAbility(
			FSeinEntityHandle::Invalid(),
			FFixedVector::ZeroVector)));
		ASSERT_THAT(IsFalse(
			Fixture.World->GetAbilityInstance(
				Fixture.OrdinaryAbilityID) == OldAbility));
		ASSERT_THAT(AreEqual(
			0,
			Fixture.World->LatentActionManager
				->GetActiveActionCount()));
	}

	TEST(SnapshotRoundTripsActiveWaitContinuationAndCanonicalIdentity,
		"SeinARTS.Unit.Snapshot.Latent")
	{
		FActorTestSpawner Spawner;
		FSnapshotAbilityFixture Fixture(Spawner);
		ASSERT_THAT(IsTrue(Fixture.bBootstrapConsumed));
		Fixture.World->StartSimulation();

		USeinAbility* Ability =
			Fixture.World->GetAbilityInstance(
				Fixture.OrdinaryAbilityID);
		ASSERT_THAT(IsNotNull(Ability));
		{
			auto SimScope =
				FSeinSimContextTestAccess::Enter(*Fixture.World);
			ASSERT_THAT(IsTrue(Ability->ActivateAbility(
				FSeinEntityHandle::Invalid(),
				FFixedVector::ZeroVector)));
		}

		USeinWaitAction* Wait =
			NewObject<USeinWaitAction>(
				Fixture.World->LatentActionManager);
		ASSERT_THAT(IsNotNull(Wait));
		Wait->OwningAbility = Ability;
		Wait->OwnerEntity = Fixture.Entity;
		Wait->Initialize(FFixedPoint::FromInt(10));
		ASSERT_THAT(IsTrue(
			Fixture.World->LatentActionManager->RegisterAction(
				Wait)));
		const int64 ExpectedActionID = Wait->GetActionID();
		const int64 ExpectedActivationID =
			Wait->GetAbilityActivationID();
		{
			auto SimScope =
				FSeinSimContextTestAccess::Enter(*Fixture.World);
			Fixture.World->LatentActionManager->TickAll(
				FFixedPoint::FromInt(2), *Fixture.World);
		}

		FGuid RootBefore;
		FString RootError;
		ASSERT_THAT(IsTrue(
			Fixture.World->ComputeCanonicalStateRoot(
				RootBefore, RootError)));
		const int32 HashBefore =
			Fixture.World->ComputeStateHash();
		FSeinWorldSnapshot Snapshot;
		Fixture.World->CaptureSnapshot(Snapshot);
		ASSERT_THAT(AreEqual(
			FSeinWorldSnapshot::CurrentVersion,
			Snapshot.SnapshotVersion));
		ASSERT_THAT(AreEqual(
			1, Snapshot.LatentActionRecords.Num()));
		ASSERT_THAT(IsTrue(
			Snapshot.LatentActionSequenceDigest.IsValid()));
		ASSERT_THAT(AreEqual(
			ExpectedActionID,
			Snapshot.LatentActionRecords[0].ActionID));
		ASSERT_THAT(AreEqual(
			ExpectedActivationID,
			Snapshot.LatentActionRecords[0]
				.AbilityActivationID));

		{
			auto SimScope =
				FSeinSimContextTestAccess::Enter(*Fixture.World);
			Fixture.World->LatentActionManager->TickAll(
				FFixedPoint::FromInt(1), *Fixture.World);
		}
		ASSERT_THAT(IsFalse(
			HashBefore == Fixture.World->ComputeStateHash()));

		ASSERT_THAT(IsTrue(
			SeinTestSnapshotRestore::RestoreTrusted(
				*Fixture.World, Snapshot)));
		ASSERT_THAT(AreEqual(
			HashBefore, Fixture.World->ComputeStateHash()));
		FGuid RootAfter;
		RootError.Reset();
		ASSERT_THAT(IsTrue(
			Fixture.World->ComputeCanonicalStateRoot(
				RootAfter, RootError)));
		ASSERT_THAT(IsTrue(RootBefore == RootAfter));

		const TConstArrayView<TObjectPtr<USeinLatentAction>>
			RestoredActions =
				Fixture.World->LatentActionManager
					->GetActiveActions();
		ASSERT_THAT(AreEqual(1, RestoredActions.Num()));
		const USeinWaitAction* RestoredWait =
			Cast<USeinWaitAction>(RestoredActions[0]);
		ASSERT_THAT(IsNotNull(RestoredWait));
		ASSERT_THAT(AreEqual(
			ExpectedActionID, RestoredWait->GetActionID()));
		ASSERT_THAT(AreEqual(
			ExpectedActivationID,
			RestoredWait->GetAbilityActivationID()));
		ASSERT_THAT(AreEqual(
			FFixedPoint::FromInt(10).Value,
			RestoredWait->Duration.Value));
		ASSERT_THAT(AreEqual(
			FFixedPoint::FromInt(2).Value,
			RestoredWait->Elapsed.Value));
		ASSERT_THAT(AreEqual(
			Fixture.World->GetAbilityInstance(
				Fixture.OrdinaryAbilityID),
			RestoredWait->OwningAbility.Get()));
		Fixture.World->StopSimulation();
	}

	TEST(SnapshotRoundTripsExhaustedLatentIdentityCursors,
		"SeinARTS.Unit.Snapshot.Latent")
	{
		FActorTestSpawner Spawner;
		FSnapshotAbilityFixture Fixture(Spawner);
		ASSERT_THAT(IsTrue(Fixture.bBootstrapConsumed));

		FSeinWorldSnapshot Snapshot;
		Fixture.World->CaptureSnapshot(Snapshot);
		ASSERT_THAT(IsTrue(Snapshot.LatentActionRecords.IsEmpty()));
		Snapshot.NextLatentActionID = MAX_int64;
		Snapshot.NextAbilityActivationID = MAX_int64;
		FString DigestError;
		ASSERT_THAT(IsTrue(
			FSeinLatentActionCodecRegistry::
				RecomputeRecordDigestsForTests(
					Snapshot.NextLatentActionID,
					Snapshot.NextAbilityActivationID,
					Snapshot.LatentActionRecords,
					Snapshot.LatentActionSequenceDigest,
					DigestError)));
		ASSERT_THAT(IsTrue(
			SeinTestSnapshotRestore::RestoreTrusted(
				*Fixture.World, Snapshot)));
		ASSERT_THAT(AreEqual(
			MAX_int64,
			Fixture.World->LatentActionManager
				->GetNextActionID()));
		ASSERT_THAT(AreEqual(
			MAX_int64,
			Fixture.World->GetNextAbilityActivationID()));

		FSeinWorldSnapshot RoundTrip;
		Fixture.World->CaptureSnapshot(RoundTrip);
		ASSERT_THAT(AreEqual(
			FSeinWorldSnapshot::CurrentVersion,
			RoundTrip.SnapshotVersion));
		ASSERT_THAT(AreEqual(
			MAX_int64, RoundTrip.NextLatentActionID));
		ASSERT_THAT(AreEqual(
			MAX_int64, RoundTrip.NextAbilityActivationID));

		USeinAbility* Ordinary =
			Fixture.World->GetAbilityInstance(
				Fixture.OrdinaryAbilityID);
		ASSERT_THAT(IsNotNull(Ordinary));
		TestRunner->AddExpectedError(
			TEXT("deterministic activation ID space is exhausted"),
			EAutomationExpectedErrorFlags::Contains,
			1,
			false);
		{
			auto SimScope =
				FSeinSimContextTestAccess::Enter(*Fixture.World);
			ASSERT_THAT(IsFalse(Ordinary->ActivateAbility(
				FSeinEntityHandle::Invalid(),
				FFixedVector::ZeroVector)));
		}

		USeinAbility* Passive =
			Fixture.World->GetAbilityInstance(
				Fixture.PassiveAbilityID);
		ASSERT_THAT(IsNotNull(Passive));
		ASSERT_THAT(IsTrue(Passive->bIsActive));
		USeinWaitAction* Rejected =
			NewObject<USeinWaitAction>(
				Fixture.World->LatentActionManager);
		Rejected->OwningAbility = Passive;
		Rejected->OwnerEntity = Fixture.Entity;
		Rejected->Initialize(FFixedPoint::One);
		TestRunner->AddExpectedError(
			TEXT("RegisterAction rejected invalid identity, ownership, or exhausted latent-action ID space."),
			EAutomationExpectedErrorFlags::Contains,
			1,
			false);
		ASSERT_THAT(IsFalse(
			Fixture.World->LatentActionManager
				->RegisterAction(Rejected)));
		ASSERT_THAT(AreEqual(
			static_cast<int64>(0), Rejected->GetActionID()));
	}

	TEST(SnapshotProviderStagingSeesDetachedAbilityCandidates,
		"SeinARTS.Unit.Snapshot.Latent.Staging")
	{
		int32 TargetAbilityID = INDEX_NONE;
		bool bStageCalled = false;
		bool bCandidateFound = false;
		bool bCandidateDetached = false;

		FSeinCanonicalStateDescriptor Descriptor;
		Descriptor.Key.StableDomainId =
			TEXT("SeinFrameworkTest.Snapshot");
		Descriptor.Key.StableContributorId =
			TEXT("DetachedAbilityStaging");
		Descriptor.SchemaVersion = 1;
		Descriptor.ImplementationRevision = 1;
		Descriptor.Role =
			ESeinCanonicalStateRole::Authoritative;
		Descriptor.PayloadStruct =
			FSeinWaitActionCanonicalState::StaticStruct();
		// Subsystem-owned test fixture: no test system claims it, so it must
		// declare external ownership to pass the orphan bootstrap gate.
		Descriptor.bExternallyOwned = true;

		FSeinCanonicalStateContributorOps Ops;
		Ops.Capture = [](
			const FSeinCanonicalStateCaptureContext&,
			FInstancedStruct& OutState,
			FString&)
		{
			OutState = FInstancedStruct::Make(
				FSeinWaitActionCanonicalState());
			return true;
		};
		Ops.StageRestore =
			[&](
				const FSeinCanonicalStateStageContext& Context,
				const FInstancedStruct&,
				TUniquePtr<ISeinCanonicalStateRestoreStage>&,
				FString&)
		{
			bStageCalled = true;
			const USeinAbility* Candidate =
				Context.Candidate
					? Context.Candidate->FindAbility(
						TargetAbilityID)
					: nullptr;
			bCandidateFound = Candidate != nullptr;
			bCandidateDetached = Candidate
				&& Candidate->WorldSubsystem == nullptr
				&& Candidate->GetRuntimePoolID()
					== TargetAbilityID;
			return true;
		};
		Ops.CommitRestore = [](
			FSeinCanonicalStateCommitContext&,
			TUniquePtr<ISeinCanonicalStateRestoreStage>&&)
		{
		};
		FString Error;
		FSeinCanonicalStateRegistrationHandle Provider =
			FSeinCanonicalStateRegistry::Register(
				TEXT("SeinFrameworkTests.SnapshotStaging"),
				Descriptor,
				MoveTemp(Ops),
				&Error);
		ASSERT_THAT(IsTrue(Provider.IsValid()));

		{
			// Freeze the provider above into this world before authoring its
			// ability pool.
			FActorTestSpawner Spawner;
			FSnapshotAbilityFixture Fixture(Spawner);
			ASSERT_THAT(IsTrue(Fixture.bBootstrapConsumed));
			TargetAbilityID = Fixture.OrdinaryAbilityID;
			ASSERT_THAT(IsTrue(
				TargetAbilityID != INDEX_NONE));

			FSeinWorldSnapshot Snapshot;
			Fixture.World->CaptureSnapshot(Snapshot);
			const int32 HashBefore =
				Fixture.World->ComputeStateHash();
			Snapshot.LatentActionSequenceDigest =
				Snapshot.LatentActionSequenceDigest
					== FGuid(11, 12, 13, 14)
					? FGuid(15, 16, 17, 18)
					: FGuid(11, 12, 13, 14);
			TestRunner->AddExpectedError(
				TEXT("RestoreSnapshot: latent continuation state failed staging"),
				EAutomationExpectedErrorFlags::Contains,
				1,
				false);
			ASSERT_THAT(IsFalse(
				SeinTestSnapshotRestore::RestoreTrusted(
					*Fixture.World, Snapshot)));
			ASSERT_THAT(IsTrue(bStageCalled));
			ASSERT_THAT(IsTrue(bCandidateFound));
			ASSERT_THAT(IsTrue(bCandidateDetached));
			ASSERT_THAT(AreEqual(
				HashBefore,
				Fixture.World->ComputeStateHash()));
		}

		ASSERT_THAT(IsTrue(
			FSeinCanonicalStateRegistry::Unregister(Provider)));
	}
}
