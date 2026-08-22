#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Brokers/SeinBrokerTypes.h"
#include "Brokers/SeinDefaultCommandBrokerResolver.h"
#include "Containers/Ticker.h"
#include "Components/SeinBrokerMembershipData.h"
#include "Components/SeinCommandBrokerData.h"
#include "Data/SeinWorldSnapshot.h"
#include "Events/SeinVisualEvent.h"
#include "Formations/SeinFormation.h"
#include "Input/SeinCommand.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSimContext.h"
#include "Simulation/SeinTestSnapshotRestore.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Settings/PluginSettings.h"
#include "Tags/SeinARTSGameplayTags.h"

struct FSeinWorldSubsystemTestAccess
{
	static bool HandleBrokerOrder(
		USeinWorldSubsystem& World,
		const FSeinCommand& Command)
	{
		int32 CohesionOrderSequence = 0;
		return World.TryHandleBrokerOrderCommand(
			Command, CohesionOrderSequence)
			== USeinWorldSubsystem::ECommandHandleResult::Handled;
	}
};

namespace UE::SeinARTSTests
{
	namespace FrozenDestinationTestLocal
	{
		struct FScopedBrokerPolicy
		{
			FScopedBrokerPolicy()
				: Settings(GetMutableDefault<USeinARTSCoreSettings>())
				, SavedResolver(Settings->DefaultBrokerResolverClass)
			{
				Settings->DefaultBrokerResolverClass = FSoftClassPath(
					USeinDefaultCommandBrokerResolver::StaticClass());
			}

			~FScopedBrokerPolicy()
			{
				Settings->DefaultBrokerResolverClass = SavedResolver;
			}

			USeinARTSCoreSettings* Settings;
			TSoftClassPtr<USeinCommandBrokerResolver> SavedResolver;
		};

		FSeinFrozenDestination MakeDestination(
			USeinWorldSubsystem& World,
			FSeinEntityHandle Member,
			int32 X)
		{
			FSeinFrozenDestination Destination;
			Destination.Member = Member;
			Destination.WorldPosition = FFixedVector(
				FFixedPoint::FromInt(X),
				FFixedPoint::Zero,
				FFixedPoint::Zero);
			Destination.FootprintRadius =
				USeinFormation::GetFootprintRadius(&World, Member);
			Destination.bReserveFootprint = true;
			Destination.SourceEntity = Member;
			Destination.SourceIndex = 0;
			return Destination;
		}

		FSeinCommand MakeOrder(
			FSeinPlayerID Player,
			FSeinEntityHandle Member,
			const FSeinFrozenDestination& Destination,
			bool bQueue)
		{
			FSeinBrokerOrderPayload Payload;
			Payload.CommandContext.AddTag(
				SeinARTSTags::Command_Context_RightClick);
			Payload.CommandContext.AddTag(
				SeinARTSTags::Command_Context_Target_Ground);
			FSeinBrokerRecipientPlanSegment& Segment =
				Payload.RecipientPlan.AddDefaulted_GetRef();
			Segment.Recipient = Member;
			Segment.MemberCount = 1;
			Payload.DestinationArtifact.Add(Destination);

			FSeinCommand Command;
			Command.PlayerID = Player;
			Command.IssuerKind = ESeinCommandIssuerKind::Player;
			Command.CommandType =
				SeinARTSTags::Command_Type_BrokerOrder;
			Command.SchemaVersion =
				SeinBrokerOrderProtocol::SchemaVersion;
			Command.TargetLocation = Destination.WorldPosition;
			Command.EntityList.Add(Member);
			Command.bQueueCommand = bQueue;
			Command.Payload = FInstancedStruct::Make(Payload);
			return Command;
		}
	}

