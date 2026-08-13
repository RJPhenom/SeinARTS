#include "CQTest.h"
#include "Core/SeinEntityPool.h"
#include "Simulation/ComponentStorage.h"
#include "TestTypes/SeinComponentStorageTestTypes.h"

int32 FSeinComponentStorageLifecycleProbe::ConstructionCount = 0;
int32 FSeinComponentStorageLifecycleProbe::DestructionCount = 0;

struct FSeinComponentStorageTestAccess
{
	static void ForceMutationRevision(
		FSeinGenericComponentStorage& Storage,
		uint64 Revision)
	{
		Storage.MutationRevisionCounter = Revision;
		Storage.bRevisionWrapped = false;
	}

	static void ForceTopologyRevision(
		FSeinGenericComponentStorage& Storage,
		uint64 Revision)
	{
		Storage.TopologyRevision = Revision;
		Storage.bRevisionWrapped = false;
	}
};

namespace UE::SeinARTSTests
{
	TEST(ComponentStorageLifecycle, "SeinARTS.Unit.Entity")
	{
		UScriptStruct* ProbeType = FSeinComponentStorageLifecycleProbe::StaticStruct();
		FSeinComponentStorageLifecycleProbe::ResetCounts();

		{
			FSeinGenericComponentStorage Storage(ProbeType, 1);
			const FSeinEntityHandle Handle(1, 1);

			ASSERT_THAT(AreEqual(2, FSeinComponentStorageLifecycleProbe::ConstructionCount));
			ASSERT_THAT(AreEqual(0, FSeinComponentStorageLifecycleProbe::DestructionCount));

			Storage.AddComponent(Handle, nullptr);
			ASSERT_THAT(AreEqual(3, FSeinComponentStorageLifecycleProbe::ConstructionCount));
			ASSERT_THAT(AreEqual(1, FSeinComponentStorageLifecycleProbe::DestructionCount));

			Storage.RemoveComponent(Handle);
			ASSERT_THAT(AreEqual(4, FSeinComponentStorageLifecycleProbe::ConstructionCount));
			ASSERT_THAT(AreEqual(2, FSeinComponentStorageLifecycleProbe::DestructionCount));

			Storage.AddComponent(Handle, nullptr);
			Storage.Clear();
			ASSERT_THAT(AreEqual(6, FSeinComponentStorageLifecycleProbe::ConstructionCount));
			ASSERT_THAT(AreEqual(4, FSeinComponentStorageLifecycleProbe::DestructionCount));
		}

		ASSERT_THAT(AreEqual(
			FSeinComponentStorageLifecycleProbe::ConstructionCount,
			FSeinComponentStorageLifecycleProbe::DestructionCount));
	}

	TEST(EntityHandleBoundariesRejectMalformedAndDeadSlots,
		"SeinARTS.Unit.Entity")
	{
		ASSERT_THAT(IsFalse(FSeinEntityHandle::Invalid().IsValid()));
		ASSERT_THAT(IsFalse(FSeinEntityHandle(-1, 1).IsValid()));
		ASSERT_THAT(IsFalse(FSeinEntityHandle(1, -1).IsValid()));
		ASSERT_THAT(IsFalse(FSeinEntityHandle(0, 1).IsValid()));
		ASSERT_THAT(IsFalse(FSeinEntityHandle(1, 0).IsValid()));

		FSeinEntityPool Pool;
		Pool.Initialize(1);
		const FSeinEntityHandle First = Pool.Acquire(
			FFixedTransform(), FSeinPlayerID::Neutral());
		ASSERT_THAT(IsTrue(Pool.IsValid(First)));
		ASSERT_THAT(IsNull(Pool.Get(FSeinEntityHandle(-1, 1))));

		Pool.Release(First);
		ASSERT_THAT(IsFalse(Pool.IsValid(First)));
		ASSERT_THAT(IsFalse(Pool.IsValid(FSeinEntityHandle(
			First.Index, Pool.GetSlotGeneration(First.Index)))));

		const FSeinEntityHandle Reused = Pool.Acquire(
			FFixedTransform(), FSeinPlayerID::Neutral());
		ASSERT_THAT(IsTrue(Pool.IsValid(Reused)));
		ASSERT_THAT(IsTrue(Reused.Index == First.Index));
		ASSERT_THAT(IsTrue(Reused.Generation != First.Generation));
	}

