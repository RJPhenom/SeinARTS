#include "CQTest.h"
#include "Components/ActorTestSpawner.h"
#include "Actor/SeinActor.h"
#include "Components/SeinBrokerMembershipData.h"
#include "Components/SeinCommandBrokerData.h"
#include "Components/SeinSquadComponent.h"
#include "Components/SeinSquadMemberComponent.h"
#include "Core/SeinPlayerState.h"
#include "Data/SeinWorldSnapshot.h"
#include "Reinforcement/SeinSquadReinforcementService.h"
#include "SeinSquadMutationBPFL.h"
#include "Serialization/SeinSnapshotTransfer.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSimContext.h"
#include "Simulation/SeinTestSnapshotRestore.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinARTSGameplayTags.h"

namespace
{
	FSeinResourceCost Cost(int32 Amount)
	{
		FSeinResourceCost Result;
		Result.Amounts.Add(
			SeinARTSTags::Resource,
			FFixedPoint::FromInt(Amount));
		return Result;
	}

	struct FScopedAddTowardCapCatalog
	{
		FScopedAddTowardCapCatalog()
		{
			Settings = GetMutableDefault<USeinARTSCoreSettings>();
			check(Settings);
			PreviousCatalog = Settings->ResourceCatalog;
			FSeinResourceDefinition Definition;
			Definition.ResourceTag = SeinARTSTags::Resource;
			Definition.CostDirection = ESeinCostDirection::AddTowardCap;
			Definition.DefaultCap = FFixedPoint::Zero;
			Definition.OverflowBehavior =
				ESeinResourceOverflowBehavior::AllowUnbounded;
			Settings->ResourceCatalog = {Definition};
		}

		~FScopedAddTowardCapCatalog()
		{
			Settings->ResourceCatalog = MoveTemp(PreviousCatalog);
		}

		USeinARTSCoreSettings* Settings = nullptr;
		TArray<FSeinResourceDefinition> PreviousCatalog;
	};

	struct FSquadReinforcementFixture
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = nullptr;
		FSeinPlayerID Player = FSeinPlayerID(1);
		FSeinPlayerID OtherPlayer = FSeinPlayerID(2);
		FSeinEntityHandle Squad;

		bool Initialize(FFixedPoint BuildTime = FFixedPoint::FromInt(1))
		{
			World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
			if (!World) return false;

			FString Error;
			return SeinTestMatchBootstrap::Materialize(
					*World,
					[this, BuildTime]()
					{
						World->RegisterPlayer(Player, FSeinFactionID(1));
						World->RegisterPlayer(OtherPlayer, FSeinFactionID(2));
						World->GetPlayerStateMutable(Player)->SetResource(
							SeinARTSTags::Resource,
							FFixedPoint::FromInt(100));
						World->GetPlayerStateMutable(OtherPlayer)->SetResource(
							SeinARTSTags::Resource,
							FFixedPoint::FromInt(50));

						Squad = World->SpawnAbstractEntity(
							FFixedTransform(), Player);
						FSeinSquadComponent SquadData;
						for (int32 Index = 0; Index < 2; ++Index)
						{
							FSeinSquadSlot Slot;
							Slot.SlotTags.AddTag(SeinARTSTags::State);
							Slot.Entity = ASeinActor::StaticClass();
							Slot.ReinforceCost = Cost(Index == 0 ? 10 : 20);
							Slot.ReinforceBuildTime = BuildTime;
							Slot.ReinforceCooldown = FFixedPoint::FromInt(Index + 1);
							Slot.OffsetTransform.SetLocation(FFixedVector(
								FFixedPoint::FromInt(Index * 100),
								FFixedPoint::Zero,
								FFixedPoint::Zero));
							SquadData.Slots.Add(Slot);
						}
						World->AddComponent(Squad, SquadData);

						FSeinCommandBrokerData Broker;
						Broker.bSelfCullOnEmpty = false;
						World->AddComponent(Squad, Broker);
					},
					FSeinMatchSettings(),
					0x53515541,
					TEXT("Squad.Reinforcement"),
					&Error)
				&& SeinTestMatchBootstrap::Start(*World, &Error);
		}

