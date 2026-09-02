#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Actor/SeinActor.h"
#include "Brokers/SeinBrokerTypes.h"
#include "Components/SeinBrokerMembershipData.h"
#include "Components/SeinCommandBrokerData.h"
#include "Components/SeinCoverPayload.h"
#include "Components/SeinExtentsPayload.h"
#include "Components/SeinNavigationPayload.h"
#include "Containers/Ticker.h"
#include "Events/SeinVisualEvent.h"
#include "Formations/SeinFormation.h"
#include "Input/SeinCommand.h"
#include "Lib/SeinCommandBrokerBPFL.h"
#include "Player/SeinPlayerController.h"
#include "Settings/SeinARTSCoverSettings.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSimContext.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "System/SeinCoverDefault.h"
#include "System/SeinCoverSubsystem.h"
#include "System/SeinCoverSystem.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "Tags/SeinCoverGameplayTags.h"

namespace UE::SeinARTSExtensionTests
{
	namespace MovingCoverFrozenDestinationTestLocal
	{
		struct FScopedCoverPolicy
		{
			FScopedCoverPolicy()
				: Settings(GetMutableDefault<USeinARTSCoverSettings>())
				, SavedSystem(Settings->CoverSystemClass)
				, SavedRadius(Settings->CoverSnapRadius)
			{
				Settings->CoverSystemClass = FSoftClassPath(
					USeinCoverDefault::StaticClass());
				Settings->CoverSnapRadius = FFixedPoint::FromInt(500);
			}

			~FScopedCoverPolicy()
			{
				Settings->CoverSystemClass = SavedSystem;
				Settings->CoverSnapRadius = SavedRadius;
			}

			USeinARTSCoverSettings* Settings;
			FSoftClassPath SavedSystem;
			FFixedPoint SavedRadius;
		};

		FFixedVector Position(int32 X)
		{
			return FFixedVector(
				FFixedPoint::FromInt(X),
				FFixedPoint::Zero,
				FFixedPoint::Zero);
		}

		TArray<FSeinFrozenDestination> MakeBaseDestinations(
			USeinWorldSubsystem& World,
			const TArray<FSeinEntityHandle>& Members,
			int32 Offset)
		{
			TArray<FSeinFrozenDestination> Result;
			for (int32 Index = 0; Index < Members.Num(); ++Index)
			{
				FSeinFrozenDestination& Destination =
					Result.Emplace_GetRef();
				Destination.Member = Members[Index];
				Destination.WorldPosition = Position(
					Offset + (Index == 0 ? -100 : 100));
				Destination.FootprintRadius =
					USeinFormation::GetFootprintRadius(
						&World, Members[Index]);
			}
			return Result;
		}
	}

	TEST(MovingCoverExposesCurrentSlotsWithoutRetargetingFrozenOrders,
		"SeinARTS.Sim.Cover.FrozenDestination")
	{
		using namespace MovingCoverFrozenDestinationTestLocal;
		FScopedCoverPolicy Policy;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		USeinCoverSubsystem* CoverSubsystem =
			Spawner.GetWorld().GetSubsystem<USeinCoverSubsystem>();
		USeinCoverSystem* Cover = CoverSubsystem
			? CoverSubsystem->GetCoverSystem()
			: nullptr;
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsNotNull(Cover));
		World->DynamicPassableResolver.Unbind();