	TEST(ComponentStorageRevisionWrapDisablesSnapshotReuse,
		"SeinARTS.Unit.Entity")
	{
		const FSeinEntityHandle Handle(1, 1);
		FSeinGenericComponentStorage MutationStorage(
			FSeinComponentStorageLifecycleProbe::StaticStruct(), 1);
		MutationStorage.AddComponent(Handle, nullptr);
		ASSERT_THAT(IsTrue(MutationStorage.CanReuseSnapshotSerialization()));
		FSeinComponentStorageTestAccess::ForceMutationRevision(
			MutationStorage, MAX_uint64);
		MutationStorage.AddComponent(Handle, nullptr);
		ASSERT_THAT(AreEqual(
			static_cast<uint64>(1),
			MutationStorage.GetLatestMutationRevision()));
		ASSERT_THAT(IsFalse(MutationStorage.CanReuseSnapshotSerialization()));

		FSeinGenericComponentStorage TopologyStorage(
			FSeinComponentStorageLifecycleProbe::StaticStruct(), 1);
		FSeinComponentStorageTestAccess::ForceTopologyRevision(
			TopologyStorage, MAX_uint64);
		TopologyStorage.Grow(2);
		ASSERT_THAT(AreEqual(
			static_cast<uint64>(1),
			TopologyStorage.GetTopologyRevision()));
		ASSERT_THAT(IsFalse(TopologyStorage.CanReuseSnapshotSerialization()));
	}

	TEST(EntityPoolResetRegrowsWithoutAllocatingReservedSlotZero,
		"SeinARTS.Unit.Entity")
	{
		FSeinEntityPool Pool;
		Pool.Initialize(2);
		Pool.Reset();

		const FSeinEntityHandle Acquired = Pool.Acquire(
			FFixedTransform(), FSeinPlayerID::Neutral());
		ASSERT_THAT(IsTrue(Acquired.IsValid()));
		ASSERT_THAT(IsTrue(Pool.IsValid(Acquired)));
		ASSERT_THAT(AreEqual(1, Acquired.Index));
		ASSERT_THAT(AreEqual(0, Pool.GetSlotGeneration(0)));
		ASSERT_THAT(AreEqual(1, Pool.GetActiveCount()));
	}

	TEST(EntityPoolRetiresGenerationExhaustedSlotsInsteadOfWrapping,
		"SeinARTS.Unit.Entity")
	{
		FSeinEntityPool Pool;
		const TArray<int32> Slots = {1};
		const TArray<int32> Generations = {MAX_int32};
		const TArray<FFixedTransform> Transforms = {FFixedTransform()};
		const TArray<FSeinPlayerID> Owners = {
			FSeinPlayerID::Neutral()};
		const TArray<bool> Alive = {true};
		ASSERT_THAT(IsTrue(Pool.RebuildFromSnapshot(
			1, Slots, Generations, Transforms, Owners, Alive)));

		const FSeinEntityHandle Exhausted(1, MAX_int32);
		ASSERT_THAT(IsTrue(Pool.IsValid(Exhausted)));
		Pool.Release(Exhausted);
		ASSERT_THAT(IsFalse(Pool.IsValid(Exhausted)));
		ASSERT_THAT(AreEqual(MAX_int32, Pool.GetSlotGeneration(1)));

		const FSeinEntityHandle Fresh = Pool.Acquire(
			FFixedTransform(), FSeinPlayerID::Neutral());
		ASSERT_THAT(IsTrue(Pool.IsValid(Fresh)));
		ASSERT_THAT(IsTrue(Fresh.Index != Exhausted.Index));
		ASSERT_THAT(AreEqual(MAX_int32, Pool.GetSlotGeneration(1)));
	}