		FFixedPoint Balance(FSeinPlayerID ForPlayer) const
		{
			const FSeinPlayerState* State = World->GetPlayerState(ForPlayer);
			return State
				? State->GetResource(SeinARTSTags::Resource)
				: FFixedPoint::Zero;
		}

		FSeinEntityHandle AddEmptySquad()
		{
			const FSeinEntityHandle Handle = World->SpawnAbstractEntity(
				FFixedTransform(), Player);
			FSeinSquadComponent Data;
			FSeinSquadSlot Slot;
			Slot.SlotTags.AddTag(SeinARTSTags::State);
			Slot.Entity = ASeinActor::StaticClass();
			Data.Slots.Add(Slot);
			World->AddComponent(Handle, Data);
			FSeinCommandBrokerData Broker;
			Broker.bSelfCullOnEmpty = false;
			World->AddComponent(Handle, Broker);
			return Handle;
		}
	};

	bool ComputeReinforcementRoot(
		USeinWorldSubsystem& World,
		FGuid& OutRoot)
	{
		FString Error;
		return World.ComputeCanonicalStateRoot(OutRoot, Error);
	}
}

TEST(SquadReinforcementRejectsInvalidOrOverflowingCharges,
	"SeinARTS.Unit.Squad.Reinforcement.AccountingBounds")
{
	FScopedAddTowardCapCatalog CatalogScope;
	FSquadReinforcementFixture Fixture;
	ASSERT_THAT(IsTrue(Fixture.Initialize()));

	int64 RequestID = -1;
	{
		auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
		Fixture.World->GetPlayerStateMutable(Fixture.Player)->SetResource(
			SeinARTSTags::Resource, FFixedPoint::MaxValue);
		ASSERT_THAT(IsFalse(USeinSquadMutationBPFL::
			SeinQueueSquadReinforcement(
				Fixture.World, Fixture.Squad, 0, RequestID)));
	}
	ASSERT_THAT(AreEqual(static_cast<int64>(0), RequestID));
	ASSERT_THAT(IsTrue(
		Fixture.Balance(Fixture.Player) == FFixedPoint::MaxValue));
	const FSeinSquadComponent* Squad =
		Fixture.World->GetComponent<FSeinSquadComponent>(Fixture.Squad);
	ASSERT_THAT(IsNotNull(Squad));
	ASSERT_THAT(AreEqual(0, Squad->ReinforceQueue.Num()));
	ASSERT_THAT(AreEqual(static_cast<int64>(1),
		Squad->NextReinforceRequestID));

	{
		auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
		FSeinSquadComponent* Mutable =
			Fixture.World->GetComponentMutable<FSeinSquadComponent>(
				Fixture.Squad);
		Mutable->Slots[0].ReinforceCost.Amounts[
			SeinARTSTags::Resource] = -FFixedPoint::One;
		ASSERT_THAT(IsFalse(USeinSquadMutationBPFL::
			SeinQueueSquadReinforcement(
				Fixture.World, Fixture.Squad, 0, RequestID)));
		Mutable->Slots[0].ReinforceCost = Cost(1);
		Mutable->Slots[0].ReinforceBuildTime = -FFixedPoint::One;
		ASSERT_THAT(IsFalse(USeinSquadMutationBPFL::
			SeinQueueSquadReinforcement(
				Fixture.World, Fixture.Squad, 0, RequestID)));
	}
}

