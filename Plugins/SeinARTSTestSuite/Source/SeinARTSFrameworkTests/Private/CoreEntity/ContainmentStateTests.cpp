#include "CQTest.h"
#include "Components/ActorTestSpawner.h"
#include "Containers/Ticker.h"

#include "Components/SeinAttachmentSpec.h"
#include "Components/SeinContainmentData.h"
#include "Components/SeinContainmentMemberData.h"
#include "Data/SeinWorldSnapshot.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSimContext.h"
#include "Simulation/SeinTestSnapshotRestore.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinARTSGameplayTags.h"

namespace UE::SeinARTSTests
{
	namespace ContainmentStateTestLocal
	{
		template<typename ComponentType>
		struct TBlobEntry
		{
			int32 Slot = 0;
			int32 Generation = 0;
			ComponentType Component;
		};

		template<typename ComponentType>
		bool RewriteComponent(
			FSeinWorldSnapshot& Snapshot,
			FSeinEntityHandle Target,
			TFunctionRef<void(ComponentType&)> Mutator)
		{
			FSeinSnapshotComponentStorageBlob* Blob =
				Snapshot.ComponentStorageBlobs.Find(
					ComponentType::StaticStruct()->GetPathName());
			if (!Blob)
			{
				return false;
			}

			TArray<uint8> SourceBytes = Blob->Bytes;
			FMemoryReader MemoryReader(SourceBytes, /*bIsPersistent=*/true);
			FObjectAndNameAsStringProxyArchive Reader(
				MemoryReader, /*bInLoadIfFindFails=*/true);
			int32 EntryCount = 0;
			Reader << EntryCount;
			if (Reader.IsError() || EntryCount != Blob->EntryCount
				|| EntryCount < 0)
			{
				return false;
			}

			bool bFound = false;
			TArray<TBlobEntry<ComponentType>> Entries;
			Entries.Reserve(EntryCount);
			for (int32 Index = 0; Index < EntryCount; ++Index)
			{
				TBlobEntry<ComponentType>& Entry =
					Entries.AddDefaulted_GetRef();
				Reader << Entry.Slot;
				Reader << Entry.Generation;
				ComponentType::StaticStruct()->SerializeBin(
					Reader, &Entry.Component);
				if (Reader.IsError())
				{
					return false;
				}
				if (Entry.Slot == Target.Index
					&& Entry.Generation == Target.Generation)
				{
					Mutator(Entry.Component);
					bFound = true;
				}
			}
			if (!bFound || MemoryReader.Tell() != SourceBytes.Num())
			{
				return false;
			}

			TArray<uint8> RewrittenBytes;
			FMemoryWriter MemoryWriter(
				RewrittenBytes, /*bIsPersistent=*/true);
			FObjectAndNameAsStringProxyArchive Writer(
				MemoryWriter, /*bInLoadIfFindFails=*/false);
			Writer << EntryCount;
			for (TBlobEntry<ComponentType>& Entry : Entries)
			{
				Writer << Entry.Slot;
				Writer << Entry.Generation;
				ComponentType::StaticStruct()->SerializeBin(
					Writer, &Entry.Component);
			}
			if (Writer.IsError())
			{
				return false;
			}
			Blob->Bytes = MoveTemp(RewrittenBytes);
			return true;
		}

		bool MaterializeNestedFixture(
			USeinWorldSubsystem& World,
			FSeinEntityHandle& OutRoot,
			FSeinEntityHandle& OutMiddle,
			FSeinEntityHandle& OutLeaf)
		{
			return SeinTestMatchBootstrap::Materialize(
				World,
				[&]()
				{
					OutRoot = World.SpawnAbstractEntity(
						FFixedTransform(), FSeinPlayerID::Neutral());
					OutMiddle = World.SpawnAbstractEntity(
						FFixedTransform(), FSeinPlayerID::Neutral());
					OutLeaf = World.SpawnAbstractEntity(
						FFixedTransform(), FSeinPlayerID::Neutral());

					FSeinContainmentData RootContainer;
					RootContainer.TotalCapacity = 3;
					RootContainer.bTracksVisualSlots = true;
					World.AddComponent(OutRoot, RootContainer);
					FSeinAttachmentSpec Attachment;
					FSeinAttachmentSlotDef& Slot =
						Attachment.Slots.AddDefaulted_GetRef();
					Slot.SlotTag = SeinARTSTags::State;
					World.AddComponent(OutRoot, Attachment);

					FSeinContainmentData MiddleContainer;
					MiddleContainer.TotalCapacity = 2;
					World.AddComponent(OutMiddle, MiddleContainer);
					World.AddComponent(
						OutMiddle, FSeinContainmentMemberData());
					World.AddComponent(
						OutLeaf, FSeinContainmentMemberData());

					World.AttachToSlot(
						OutMiddle, OutRoot, SeinARTSTags::State);
					World.EnterContainer(OutLeaf, OutMiddle);
				},
				FSeinMatchSettings(),
				0x434F4E54,
				TEXT("Containment.Nested"));
		}
	}

