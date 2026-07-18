#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Abilities/SeinAbility.h"
#include "Brokers/SeinCommandBrokerResolver.h"
#include "Brokers/SeinDefaultCommandBrokerResolver.h"
#include "Components/SeinAbilityComponent.h"
#include "Data/SeinWorldSnapshot.h"
#include "Lib/SeinAbilityBPFL.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "TestTypes/SeinSnapshotValidationTestTypes.h"

USeinSnapshotPassiveTestAbility::USeinSnapshotPassiveTestAbility()
{
	bIsPassive = true;
}

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

		explicit FSnapshotAbilityFixture(FActorTestSpawner& Spawner)
		{
			World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
			check(World);
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
		}
	};

	bool RewriteAbilityState(FSeinSnapshotPoolInstanceRecord& Record,
		TFunctionRef<void(USeinAbility&)> Mutator)
	{
		UClass* Class = LoadObject<UClass>(nullptr, *Record.ClassPath);
		if (!Class || !Class->IsChildOf(USeinAbility::StaticClass())) return false;
		USeinAbility* Scratch = NewObject<USeinAbility>(
			GetTransientPackage(), Class, NAME_None, RF_Transient);
		if (!Scratch) return false;

		TArray<uint8> SourceBytes = Record.StateBytes;
		FMemoryReader MemoryReader(SourceBytes, /*bIsPersistent=*/true);
		FObjectAndNameAsStringProxyArchive Reader(
			MemoryReader, /*bInLoadIfFindFails=*/true);
		Class->SerializeTaggedProperties(
			Reader, reinterpret_cast<uint8*>(Scratch), Class, nullptr);
		if (Reader.IsError() || MemoryReader.Tell() != SourceBytes.Num()) return false;

		Mutator(*Scratch);
		TArray<uint8> RewrittenBytes;
		FMemoryWriter MemoryWriter(RewrittenBytes, /*bIsPersistent=*/true);
		FObjectAndNameAsStringProxyArchive Writer(
			MemoryWriter, /*bInLoadIfFindFails=*/false);
		Class->SerializeTaggedProperties(
			Writer, reinterpret_cast<uint8*>(Scratch), Class, nullptr);
		if (Writer.IsError()) return false;
		Record.StateBytes = MoveTemp(RewrittenBytes);
		return true;
	}

	struct FAbilityBlobEntry
	{
		int32 Slot = 0;
		FSeinAbilityComponent Component;
	};

	bool RewriteAbilityComponent(FSeinWorldSnapshot& Snapshot, int32 TargetSlot,
		TFunctionRef<void(FSeinAbilityComponent&)> Mutator)
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
			FSeinAbilityComponent::StaticStruct()->SerializeBin(
				Reader, &Entry.Component);
			if (Reader.IsError()) return false;
			if (Entry.Slot == TargetSlot)
			{
				Mutator(Entry.Component);
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
				"RestoreSnapshot: active effect state is malformed; allocator validation failed."));
			ASSERT_THAT(IsFalse(Fixture.World->RestoreSnapshot(Bad)));
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
		FSeinWorldSnapshot Valid;
		Fixture.World->CaptureSnapshot(Valid);
		const int32 HashBefore = Fixture.World->ComputeStateHash();

		auto ExpectRejectedWithoutMutation = [&](FSeinWorldSnapshot& Bad)
		{
			Assert.ExpectError(TEXT(
				"RestoreSnapshot: active effect state is malformed; allocator validation failed."));
			ASSERT_THAT(IsFalse(Fixture.World->RestoreSnapshot(Bad)));
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
		FSeinWorldSnapshot Valid;
		Fixture.World->CaptureSnapshot(Valid);
		const int32 HashBefore = Fixture.World->ComputeStateHash();
		const FSeinEntityHandle LiveOwner = Fixture.World
			->GetAbilityInstance(Fixture.OrdinaryAbilityID)->OwnerEntity;

		auto ExpectRejectedWithoutMutation = [&](FSeinWorldSnapshot& Bad)
		{
			Assert.ExpectError(TEXT(
				"RestoreSnapshot: active effect state is malformed; allocator validation failed."));
			ASSERT_THAT(IsFalse(Fixture.World->RestoreSnapshot(Bad)));
			ASSERT_THAT(AreEqual(HashBefore, Fixture.World->ComputeStateHash()));
			ASSERT_THAT(AreEqual(LiveOwner, Fixture.World
				->GetAbilityInstance(Fixture.OrdinaryAbilityID)->OwnerEntity));
		};

		FSeinWorldSnapshot WrongOwner = Valid;
		ASSERT_THAT(IsTrue(RewriteAbilityState(
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

	}

	TEST(SnapshotRestoreRetainsPassiveIndexTypeCoherence,
		"SeinARTS.Unit.Snapshot")
	{
		FActorTestSpawner Spawner;
		FSnapshotAbilityFixture Fixture(Spawner);
		ASSERT_THAT(AreEqual(USeinWorldSubsystem::StaticClass(),
			USeinSnapshotWithinTestAbility::StaticClass()->ClassWithin));
		ASSERT_THAT(AreEqual(USeinWorldSubsystem::StaticClass(),
			USeinSnapshotWithinTestResolver::StaticClass()->ClassWithin));
		FSeinWorldSnapshot First;
		Fixture.World->CaptureSnapshot(First);
		ASSERT_THAT(IsTrue(Fixture.World->RestoreSnapshot(First)));
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
		ASSERT_THAT(IsTrue(Fixture.World->RestoreSnapshot(Second)));
		Fixture.World->StopSimulation();
	}
}