TEST(SquadReinforcementRequestsAreExactAndRefundTheirFundingPlayer,
	"SeinARTS.Unit.Squad.Reinforcement.Accounting")
{
	FSquadReinforcementFixture Fixture;
	ASSERT_THAT(IsTrue(Fixture.Initialize()));

	int64 FirstRequest = 0;
	int64 SecondRequest = 0;
	int64 DuplicateRequest = -1;
	{
		auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
		ASSERT_THAT(IsTrue(USeinSquadMutationBPFL::
			SeinQueueSquadReinforcement(
				Fixture.World, Fixture.Squad, 0, FirstRequest)));
		ASSERT_THAT(IsTrue(USeinSquadMutationBPFL::
			SeinQueueSquadReinforcement(
				Fixture.World, Fixture.Squad, 1, SecondRequest)));
		ASSERT_THAT(IsFalse(USeinSquadMutationBPFL::
			SeinQueueSquadReinforcement(
				Fixture.World, Fixture.Squad, 1, DuplicateRequest)));
	}

	ASSERT_THAT(AreEqual(static_cast<int64>(1), FirstRequest));
	ASSERT_THAT(AreEqual(static_cast<int64>(2), SecondRequest));
	ASSERT_THAT(AreEqual(static_cast<int64>(0), DuplicateRequest));
	ASSERT_THAT(IsTrue(
		Fixture.Balance(Fixture.Player) == FFixedPoint::FromInt(70)));

	const FSeinSquadComponent* Squad =
		Fixture.World->GetComponent<FSeinSquadComponent>(Fixture.Squad);
	ASSERT_THAT(IsNotNull(Squad));
	ASSERT_THAT(AreEqual(2, Squad->ReinforceQueue.Num()));
	ASSERT_THAT(AreEqual(0, Squad->ReinforceQueue[0].RequestedSlotIndex));
	ASSERT_THAT(AreEqual(1, Squad->ReinforceQueue[1].RequestedSlotIndex));
	ASSERT_THAT(IsTrue(
		Squad->ReinforceQueue[0].SlotTag
			== Squad->ReinforceQueue[1].SlotTag));
	ASSERT_THAT(IsTrue(
		Squad->ReinforceQueue[0].ResourcePayer == Fixture.Player));
	ASSERT_THAT(AreEqual(static_cast<int64>(3),
		Squad->NextReinforceRequestID));

	{
		auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
		Fixture.World->GetPlayerStateMutable(Fixture.Player)->SetResource(
			SeinARTSTags::Resource, FFixedPoint::MaxValue);
		ASSERT_THAT(IsFalse(USeinSquadMutationBPFL::
			SeinCancelSquadReinforcement(
				Fixture.World, Fixture.Squad, FirstRequest)));
		ASSERT_THAT(AreEqual(2,
			Fixture.World->GetComponent<FSeinSquadComponent>(
				Fixture.Squad)->ReinforceQueue.Num()));
		Fixture.World->GetPlayerStateMutable(Fixture.Player)->SetResource(
			SeinARTSTags::Resource, FFixedPoint::FromInt(100));
		Fixture.World->SetEntityOwner(Fixture.Squad, Fixture.OtherPlayer);
		ASSERT_THAT(IsTrue(USeinSquadMutationBPFL::
			SeinCancelSquadReinforcement(
				Fixture.World, Fixture.Squad, FirstRequest)));
		ASSERT_THAT(IsFalse(USeinSquadMutationBPFL::
			SeinCancelSquadReinforcement(
				Fixture.World, Fixture.Squad, FirstRequest)));
	}
	ASSERT_THAT(IsTrue(
		Fixture.Balance(Fixture.Player) == FFixedPoint::FromInt(110)));
	ASSERT_THAT(IsTrue(
		Fixture.Balance(Fixture.OtherPlayer) == FFixedPoint::FromInt(50)));

	Squad = Fixture.World->GetComponent<FSeinSquadComponent>(Fixture.Squad);
	ASSERT_THAT(AreEqual(1, Squad->ReinforceQueue.Num()));
	ASSERT_THAT(AreEqual(SecondRequest,
		Squad->ReinforceQueue[0].RequestID));
}