	TEST(ComponentStorageRejectsStaleAndOversizedHandles,
		"SeinARTS.Unit.Entity")
	{
		FSeinGenericComponentStorage Storage(
			FSeinComponentStorageLifecycleProbe::StaticStruct(), 1);
		const FSeinEntityHandle First(1, 1);
		const FSeinEntityHandle Reused(1, 2);

		Storage.AddComponent(First, nullptr);
		ASSERT_THAT(IsTrue(Storage.HasComponent(First)));
		Storage.AddComponent(Reused, nullptr);
		ASSERT_THAT(IsFalse(Storage.HasComponent(First)));
		ASSERT_THAT(IsNull(Storage.GetComponentRaw(First)));
		ASSERT_THAT(IsTrue(Storage.HasComponent(Reused)));

		Storage.RemoveComponent(First);
		ASSERT_THAT(IsTrue(Storage.HasComponent(Reused)));
		Storage.AddComponent(FSeinEntityHandle(-1, 1), nullptr);
		Storage.AddComponent(FSeinEntityHandle(MAX_int32, 1), nullptr);
		ASSERT_THAT(AreEqual(1, Storage.GetComponentCount()));
		ASSERT_THAT(IsTrue(Storage.HasComponent(Reused)));
	}

	TEST(ComponentStorageIterationPreservesStoredGeneration,
		"SeinARTS.Unit.Entity")
	{
		FSeinEntityPool Pool;
		Pool.Initialize(1);
		const FSeinEntityHandle First = Pool.Acquire(
			FFixedTransform(), FSeinPlayerID::Neutral());

		FSeinGenericComponentStorage Storage(
			FSeinComponentStorageLifecycleProbe::StaticStruct(), 1);
		Storage.AddComponent(First, nullptr);

		Pool.Release(First);
		const FSeinEntityHandle Reused = Pool.Acquire(
			FFixedTransform(), FSeinPlayerID::Neutral());
		ASSERT_THAT(IsTrue(Reused.Index == First.Index));
		ASSERT_THAT(IsTrue(Reused.Generation != First.Generation));

		TArray<FSeinEntityHandle> Visited;
		Storage.ForEachLiveComponent(
			[&Visited](FSeinEntityHandle Handle, void*)
			{
				Visited.Add(Handle);
			});

		ASSERT_THAT(AreEqual(1, Visited.Num()));
		ASSERT_THAT(IsTrue(Visited[0] == First));
		ASSERT_THAT(IsFalse(Pool.IsValid(Visited[0])));
		ASSERT_THAT(IsTrue(Pool.IsValid(Reused)));
	}

	TEST(DeferredMutationRevisionsPublishOnlyOnCommit,
		"SeinARTS.Unit.Entity")
	{
		FSeinEntityPool Pool;
		Pool.Initialize(1);
		const FSeinEntityHandle Handle = Pool.Acquire(
			FFixedTransform(), FSeinPlayerID::Neutral());
		const uint64 EntityRevisionBefore =
			Pool.GetLatestMutationRevision();
		FSeinEntity* Entity = Pool.GetForDeferredMutation(Handle);
		ASSERT_THAT(IsNotNull(Entity));
		Entity->Flags |= FSeinEntity::FLAG_INVULNERABLE;
		ASSERT_THAT(AreEqual(
			EntityRevisionBefore, Pool.GetLatestMutationRevision()));
		Pool.CommitDeferredMutation(Handle);
		ASSERT_THAT(IsTrue(
			Pool.GetLatestMutationRevision() > EntityRevisionBefore));

		FSeinGenericComponentStorage Storage(
			FSeinComponentStorageLifecycleProbe::StaticStruct(), 1);
		Storage.AddComponent(Handle, nullptr);
		const uint64 ComponentRevisionBefore =
			Storage.GetLatestMutationRevision();
		auto* Component =
			static_cast<FSeinComponentStorageLifecycleProbe*>(
				Storage.GetComponentRawForDeferredMutation(Handle));
		ASSERT_THAT(IsNotNull(Component));
		Component->Values.Add(7);
		ASSERT_THAT(AreEqual(
			ComponentRevisionBefore,
			Storage.GetLatestMutationRevision()));
		Storage.CommitDeferredMutation(Handle);
		ASSERT_THAT(IsTrue(
			Storage.GetLatestMutationRevision()
				> ComponentRevisionBefore));
	}
}