	TEST(FrozenDestinationAdmissionKeepsShownDestinationsAndReleases,
		"SeinARTS.Unit.CoreEntity.FrozenDestination")
	{
		using namespace FrozenDestinationTestLocal;
		FScopedBrokerPolicy Policy;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		// Under the All profile the Cover extension registers destination
		// providers into every world; this test asserts BROKER-owned frozen
		// authority in isolation, so detach extension providers before the
		// bootstrap freezes the (then empty) provider binding frame. Clearing
		// after the freeze would instead trip the per-tick binding validation.
		World->ClearAuthoritativeDestinationProvidersForTests();

		const FSeinPlayerID Player(1);
		FSeinEntityHandle FirstMember;
		FSeinEntityHandle SecondMember;
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));
			FirstMember = World->SpawnAbstractEntity(
				FFixedTransform(), Player);
			SecondMember = World->SpawnAbstractEntity(
				FFixedTransform(), Player);
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World,
			AuthorState,
			FSeinMatchSettings(),
			0x46524F5A,
			TEXT("SeinARTS.FrozenDestination.Admission"))));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));

		const FSeinFrozenDestination FirstDestination =
			MakeDestination(*World, FirstMember, 1000);
		const FSeinCommand FirstOrder = MakeOrder(
			Player, FirstMember, FirstDestination, false);
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			ASSERT_THAT(IsTrue(
				FSeinWorldSubsystemTestAccess::HandleBrokerOrder(
					*World, FirstOrder)));
		}

		const FSeinBrokerMembershipData* FirstMembership =
			World->GetComponent<FSeinBrokerMembershipData>(FirstMember);
		ASSERT_THAT(IsNotNull(FirstMembership));
		FSeinEntityHandle FirstBroker =
			FirstMembership->CurrentBrokerHandle;
		const FSeinCommandBrokerData* FirstBrokerData =
			World->GetComponent<FSeinCommandBrokerData>(FirstBroker);
		ASSERT_THAT(IsNotNull(FirstBrokerData));
		ASSERT_THAT(AreEqual(1, FirstBrokerData->OrderQueue.Num()));
		ASSERT_THAT(IsTrue(World->IsDestinationFootprintReserved(
			FirstDestination.WorldPosition,
			FirstDestination.FootprintRadius)));
		ASSERT_THAT(IsTrue(
			World->HasAuthoritativeDestinationProviders()));
		FSeinAuthoritativeDestinationQuery AuthorityQuery;
		AuthorityQuery.Requester = FirstMember;
		AuthorityQuery.WorldPosition = FirstDestination.WorldPosition;
		ASSERT_THAT(IsTrue(
			World->IsAuthoritativeDestination(AuthorityQuery)));
		World->FlushVisualEvents();

		// POLICY: a contending order targeting an already-reserved spot ADMITS
		// with the destination the player was shown. The ledger keeps both
		// claims; the conflict resolves physically at arrival, never by
		// rejecting or silently re-planning the order.
		FSeinFrozenDestination ContendingDestination =
			MakeDestination(*World, SecondMember, 1000);
		const FSeinCommand ContendingOrder = MakeOrder(
			Player, SecondMember, ContendingDestination, false);
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			ASSERT_THAT(IsTrue(
				FSeinWorldSubsystemTestAccess::HandleBrokerOrder(
					*World, ContendingOrder)));
		}
		const FSeinBrokerMembershipData* SecondMembership =
			World->GetComponent<FSeinBrokerMembershipData>(SecondMember);
		ASSERT_THAT(IsNotNull(SecondMembership));
		const FSeinEntityHandle SecondBroker =
			SecondMembership->CurrentBrokerHandle;
		const FSeinCommandBrokerData* SecondBrokerData =
			World->GetComponent<FSeinCommandBrokerData>(SecondBroker);
		ASSERT_THAT(IsNotNull(SecondBrokerData));
		ASSERT_THAT(AreEqual(1, SecondBrokerData->OrderQueue.Num()));
		ASSERT_THAT(IsTrue(
			SecondBrokerData->OrderQueue[0].DestinationArtifact[0]
				.WorldPosition == ContendingDestination.WorldPosition));
		TArray<FSeinVisualEvent> Rejections = World->FlushVisualEvents();
		ASSERT_THAT(IsFalse(Rejections.ContainsByPredicate(
			[](const FSeinVisualEvent& Event)
			{
				return Event.ReasonTag
					== SeinARTSTags::Command_Reject_DestinationReserved;
			})));

		// A queued order that conflicts with the issuer's own prior entry also
		// admits: the player saw both destinations and gets both.
		const FSeinCommand QueuedSelfConflict = MakeOrder(
			Player, FirstMember, FirstDestination, true);
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			ASSERT_THAT(IsTrue(
				FSeinWorldSubsystemTestAccess::HandleBrokerOrder(
					*World, QueuedSelfConflict)));
		}
		const FSeinBrokerMembershipData* ReplacementMembership =
			World->GetComponent<FSeinBrokerMembershipData>(FirstMember);
		ASSERT_THAT(IsNotNull(ReplacementMembership));
		FirstBroker = ReplacementMembership->CurrentBrokerHandle;
		FirstBrokerData =
			World->GetComponent<FSeinCommandBrokerData>(FirstBroker);
		ASSERT_THAT(IsNotNull(FirstBrokerData));
		ASSERT_THAT(AreEqual(2, FirstBrokerData->OrderQueue.Num()));
		Rejections = World->FlushVisualEvents();
		ASSERT_THAT(IsFalse(Rejections.ContainsByPredicate(
			[](const FSeinVisualEvent& Event)
			{
				return Event.ReasonTag
					== SeinARTSTags::Command_Reject_DestinationReserved;
			})));

		FSeinFrozenDestination ReplacementDestination =
			MakeDestination(*World, FirstMember, 1400);
		const FSeinCommand ReplacementOrder = MakeOrder(
			Player, FirstMember, ReplacementDestination, false);
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			ASSERT_THAT(IsTrue(
				FSeinWorldSubsystemTestAccess::HandleBrokerOrder(
					*World, ReplacementOrder)));
		}
		Rejections = World->FlushVisualEvents();
		ASSERT_THAT(IsFalse(Rejections.ContainsByPredicate(
			[](const FSeinVisualEvent& Event)
			{
				return Event.ReasonTag
					== SeinARTSTags::Command_Reject_DestinationReserved;
			})));
		ReplacementMembership =
			World->GetComponent<FSeinBrokerMembershipData>(FirstMember);
		ASSERT_THAT(IsNotNull(ReplacementMembership));
		FirstBroker = ReplacementMembership->CurrentBrokerHandle;
		FirstBrokerData =
			World->GetComponent<FSeinCommandBrokerData>(FirstBroker);
		ASSERT_THAT(IsNotNull(FirstBrokerData));
		ASSERT_THAT(AreEqual(1, FirstBrokerData->OrderQueue.Num()));
		ASSERT_THAT(IsTrue(
			FirstBrokerData->OrderQueue[0].DestinationArtifact[0]
				.WorldPosition == ReplacementDestination.WorldPosition));
		// The second member's admitted claim on the original spot survives the
		// first member's replacement — release is per-order, never global.
		ASSERT_THAT(IsTrue(World->IsDestinationFootprintReserved(
			FirstDestination.WorldPosition,
			FirstDestination.FootprintRadius)));
		AuthorityQuery.WorldPosition = ReplacementDestination.WorldPosition;
		ASSERT_THAT(IsTrue(
			World->IsAuthoritativeDestination(AuthorityQuery)));

		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			FSeinCommandBrokerData* MutableBroker =
				World->GetComponentMutable<FSeinCommandBrokerData>(FirstBroker);
			ASSERT_THAT(IsNotNull(MutableBroker));
			MutableBroker->OrderQueue.Reset();
		}
		ASSERT_THAT(IsFalse(World->IsDestinationFootprintReserved(
			ReplacementDestination.WorldPosition,
			ReplacementDestination.FootprintRadius)));
		ASSERT_THAT(IsFalse(
			World->IsAuthoritativeDestination(AuthorityQuery)));
		ASSERT_THAT(IsTrue(World->IsDestinationFootprintReserved(
			ContendingDestination.WorldPosition,
			ContendingDestination.FootprintRadius)));
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			FSeinCommandBrokerData* MutableBroker =
				World->GetComponentMutable<FSeinCommandBrokerData>(
					SecondBroker);
			ASSERT_THAT(IsNotNull(MutableBroker));
			MutableBroker->OrderQueue.Reset();
		}
		ASSERT_THAT(IsFalse(World->IsDestinationFootprintReserved(
			ContendingDestination.WorldPosition,
			ContendingDestination.FootprintRadius)));
		ASSERT_THAT(IsFalse(
			World->HasAuthoritativeDestinationProviders()));
		World->StopSimulation();
	}

	TEST(BrokerOrderRejectsOwnedBrokerWithForeignLiveMember,
		"SeinARTS.Unit.Authority.BrokerOrder")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		const FSeinPlayerID OrderingPlayer(1);
		const FSeinPlayerID ForeignPlayer(2);
		FSeinEntityHandle Broker;
		FSeinEntityHandle ForeignMember;
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(OrderingPlayer, FSeinFactionID(1));
			World->RegisterPlayer(ForeignPlayer, FSeinFactionID(2));
			Broker = World->SpawnAbstractEntity(
				FFixedTransform(), OrderingPlayer);
			ForeignMember = World->SpawnAbstractEntity(
				FFixedTransform(), ForeignPlayer);
			FSeinCommandBrokerData BrokerData;
			BrokerData.Members = {ForeignMember};
			World->AddComponent(Broker, BrokerData);
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World, AuthorState, FSeinMatchSettings(), 0x42524155,
			TEXT("SeinARTS.BrokerOrder.ForeignMember"))));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));

		FSeinBrokerOrderPayload Payload;
		Payload.CommandContext.AddTag(
			SeinARTSTags::Command_Context_RightClick);
		Payload.CommandContext.AddTag(
			SeinARTSTags::Command_Context_Target_Ground);
		FSeinCommand Command;
		Command.PlayerID = OrderingPlayer;
		Command.IssuerKind = ESeinCommandIssuerKind::Player;
		Command.CommandType = SeinARTSTags::Command_Type_BrokerOrder;
		Command.SchemaVersion = SeinBrokerOrderProtocol::SchemaVersion;
		Command.EntityList = {Broker};
		Command.Payload = FInstancedStruct::Make(Payload);
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			ASSERT_THAT(IsTrue(FSeinWorldSubsystemTestAccess::HandleBrokerOrder(
				*World, Command)));
		}
		const FSeinCommandBrokerData* BrokerData =
			World->GetComponent<FSeinCommandBrokerData>(Broker);
		ASSERT_THAT(IsNotNull(BrokerData));
		ASSERT_THAT(AreEqual(0, BrokerData->OrderQueue.Num()));
		World->StopSimulation();
	}

	TEST(BrokerOrderRejectsBrokerMemberOverlapForReplaceAndQueue,
		"SeinARTS.Unit.Authority.BrokerOrder")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		const FSeinPlayerID Player(1);
		FSeinEntityHandle Broker;
		FSeinEntityHandle Member;
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));
			Broker = World->SpawnAbstractEntity(FFixedTransform(), Player);
			Member = World->SpawnAbstractEntity(FFixedTransform(), Player);
			FSeinCommandBrokerData BrokerData;
			BrokerData.Members = {Member};
			World->AddComponent(Broker, BrokerData);
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World, AuthorState, FSeinMatchSettings(), 0x42524F56,
			TEXT("SeinARTS.BrokerOrder.RecipientOverlap"))));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));

		FSeinBrokerOrderPayload Payload;
		Payload.CommandContext.AddTag(
			SeinARTSTags::Command_Context_RightClick);
		Payload.CommandContext.AddTag(
			SeinARTSTags::Command_Context_Target_Ground);
		FSeinCommand Command;
		Command.PlayerID = Player;
		Command.CommandType = SeinARTSTags::Command_Type_BrokerOrder;
		Command.SchemaVersion = SeinBrokerOrderProtocol::SchemaVersion;
		Command.EntityList = {Broker, Member};
		Command.Payload = FInstancedStruct::Make(Payload);
		for (const bool bQueue : {false, true})
		{
			Command.bQueueCommand = bQueue;
			ASSERT_THAT(IsTrue(World->SubmitLocalCommandDraft(Command)));
			FTSTicker::GetCoreTicker().Tick(
				World->GetFixedDeltaTimeSeconds());
			const FSeinCommandBrokerData* BrokerData =
				World->GetComponent<FSeinCommandBrokerData>(Broker);
			ASSERT_THAT(IsNotNull(BrokerData));
			ASSERT_THAT(AreEqual(0, BrokerData->OrderQueue.Num()));
			ASSERT_THAT(IsNull(
				World->GetComponent<FSeinBrokerMembershipData>(Member)));
			const TArray<FSeinVisualEvent> Events = World->FlushVisualEvents();
			ASSERT_THAT(IsTrue(Events.ContainsByPredicate(
				[](const FSeinVisualEvent& Event)
				{
					return Event.ReasonTag
						== SeinARTSTags::Command_Reject_InvalidTarget;
				})));
		}
		World->StopSimulation();
	}

	TEST(ExactBrokerOrderRejectsRecipientTransferAfterBuffering,
		"SeinARTS.Unit.Authority.BrokerOrder")
	{
		using namespace FrozenDestinationTestLocal;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		const FSeinPlayerID Player(1);
		FSeinEntityHandle FirstBroker;
		FSeinEntityHandle SecondBroker;
		TArray<FSeinEntityHandle> Members;
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));
			for (int32 Index = 0; Index < 3; ++Index)
			{
				Members.Add(World->SpawnAbstractEntity(
					FFixedTransform(), Player));
			}
			FirstBroker = World->SpawnAbstractEntity(
				FFixedTransform(), Player);
			SecondBroker = World->SpawnAbstractEntity(
				FFixedTransform(), Player);
			FSeinCommandBrokerData FirstData;
			FirstData.Members = {Members[0], Members[1]};
			World->AddComponent(FirstBroker, FirstData);
			FSeinCommandBrokerData SecondData;
			SecondData.Members = {Members[2]};
			World->AddComponent(SecondBroker, SecondData);
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World, AuthorState, FSeinMatchSettings(), 0x42525254,
			TEXT("SeinARTS.BrokerOrder.DelayedRosterTransfer"))));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));

		FSeinBrokerOrderPayload Payload;
		Payload.CommandContext.AddTag(
			SeinARTSTags::Command_Context_RightClick);
		Payload.CommandContext.AddTag(
			SeinARTSTags::Command_Context_Target_Ground);
		for (const TPair<FSeinEntityHandle, int32>& Pair : {
			TPair<FSeinEntityHandle, int32>(FirstBroker, 2),
			TPair<FSeinEntityHandle, int32>(SecondBroker, 1)})
		{
			FSeinBrokerRecipientPlanSegment& Segment =
				Payload.RecipientPlan.AddDefaulted_GetRef();
			Segment.Recipient = Pair.Key;
			Segment.MemberCount = Pair.Value;
		}
		for (int32 Index = 0; Index < Members.Num(); ++Index)
		{
			Payload.DestinationArtifact.Add(
				MakeDestination(*World, Members[Index], 1000 + Index * 200));
		}
		FSeinCommand Command;
		Command.PlayerID = Player;
		Command.CommandType = SeinARTSTags::Command_Type_BrokerOrder;
		Command.SchemaVersion = SeinBrokerOrderProtocol::SchemaVersion;
		Command.TargetLocation = Payload.DestinationArtifact[0].WorldPosition;
		Command.EntityList = {FirstBroker, SecondBroker};
		Command.Payload = FInstancedStruct::Make(Payload);
		ASSERT_THAT(IsTrue(World->SubmitLocalCommandDraft(Command)));

		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			World->GetComponentMutable<FSeinCommandBrokerData>(FirstBroker)
				->Members = {Members[0]};
			World->GetComponentMutable<FSeinCommandBrokerData>(SecondBroker)
				->Members = {Members[1], Members[2]};
		}
		FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(AreEqual(0,
			World->GetComponent<FSeinCommandBrokerData>(FirstBroker)
				->OrderQueue.Num()));
		ASSERT_THAT(AreEqual(0,
			World->GetComponent<FSeinCommandBrokerData>(SecondBroker)
				->OrderQueue.Num()));
		const TArray<FSeinVisualEvent> Events = World->FlushVisualEvents();
		ASSERT_THAT(IsTrue(Events.ContainsByPredicate(
			[](const FSeinVisualEvent& Event)
			{
				return Event.ReasonTag
					== SeinARTSTags::Command_Reject_InvalidTarget;
			})));
		World->StopSimulation();
	}

	TEST(FrozenDestinationSnapshotRestorePreservesAuthorityAndContinuation,
		"SeinARTS.Unit.CoreEntity.FrozenDestination")
	{
		using namespace FrozenDestinationTestLocal;
		FScopedBrokerPolicy Policy;
		FActorTestSpawner SourceSpawner;
		USeinWorldSubsystem* Source =
			SourceSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Source));

		const FSeinPlayerID Player(1);
		FSeinEntityHandle Member;
		const auto AuthorState = [&]()
		{
			Source->RegisterPlayer(Player, FSeinFactionID(1));
			Member = Source->SpawnAbstractEntity(
				FFixedTransform(), Player);
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*Source,
			AuthorState,
			FSeinMatchSettings(),
			0x46524F5B,
			TEXT("SeinARTS.FrozenDestination.Snapshot"))));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*Source)));

		const FSeinFrozenDestination Destination =
			MakeDestination(*Source, Member, 1000);
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Source);
			ASSERT_THAT(IsTrue(
				FSeinWorldSubsystemTestAccess::HandleBrokerOrder(
					*Source,
					MakeOrder(Player, Member, Destination, false))));
		}
		const FSeinBrokerMembershipData* SourceMembership =
			Source->GetComponent<FSeinBrokerMembershipData>(Member);
		ASSERT_THAT(IsNotNull(SourceMembership));
		const FSeinEntityHandle Broker =
			SourceMembership->CurrentBrokerHandle;
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Source);
			ASSERT_THAT(IsTrue(Source->ConfirmFrozenDestinationArrival(
				Member, Destination.WorldPosition)));
			FSeinCommandBrokerData* BrokerData =
				Source->GetComponentMutable<FSeinCommandBrokerData>(Broker);
			ASSERT_THAT(IsNotNull(BrokerData));
			BrokerData->OrderQueue.Reset();
		}

		FSeinWorldSnapshot Snapshot;
		FSeinWorldSnapshotReferenceGuard SnapshotGuard(Snapshot);
		Source->CaptureSnapshot(Snapshot);

		FActorTestSpawner RestoredSpawner;
		USeinWorldSubsystem* Restored =
			RestoredSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Restored));
		FString Error;
		ASSERT_THAT(IsTrue(SeinTestSnapshotRestore::RestoreTrusted(
			*Restored, Snapshot, &Error)));
		ASSERT_THAT(IsTrue(Restored->IsDestinationFootprintReserved(
			Destination.WorldPosition,
			Destination.FootprintRadius)));
		FSeinAuthoritativeDestinationQuery AuthorityQuery;
		AuthorityQuery.Requester = Member;
		AuthorityQuery.WorldPosition = Destination.WorldPosition;
		ASSERT_THAT(IsTrue(
			Restored->IsAuthoritativeDestination(AuthorityQuery)));

		FGuid SourceRoot;
		FGuid RestoredRoot;
		ASSERT_THAT(IsTrue(
			Source->ComputeCanonicalStateRoot(SourceRoot, Error)));
		ASSERT_THAT(IsTrue(
			Restored->ComputeCanonicalStateRoot(RestoredRoot, Error)));
		ASSERT_THAT(IsTrue(SourceRoot == RestoredRoot));

		for (USeinWorldSubsystem* World : {Source, Restored})
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			FSeinCommandBrokerData* BrokerData =
				World->GetComponentMutable<FSeinCommandBrokerData>(Broker);
			ASSERT_THAT(IsNotNull(BrokerData));
			BrokerData->OrderQueue.Reset();
			BrokerData->SettledDestinationArtifact.Reset();
		}
		ASSERT_THAT(IsFalse(Source->IsDestinationFootprintReserved(
			Destination.WorldPosition,
			Destination.FootprintRadius)));
		ASSERT_THAT(IsFalse(Restored->IsDestinationFootprintReserved(
			Destination.WorldPosition,
			Destination.FootprintRadius)));
		ASSERT_THAT(IsTrue(
			Source->ComputeCanonicalStateRoot(SourceRoot, Error)));
		ASSERT_THAT(IsTrue(
			Restored->ComputeCanonicalStateRoot(RestoredRoot, Error)));
		ASSERT_THAT(IsTrue(SourceRoot == RestoredRoot));

		Source->StopSimulation();
		Restored->StopSimulation();
	}

	TEST(DeadExplicitSubsetDropsOrderWithoutRetargetingBroker,
		"SeinARTS.Unit.CoreEntity.FrozenDestination")
	{
		using namespace FrozenDestinationTestLocal;
		FScopedBrokerPolicy Policy;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FSeinPlayerID Player(1);
		FSeinEntityHandle FirstMember;
		FSeinEntityHandle SecondMember;
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));
			FirstMember = World->SpawnAbstractEntity(
				FFixedTransform(), Player);
			SecondMember = World->SpawnAbstractEntity(
				FFixedTransform(), Player);
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World,
			AuthorState,
			FSeinMatchSettings(),
			0x46524F5C,
			TEXT("SeinARTS.FrozenDestination.DeadSubset"))));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));

		FSeinBrokerQueuedOrder Order;
		Order.TargetMembers.Add(FirstMember);
		FSeinEntityHandle Broker;
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			Broker = World->CreateBrokerForMembers(
				{FirstMember, SecondMember}, Player, Order);
			World->DestroyEntity(FirstMember);
		}
		ASSERT_THAT(IsTrue(Broker.IsValid()));
		FTSTicker::GetCoreTicker().Tick(
			World->GetFixedDeltaTimeSeconds());

		const FSeinCommandBrokerData* BrokerData =
			World->GetComponent<FSeinCommandBrokerData>(Broker);
		ASSERT_THAT(IsNotNull(BrokerData));
		ASSERT_THAT(AreEqual(1, BrokerData->Members.Num()));
		ASSERT_THAT(IsTrue(BrokerData->Members[0] == SecondMember));
		ASSERT_THAT(IsTrue(BrokerData->OrderQueue.IsEmpty()));
		World->StopSimulation();
	}

	TEST(DeadMemberDropsOnlyItsSlotFromAdmittedArtifact,
		"SeinARTS.Unit.CoreEntity.FrozenDestination")
	{
		using namespace FrozenDestinationTestLocal;
		FScopedBrokerPolicy Policy;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FSeinPlayerID Player(1);
		FSeinEntityHandle FirstMember;
		FSeinEntityHandle SecondMember;
		FSeinEntityHandle ThirdMember;
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));
			FirstMember = World->SpawnAbstractEntity(
				FFixedTransform(), Player);
			SecondMember = World->SpawnAbstractEntity(
				FFixedTransform(), Player);
			ThirdMember = World->SpawnAbstractEntity(
				FFixedTransform(), Player);
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World,
			AuthorState,
			FSeinMatchSettings(),
			0x46524F5F,
			TEXT("SeinARTS.FrozenDestination.DeadSlot"))));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));

		// The player was shown three slots; one member dies before the order
		// admits. POLICY: drop only the dead member's slot — the survivors
		// keep exactly the destinations the preview showed.
		const FSeinFrozenDestination FirstSlot =
			MakeDestination(*World, FirstMember, 1000);
		const FSeinFrozenDestination SecondSlot =
			MakeDestination(*World, SecondMember, 1200);
		const FSeinFrozenDestination ThirdSlot =
			MakeDestination(*World, ThirdMember, 1400);

		FSeinBrokerOrderPayload Payload;
		Payload.CommandContext.AddTag(
			SeinARTSTags::Command_Context_RightClick);
		Payload.CommandContext.AddTag(
			SeinARTSTags::Command_Context_Target_Ground);
		for (const FSeinEntityHandle Member : {
			FirstMember, SecondMember, ThirdMember})
		{
			FSeinBrokerRecipientPlanSegment& Segment =
				Payload.RecipientPlan.AddDefaulted_GetRef();
			Segment.Recipient = Member;
			Segment.MemberCount = 1;
		}
		Payload.DestinationArtifact.Add(FirstSlot);
		Payload.DestinationArtifact.Add(SecondSlot);
		Payload.DestinationArtifact.Add(ThirdSlot);
		FSeinCommand Command;
		Command.PlayerID = Player;
		Command.IssuerKind = ESeinCommandIssuerKind::Player;
		Command.CommandType = SeinARTSTags::Command_Type_BrokerOrder;
		Command.SchemaVersion = SeinBrokerOrderProtocol::SchemaVersion;
		Command.TargetLocation = FirstSlot.WorldPosition;
		Command.EntityList.Add(FirstMember);
		Command.EntityList.Add(SecondMember);
		Command.EntityList.Add(ThirdMember);
		Command.bQueueCommand = false;
		Command.Payload = FInstancedStruct::Make(Payload);

		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			World->DestroyEntity(SecondMember);
			ASSERT_THAT(IsTrue(
				FSeinWorldSubsystemTestAccess::HandleBrokerOrder(
					*World, Command)));
		}

		const TArray<FSeinVisualEvent> Rejections =
			World->FlushVisualEvents();
		ASSERT_THAT(IsFalse(Rejections.ContainsByPredicate(
			[](const FSeinVisualEvent& Event)
			{
				return Event.ReasonTag
						== SeinARTSTags::Command_Reject_DestinationReserved
					|| Event.ReasonTag
						== SeinARTSTags::Command_Reject_InvalidTarget;
			})));
		const FSeinBrokerMembershipData* Membership =
			World->GetComponent<FSeinBrokerMembershipData>(FirstMember);
		ASSERT_THAT(IsNotNull(Membership));
		const FSeinCommandBrokerData* BrokerData =
			World->GetComponent<FSeinCommandBrokerData>(
				Membership->CurrentBrokerHandle);
		ASSERT_THAT(IsNotNull(BrokerData));
		ASSERT_THAT(AreEqual(1, BrokerData->OrderQueue.Num()));
		const TArray<FSeinFrozenDestination>& Admitted =
			BrokerData->OrderQueue[0].DestinationArtifact;
		ASSERT_THAT(AreEqual(2, Admitted.Num()));
		ASSERT_THAT(IsTrue(Admitted[0].Member == FirstMember));
		ASSERT_THAT(IsTrue(
			Admitted[0].WorldPosition == FirstSlot.WorldPosition));
		ASSERT_THAT(IsTrue(Admitted[1].Member == ThirdMember));
		ASSERT_THAT(IsTrue(
			Admitted[1].WorldPosition == ThirdSlot.WorldPosition));
		ASSERT_THAT(IsTrue(World->IsDestinationFootprintReserved(
			FirstSlot.WorldPosition, FirstSlot.FootprintRadius)));
		ASSERT_THAT(IsTrue(World->IsDestinationFootprintReserved(
			ThirdSlot.WorldPosition, ThirdSlot.FootprintRadius)));
		ASSERT_THAT(IsFalse(World->IsDestinationFootprintReserved(
			SecondSlot.WorldPosition, SecondSlot.FootprintRadius)));
		World->StopSimulation();
	}

	TEST(ReorderedReselectionQueuedArtifactSurvivesRestore,
		"SeinARTS.Unit.CoreEntity.FrozenDestination")
	{
		using namespace FrozenDestinationTestLocal;
		FScopedBrokerPolicy Policy;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FSeinPlayerID Player(1);
		FSeinEntityHandle FirstMember;
		FSeinEntityHandle SecondMember;
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));
			FirstMember = World->SpawnAbstractEntity(
				FFixedTransform(), Player);
			SecondMember = World->SpawnAbstractEntity(
				FFixedTransform(), Player);
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World,
			AuthorState,
			FSeinMatchSettings(),
			0x46524F60,
			TEXT("SeinARTS.FrozenDestination.Reorder"))));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));

		const auto MakeMultiOrder = [&](
			std::initializer_list<FSeinEntityHandle> Members,
			std::initializer_list<FSeinFrozenDestination> Artifact,
			bool bQueue)
		{
			FSeinBrokerOrderPayload Payload;
			Payload.CommandContext.AddTag(
				SeinARTSTags::Command_Context_RightClick);
			Payload.CommandContext.AddTag(
				SeinARTSTags::Command_Context_Target_Ground);
			for (const FSeinFrozenDestination& Destination : Artifact)
			{
				Payload.DestinationArtifact.Add(Destination);
			}
			FSeinCommand Command;
			Command.PlayerID = Player;
			Command.IssuerKind = ESeinCommandIssuerKind::Player;
			Command.CommandType = SeinARTSTags::Command_Type_BrokerOrder;
			Command.SchemaVersion = SeinBrokerOrderProtocol::SchemaVersion;
			Command.TargetLocation =
				Payload.DestinationArtifact[0].WorldPosition;
			for (const FSeinEntityHandle& Member : Members)
			{
				Command.EntityList.Add(Member);
				FSeinBrokerRecipientPlanSegment& Segment =
					Payload.RecipientPlan.AddDefaulted_GetRef();
				Segment.Recipient = Member;
				Segment.MemberCount = 1;
			}
			Command.bQueueCommand = bQueue;
			Command.Payload = FInstancedStruct::Make(Payload);
			return Command;
		};

		// First order selects [First, Second] — the shared ephemeral broker's
		// stored member order. The queued follow-up re-selects the SAME units
		// in the opposite order; its artifact rides in that click order and
		// must be re-keyed to broker order at queue time, or the restore
		// preflight's ordered-subset walk rejects the whole snapshot.
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			ASSERT_THAT(IsTrue(
				FSeinWorldSubsystemTestAccess::HandleBrokerOrder(
					*World,
					MakeMultiOrder(
						{FirstMember, SecondMember},
						{MakeDestination(*World, FirstMember, 1000),
							MakeDestination(*World, SecondMember, 1200)},
						false))));
			ASSERT_THAT(IsTrue(
				FSeinWorldSubsystemTestAccess::HandleBrokerOrder(
					*World,
					MakeMultiOrder(
						{SecondMember, FirstMember},
						{MakeDestination(*World, SecondMember, 1400),
							MakeDestination(*World, FirstMember, 1600)},
						true))));
		}

		const FSeinBrokerMembershipData* Membership =
			World->GetComponent<FSeinBrokerMembershipData>(FirstMember);
		ASSERT_THAT(IsNotNull(Membership));
		const FSeinCommandBrokerData* BrokerData =
			World->GetComponent<FSeinCommandBrokerData>(
				Membership->CurrentBrokerHandle);
		ASSERT_THAT(IsNotNull(BrokerData));
		ASSERT_THAT(AreEqual(2, BrokerData->OrderQueue.Num()));
		const FSeinBrokerQueuedOrder& Queued = BrokerData->OrderQueue[1];
		ASSERT_THAT(IsTrue(Queued.TargetMembers.IsEmpty()));
		ASSERT_THAT(AreEqual(2, Queued.DestinationArtifact.Num()));
		ASSERT_THAT(IsTrue(
			Queued.DestinationArtifact[0].Member == BrokerData->Members[0]));
		ASSERT_THAT(IsTrue(
			Queued.DestinationArtifact[1].Member == BrokerData->Members[1]));

		FSeinWorldSnapshot Snapshot;
		FSeinWorldSnapshotReferenceGuard SnapshotGuard(Snapshot);
		World->CaptureSnapshot(Snapshot);

		FActorTestSpawner RestoredSpawner;
		USeinWorldSubsystem* Restored =
			RestoredSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Restored));
		FString Error;
		ASSERT_THAT(IsTrue(SeinTestSnapshotRestore::RestoreTrusted(
			*Restored, Snapshot, &Error)));

		FGuid SourceRoot;
		FGuid RestoredRoot;
		ASSERT_THAT(IsTrue(
			World->ComputeCanonicalStateRoot(SourceRoot, Error)));
		ASSERT_THAT(IsTrue(
			Restored->ComputeCanonicalStateRoot(RestoredRoot, Error)));
		ASSERT_THAT(IsTrue(SourceRoot == RestoredRoot));

		World->StopSimulation();
		Restored->StopSimulation();
	}

	TEST(SettledFrozenDestinationSurvivesCompletionUntilReplacement,
		"SeinARTS.Unit.CoreEntity.FrozenDestination")
	{
		using namespace FrozenDestinationTestLocal;
		FScopedBrokerPolicy Policy;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FSeinPlayerID Player(1);
		FSeinEntityHandle Member;
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));
			Member = World->SpawnAbstractEntity(
				FFixedTransform(), Player);
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World,
			AuthorState,
			FSeinMatchSettings(),
			0x46524F5D,
			TEXT("SeinARTS.FrozenDestination.Settled"))));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));

		const FSeinFrozenDestination Destination =
			MakeDestination(*World, Member, 1000);
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			ASSERT_THAT(IsTrue(
				FSeinWorldSubsystemTestAccess::HandleBrokerOrder(
					*World,
					MakeOrder(Player, Member, Destination, false))));
		}
		const FSeinBrokerMembershipData* Membership =
			World->GetComponent<FSeinBrokerMembershipData>(Member);
		ASSERT_THAT(IsNotNull(Membership));
		const FSeinEntityHandle Broker =
			Membership->CurrentBrokerHandle;
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			FSeinCommandBrokerData* BrokerData =
				World->GetComponentMutable<FSeinCommandBrokerData>(Broker);
			ASSERT_THAT(IsNotNull(BrokerData));
			ASSERT_THAT(IsTrue(
				BrokerData->SettledDestinationArtifact.IsEmpty()));
			ASSERT_THAT(IsTrue(World->ConfirmFrozenDestinationArrival(
				Member, Destination.WorldPosition)));
			BrokerData =
				World->GetComponentMutable<FSeinCommandBrokerData>(Broker);
			ASSERT_THAT(IsNotNull(BrokerData));
			ASSERT_THAT(AreEqual(
				1, BrokerData->SettledDestinationArtifact.Num()));
			BrokerData->OrderQueue.Reset();
		}

		FSeinAuthoritativeDestinationQuery AuthorityQuery;
		AuthorityQuery.Requester = Member;
		AuthorityQuery.WorldPosition = Destination.WorldPosition;
		ASSERT_THAT(IsTrue(
			World->IsAuthoritativeDestination(AuthorityQuery)));
		ASSERT_THAT(IsTrue(World->IsDestinationFootprintReserved(
			Destination.WorldPosition,
			Destination.FootprintRadius)));

		FSeinFrozenDestination Replacement =
			MakeDestination(*World, Member, 1400);
		Replacement.bReserveFootprint = false;
		Replacement.SourceIndex = INDEX_NONE;
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			ASSERT_THAT(IsTrue(
				FSeinWorldSubsystemTestAccess::HandleBrokerOrder(
					*World,
					MakeOrder(Player, Member, Replacement, false))));
		}
		ASSERT_THAT(IsFalse(
			World->IsAuthoritativeDestination(AuthorityQuery)));
		ASSERT_THAT(IsFalse(World->IsDestinationFootprintReserved(
			Destination.WorldPosition,
			Destination.FootprintRadius)));
		World->StopSimulation();
	}

	TEST(FailedDispatchDoesNotCreateSettledDestination,
		"SeinARTS.Unit.CoreEntity.FrozenDestination")
	{
		using namespace FrozenDestinationTestLocal;
		FScopedBrokerPolicy Policy;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FSeinPlayerID Player(1);
		FSeinEntityHandle Member;
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));
			Member = World->SpawnAbstractEntity(
				FFixedTransform(), Player);
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World,
			AuthorState,
			FSeinMatchSettings(),
			0x46524F5E,
			TEXT("SeinARTS.FrozenDestination.FailedDispatch"))));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));

		const FSeinFrozenDestination Destination =
			MakeDestination(*World, Member, 1000);
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			ASSERT_THAT(IsTrue(
				FSeinWorldSubsystemTestAccess::HandleBrokerOrder(
					*World,
					MakeOrder(Player, Member, Destination, false))));
		}
		const FSeinBrokerMembershipData* Membership =
			World->GetComponent<FSeinBrokerMembershipData>(Member);
		ASSERT_THAT(IsNotNull(Membership));
		const FSeinEntityHandle Broker =
			Membership->CurrentBrokerHandle;

		FTSTicker::GetCoreTicker().Tick(
			World->GetFixedDeltaTimeSeconds());
		const FSeinCommandBrokerData* BrokerData =
			World->GetComponent<FSeinCommandBrokerData>(Broker);
		ASSERT_THAT(IsNotNull(BrokerData));
		ASSERT_THAT(IsTrue(BrokerData->OrderQueue.IsEmpty()));
		ASSERT_THAT(IsTrue(
			BrokerData->SettledDestinationArtifact.IsEmpty()));
		ASSERT_THAT(IsFalse(World->IsDestinationFootprintReserved(
			Destination.WorldPosition,
			Destination.FootprintRadius)));
		World->StopSimulation();
	}
}