TEST(SquadExactSlotMutationMaintainsMembershipAndInvalidatesSettledLayout,
	"SeinARTS.Unit.Squad.Reinforcement.Membership")
{
	FSquadReinforcementFixture Fixture;
	ASSERT_THAT(IsTrue(Fixture.Initialize()));

	FSeinEntityHandle Member;
	FSeinEntityHandle OtherSquad;
	int64 RequestID = 0;
	{
		auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
		Member = Fixture.World->SpawnAbstractEntity(
			FFixedTransform(), Fixture.Player);
		OtherSquad = Fixture.AddEmptySquad();
		FSeinCommandBrokerData* Broker =
			Fixture.World->GetComponentMutable<FSeinCommandBrokerData>(
				Fixture.Squad);
		Broker->SettledSlotPositions.Add(FFixedVector::ZeroVector);
		Broker->SettledSlotFacings.Add(FFixedQuaternion::Identity);
		Broker->bSettledSlotsMemberAligned = true;

		ASSERT_THAT(IsTrue(USeinSquadMutationBPFL::
			SeinQueueSquadReinforcement(
				Fixture.World, Fixture.Squad, 1, RequestID)));
		ASSERT_THAT(IsTrue(USeinSquadMutationBPFL::
			SeinFillSquadSlotByIndex(
				Fixture.World, Fixture.Squad, 1, Member)));
	}

	ASSERT_THAT(IsTrue(
		Fixture.Balance(Fixture.Player) == FFixedPoint::FromInt(100)));
	const FSeinSquadComponent* Squad =
		Fixture.World->GetComponent<FSeinSquadComponent>(Fixture.Squad);
	ASSERT_THAT(IsNotNull(Squad));
	ASSERT_THAT(AreEqual(0, Squad->ReinforceQueue.Num()));
	ASSERT_THAT(IsTrue(Squad->Slots[1].CurrentOccupant == Member));

	const FSeinSquadMemberComponent* MemberData =
		Fixture.World->GetComponent<FSeinSquadMemberComponent>(Member);
	ASSERT_THAT(IsNotNull(MemberData));
	ASSERT_THAT(IsTrue(MemberData->SquadEntity == Fixture.Squad));
	ASSERT_THAT(AreEqual(1, MemberData->SlotIndex));
	const FSeinBrokerMembershipData* Membership =
		Fixture.World->GetComponent<FSeinBrokerMembershipData>(Member);
	ASSERT_THAT(IsNotNull(Membership));
	ASSERT_THAT(IsTrue(
		Membership->CurrentBrokerHandle == Fixture.Squad));

	const FSeinCommandBrokerData* Broker =
		Fixture.World->GetComponent<FSeinCommandBrokerData>(Fixture.Squad);
	ASSERT_THAT(IsNotNull(Broker));
	ASSERT_THAT(IsTrue(Broker->Members.Contains(Member)));
	ASSERT_THAT(AreEqual(0, Broker->SettledSlotPositions.Num()));
	ASSERT_THAT(AreEqual(0, Broker->SettledSlotFacings.Num()));
	ASSERT_THAT(IsFalse(Broker->bSettledSlotsMemberAligned));

	{
		auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
		ASSERT_THAT(IsTrue(USeinSquadMutationBPFL::
			SeinFillSquadSlotByIndex(
				Fixture.World, Fixture.Squad, 0, Member)));
		ASSERT_THAT(IsFalse(USeinSquadMutationBPFL::
			SeinFillSquadSlotByIndex(
				Fixture.World, OtherSquad, 0, Member)));
	}

	Squad = Fixture.World->GetComponent<FSeinSquadComponent>(Fixture.Squad);
	ASSERT_THAT(IsTrue(Squad->Slots[0].CurrentOccupant == Member));
	ASSERT_THAT(IsFalse(Squad->Slots[1].CurrentOccupant.IsValid()));
	MemberData = Fixture.World->GetComponent<FSeinSquadMemberComponent>(Member);
	ASSERT_THAT(AreEqual(0, MemberData->SlotIndex));
	Membership = Fixture.World->GetComponent<FSeinBrokerMembershipData>(Member);
	ASSERT_THAT(IsTrue(
		Membership->CurrentBrokerHandle == Fixture.Squad));

	{
		auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
		FSeinSquadComponent UnsafeReplacement = *Squad;
		ASSERT_THAT(IsFalse(USeinSquadMutationBPFL::SeinSetSquadData(
			Fixture.World, Fixture.Squad, UnsafeReplacement)));
		ASSERT_THAT(IsTrue(USeinSquadMutationBPFL::SeinEmptySquadSlotByIndex(
			Fixture.World, Fixture.Squad, 0)));
	}
	Squad = Fixture.World->GetComponent<FSeinSquadComponent>(Fixture.Squad);
	ASSERT_THAT(IsFalse(Squad->Slots[0].CurrentOccupant.IsValid()));
	MemberData = Fixture.World->GetComponent<FSeinSquadMemberComponent>(Member);
	ASSERT_THAT(IsFalse(MemberData->SquadEntity.IsValid()));
	ASSERT_THAT(AreEqual(INDEX_NONE, MemberData->SlotIndex));
	Membership = Fixture.World->GetComponent<FSeinBrokerMembershipData>(Member);
	ASSERT_THAT(IsFalse(Membership->CurrentBrokerHandle.IsValid()));
	Broker = Fixture.World->GetComponent<FSeinCommandBrokerData>(Fixture.Squad);
	ASSERT_THAT(IsFalse(Broker->Members.Contains(Member)));
}

