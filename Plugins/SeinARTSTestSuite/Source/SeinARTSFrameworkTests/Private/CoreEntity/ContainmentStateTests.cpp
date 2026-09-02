#include "CQTest.h"
#include "Components/ActorTestSpawner.h"
#include "Containers/Ticker.h"

#include "Components/SeinAttachmentSpec.h"
#include "Components/SeinAbilityPayload.h"
#include "Components/SeinContainmentData.h"
#include "Components/SeinContainmentMemberData.h"
#include "Components/SeinTransportSpec.h"
#include "Data/SeinReplayHeader.h"
#include "Data/SeinWorldSnapshot.h"
#include "HAL/FileManager.h"
#include "Input/SeinCommand.h"
#include "Lib/SeinAbilityBPFL.h"
#include "SeinReplayReader.h"
#include "SeinReplayWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Serialization/SeinSnapshotTransfer.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSimContext.h"
#include "Simulation/SeinTestSnapshotRestore.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "TestTypes/SeinContainmentTestTypes.h"

USeinContainmentEnterTestAbility::USeinContainmentEnterTestAbility()
{
	AbilityTag = FGameplayTag::RequestGameplayTag(
		TEXT("SeinARTS.Ability.Embark"));
	TargetType = ESeinAbilityTargetType::Entity;
}

void USeinContainmentEnterTestAbility::OnActivate_Implementation()
{
	if (WorldSubsystem)
	{
		WorldSubsystem->EnterContainer(OwnerEntity, TargetEntity);
	}
	EndAbility();
}

USeinContainmentExitTestAbility::USeinContainmentExitTestAbility()
{
	AbilityTag = FGameplayTag::RequestGameplayTag(
		TEXT("SeinARTS.Ability.Disembark"));
}

void USeinContainmentExitTestAbility::OnActivate_Implementation()
{
	if (WorldSubsystem)
	{
		WorldSubsystem->ExitContainer(OwnerEntity);
	}
	EndAbility();
}

namespace UE::SeinARTSTests
{
	namespace ContainmentStateTestLocal
	{
		const FSeinPlayerID PlayerOne(1);

		FGameplayTag EmbarkAbilityTag()
		{
			return FGameplayTag::RequestGameplayTag(
				TEXT("SeinARTS.Ability.Embark"));
		}

		FGameplayTag DisembarkAbilityTag()
		{
			return FGameplayTag::RequestGameplayTag(
				TEXT("SeinARTS.Ability.Disembark"));
		}

		FSeinMatchSettings MakeTransportMatchSettings()
		{
			FSeinMatchSettings Settings;
			FSeinMatchSlot& Slot = Settings.Slots.AddDefaulted_GetRef();
			Slot.SlotIndex = PlayerOne.Value;
			Slot.State = ESeinSlotState::Human;
			Slot.FactionID = FSeinFactionID(1);
			return Settings;
		}

		struct FTransportFixture
		{
			FSeinEntityHandle Container;
			FSeinEntityHandle Member;
			FFixedTransform ContainerTransform;
			FFixedVector DeployOffset;
		};