	TEST(ContainmentAdmissionRejectsCyclesAndLoadOverflow,
		"SeinARTS.Unit.CoreEntity.Containment")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		FSeinEntityHandle A;
		FSeinEntityHandle B;
		FSeinEntityHandle CapacityContainer;
		FSeinEntityHandle SmallMember;
		FSeinEntityHandle HugeMember;
		bool bFirstEnter = false;
		bool bCycleEnter = true;
		bool bSmallEnter = false;
		bool bOverflowEnter = true;
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World,
			[&]()
			{
				A = World->SpawnAbstractEntity(
					FFixedTransform(), FSeinPlayerID::Neutral());
				B = World->SpawnAbstractEntity(
					FFixedTransform(), FSeinPlayerID::Neutral());
				FSeinContainmentData Container;
				Container.TotalCapacity = 2;
				World->AddComponent(A, Container);
				World->AddComponent(B, Container);
				World->AddComponent(A, FSeinContainmentMemberData());
				World->AddComponent(B, FSeinContainmentMemberData());
				bFirstEnter = World->EnterContainer(A, B);
				bCycleEnter = World->EnterContainer(B, A);

				CapacityContainer = World->SpawnAbstractEntity(
					FFixedTransform(), FSeinPlayerID::Neutral());
				SmallMember = World->SpawnAbstractEntity(
					FFixedTransform(), FSeinPlayerID::Neutral());
				HugeMember = World->SpawnAbstractEntity(
					FFixedTransform(), FSeinPlayerID::Neutral());
				FSeinContainmentData LargeContainer;
				LargeContainer.TotalCapacity = MAX_int32;
				World->AddComponent(CapacityContainer, LargeContainer);
				World->AddComponent(
					SmallMember, FSeinContainmentMemberData());
				FSeinContainmentMemberData Huge;
				Huge.Size = MAX_int32;
				World->AddComponent(HugeMember, Huge);
				bSmallEnter = World->EnterContainer(
					SmallMember, CapacityContainer);
				bOverflowEnter = World->EnterContainer(
					HugeMember, CapacityContainer);
			},
			FSeinMatchSettings(), 0,
			TEXT("Containment.Admission"))));

		ASSERT_THAT(IsTrue(bFirstEnter));
		ASSERT_THAT(IsFalse(bCycleEnter));
		ASSERT_THAT(IsTrue(bSmallEnter));
		ASSERT_THAT(IsFalse(bOverflowEnter));
		ASSERT_THAT(IsTrue(World->GetRootContainer(A) == B));
		ASSERT_THAT(IsFalse(World->IsContained(B)));
		const FSeinContainmentData* Capacity =
			World->GetComponent<FSeinContainmentData>(CapacityContainer);
		ASSERT_THAT(IsNotNull(Capacity));
		ASSERT_THAT(AreEqual(1, Capacity->CurrentLoad));
	}

	TEST(ContainmentBootstrapRejectsNonReciprocalState,
		"SeinARTS.Unit.CoreEntity.Containment")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		FString Error;
		TestRunner->AddExpectedError(
			TEXT("Local match bootstrap has invalid containment state"),
			EAutomationExpectedErrorFlags::Contains, 2, false);
		TestRunner->AddExpectedError(
			TEXT("SeinMatchBootstrap: transaction closed (failed)."),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		ASSERT_THAT(IsFalse(SeinTestMatchBootstrap::Materialize(
			*World,
			[&]()
			{
				const FSeinEntityHandle Container =
					World->SpawnAbstractEntity(
						FFixedTransform(), FSeinPlayerID::Neutral());
				const FSeinEntityHandle Member =
					World->SpawnAbstractEntity(
						FFixedTransform(), FSeinPlayerID::Neutral());
				FSeinContainmentData ContainerData;
				ContainerData.TotalCapacity = 2;
				World->AddComponent(Container, ContainerData);
				FSeinContainmentMemberData MemberData;
				MemberData.CurrentContainer = Container;
				World->AddComponent(Member, MemberData);
			},
			FSeinMatchSettings(), 0,
			TEXT("Containment.InvalidBootstrap"), &Error)));
		ASSERT_THAT(IsTrue(Error.Contains(
			TEXT("invalid containment state"))));
	}

	TEST(ContainmentQueriesTerminateOnMalformedCycles,
		"SeinARTS.Unit.CoreEntity.Containment")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		FSeinEntityHandle A;
		FSeinEntityHandle B;
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World,
			[&]()
			{
				A = World->SpawnAbstractEntity(
					FFixedTransform(), FSeinPlayerID::Neutral());
				B = World->SpawnAbstractEntity(
					FFixedTransform(), FSeinPlayerID::Neutral());
				FSeinContainmentData Container;
				Container.TotalCapacity = 2;
				World->AddComponent(A, Container);
				World->AddComponent(B, Container);
				World->AddComponent(A, FSeinContainmentMemberData());
				World->AddComponent(B, FSeinContainmentMemberData());
				World->EnterContainer(A, B);
			},
			FSeinMatchSettings(), 0,
			TEXT("Containment.QueryGuard"))));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));

		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			FSeinContainmentMemberData* BMember =
				World->GetComponentMutable<FSeinContainmentMemberData>(B);
			FSeinContainmentData* AContainer =
				World->GetComponentMutable<FSeinContainmentData>(A);
			ASSERT_THAT(IsNotNull(BMember));
			ASSERT_THAT(IsNotNull(AContainer));
			BMember->CurrentContainer = A;
			AContainer->Occupants.Add(B);
			AContainer->CurrentLoad = 1;
		}
		FGuid RejectedRoot;
		FString RootError;
		ASSERT_THAT(IsFalse(World->ComputeCanonicalStateRoot(
			RejectedRoot, RootError)));
		ASSERT_THAT(IsTrue(RootError.Contains(
			TEXT("invalid containment state"))));

		TestRunner->AddExpectedError(
			TEXT("GetRootContainer: containment cycle reached"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		ASSERT_THAT(IsFalse(World->GetRootContainer(A).IsValid()));
		const TArray<FSeinEntityHandle> Nested =
			World->GetAllNestedOccupants(A);
		ASSERT_THAT(AreEqual(1, Nested.Num()));
		ASSERT_THAT(IsTrue(Nested[0] == B));
		const FSeinContainmentTree Tree = World->BuildContainmentTree(A);
		ASSERT_THAT(AreEqual(2, Tree.Entries.Num()));

		FSeinWorldSnapshot Rejected;
		TestRunner->AddExpectedError(
			TEXT("CaptureSnapshot: invalid containment state"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		World->CaptureSnapshot(Rejected);
		ASSERT_THAT(AreEqual(0, Rejected.SnapshotVersion));
		World->StopSimulation();
	}

	TEST(ContainmentSnapshotContinuationAndCorruptionRejection,
		"SeinARTS.Determinism.CoreEntity.Containment")
	{
		using namespace ContainmentStateTestLocal;
		FActorTestSpawner SourceSpawner;
		USeinWorldSubsystem* Source =
			SourceSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Source));
		FSeinEntityHandle Root;
		FSeinEntityHandle Middle;
		FSeinEntityHandle Leaf;
		ASSERT_THAT(IsTrue(MaterializeNestedFixture(
			*Source, Root, Middle, Leaf)));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*Source)));

		FGuid SourceRoot;
		FString Error;
		ASSERT_THAT(IsTrue(Source->ComputeCanonicalStateRoot(
			SourceRoot, Error)));
		FSeinWorldSnapshot Snapshot;
		Source->CaptureSnapshot(Snapshot);
		ASSERT_THAT(IsTrue(Snapshot.SnapshotVersion > 0));

		FActorTestSpawner DestinationSpawner;
		USeinWorldSubsystem* Destination = DestinationSpawner.GetWorld()
			.GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Destination));
		ASSERT_THAT(IsTrue(SeinTestSnapshotRestore::RestoreTrusted(
			*Destination, Snapshot)));
		FGuid DestinationRoot;
		ASSERT_THAT(IsTrue(Destination->ComputeCanonicalStateRoot(
			DestinationRoot, Error)));
		ASSERT_THAT(IsTrue(SourceRoot == DestinationRoot));
		ASSERT_THAT(IsTrue(Destination->GetRootContainer(Leaf) == Root));
		const FSeinContainmentTree RestoredTree =
			Destination->BuildContainmentTree(Root);
		ASSERT_THAT(AreEqual(3, RestoredTree.Entries.Num()));
		ASSERT_THAT(IsTrue(RestoredTree.Entries[0].Entity == Root));
		ASSERT_THAT(IsTrue(RestoredTree.Entries[1].Entity == Middle));
		ASSERT_THAT(IsTrue(RestoredTree.Entries[2].Entity == Leaf));
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Source);
			ASSERT_THAT(IsTrue(Source->ExitContainer(Leaf)));
			ASSERT_THAT(IsTrue(Source->EnterContainer(Leaf, Root)));
		}
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Destination);
			ASSERT_THAT(IsTrue(Destination->ExitContainer(Leaf)));
			ASSERT_THAT(IsTrue(Destination->EnterContainer(Leaf, Root)));
		}
		FGuid MutatedSourceRoot;
		FGuid MutatedDestinationRoot;
		ASSERT_THAT(IsTrue(Source->ComputeCanonicalStateRoot(
			MutatedSourceRoot, Error)));
		ASSERT_THAT(IsTrue(Destination->ComputeCanonicalStateRoot(
			MutatedDestinationRoot, Error)));
		ASSERT_THAT(IsTrue(MutatedSourceRoot == MutatedDestinationRoot));

		for (int32 Tick = 0; Tick < 3; ++Tick)
		{
			FTSTicker::GetCoreTicker().Tick(
				Source->GetFixedDeltaTimeSeconds());
		}
		FGuid ContinuedSourceRoot;
		FGuid ContinuedDestinationRoot;
		ASSERT_THAT(IsTrue(Source->ComputeCanonicalStateRoot(
			ContinuedSourceRoot, Error)));
		ASSERT_THAT(IsTrue(Destination->ComputeCanonicalStateRoot(
			ContinuedDestinationRoot, Error)));
		ASSERT_THAT(IsTrue(
			ContinuedSourceRoot == ContinuedDestinationRoot));

		FSeinWorldSnapshot Corrupt = Snapshot;
		ASSERT_THAT(IsTrue(RewriteComponent<FSeinContainmentMemberData>(
			Corrupt, Middle,
			[](FSeinContainmentMemberData& Member)
			{
				Member.CurrentContainer = FSeinEntityHandle();
			})));
		FGuid RootBeforeRejectedRestore;
		ASSERT_THAT(IsTrue(Destination->ComputeCanonicalStateRoot(
			RootBeforeRejectedRestore, Error)));
		const int32 TickBeforeRejectedRestore =
			Destination->GetCurrentTick();
		TestRunner->AddExpectedError(
			TEXT("RestoreSnapshot: containment state failed structural preflight"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		ASSERT_THAT(IsFalse(SeinTestSnapshotRestore::RestoreTrusted(
			*Destination, Corrupt)));
		FGuid RootAfterRejectedRestore;
		ASSERT_THAT(IsTrue(Destination->ComputeCanonicalStateRoot(
			RootAfterRejectedRestore, Error)));
		ASSERT_THAT(IsTrue(
			RootBeforeRejectedRestore == RootAfterRejectedRestore));
		ASSERT_THAT(AreEqual(
			TickBeforeRejectedRestore, Destination->GetCurrentTick()));
		ASSERT_THAT(IsTrue(Destination->GetRootContainer(Leaf) == Root));
		const FSeinContainmentData* RootContainer =
			Destination->GetComponent<FSeinContainmentData>(Root);
		const FSeinContainmentData* MiddleContainer =
			Destination->GetComponent<FSeinContainmentData>(Middle);
		const FSeinContainmentMemberData* LeafMember =
			Destination->GetComponent<FSeinContainmentMemberData>(Leaf);
		const FSeinAttachmentSpec* Attachment =
			Destination->GetComponent<FSeinAttachmentSpec>(Root);
		ASSERT_THAT(IsNotNull(RootContainer));
		ASSERT_THAT(IsNotNull(MiddleContainer));
		ASSERT_THAT(IsNotNull(LeafMember));
		ASSERT_THAT(IsNotNull(Attachment));
		ASSERT_THAT(AreEqual(2, RootContainer->CurrentLoad));
		ASSERT_THAT(AreEqual(0, MiddleContainer->CurrentLoad));
		ASSERT_THAT(IsTrue(RootContainer->Occupants.Contains(Middle)));
		ASSERT_THAT(IsTrue(RootContainer->Occupants.Contains(Leaf)));
		ASSERT_THAT(IsTrue(
			RootContainer->VisualSlotAssignments.Contains(Middle)));
		ASSERT_THAT(IsTrue(
			RootContainer->VisualSlotAssignments.Contains(Leaf)));
		ASSERT_THAT(IsTrue(LeafMember->CurrentContainer == Root));
		const FSeinEntityHandle* SlotOccupant =
			Attachment->Assignments.Find(SeinARTSTags::State);
		ASSERT_THAT(IsNotNull(SlotOccupant));
		ASSERT_THAT(IsTrue(*SlotOccupant == Middle));

		FTSTicker::GetCoreTicker().Tick(
			Source->GetFixedDeltaTimeSeconds());
		FGuid PostFailureSourceRoot;
		FGuid PostFailureDestinationRoot;
		ASSERT_THAT(IsTrue(Source->ComputeCanonicalStateRoot(
			PostFailureSourceRoot, Error)));
		ASSERT_THAT(IsTrue(Destination->ComputeCanonicalStateRoot(
			PostFailureDestinationRoot, Error)));
		ASSERT_THAT(IsTrue(
			PostFailureSourceRoot == PostFailureDestinationRoot));
	}
}