TEST(SquadReinforcementCompletesIntoExactSharedTagSlot,
	"SeinARTS.Sim.Squad.Reinforcement.Completion")
{
	FSquadReinforcementFixture Fixture;
	ASSERT_THAT(IsTrue(Fixture.Initialize(FFixedPoint::FromInt(1))));
	int64 RequestID = 0;
	{
		auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
		ASSERT_THAT(IsTrue(USeinSquadMutationBPFL::
			SeinQueueSquadReinforcement(
				Fixture.World, Fixture.Squad, 1, RequestID)));
		Fixture.World->GetComponentMutable<FSeinCommandBrokerData>(
			Fixture.Squad)->bSelfCullOnEmpty = true;
	}

	FTSTicker::GetCoreTicker().Tick(
		Fixture.World->GetFixedDeltaTimeSeconds());
	ASSERT_THAT(IsTrue(Fixture.World->IsEntityAlive(Fixture.Squad)));
	const FSeinCommandBrokerData* InitialBroker =
		Fixture.World->GetComponent<FSeinCommandBrokerData>(Fixture.Squad);
	ASSERT_THAT(IsNotNull(InitialBroker));
	ASSERT_THAT(IsFalse(InitialBroker->bSelfCullOnEmpty));
	ASSERT_THAT(AreEqual(1,
		Fixture.World->GetComponent<FSeinSquadComponent>(
			Fixture.Squad)->ReinforceQueue.Num()));

	for (int32 Tick = 0; Tick < 35; ++Tick)
	{
		FTSTicker::GetCoreTicker().Tick(
			Fixture.World->GetFixedDeltaTimeSeconds());
	}

	const FSeinSquadComponent* Squad =
		Fixture.World->GetComponent<FSeinSquadComponent>(Fixture.Squad);
	ASSERT_THAT(IsNotNull(Squad));
	ASSERT_THAT(AreEqual(0, Squad->ReinforceQueue.Num()));
	ASSERT_THAT(IsFalse(Squad->Slots[0].CurrentOccupant.IsValid()));
	ASSERT_THAT(IsTrue(Squad->Slots[1].CurrentOccupant.IsValid()));
	ASSERT_THAT(IsTrue(Squad->Slots[1].CurrentCooldown > FFixedPoint::Zero));

	const FSeinEntityHandle Member = Squad->Slots[1].CurrentOccupant;
	const FSeinSquadMemberComponent* MemberData =
		Fixture.World->GetComponent<FSeinSquadMemberComponent>(Member);
	ASSERT_THAT(IsNotNull(MemberData));
	ASSERT_THAT(IsTrue(MemberData->SquadEntity == Fixture.Squad));
	ASSERT_THAT(AreEqual(1, MemberData->SlotIndex));
	const FSeinCommandBrokerData* Broker =
		Fixture.World->GetComponent<FSeinCommandBrokerData>(Fixture.Squad);
	ASSERT_THAT(IsNotNull(Broker));
	ASSERT_THAT(IsTrue(Broker->Members.Contains(Member)));
	ASSERT_THAT(IsTrue(
		Fixture.Balance(Fixture.Player) == FFixedPoint::FromInt(80)));
}