		bool MaterializeTransportFixture(
			USeinWorldSubsystem& World,
			FTransportFixture& OutFixture,
			FString* OutError = nullptr)
		{
			bool bAbilitiesReady = false;
			OutFixture.ContainerTransform = FFixedTransform(
				FFixedVector(
					FFixedPoint::FromInt(1000),
					FFixedPoint::FromInt(2000),
					FFixedPoint::FromInt(50)),
				FFixedRotator(
					FFixedPoint::Zero,
					FFixedPoint::FromInt(90),
					FFixedPoint::Zero),
				FFixedVector(
					FFixedPoint::FromInt(2),
					FFixedPoint::FromInt(3),
					FFixedPoint::FromInt(4)));
			OutFixture.DeployOffset = FFixedVector(
				FFixedPoint::FromInt(120),
				FFixedPoint::FromInt(25),
				FFixedPoint::FromInt(5));
			return SeinTestMatchBootstrap::Materialize(
				World,
				[&]()
				{
					World.RegisterPlayer(
						PlayerOne, FSeinFactionID(1));
					OutFixture.Container = World.SpawnAbstractEntity(
						OutFixture.ContainerTransform, PlayerOne);
					OutFixture.Member = World.SpawnAbstractEntity(
						FFixedTransform(), PlayerOne);

					FSeinContainmentData ContainerData;
					ContainerData.TotalCapacity = 2;
					World.AddComponent(
						OutFixture.Container, ContainerData);
					FSeinTransportSpec Transport;
					Transport.DeployOffset = OutFixture.DeployOffset;
					World.AddComponent(
						OutFixture.Container, Transport);
					World.AddComponent(
						OutFixture.Member,
						FSeinContainmentMemberData());
					World.AddComponent(
						OutFixture.Member, FSeinAbilityPayload());

					const int32 EnterAbilityID =
						USeinAbilityBPFL::SeinGrantAbility(
							&World,
							OutFixture.Member,
							USeinContainmentEnterTestAbility::StaticClass());
					const int32 ExitAbilityID =
						USeinAbilityBPFL::SeinGrantAbility(
							&World,
							OutFixture.Member,
							USeinContainmentExitTestAbility::StaticClass());
					USeinAbility* EnterAbility =
						World.GetAbilityInstance(EnterAbilityID);
					USeinAbility* ExitAbility =
						World.GetAbilityInstance(ExitAbilityID);
					if (EnterAbility)
					{
						EnterAbility->AbilityTag = EmbarkAbilityTag();
						EnterAbility->TargetType =
							ESeinAbilityTargetType::Entity;
					}
					if (ExitAbility)
					{
						ExitAbility->AbilityTag = DisembarkAbilityTag();
					}
					bAbilitiesReady = EnterAbility && ExitAbility;
				},
				MakeTransportMatchSettings(),
				0x5452414E,
				TEXT("Containment.Transport"),
				OutError)
				&& bAbilitiesReady;
		}

		FSeinCommand MakeTransportAbilityCommand(
			const FTransportFixture& Fixture,
			bool bEnter)
		{
			return FSeinCommand::MakeAbilityCommand(
				PlayerOne,
				Fixture.Member,
				bEnter ? EmbarkAbilityTag() : DisembarkAbilityTag(),
				bEnter
					? Fixture.Container
					: FSeinEntityHandle::Invalid(),
				FFixedVector::ZeroVector);
		}

		FFixedVector ExpectedDeployLocation(
			const FTransportFixture& Fixture)
		{
			return Fixture.ContainerTransform.GetLocation()
				+ Fixture.ContainerTransform.TransformVector(
					Fixture.DeployOffset);
		}

		FSeinReplayHeader MakeReplayHeader(USeinWorldSubsystem& World)
		{
			FSeinReplayHeader Header;
			SeinReplayCompatibility::StampCurrent(Header, World.GetWorld());
			Header.CommandProtocolDigest = World.GetCommandProtocolDigest();
			Header.MatchSettingsDigest = World.GetMatchSettingsDigest();
			Header.BootstrapReceipt = World.GetMatchBootstrapReceipt();
			Header.ConfigFingerprint = World.GetConfigFingerprint();
			Header.RandomSeed = World.GetSessionSeed();
			Header.SettingsSnapshot = World.GetMatchSettings();
			Header.StartTick = World.GetCurrentTick();
			Header.RecordedAt = FDateTime::UtcNow();
			for (const FSeinMatchSlot& Slot : Header.SettingsSnapshot.Slots)
			{
				if (Slot.State != ESeinSlotState::Human
					&& Slot.State != ESeinSlotState::AI)
				{
					continue;
				}
				FSeinPlayerRegistration& Player =
					Header.Players.AddDefaulted_GetRef();
				Player.PlayerID = FSeinPlayerID(
					static_cast<uint8>(Slot.SlotIndex));
				Player.FactionID = Slot.FactionID;
				Player.TeamID = Slot.TeamID;
				Player.bIsAI = Slot.State == ESeinSlotState::AI;
			}
			return Header;
		}

		struct FScopedReplayFile
		{
			FString Path;

			~FScopedReplayFile()
			{
				if (!Path.IsEmpty())
				{
					IFileManager::Get().Delete(*Path, false, true);
				}
			}
		};

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

