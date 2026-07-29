#include "CQTest.h"
#include "Core/SeinEntityPool.h"

namespace UE::SeinARTSTests
{
	TEST(EntityPoolExactStatePreservesNonIndexFreeListOrder,
		"SeinARTS.Unit.Entity")
	{
		FSeinEntityPool Source;
		Source.Initialize(4);
		TArray<FSeinEntityHandle> Handles;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			Handles.Add(Source.Acquire(
				FFixedTransform(),
				FSeinPlayerID(static_cast<uint8>(Index + 1))));
		}

		Source.Release(Handles[1]);
		Source.Release(Handles[3]);
		Source.Release(Handles[0]);

		FSeinEntityPoolExactState State;
		FString Error;
		ASSERT_THAT(IsTrue(Source.CaptureExactState(State, Error)));
		ASSERT_THAT(AreEqual(3, State.FreeList.Num()));
		ASSERT_THAT(AreEqual(2, State.FreeList[0]));
		ASSERT_THAT(AreEqual(4, State.FreeList[1]));
		ASSERT_THAT(AreEqual(1, State.FreeList[2]));

		FSeinEntityPool Restored;
		ASSERT_THAT(IsTrue(Restored.TryStageExactState(
			State,
			4,
			Error)));
		ASSERT_THAT(AreEqual(Source.GetActiveCount(), Restored.GetActiveCount()));
		ASSERT_THAT(IsTrue(Restored.IsValid(Handles[2])));
		ASSERT_THAT(IsTrue(
			Restored.GetOwner(Handles[2]) == FSeinPlayerID(3)));

		for (int32 Index = 0; Index < 3; ++Index)
		{
			const FSeinEntityHandle SourceNext = Source.Acquire(
				FFixedTransform(),
				FSeinPlayerID::Neutral());
			const FSeinEntityHandle RestoredNext = Restored.Acquire(
				FFixedTransform(),
				FSeinPlayerID::Neutral());
			ASSERT_THAT(IsTrue(SourceNext == RestoredNext));
		}
	}

	TEST(EntityPoolExactStateRejectsMalformedTopologyAtomically,
		"SeinARTS.Unit.Entity")
	{
		FSeinEntityPool Destination;
		Destination.Initialize(2);
		const FSeinEntityHandle Existing = Destination.Acquire(
			FFixedTransform(),
			FSeinPlayerID(7));

		FSeinEntityPoolExactState Before;
		FString Error;
		ASSERT_THAT(IsTrue(Destination.CaptureExactState(Before, Error)));

		FSeinEntityPool Source;
		Source.Initialize(3);
		FSeinEntityPoolExactState Malformed;
		ASSERT_THAT(IsTrue(Source.CaptureExactState(Malformed, Error)));
		ASSERT_THAT(IsFalse(Destination.TryStageExactState(
			Malformed,
			2,
			Error)));
		ASSERT_THAT(IsTrue(Destination.IsValid(Existing)));

		const int32 DuplicateFreeSlot = Malformed.FreeList[0];
		Malformed.FreeList.Add(DuplicateFreeSlot);

		ASSERT_THAT(IsFalse(Destination.TryStageExactState(
			Malformed,
			3,
			Error)));
		ASSERT_THAT(IsTrue(Destination.IsValid(Existing)));
		ASSERT_THAT(IsTrue(
			Destination.GetOwner(Existing) == FSeinPlayerID(7)));

		FSeinEntityPoolExactState After;
		ASSERT_THAT(IsTrue(Destination.CaptureExactState(After, Error)));
		ASSERT_THAT(IsTrue(Before == After));
	}

	TEST(EntityPoolExactStateRequiresExplicitTombstoneOptIn,
		"SeinARTS.Unit.Entity")
	{
		FSeinEntityPool Source;
		Source.Initialize(1);
		const FSeinEntityHandle Handle = Source.Acquire(
			FFixedTransform(),
			FSeinPlayerID(1));

		FSeinEntityPoolExactState Tombstoned;
		FString Error;
		ASSERT_THAT(IsTrue(Source.CaptureExactState(Tombstoned, Error)));
		Tombstoned.Slots[Handle.Index].Entity.SetAlive(false);

		FSeinEntityPool Destination;
		ASSERT_THAT(IsFalse(Destination.TryStageExactState(
			Tombstoned,
			1,
			Error)));
		ASSERT_THAT(IsTrue(Destination.TryStageExactState(
			Tombstoned,
			1,
			Error,
			true)));
		ASSERT_THAT(AreEqual(1, Destination.GetActiveCount()));
		ASSERT_THAT(IsFalse(Destination.IsValid(Handle)));

		FSeinEntityPoolExactState Captured;
		ASSERT_THAT(IsFalse(Destination.CaptureExactState(Captured, Error)));
		ASSERT_THAT(IsTrue(Destination.CaptureExactState(
			Captured,
			Error,
			true)));
		ASSERT_THAT(IsTrue(Captured == Tombstoned));
	}
}