TEST(SquadInvalidCompletionClampsAndRetriesExactRefund,
	"SeinARTS.Sim.Squad.Reinforcement.CompletionFailureRetry")
{
	FSquadReinforcementFixture Fixture;
	ASSERT_THAT(IsTrue(Fixture.Initialize(FFixedPoint::FromInt(1))));
	int64 RequestID = 0;
	{
		auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
		ASSERT_THAT(IsTrue(USeinSquadMutationBPFL::
			SeinQueueSquadReinforcement(
				Fixture.World, Fixture.Squad, 1, RequestID)));
		FSeinSquadComponent* Squad =
			Fixture.World->GetComponentMutable<FSeinSquadComponent>(
				Fixture.Squad);
		Squad->ReinforceQueue[0].BuildProgress =
			Squad->ReinforceQueue[0].TotalBuildTime
			- FFixedPoint(1);
		Squad->Slots[1].Entity = nullptr;
		Fixture.World->GetPlayerStateMutable(Fixture.Player)->SetResource(
			SeinARTSTags::Resource, FFixedPoint::MaxValue);
	}

	FTSTicker::GetCoreTicker().Tick(
		Fixture.World->GetFixedDeltaTimeSeconds());
	const FSeinSquadComponent* Squad =
		Fixture.World->GetComponent<FSeinSquadComponent>(Fixture.Squad);
	ASSERT_THAT(IsNotNull(Squad));
	ASSERT_THAT(AreEqual(1, Squad->ReinforceQueue.Num()));
	ASSERT_THAT(IsTrue(
		Squad->ReinforceQueue[0].BuildProgress
			== Squad->ReinforceQueue[0].TotalBuildTime));
	ASSERT_THAT(AreEqual(RequestID,
		Squad->ReinforceQueue[0].RequestID));
	ASSERT_THAT(IsTrue(
		Fixture.Balance(Fixture.Player) == FFixedPoint::MaxValue));

	{
		auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
		Fixture.World->GetPlayerStateMutable(Fixture.Player)->SetResource(
			SeinARTSTags::Resource, FFixedPoint::FromInt(80));
	}
	FTSTicker::GetCoreTicker().Tick(
		Fixture.World->GetFixedDeltaTimeSeconds());
	ASSERT_THAT(IsTrue(
		Fixture.Balance(Fixture.Player) == FFixedPoint::FromInt(100)));
	ASSERT_THAT(IsFalse(Fixture.World->IsEntityAlive(Fixture.Squad)));
}