		const FSeinPlayerID Player(1);
		TArray<FSeinEntityHandle> Members;
		FSeinEntityHandle Provider;
		FSeinEntityHandle Broker;
		bool bAuthored = true;
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));
			for (int32 Index = 0; Index < 2; ++Index)
			{
				const FSeinEntityHandle Member =
					World->SpawnAbstractEntity(
						FFixedTransform(Position(Index == 0 ? -100 : 100)),
						Player);
				FSeinExtentsShape Shape;
				Shape.Shape = ESeinExtentsShape::Capsule;
				Shape.Radius = FFixedPoint::FromInt(40);
				FSeinExtentsPayload Extents;
				Extents.Shapes.Add(Shape);
				World->AddComponent(Member, Extents);
				bAuthored = World->GrantTag(
					Member, SeinCoverTags::Cover_UsesCover) && bAuthored;
				Members.Add(Member);
			}

			Provider = World->SpawnAbstractEntity(
				FFixedTransform(Position(0)), FSeinPlayerID::Neutral());
			FSeinCoverPayload CoverData;
			CoverData.QualityTag = SeinCoverTags::Cover_Light;
			CoverData.SlotRadius = FFixedPoint::FromInt(20);
			CoverData.Slots.Add(Position(-100));
			CoverData.Slots.Add(Position(100));
			World->AddComponent(Provider, CoverData);
			Cover->RegisterAuthoritativeProvider(Provider);

			Broker = World->SpawnAbstractEntity(
				FFixedTransform(), Player);
			FSeinCommandBrokerData BrokerData;
			BrokerData.Members = Members;
			World->AddComponent(Broker, BrokerData);
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World,
			AuthorState,
			FSeinMatchSettings(),
			0x4D4F5643,
			TEXT("SeinARTS.Cover.MovingFrozenDestination"))));
		ASSERT_THAT(IsTrue(bAuthored));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));

		FSeinSelectionDestinationPlanQuery FirstQuery;
		FirstQuery.OrderingPlayer = FSeinPlayerID::Neutral();
		FirstQuery.TargetLocation = Position(0);
		FirstQuery.Members = Members;
		TArray<FSeinFrozenDestination> FirstPlan =
			MakeBaseDestinations(*World, Members, 0);
		FString Error;
		ASSERT_THAT(IsTrue(World->ApplySelectionDestinationPlanProviders(
			FirstQuery, FirstPlan, &Error)));
		ASSERT_THAT(AreEqual(2, FirstPlan.Num()));
		for (int32 Index = 0; Index < FirstPlan.Num(); ++Index)
		{
			ASSERT_THAT(IsTrue(FirstPlan[Index].bReserveFootprint));
			ASSERT_THAT(IsTrue(FirstPlan[Index].SourceEntity == Provider));
			ASSERT_THAT(AreEqual(Index, FirstPlan[Index].SourceIndex));
			ASSERT_THAT(IsTrue(
				FirstPlan[Index].WorldPosition
					== Position(Index == 0 ? -100 : 100)));
		}

		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			FSeinCommandBrokerData* BrokerData =
				World->GetComponentMutable<FSeinCommandBrokerData>(Broker);
			ASSERT_THAT(IsNotNull(BrokerData));
			BrokerData->SettledDestinationArtifact = FirstPlan;
		}

		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			FSeinEntity* MutableProvider = World->GetEntityMutable(Provider);
			ASSERT_THAT(IsNotNull(MutableProvider));
			MutableProvider->Transform.SetLocation(Position(1000));
		}

		FSeinSelectionDestinationPlanQuery SecondQuery;
		SecondQuery.OrderingPlayer = FSeinPlayerID::Neutral();
		SecondQuery.TargetLocation = Position(1000);
		SecondQuery.Members = Members;
		TArray<FSeinFrozenDestination> SecondPlan =
			MakeBaseDestinations(*World, Members, 1000);
		ASSERT_THAT(IsTrue(World->ApplySelectionDestinationPlanProviders(
			SecondQuery, SecondPlan, &Error)));
		for (int32 Index = 0; Index < SecondPlan.Num(); ++Index)
		{
			ASSERT_THAT(IsTrue(SecondPlan[Index].bReserveFootprint));
			ASSERT_THAT(IsTrue(
				SecondPlan[Index].WorldPosition
					== Position(Index == 0 ? 900 : 1100)));
			ASSERT_THAT(IsTrue(
				FirstPlan[Index].WorldPosition
					== Position(Index == 0 ? -100 : 100)));
		}

		FSeinAuthoritativeDestinationQuery AuthorityQuery;
		AuthorityQuery.Requester = Members[0];
		AuthorityQuery.WorldPosition = FirstPlan[0].WorldPosition;
		ASSERT_THAT(IsTrue(
			World->IsAuthoritativeDestination(AuthorityQuery)));
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			World->DestroyEntity(Provider);
		}
		ASSERT_THAT(IsTrue(
			World->IsAuthoritativeDestination(AuthorityQuery)));
		World->StopSimulation();
	}

	TEST(DirectHitOnMovingCoverCommitsDisplayedSlotAsGround,
		"SeinARTS.Sim.Cover.FrozenDestination")
	{
		using namespace MovingCoverFrozenDestinationTestLocal;
		FScopedCoverPolicy Policy;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		USeinCoverSubsystem* CoverSubsystem =
			Spawner.GetWorld().GetSubsystem<USeinCoverSubsystem>();
		USeinCoverSystem* Cover = CoverSubsystem
			? CoverSubsystem->GetCoverSystem()
			: nullptr;
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsNotNull(Cover));
		World->NavProjectResolver.Unbind();
		World->DynamicPassableResolver.Unbind();

		const FSeinPlayerID Player(1);
		FSeinEntityHandle Member;
		FSeinEntityHandle Provider;
		bool bAuthored = true;
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));
			Member = World->SpawnAbstractEntity(
				FFixedTransform(Position(0)), Player);
			FSeinExtentsShape Shape;
			Shape.Shape = ESeinExtentsShape::Capsule;
			Shape.Radius = FFixedPoint::FromInt(60);
			FSeinExtentsPayload Extents;
			Extents.Shapes.Add(Shape);
			World->AddComponent(Member, Extents);
			World->AddComponent(Member, FSeinNavigationPayload());
			bAuthored = World->GrantTag(
				Member, SeinCoverTags::Cover_UsesCover);

			Provider = World->SpawnAbstractEntity(
				FFixedTransform(Position(0)), Player);
			FSeinCoverPayload CoverData;
			CoverData.QualityTag = SeinCoverTags::Cover_Light;
			CoverData.SlotRadius = FFixedPoint::FromInt(20);
			CoverData.Slots.Add(Position(0));
			World->AddComponent(Provider, CoverData);
			Cover->RegisterAuthoritativeProvider(Provider);
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World,
			AuthorState,
			FSeinMatchSettings(),
			0x4D4F5644,
			TEXT("SeinARTS.Cover.MovingFrozenDestination.DirectHit"))));
		ASSERT_THAT(IsTrue(bAuthored));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));

		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			FSeinEntity* MutableProvider = World->GetEntityMutable(Provider);
			ASSERT_THAT(IsNotNull(MutableProvider));
			MutableProvider->Transform.SetLocation(Position(1000));
		}

		ASeinActor& MemberActor = Spawner.SpawnActor<ASeinActor>();
		ASeinActor& ProviderActor = Spawner.SpawnActor<ASeinActor>();
		ASeinPlayerController& Controller =
			Spawner.SpawnActor<ASeinPlayerController>();
		MemberActor.InitializeWithEntity(Member);
		ProviderActor.InitializeWithEntity(Provider);
		Controller.SeinPlayerID = Player;
		Controller.SelectedActors.Add(&MemberActor);
		Controller.IssueSmartCommandEx(
			FVector(1000.0, 0.0, 0.0),
			&ProviderActor,
			false,
			{},
			FGameplayTag());

		ASSERT_THAT(AreEqual(1, World->GetPendingCommands().Num()));
		const FSeinCommand& Command =
			World->GetPendingCommands().GetCommands()[0];
		ASSERT_THAT(IsTrue(
			Command.CommandType == SeinARTSTags::Command_Type_BrokerOrder));
		ASSERT_THAT(IsFalse(Command.TargetEntity.IsValid()));
		ASSERT_THAT(AreEqual(1, Command.EntityList.Num()));
		ASSERT_THAT(IsTrue(Command.EntityList[0] == Member));
		const FSeinBrokerOrderPayload* Payload =
			Command.Payload.GetPtr<FSeinBrokerOrderPayload>();
		ASSERT_THAT(IsNotNull(Payload));
		ASSERT_THAT(IsTrue(Payload->CommandContext.HasTagExact(
			SeinARTSTags::Command_Context_Target_Ground)));
		ASSERT_THAT(IsFalse(Payload->CommandContext.HasTagExact(
			SeinARTSTags::Command_Context_Target_Neutral)));
		ASSERT_THAT(IsFalse(Payload->CommandContext.HasTagExact(
			SeinARTSTags::Command_Context_Target_Friendly)));
		ASSERT_THAT(AreEqual(1, Payload->DestinationArtifact.Num()));
		const FSeinFrozenDestination& Destination =
			Payload->DestinationArtifact[0];
		ASSERT_THAT(IsTrue(Destination.Member == Member));
		ASSERT_THAT(IsTrue(Destination.WorldPosition == Position(1000)));
		ASSERT_THAT(IsTrue(Destination.bReserveFootprint));
		ASSERT_THAT(IsTrue(Destination.SourceEntity == Provider));
		ASSERT_THAT(AreEqual(0, Destination.SourceIndex));

		FTSTicker::GetCoreTicker().Tick(
			World->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(IsNotNull(
			World->GetComponent<FSeinBrokerMembershipData>(Member)));
		const TArray<FSeinVisualEvent> Events = World->FlushVisualEvents();
		ASSERT_THAT(IsFalse(Events.ContainsByPredicate(
			[](const FSeinVisualEvent& Event)
			{
				return Event.ReasonTag
					== SeinARTSTags::Command_Reject_InvalidTarget;
			})));
		World->StopSimulation();
	}

	TEST(NativeBrokerOrderFreezesMovingCoverSlot,
		"SeinARTS.Sim.Cover.FrozenDestination")
	{
		using namespace MovingCoverFrozenDestinationTestLocal;
		FScopedCoverPolicy Policy;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		USeinCoverSubsystem* CoverSubsystem =
			Spawner.GetWorld().GetSubsystem<USeinCoverSubsystem>();
		USeinCoverSystem* Cover = CoverSubsystem
			? CoverSubsystem->GetCoverSystem()
			: nullptr;
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsNotNull(Cover));
		World->NavProjectResolver.Unbind();
		World->DynamicPassableResolver.Unbind();

		const FSeinPlayerID Player(1);
		FSeinEntityHandle Member;
		FSeinEntityHandle Provider;
		bool bAuthored = true;
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));
			Member = World->SpawnAbstractEntity(
				FFixedTransform(Position(0)), Player);
			FSeinExtentsShape Shape;
			Shape.Shape = ESeinExtentsShape::Capsule;
			Shape.Radius = FFixedPoint::FromInt(60);
			FSeinExtentsPayload Extents;
			Extents.Shapes.Add(Shape);
			World->AddComponent(Member, Extents);
			World->AddComponent(Member, FSeinNavigationPayload());
			bAuthored = World->GrantTag(
				Member, SeinCoverTags::Cover_UsesCover);

			Provider = World->SpawnAbstractEntity(
				FFixedTransform(Position(0)), Player);
			FSeinCoverPayload CoverData;
			CoverData.QualityTag = SeinCoverTags::Cover_Light;
			CoverData.SlotRadius = FFixedPoint::FromInt(20);
			CoverData.Slots.Add(Position(0));
			World->AddComponent(Provider, CoverData);
			Cover->RegisterAuthoritativeProvider(Provider);
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World,
			AuthorState,
			FSeinMatchSettings(),
			0x4D4F5645,
			TEXT("SeinARTS.Cover.MovingFrozenDestination.NativeOrder"))));
		ASSERT_THAT(IsTrue(bAuthored));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));

		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			FSeinEntity* MutableProvider = World->GetEntityMutable(Provider);
			ASSERT_THAT(IsNotNull(MutableProvider));
			MutableProvider->Transform.SetLocation(Position(1000));
		}

		FGameplayTagContainer Context;
		Context.AddTag(SeinARTSTags::Command_Context_RightClick);
		Context.AddTag(SeinARTSTags::Command_Context_Target_Ground);
		USeinCommandBrokerBPFL::SeinIssueBrokerOrder(
			World,
			Player,
			{Member},
			Context,
			FSeinEntityHandle::Invalid(),
			Position(1000));

		ASSERT_THAT(AreEqual(1, World->GetPendingCommands().Num()));
		const FSeinCommand& Command =
			World->GetPendingCommands().GetCommands()[0];
		const FSeinBrokerOrderPayload* Payload =
			Command.Payload.GetPtr<FSeinBrokerOrderPayload>();
		ASSERT_THAT(IsNotNull(Payload));
		ASSERT_THAT(AreEqual(1, Payload->DestinationArtifact.Num()));
		const FSeinFrozenDestination& Destination =
			Payload->DestinationArtifact[0];
		ASSERT_THAT(IsTrue(Destination.Member == Member));
		ASSERT_THAT(IsTrue(Destination.WorldPosition == Position(1000)));
		ASSERT_THAT(IsTrue(Destination.bReserveFootprint));
		ASSERT_THAT(IsTrue(Destination.SourceEntity == Provider));
		ASSERT_THAT(AreEqual(0, Destination.SourceIndex));
		World->StopSimulation();
	}

	TEST(BlueprintFormationOrderTokenFreezesDisplayedMovingCoverSlot,
		"SeinARTS.Sim.Cover.FrozenDestination")
	{
		using namespace MovingCoverFrozenDestinationTestLocal;
		FScopedCoverPolicy Policy;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		USeinCoverSubsystem* CoverSubsystem =
			Spawner.GetWorld().GetSubsystem<USeinCoverSubsystem>();
		USeinCoverSystem* Cover = CoverSubsystem
			? CoverSubsystem->GetCoverSystem()
			: nullptr;
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsNotNull(Cover));
		World->NavProjectResolver.Unbind();
		World->DynamicPassableResolver.Unbind();

		const FSeinPlayerID Player(1);
		FSeinEntityHandle Member;
		FSeinEntityHandle Provider;
		bool bAuthored = true;
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));
			Member = World->SpawnAbstractEntity(
				FFixedTransform(Position(0)), Player);
			FSeinExtentsShape Shape;
			Shape.Shape = ESeinExtentsShape::Capsule;
			Shape.Radius = FFixedPoint::FromInt(60);
			FSeinExtentsPayload Extents;
			Extents.Shapes.Add(Shape);
			World->AddComponent(Member, Extents);
			bAuthored = World->GrantTag(
				Member, SeinCoverTags::Cover_UsesCover);

			Provider = World->SpawnAbstractEntity(
				FFixedTransform(Position(0)), Player);
			FSeinCoverPayload CoverData;
			CoverData.QualityTag = SeinCoverTags::Cover_Light;
			CoverData.SlotRadius = FFixedPoint::FromInt(20);
			CoverData.Slots.Add(Position(0));
			World->AddComponent(Provider, CoverData);
			Cover->RegisterAuthoritativeProvider(Provider);
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World,
			AuthorState,
			FSeinMatchSettings(),
			0x4D4F5646,
			TEXT("SeinARTS.Cover.MovingFrozenDestination.Token"))));
		ASSERT_THAT(IsTrue(bAuthored));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));

		FGameplayTagContainer Context;
		Context.AddTag(SeinARTSTags::Command_Context_RightClick);
		Context.AddTag(SeinARTSTags::Command_Context_Target_Ground);
		FSeinFormationLayout FirstLayout;
		USeinFormationOrderToken* FirstToken = nullptr;
		ASSERT_THAT(IsTrue(
			USeinCommandBrokerBPFL::SeinPlanFormationOrder(
				World,
				Player,
				{Member},
				Context,
				Position(0),
				{},
				FGameplayTag(),
				false,
				FirstLayout,
				FirstToken)
			== ESeinFormationOrderTokenResult::Success));
		ASSERT_THAT(IsNotNull(FirstToken));
		ASSERT_THAT(AreEqual(1, FirstLayout.Positions.Num()));
		ASSERT_THAT(IsTrue(FirstLayout.Positions[0] == Position(0)));

		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			FSeinEntity* MutableProvider = World->GetEntityMutable(Provider);
			ASSERT_THAT(IsNotNull(MutableProvider));
			MutableProvider->Transform.SetLocation(Position(1000));
		}

		FSeinFormationLayout CurrentLayout;
		USeinFormationOrderToken* CurrentToken = nullptr;
		ASSERT_THAT(IsTrue(
			USeinCommandBrokerBPFL::SeinPlanFormationOrder(
				World,
				Player,
				{Member},
				Context,
				Position(1000),
				{},
				FGameplayTag(),
				false,
				CurrentLayout,
				CurrentToken)
			== ESeinFormationOrderTokenResult::Success));
		ASSERT_THAT(IsNotNull(CurrentToken));
		ASSERT_THAT(AreEqual(1, CurrentLayout.Positions.Num()));
		ASSERT_THAT(IsTrue(CurrentLayout.Positions[0] == Position(1000)));
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			World->DestroyEntity(Provider);
		}
		ASSERT_THAT(IsFalse(World->IsEntityAlive(Provider)));

		ASSERT_THAT(IsTrue(
			USeinCommandBrokerBPFL::SeinIssueFormationOrder(
				World, FirstToken)
			== ESeinFormationOrderTokenResult::Success));
		ASSERT_THAT(AreEqual(1, World->GetPendingCommands().Num()));
		const FSeinCommand& Command =
			World->GetPendingCommands().GetCommands()[0];
		const FSeinBrokerOrderPayload* Payload =
			Command.Payload.GetPtr<FSeinBrokerOrderPayload>();
		ASSERT_THAT(IsNotNull(Payload));
		ASSERT_THAT(AreEqual(1, Payload->DestinationArtifact.Num()));
		const FSeinFrozenDestination& Destination =
			Payload->DestinationArtifact[0];
		ASSERT_THAT(IsTrue(Destination.WorldPosition == Position(0)));
		ASSERT_THAT(IsTrue(Destination.bReserveFootprint));
		ASSERT_THAT(IsTrue(Destination.SourceEntity == Provider));
		ASSERT_THAT(AreEqual(0, Destination.SourceIndex));
		World->StopSimulation();
	}

	TEST(MixedMemberRadiiDoNotPreviewAnUnadmittableSlotPair,
		"SeinARTS.Sim.Cover.FrozenDestination")
	{
		using namespace MovingCoverFrozenDestinationTestLocal;
		FScopedCoverPolicy Policy;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		USeinCoverSubsystem* CoverSubsystem =
			Spawner.GetWorld().GetSubsystem<USeinCoverSubsystem>();
		USeinCoverSystem* Cover = CoverSubsystem
			? CoverSubsystem->GetCoverSystem()
			: nullptr;
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsNotNull(Cover));
		World->DynamicPassableResolver.Unbind();

		const FSeinPlayerID Player(1);
		TArray<FSeinEntityHandle> Members;
		FSeinEntityHandle Provider;
		bool bAuthored = true;
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));
			for (const int32 Radius : {100, 60})
			{
				const FSeinEntityHandle Member =
					World->SpawnAbstractEntity(FFixedTransform(), Player);
				FSeinExtentsShape Shape;
				Shape.Shape = ESeinExtentsShape::Capsule;
				Shape.Radius = FFixedPoint::FromInt(Radius);
				FSeinExtentsPayload Extents;
				Extents.Shapes.Add(Shape);
				World->AddComponent(Member, Extents);
				bAuthored = World->GrantTag(
					Member, SeinCoverTags::Cover_UsesCover) && bAuthored;
				Members.Add(Member);
			}

			Provider = World->SpawnAbstractEntity(
				FFixedTransform(), Player);
			FSeinCoverPayload CoverData;
			CoverData.QualityTag = SeinCoverTags::Cover_Light;
			CoverData.SlotRadius = FFixedPoint::FromInt(20);
			CoverData.Slots.Add(Position(-60));
			CoverData.Slots.Add(Position(60));
			World->AddComponent(Provider, CoverData);
			Cover->RegisterAuthoritativeProvider(Provider);
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World,
			AuthorState,
			FSeinMatchSettings(),
			0x4D4F5645,
			TEXT("SeinARTS.Cover.MovingFrozenDestination.MixedRadii"))));
		ASSERT_THAT(IsTrue(bAuthored));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));

		FSeinSelectionDestinationPlanQuery Query;
		Query.OrderingPlayer = Player;
		Query.TargetLocation = Position(0);
		Query.Members = Members;
		TArray<FSeinFrozenDestination> Plan =
			MakeBaseDestinations(*World, Members, 0);
		FString Error;
		ASSERT_THAT(IsTrue(World->ApplySelectionDestinationPlanProviders(
			Query, Plan, &Error)));
		ASSERT_THAT(AreEqual(2, Plan.Num()));
		int32 ReservedCount = 0;
		for (const FSeinFrozenDestination& Destination : Plan)
		{
			ReservedCount += Destination.bReserveFootprint ? 1 : 0;
		}
		ASSERT_THAT(AreEqual(1, ReservedCount));

		FSeinBrokerOrderPayload Payload;
		Payload.CommandContext.AddTag(
			SeinARTSTags::Command_Context_RightClick);
		Payload.CommandContext.AddTag(
			SeinARTSTags::Command_Context_Target_Ground);
		for (const FSeinEntityHandle Member : Members)
		{
			FSeinBrokerRecipientPlanSegment& Segment =
				Payload.RecipientPlan.AddDefaulted_GetRef();
			Segment.Recipient = Member;
			Segment.MemberCount = 1;
		}
		Payload.DestinationArtifact = Plan;
		FSeinCommand Command;
		Command.PlayerID = Player;
		Command.CommandType = SeinARTSTags::Command_Type_BrokerOrder;
		Command.SchemaVersion = SeinBrokerOrderProtocol::SchemaVersion;
		Command.TargetLocation = Position(0);
		Command.EntityList = Members;
		Command.Payload = FInstancedStruct::Make(Payload);
		World->SubmitLocalCommandDraft(Command);
		ASSERT_THAT(AreEqual(1, World->GetPendingCommands().Num()));

		FTSTicker::GetCoreTicker().Tick(
			World->GetFixedDeltaTimeSeconds());
		for (const FSeinEntityHandle Member : Members)
		{
			ASSERT_THAT(IsNotNull(
				World->GetComponent<FSeinBrokerMembershipData>(Member)));
		}
		const TArray<FSeinVisualEvent> Events = World->FlushVisualEvents();
		ASSERT_THAT(IsFalse(Events.ContainsByPredicate(
			[](const FSeinVisualEvent& Event)
			{
				return Event.ReasonTag
					== SeinARTSTags::Command_Reject_DestinationReserved;
			})));
		World->StopSimulation();
	}
}