	TEST(TransportExitRotatesLocalDeployOffsetAndHonorsExplicitLocation,
		"SeinARTS.Unit.CoreEntity.Containment")
	{
		using namespace ContainmentStateTestLocal;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		FTransportFixture Fixture;
		FString Error;
		ASSERT_THAT(IsTrue(MaterializeTransportFixture(
			*World, Fixture, &Error)));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World, &Error)));

		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			ASSERT_THAT(IsTrue(World->EnterContainer(
				Fixture.Member, Fixture.Container)));
			ASSERT_THAT(IsTrue(World->ExitContainer(Fixture.Member)));
		}
		const FSeinEntity* Member = World->GetEntity(Fixture.Member);
		ASSERT_THAT(IsNotNull(Member));
		const FFixedVector Expected = ExpectedDeployLocation(Fixture);
		ASSERT_THAT(IsTrue(Member->Transform.GetLocation() == Expected));
		ASSERT_THAT(IsFalse(
			Expected
				== Fixture.ContainerTransform.GetLocation()
					+ Fixture.DeployOffset));

		const FFixedVector ExplicitLocation(
			FFixedPoint::FromInt(-700),
			FFixedPoint::FromInt(800),
			FFixedPoint::FromInt(90));
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			ASSERT_THAT(IsTrue(World->EnterContainer(
				Fixture.Member, Fixture.Container)));
			ASSERT_THAT(IsTrue(World->ExitContainer(
				Fixture.Member, ExplicitLocation)));
		}
		Member = World->GetEntity(Fixture.Member);
		ASSERT_THAT(IsNotNull(Member));
		ASSERT_THAT(IsTrue(
			Member->Transform.GetLocation() == ExplicitLocation));
		World->StopSimulation();
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

	TEST(TransportAbilityCommandContinuesAcrossCheckpointTransfer,
		"SeinARTS.Determinism.CoreEntity.Containment")
	{
		using namespace ContainmentStateTestLocal;
		FActorTestSpawner SourceSpawner;
		USeinWorldSubsystem* Source =
			SourceSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Source));
		FTransportFixture Fixture;
		FString Error;
		ASSERT_THAT(IsTrue(MaterializeTransportFixture(
			*Source, Fixture, &Error)));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*Source, &Error)));

		Source->SubmitLocalCommandDraft(
			MakeTransportAbilityCommand(Fixture, /*bEnter=*/true));
		FTSTicker::GetCoreTicker().Tick(
			Source->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(IsTrue(Source->IsContained(Fixture.Member)));

		Source->SubmitLocalCommandDraft(
			MakeTransportAbilityCommand(Fixture, /*bEnter=*/false));
		ASSERT_THAT(AreEqual(1, Source->GetPendingCommands().Num()));
		FGuid CheckpointRoot;
		ASSERT_THAT(IsTrue(Source->ComputeCanonicalStateRoot(
			CheckpointRoot, Error)));
		FSeinWorldSnapshot Checkpoint;
		FSeinWorldSnapshotReferenceGuard CheckpointGuard(Checkpoint);
		Source->CaptureSnapshot(Checkpoint);
		ASSERT_THAT(AreEqual(1, Checkpoint.PendingCommands.Num()));

		TArray<uint8> EnvelopeBytes;
		FSeinSnapshotEnvelopeMetadata Metadata;
		ASSERT_THAT(IsTrue(SeinSnapshotTransfer::EncodeCheckpointEnvelope(
			Checkpoint, EnvelopeBytes, Metadata, Error)));
		FSeinWorldSnapshot Transferred;
		FSeinWorldSnapshotReferenceGuard TransferredGuard(Transferred);
		FSeinSnapshotEnvelopeMetadata TransferredMetadata;
		ASSERT_THAT(IsTrue(SeinSnapshotTransfer::DecodeCheckpointEnvelope(
			EnvelopeBytes, Transferred, TransferredMetadata, Error)));

		FTSTicker::GetCoreTicker().Tick(
			Source->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(IsFalse(Source->IsContained(Fixture.Member)));
		const int32 ContinuedTick = Source->GetCurrentTick();
		FGuid SourceRoot;
		ASSERT_THAT(IsTrue(Source->ComputeCanonicalStateRoot(
			SourceRoot, Error)));
		Source->StopSimulation();

		FActorTestSpawner DestinationSpawner;
		USeinWorldSubsystem* Destination = DestinationSpawner.GetWorld()
			.GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Destination));
		ASSERT_THAT(IsTrue(SeinTestSnapshotRestore::RestoreTrusted(
			*Destination, Transferred, &Error)));
		ASSERT_THAT(IsTrue(Destination->IsContained(Fixture.Member)));
		ASSERT_THAT(AreEqual(
			1, Destination->GetPendingCommands().Num()));
		FGuid RestoredCheckpointRoot;
		ASSERT_THAT(IsTrue(Destination->ComputeCanonicalStateRoot(
			RestoredCheckpointRoot, Error)));
		ASSERT_THAT(IsTrue(CheckpointRoot == RestoredCheckpointRoot));
		FTSTicker::GetCoreTicker().Tick(
			Destination->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(IsFalse(Destination->IsContained(Fixture.Member)));
		ASSERT_THAT(AreEqual(
			ContinuedTick, Destination->GetCurrentTick()));
		const FSeinEntity* SourceMember =
			Source->GetEntity(Fixture.Member);
		const FSeinEntity* DestinationMember =
			Destination->GetEntity(Fixture.Member);
		ASSERT_THAT(IsNotNull(SourceMember));
		ASSERT_THAT(IsNotNull(DestinationMember));
		const FFixedVector Expected = ExpectedDeployLocation(Fixture);
		ASSERT_THAT(IsTrue(
			SourceMember->Transform.GetLocation() == Expected));
		ASSERT_THAT(IsTrue(
			DestinationMember->Transform.GetLocation() == Expected));

		FGuid DestinationRoot;
		ASSERT_THAT(IsTrue(Destination->ComputeCanonicalStateRoot(
			DestinationRoot, Error)));
		ASSERT_THAT(IsTrue(SourceRoot == DestinationRoot));
		Destination->StopSimulation();
	}

	TEST(TransportAbilityCommandsReplayToExactDeploymentState,
		"SeinARTS.Determinism.CoreEntity.Containment")
	{
		using namespace ContainmentStateTestLocal;
		FActorTestSpawner SourceSpawner;
		USeinWorldSubsystem* Source =
			SourceSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Source));
		FTransportFixture Fixture;
		FString Error;
		ASSERT_THAT(IsTrue(MaterializeTransportFixture(
			*Source, Fixture, &Error)));
		ASSERT_THAT(IsTrue(
			SeinTestMatchBootstrap::Authorize(*Source, &Error)));

		USeinReplayWriter* Writer = NewObject<USeinReplayWriter>(Source);
		ASSERT_THAT(IsNotNull(Writer));
		Writer->StartRecording(MakeReplayHeader(*Source));
		ASSERT_THAT(IsTrue(Writer->IsRecording()));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*Source, &Error)));
		ASSERT_THAT(IsTrue(
			Writer->CaptureCheckpoint(/*bRequired=*/true)));

		const USeinARTSCoreSettings* Settings =
			GetDefault<USeinARTSCoreSettings>();
		ASSERT_THAT(IsNotNull(Settings));
		const int32 TicksPerTurn = Settings->TurnRate > 0
			? FMath::Max(
				1, Settings->SimulationTickRate / Settings->TurnRate)
			: 1;
		const int32 FirstTurn = Settings->InputDelayTurns > 0
			? Settings->InputDelayTurns
			: 3;
		const int32 SecondTurn = FirstTurn + 1;
		const int32 EnterTick = FirstTurn * TicksPerTurn;
		const int32 ExitTick = SecondTurn * TicksPerTurn;
		FSeinCommand Enter = MakeTransportAbilityCommand(
			Fixture, /*bEnter=*/true);
		Enter.Tick = EnterTick;
		Enter.IssuerKind = ESeinCommandIssuerKind::Player;
		FSeinCommand Exit = MakeTransportAbilityCommand(
			Fixture, /*bEnter=*/false);
		Exit.Tick = ExitTick;
		Exit.IssuerKind = ESeinCommandIssuerKind::Player;
		Writer->RecordTurn(FirstTurn, {Enter});
		Writer->RecordTurn(SecondTurn, {Exit});

		TArray<FGuid> SourceRoots;
		TArray<uint8> SourceContainment;
		SourceRoots.SetNum(ExitTick + 1);
		SourceContainment.SetNumZeroed(ExitTick + 1);
		for (int32 ExpectedTick = 1;
			ExpectedTick <= ExitTick;
			++ExpectedTick)
		{
			if (ExpectedTick == EnterTick)
			{
				Source->SubmitLocalCommandDraft(Enter);
			}
			if (ExpectedTick == ExitTick)
			{
				Source->SubmitLocalCommandDraft(Exit);
			}
			FTSTicker::GetCoreTicker().Tick(
				Source->GetFixedDeltaTimeSeconds());
			ASSERT_THAT(AreEqual(
				ExpectedTick, Source->GetCurrentTick()));
			const bool bExpectedContained =
				ExpectedTick >= EnterTick && ExpectedTick < ExitTick;
			ASSERT_THAT(IsTrue(
				Source->IsContained(Fixture.Member) == bExpectedContained));
			ASSERT_THAT(IsTrue(Source->ComputeCanonicalStateRoot(
				SourceRoots[ExpectedTick], Error)));
			SourceContainment[ExpectedTick] = bExpectedContained ? 1 : 0;
			Writer->ObserveCompletedTick(ExpectedTick);
		}
		ASSERT_THAT(IsFalse(Source->IsContained(Fixture.Member)));
		const FSeinEntity* SourceMember =
			Source->GetEntity(Fixture.Member);
		ASSERT_THAT(IsNotNull(SourceMember));
		ASSERT_THAT(IsTrue(
			SourceMember->Transform.GetLocation()
				== ExpectedDeployLocation(Fixture)));
		FGuid SourceRoot;
		ASSERT_THAT(IsTrue(Source->ComputeCanonicalStateRoot(
			SourceRoot, Error)));
		const int32 SourceHash = Source->ComputeStateHash();
		Source->StopSimulation();

		FScopedReplayFile ReplayFile{Writer->FinishRecording()};
		ASSERT_THAT(IsFalse(ReplayFile.Path.IsEmpty()));
		ASSERT_THAT(IsTrue(
			IFileManager::Get().FileExists(*ReplayFile.Path)));
		for (int32 ExpectedTick = 1;
			ExpectedTick <= ExitTick;
			++ExpectedTick)
		{
			FActorTestSpawner TargetSpawner;
			USeinWorldSubsystem* Target = TargetSpawner.GetWorld()
				.GetSubsystem<USeinWorldSubsystem>();
			ASSERT_THAT(IsNotNull(Target));
			USeinReplayReader* Reader = NewObject<USeinReplayReader>(
				&TargetSpawner.GetWorld());
			ASSERT_THAT(IsNotNull(Reader));
			ASSERT_THAT(IsTrue(Reader->LoadFromFile(ReplayFile.Path)));
			ASSERT_THAT(IsTrue(Reader->Play()));
			for (int32 Pump = 0;
				Pump < ExpectedTick * 4
					&& Target->GetCurrentTick() < ExpectedTick
					&& Reader->IsPlaying();
				++Pump)
			{
				FTSTicker::GetCoreTicker().Tick(
					Target->GetFixedDeltaTimeSeconds());
			}
			ASSERT_THAT(AreEqual(
				ExpectedTick, Target->GetCurrentTick()));
			if (Reader->IsPlaying())
			{
				Reader->Stop();
			}
			ASSERT_THAT(IsFalse(Reader->IsPlaying()));
			if (!Target->IsSimulationRunning())
			{
				ASSERT_THAT(IsTrue(Target->StartSimulation()));
			}
			ASSERT_THAT(IsTrue(
				Target->IsContained(Fixture.Member)
					== (SourceContainment[ExpectedTick] != 0)));
			FGuid TargetTickRoot;
			ASSERT_THAT(IsTrue(Target->ComputeCanonicalStateRoot(
				TargetTickRoot, Error)));
			ASSERT_THAT(IsTrue(
				TargetTickRoot == SourceRoots[ExpectedTick]));
			if (ExpectedTick == ExitTick)
			{
				const FSeinEntity* TargetMember =
					Target->GetEntity(Fixture.Member);
				ASSERT_THAT(IsNotNull(TargetMember));
				ASSERT_THAT(IsTrue(
					TargetMember->Transform.GetLocation()
						== ExpectedDeployLocation(Fixture)));
				ASSERT_THAT(IsTrue(SourceRoot == TargetTickRoot));
				ASSERT_THAT(AreEqual(
					SourceHash, Target->ComputeStateHash()));
			}
			Target->StopSimulation();
		}
	}
}