TEST(SquadReinforcementSnapshotRestoresAndContinuesCanonically,
	"SeinARTS.Determinism.Squad.Reinforcement.SnapshotContinuation")
{
	// Wedge is registered before Ring, but Ring sorts first lexically. This
	// catches accidental process-local FName ordering in canonical slot identity.
	const FGameplayTag TestSquadSlotRoleWedge = SeinARTSTags::Formation_Wedge;
	const FGameplayTag TestSquadSlotRoleRing = SeinARTSTags::Formation_Ring;
	FSquadReinforcementFixture Source;
	ASSERT_THAT(IsTrue(Source.Initialize(FFixedPoint::FromInt(1))));
	int64 FirstRequest = 0;
	int64 SecondRequest = 0;
	{
		auto SimScope = FSeinSimContextTestAccess::Enter(*Source.World);
		FSeinSquadComponent* MutableSquad =
			Source.World->GetComponentMutable<FSeinSquadComponent>(
				Source.Squad);
		MutableSquad->Slots[0].SlotTags.Reset();
		MutableSquad->Slots[0].SlotTags.AddTag(TestSquadSlotRoleWedge);
		MutableSquad->Slots[0].SlotTags.AddTag(TestSquadSlotRoleRing);
		ASSERT_THAT(IsTrue(USeinSquadMutationBPFL::
			SeinQueueSquadReinforcement(
				Source.World, Source.Squad, 0, FirstRequest)));
		ASSERT_THAT(IsTrue(USeinSquadMutationBPFL::
			SeinQueueSquadReinforcement(
				Source.World, Source.Squad, 1, SecondRequest)));
	}
	for (int32 Tick = 0; Tick < 5; ++Tick)
	{
		FTSTicker::GetCoreTicker().Tick(
			Source.World->GetFixedDeltaTimeSeconds());
	}

	FSeinWorldSnapshot Snapshot;
	FSeinWorldSnapshotReferenceGuard SnapshotGuard(Snapshot);
	Source.World->CaptureSnapshot(Snapshot);
	ASSERT_THAT(AreEqual(
		FSeinWorldSnapshot::CurrentVersion,
		Snapshot.SnapshotVersion));
	TArray<uint8> EnvelopeBytes;
	FSeinSnapshotEnvelopeMetadata EnvelopeMetadata;
	FString Error;
	ASSERT_THAT(IsTrue(SeinSnapshotTransfer::EncodeCheckpointEnvelope(
		Snapshot, EnvelopeBytes, EnvelopeMetadata, Error)));
	FSeinWorldSnapshot Transferred;
	FSeinWorldSnapshotReferenceGuard TransferredGuard(Transferred);
	FSeinSnapshotEnvelopeMetadata TransferredMetadata;
	ASSERT_THAT(IsTrue(SeinSnapshotTransfer::DecodeCheckpointEnvelope(
		EnvelopeBytes, Transferred, TransferredMetadata, Error)));

	FActorTestSpawner DestinationSpawner;
	USeinWorldSubsystem* Destination = DestinationSpawner.GetWorld().
		GetSubsystem<USeinWorldSubsystem>();
	ASSERT_THAT(IsNotNull(Destination));
	ASSERT_THAT(IsTrue(SeinTestSnapshotRestore::RestoreTrusted(
		*Destination, Transferred, &Error)));

	const FSeinSquadComponent* Restored =
		Destination->GetComponent<FSeinSquadComponent>(Source.Squad);
	ASSERT_THAT(IsNotNull(Restored));
	ASSERT_THAT(AreEqual(2, Restored->ReinforceQueue.Num()));
	ASSERT_THAT(AreEqual(FirstRequest,
		Restored->ReinforceQueue[0].RequestID));
	ASSERT_THAT(AreEqual(SecondRequest,
		Restored->ReinforceQueue[1].RequestID));
	ASSERT_THAT(AreEqual(static_cast<int64>(3),
		Restored->NextReinforceRequestID));
	ASSERT_THAT(IsTrue(
		Restored->ReinforceQueue[0].ResourcePayer == Source.Player));
	ASSERT_THAT(IsTrue(
		Restored->ReinforceQueue[0].SlotTag == TestSquadSlotRoleRing));

	FGuid SourceRoot;
	FGuid DestinationRoot;
	ASSERT_THAT(IsTrue(ComputeReinforcementRoot(
		*Source.World, SourceRoot)));
	ASSERT_THAT(IsTrue(ComputeReinforcementRoot(
		*Destination, DestinationRoot)));
	ASSERT_THAT(IsTrue(SourceRoot == DestinationRoot));
	for (int32 Tick = 0; Tick < 65; ++Tick)
	{
		FTSTicker::GetCoreTicker().Tick(
			Source.World->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(IsTrue(ComputeReinforcementRoot(
			*Source.World, SourceRoot)));
		ASSERT_THAT(IsTrue(ComputeReinforcementRoot(
			*Destination, DestinationRoot)));
		ASSERT_THAT(IsTrue(SourceRoot == DestinationRoot));
	}

	const FSeinSquadComponent* SourceSquad =
		Source.World->GetComponent<FSeinSquadComponent>(Source.Squad);
	const FSeinSquadComponent* DestinationSquad =
		Destination->GetComponent<FSeinSquadComponent>(Source.Squad);
	ASSERT_THAT(IsNotNull(SourceSquad));
	ASSERT_THAT(IsNotNull(DestinationSquad));
	ASSERT_THAT(AreEqual(0, SourceSquad->ReinforceQueue.Num()));
	ASSERT_THAT(AreEqual(0, DestinationSquad->ReinforceQueue.Num()));
	ASSERT_THAT(IsTrue(
		SourceSquad->Slots[0].CurrentOccupant
			== DestinationSquad->Slots[0].CurrentOccupant));
	ASSERT_THAT(IsTrue(
		SourceSquad->Slots[1].CurrentOccupant
			== DestinationSquad->Slots[1].CurrentOccupant));

	FSeinWorldSnapshot InvalidSnapshot;
	FSeinWorldSnapshotReferenceGuard InvalidSnapshotGuard(InvalidSnapshot);
	{
		auto SimScope = FSeinSimContextTestAccess::Enter(*Source.World);
		Source.World->GetComponentMutable<FSeinSquadComponent>(
			Source.Squad)->NextReinforceRequestID = 0;
	}
	Source.World->CaptureSnapshot(InvalidSnapshot);
	FActorTestSpawner RejectedSpawner;
	USeinWorldSubsystem* Rejected = RejectedSpawner.GetWorld().
		GetSubsystem<USeinWorldSubsystem>();
	ASSERT_THAT(IsNotNull(Rejected));
	TestRunner->AddExpectedError(
		TEXT("authoritative sim state failed structural preflight"),
		EAutomationExpectedErrorFlags::Contains, 1, false);
	ASSERT_THAT(IsFalse(SeinTestSnapshotRestore::RestoreTrusted(
		*Rejected, InvalidSnapshot)));

	FSeinWorldSnapshot InvalidBrokerSnapshot;
	FSeinWorldSnapshotReferenceGuard InvalidBrokerSnapshotGuard(
		InvalidBrokerSnapshot);
	{
		auto SimScope = FSeinSimContextTestAccess::Enter(*Source.World);
		Source.World->GetComponentMutable<FSeinSquadComponent>(
			Source.Squad)->NextReinforceRequestID = 3;
		Source.World->GetComponentMutable<FSeinCommandBrokerData>(
			Source.Squad)->bSelfCullOnEmpty = true;
	}
	Source.World->CaptureSnapshot(InvalidBrokerSnapshot);
	FActorTestSpawner RejectedBrokerSpawner;
	USeinWorldSubsystem* RejectedBroker = RejectedBrokerSpawner.GetWorld().
		GetSubsystem<USeinWorldSubsystem>();
	ASSERT_THAT(IsNotNull(RejectedBroker));
	TestRunner->AddExpectedError(
		TEXT("authoritative sim state failed structural preflight"),
		EAutomationExpectedErrorFlags::Contains, 1, false);
	ASSERT_THAT(IsFalse(SeinTestSnapshotRestore::RestoreTrusted(
		*RejectedBroker, InvalidBrokerSnapshot)));

	Source.World->StopSimulation();
	Destination->StopSimulation();
}

TEST(SquadDestructionSettlesQueuedChargesPerAuthoredPolicy,
	"SeinARTS.Unit.Squad.Reinforcement.DestructionSettlement")
{
	// RJ's policy ruling (2026-08-16): destruction settlement is a per-squad
	// authored toggle — full refund, forfeit, or partial refund with a
	// tunable fraction. Two queued entries cost 10 + 20 → balance 70 of 100
	// before the squad dies.
	struct FPolicyCase
	{
		ESeinSquadReinforceRefundPolicy Policy;
		int32 PartialPercentHundredths;
		int32 ExpectedBalance;
	};
	const FPolicyCase Cases[] = {
		{ESeinSquadReinforceRefundPolicy::Refund, 0, 100},
		{ESeinSquadReinforceRefundPolicy::Forfeit, 0, 70},
		{ESeinSquadReinforceRefundPolicy::PartialRefund, 50, 85},
	};

	for (const FPolicyCase& Case : Cases)
	{
		FSquadReinforcementFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize()));

		int64 FirstRequest = 0;
		int64 SecondRequest = 0;
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
			ASSERT_THAT(IsTrue(USeinSquadMutationBPFL::
				SeinQueueSquadReinforcement(
					Fixture.World, Fixture.Squad, 0, FirstRequest)));
			ASSERT_THAT(IsTrue(USeinSquadMutationBPFL::
				SeinQueueSquadReinforcement(
					Fixture.World, Fixture.Squad, 1, SecondRequest)));
			FSeinSquadComponent* Mutable =
				Fixture.World->GetComponentMutable<FSeinSquadComponent>(
					Fixture.Squad);
			ASSERT_THAT(IsNotNull(Mutable));
			Mutable->ReinforceRefundPolicy = Case.Policy;
			Mutable->PartialRefundPercent =
				FFixedPoint::FromInt(Case.PartialPercentHundredths)
					/ FFixedPoint::FromInt(100);
		}
		ASSERT_THAT(IsTrue(
			Fixture.Balance(Fixture.Player) == FFixedPoint::FromInt(70)));

		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
			Fixture.World->DestroyEntity(Fixture.Squad);
		}
		FTSTicker::GetCoreTicker().Tick(
			Fixture.World->GetFixedDeltaTimeSeconds());

		ASSERT_THAT(IsTrue(
			Fixture.Balance(Fixture.Player)
				== FFixedPoint::FromInt(Case.ExpectedBalance)));
		Fixture.World->StopSimulation();
	}
}
