#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Brokers/SeinFormationOrderToken.h"
#include "Components/SeinCommandBrokerData.h"
#include "Data/SeinWorldSnapshot.h"
#include "Input/SeinCommand.h"
#include "Lib/SeinCommandBrokerBPFL.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSimContext.h"
#include "Simulation/SeinTestSnapshotRestore.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinARTSGameplayTags.h"

namespace UE::SeinARTSTests
{
	namespace FormationOrderTokenTestLocal
	{
		FFixedVector Position(int32 X, int32 Y = 0)
		{
			return FFixedVector(
				FFixedPoint::FromInt(X),
				FFixedPoint::FromInt(Y),
				FFixedPoint::Zero);
		}

		FGameplayTagContainer GroundContext()
		{
			FGameplayTagContainer Context;
			Context.AddTag(SeinARTSTags::Command_Context_RightClick);
			Context.AddTag(
				SeinARTSTags::Command_Context_Target_Ground);
			return Context;
		}

		struct FFormationOrderWorld
		{
			FActorTestSpawner Spawner;
			USeinWorldSubsystem* World = nullptr;
			FSeinPlayerID Player = FSeinPlayerID(1);
			TArray<FSeinEntityHandle> Members;

			explicit FFormationOrderWorld(int32 MemberCount)
			{
				World = Spawner.GetWorld()
					.GetSubsystem<USeinWorldSubsystem>();
				if (!World) return;
				const auto AuthorState = [&]()
				{
					World->RegisterPlayer(Player, FSeinFactionID(1));
					World->RegisterPlayer(FSeinPlayerID(2), FSeinFactionID(2));
					for (int32 Index = 0; Index < MemberCount; ++Index)
					{
						Members.Add(World->SpawnAbstractEntity(
							FFixedTransform(Position(Index * 100)),
							Player));
					}
				};
				if (!SeinTestMatchBootstrap::Materialize(
						*World,
						AuthorState,
						FSeinMatchSettings(),
						0x464F544B,
						TEXT("SeinARTS.FormationOrderToken"))
					|| !SeinTestMatchBootstrap::Start(*World))
				{
					World = nullptr;
				}
			}

			~FFormationOrderWorld()
			{
				if (World && World->IsSimulationRunning())
				{
					World->StopSimulation();
				}
			}
		};
	}

	TEST(FormationOrderTokenRetriesRejectedSubmissionWithoutDuplicateIngress,
		"SeinARTS.Unit.CoreEntity.FormationOrderToken")
	{
		using namespace FormationOrderTokenTestLocal;
		FFormationOrderWorld Fixture(1);
		ASSERT_THAT(IsNotNull(Fixture.World));

		FSeinFormationLayout Layout;
		USeinFormationOrderToken* Token = nullptr;
		ASSERT_THAT(IsTrue(
			USeinCommandBrokerBPFL::SeinPlanFormationOrder(
				Fixture.World, Fixture.Player, Fixture.Members,
				GroundContext(), Position(1000), {}, FGameplayTag(), false,
				Layout, Token)
			== ESeinFormationOrderTokenResult::Success));

		int32 RejectedSubmissionCount = 0;
		FSeinLocalCommandPrincipalResolver PrincipalResolver;
		PrincipalResolver.BindLambda(
			[](FSeinPlayerID ClaimedPlayer)
			{
				return ClaimedPlayer;
			});
		Fixture.World->SetLocalCommandPrincipalResolver(
			MoveTemp(PrincipalResolver));
		FSeinLocalCommandSubmitter RejectingSubmitter;
		RejectingSubmitter.BindLambda(
			[&RejectedSubmissionCount](const FSeinCommand&, bool)
			{
				++RejectedSubmissionCount;
				return false;
			});
		Fixture.World->SetLocalCommandSubmitter(MoveTemp(RejectingSubmitter));
		ASSERT_THAT(IsTrue(
			USeinCommandBrokerBPFL::SeinIssueFormationOrder(
				Fixture.World, Token)
			== ESeinFormationOrderTokenResult::SubmissionRejected));
		ASSERT_THAT(AreEqual(1, RejectedSubmissionCount));
		ASSERT_THAT(AreEqual(0, Fixture.World->GetPendingCommands().Num()));

		Fixture.World->ClearLocalCommandSubmitter();
		ASSERT_THAT(IsTrue(
			USeinCommandBrokerBPFL::SeinIssueFormationOrder(
				Fixture.World, Token)
			== ESeinFormationOrderTokenResult::Success));
		ASSERT_THAT(AreEqual(1, Fixture.World->GetPendingCommands().Num()));
		ASSERT_THAT(IsTrue(
			USeinCommandBrokerBPFL::SeinIssueFormationOrder(
				Fixture.World, Token)
			== ESeinFormationOrderTokenResult::AlreadyIssued));
	}

	TEST(FormationOrderTokenAuthenticatesPlanningPrincipalAndRechecksAuthority,
		"SeinARTS.Unit.CoreEntity.FormationOrderToken")
	{
		using namespace FormationOrderTokenTestLocal;
		FFormationOrderWorld Fixture(1);
		ASSERT_THAT(IsNotNull(Fixture.World));

		FSeinFormationLayout InvalidLayout;
		USeinFormationOrderToken* InvalidToken = nullptr;
		ASSERT_THAT(IsTrue(
			USeinCommandBrokerBPFL::SeinPlanFormationOrder(
				Fixture.World, Fixture.Player, Fixture.Members,
				FGameplayTagContainer(), Position(1000), {}, FGameplayTag(),
				false, InvalidLayout, InvalidToken)
			== ESeinFormationOrderTokenResult::InvalidInput));
		ASSERT_THAT(IsNull(InvalidToken));
		FSeinLocalCommandSubmitter UnpairedSubmitter;
		UnpairedSubmitter.BindLambda(
			[](const FSeinCommand&, bool)
			{
				return true;
			});
		Fixture.World->SetLocalCommandSubmitter(MoveTemp(UnpairedSubmitter));
		ASSERT_THAT(IsTrue(
			USeinCommandBrokerBPFL::SeinPlanFormationOrder(
				Fixture.World, Fixture.Player, Fixture.Members,
				GroundContext(), Position(1000), {}, FGameplayTag(), false,
				InvalidLayout, InvalidToken)
			== ESeinFormationOrderTokenResult::Unauthorized));
		ASSERT_THAT(IsNull(InvalidToken));
		Fixture.World->ClearLocalCommandSubmitter();

		FSeinLocalCommandPrincipalResolver PrincipalResolver;
		PrincipalResolver.BindLambda(
			[Player = Fixture.Player](FSeinPlayerID)
			{
				return Player;
			});
		Fixture.World->SetLocalCommandPrincipalResolver(
			MoveTemp(PrincipalResolver));
		FSeinCommand SubmittedCommand;
		FSeinLocalCommandSubmitter CapturingSubmitter;
		CapturingSubmitter.BindLambda(
			[&SubmittedCommand](const FSeinCommand& Command, bool)
			{
				SubmittedCommand = Command;
				return true;
			});
		Fixture.World->SetLocalCommandSubmitter(MoveTemp(CapturingSubmitter));

		FSeinFormationLayout Layout;
		USeinFormationOrderToken* Token = nullptr;
		ASSERT_THAT(IsTrue(
			USeinCommandBrokerBPFL::SeinPlanFormationOrder(
				Fixture.World, FSeinPlayerID(2), Fixture.Members,
				GroundContext(), Position(1000), {}, FGameplayTag(), false,
				Layout, Token)
			== ESeinFormationOrderTokenResult::Success));
		ASSERT_THAT(IsTrue(
			USeinCommandBrokerBPFL::SeinIssueFormationOrder(
				Fixture.World, Token)
			== ESeinFormationOrderTokenResult::Success));
		ASSERT_THAT(IsTrue(SubmittedCommand.PlayerID == Fixture.Player));

		Fixture.World->ClearLocalCommandSubmitter();
		Fixture.World->ClearLocalCommandPrincipalResolver();
		USeinFormationOrderToken* AuthorityToken = nullptr;
		ASSERT_THAT(IsTrue(
			USeinCommandBrokerBPFL::SeinPlanFormationOrder(
				Fixture.World, Fixture.Player, Fixture.Members,
				GroundContext(), Position(2000), {}, FGameplayTag(), false,
				Layout, AuthorityToken)
			== ESeinFormationOrderTokenResult::Success));
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
			Fixture.World->SetEntityOwner(
				Fixture.Members[0], FSeinPlayerID(2));
		}
		ASSERT_THAT(IsTrue(
			USeinCommandBrokerBPFL::SeinIssueFormationOrder(
				Fixture.World, AuthorityToken)
			== ESeinFormationOrderTokenResult::Unauthorized));
		ASSERT_THAT(AreEqual(0, Fixture.World->GetPendingCommands().Num()));

		USeinFormationOrderToken* UnauthorizedToken = nullptr;
		ASSERT_THAT(IsTrue(
			USeinCommandBrokerBPFL::SeinPlanFormationOrder(
				Fixture.World, Fixture.Player, Fixture.Members,
				GroundContext(), Position(3000), {}, FGameplayTag(), false,
				Layout, UnauthorizedToken)
			== ESeinFormationOrderTokenResult::Unauthorized));
		ASSERT_THAT(IsNull(UnauthorizedToken));
	}

	TEST(FormationOrderTokenCarriesExactRequestWithoutPlanningState,
		"SeinARTS.Unit.CoreEntity.FormationOrderToken")
	{
		using namespace FormationOrderTokenTestLocal;
		FFormationOrderWorld Fixture(2);
		ASSERT_THAT(IsNotNull(Fixture.World));

		FGuid RootBefore;
		FString Error;
		ASSERT_THAT(IsTrue(Fixture.World->ComputeCanonicalStateRoot(
			RootBefore, Error)));
		const int32 HashBefore = Fixture.World->ComputeStateHash();

		const FGameplayTagContainer Context = GroundContext();
		const TArray<FFixedVector> GuidePoints{
			Position(800, -200), Position(1200, 200)};
		FSeinFormationLayout Layout;
		USeinFormationOrderToken* Token = nullptr;
		ASSERT_THAT(IsTrue(
			USeinCommandBrokerBPFL::SeinPlanFormationOrder(
				Fixture.World,
				Fixture.Player,
				Fixture.Members,
				Context,
				Position(1000),
				GuidePoints,
				SeinARTSTags::Formation_Box,
				true,
				Layout,
				Token)
			== ESeinFormationOrderTokenResult::Success));
		ASSERT_THAT(IsNotNull(Token));
		ASSERT_THAT(AreEqual(2, Layout.Positions.Num()));
		ASSERT_THAT(AreEqual(0, Fixture.World->GetPendingCommands().Num()));

		FGuid RootAfter;
		ASSERT_THAT(IsTrue(Fixture.World->ComputeCanonicalStateRoot(
			RootAfter, Error)));
		ASSERT_THAT(IsTrue(RootBefore == RootAfter));
		ASSERT_THAT(AreEqual(HashBefore, Fixture.World->ComputeStateHash()));

		FActorTestSpawner OtherSpawner;
		USeinWorldSubsystem* OtherWorld = OtherSpawner.GetWorld()
			.GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(OtherWorld));
		ASSERT_THAT(IsTrue(
			USeinCommandBrokerBPFL::SeinIssueFormationOrder(
				OtherWorld, Token)
			== ESeinFormationOrderTokenResult::WrongWorld));

		ASSERT_THAT(IsTrue(
			USeinCommandBrokerBPFL::SeinIssueFormationOrder(
				Fixture.World, Token)
			== ESeinFormationOrderTokenResult::Success));
		ASSERT_THAT(AreEqual(1, Fixture.World->GetPendingCommands().Num()));
		const FSeinCommand& Command =
			Fixture.World->GetPendingCommands().GetCommands()[0];
		ASSERT_THAT(IsTrue(Command.PlayerID == Fixture.Player));
		ASSERT_THAT(IsTrue(Command.EntityList == Fixture.Members));
		ASSERT_THAT(IsTrue(Command.TargetLocation == Position(1000)));
		ASSERT_THAT(IsTrue(Command.bQueueCommand));
		ASSERT_THAT(IsFalse(Command.TargetEntity.IsValid()));
		const FSeinBrokerOrderPayload* Payload =
			Command.Payload.GetPtr<FSeinBrokerOrderPayload>();
		ASSERT_THAT(IsNotNull(Payload));
		ASSERT_THAT(IsTrue(Payload->CommandContext == Context));
		ASSERT_THAT(IsTrue(Payload->GuidePoints == GuidePoints));
		ASSERT_THAT(IsTrue(
			Payload->FormationTag == SeinARTSTags::Formation_Box));
		ASSERT_THAT(AreEqual(2, Payload->RecipientPlan.Num()));
		for (int32 Index = 0; Index < Payload->RecipientPlan.Num(); ++Index)
		{
			ASSERT_THAT(IsTrue(
				Payload->RecipientPlan[Index].Recipient
					== Fixture.Members[Index]));
			ASSERT_THAT(AreEqual(
				1, Payload->RecipientPlan[Index].MemberCount));
		}
		ASSERT_THAT(AreEqual(2, Payload->DestinationArtifact.Num()));
		for (int32 Index = 0; Index < Layout.Positions.Num(); ++Index)
		{
			ASSERT_THAT(IsTrue(
				Payload->DestinationArtifact[Index].Member
					== Fixture.Members[Index]));
			ASSERT_THAT(IsTrue(
				Payload->DestinationArtifact[Index].WorldPosition
					== Layout.Positions[Index]));
		}

		ASSERT_THAT(IsTrue(
			USeinCommandBrokerBPFL::SeinIssueFormationOrder(
				Fixture.World, Token)
			== ESeinFormationOrderTokenResult::AlreadyIssued));
		ASSERT_THAT(AreEqual(1, Fixture.World->GetPendingCommands().Num()));
	}

	TEST(FormationOrderTokenAllowsDeadEntriesButRejectsLiveMembershipDrift,
		"SeinARTS.Unit.CoreEntity.FormationOrderToken")
	{
		using namespace FormationOrderTokenTestLocal;
		FFormationOrderWorld Fixture(3);
		ASSERT_THAT(IsNotNull(Fixture.World));

		FSeinEntityHandle Broker;
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
			Broker = Fixture.World->SpawnAbstractEntity(
				FFixedTransform(), Fixture.Player);
			FSeinCommandBrokerData BrokerData;
			BrokerData.Members = {
				Fixture.Members[0], Fixture.Members[1]};
			Fixture.World->AddComponent(Broker, BrokerData);
		}

		FSeinFormationLayout DriftLayout;
		USeinFormationOrderToken* DriftToken = nullptr;
		ASSERT_THAT(IsTrue(
			USeinCommandBrokerBPFL::SeinPlanFormationOrder(
				Fixture.World,
				Fixture.Player,
				{Broker},
				GroundContext(),
				Position(1000),
				{},
				FGameplayTag(),
				false,
				DriftLayout,
				DriftToken)
			== ESeinFormationOrderTokenResult::Success));
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
			FSeinCommandBrokerData* BrokerData =
				Fixture.World->GetComponentMutable<FSeinCommandBrokerData>(
					Broker);
			ASSERT_THAT(IsNotNull(BrokerData));
			BrokerData->Members = {
				Fixture.Members[0], Fixture.Members[2]};
		}
		ASSERT_THAT(IsTrue(
			USeinCommandBrokerBPFL::SeinIssueFormationOrder(
				Fixture.World, DriftToken)
			== ESeinFormationOrderTokenResult::StaleRecipients));
		ASSERT_THAT(AreEqual(0, Fixture.World->GetPendingCommands().Num()));

		FSeinEntityHandle FirstBroker;
		FSeinEntityHandle SecondBroker;
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
			FirstBroker = Fixture.World->SpawnAbstractEntity(
				FFixedTransform(), Fixture.Player);
			SecondBroker = Fixture.World->SpawnAbstractEntity(
				FFixedTransform(), Fixture.Player);
			FSeinCommandBrokerData FirstData;
			FirstData.Members = {Fixture.Members[0]};
			Fixture.World->AddComponent(FirstBroker, FirstData);
			FSeinCommandBrokerData SecondData;
			SecondData.Members = {
				Fixture.Members[1], Fixture.Members[2]};
			Fixture.World->AddComponent(SecondBroker, SecondData);
		}
		FSeinFormationLayout BoundaryLayout;
		USeinFormationOrderToken* BoundaryToken = nullptr;
		ASSERT_THAT(IsTrue(
			USeinCommandBrokerBPFL::SeinPlanFormationOrder(
				Fixture.World, Fixture.Player,
				{FirstBroker, SecondBroker}, GroundContext(), Position(1500),
				{}, FGameplayTag(), false, BoundaryLayout, BoundaryToken)
			== ESeinFormationOrderTokenResult::Success));
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
			Fixture.World->GetComponentMutable<FSeinCommandBrokerData>(
				FirstBroker)->Members = {
					Fixture.Members[0], Fixture.Members[1]};
			Fixture.World->GetComponentMutable<FSeinCommandBrokerData>(
				SecondBroker)->Members = {Fixture.Members[2]};
		}
		ASSERT_THAT(IsTrue(
			USeinCommandBrokerBPFL::SeinIssueFormationOrder(
				Fixture.World, BoundaryToken)
			== ESeinFormationOrderTokenResult::StaleRecipients));
		ASSERT_THAT(AreEqual(0, Fixture.World->GetPendingCommands().Num()));

		FSeinFormationLayout SurvivorLayout;
		USeinFormationOrderToken* SurvivorToken = nullptr;
		ASSERT_THAT(IsTrue(
			USeinCommandBrokerBPFL::SeinPlanFormationOrder(
				Fixture.World,
				Fixture.Player,
				Fixture.Members,
				GroundContext(),
				Position(2000),
				{},
				FGameplayTag(),
				false,
				SurvivorLayout,
				SurvivorToken)
			== ESeinFormationOrderTokenResult::Success));
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
			Fixture.World->DestroyEntity(Fixture.Members[1]);
		}
		ASSERT_THAT(IsTrue(
			USeinCommandBrokerBPFL::SeinIssueFormationOrder(
				Fixture.World, SurvivorToken)
			== ESeinFormationOrderTokenResult::Success));
		ASSERT_THAT(AreEqual(1, Fixture.World->GetPendingCommands().Num()));
		const FSeinBrokerOrderPayload* SurvivorPayload =
			Fixture.World->GetPendingCommands().GetCommands()[0]
				.Payload.GetPtr<FSeinBrokerOrderPayload>();
		ASSERT_THAT(IsNotNull(SurvivorPayload));
		ASSERT_THAT(AreEqual(
			3, SurvivorPayload->DestinationArtifact.Num()));
	}

	TEST(FormationOrderTokenRejectsOverlappingRecipientExpansion,
		"SeinARTS.Unit.CoreEntity.FormationOrderToken")
	{
		using namespace FormationOrderTokenTestLocal;
		FFormationOrderWorld Fixture(1);
		ASSERT_THAT(IsNotNull(Fixture.World));

		FSeinEntityHandle Broker;
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
			Broker = Fixture.World->SpawnAbstractEntity(
				FFixedTransform(), Fixture.Player);
			FSeinCommandBrokerData BrokerData;
			BrokerData.Members = {Fixture.Members[0]};
			Fixture.World->AddComponent(Broker, BrokerData);
		}

		FSeinFormationLayout Layout;
		USeinFormationOrderToken* Token = nullptr;
		ASSERT_THAT(IsTrue(
			USeinCommandBrokerBPFL::SeinPlanFormationOrder(
				Fixture.World,
				Fixture.Player,
				{Broker, Fixture.Members[0]},
				GroundContext(),
				Position(1000),
				{},
				FGameplayTag(),
				false,
				Layout,
				Token)
			== ESeinFormationOrderTokenResult::InvalidInput));
		ASSERT_THAT(IsNull(Token));
		ASSERT_THAT(IsTrue(Layout.Positions.IsEmpty()));
	}

	TEST(FormationOrderTokenBecomesStaleAfterSameWorldRestore,
		"SeinARTS.Unit.CoreEntity.FormationOrderToken")
	{
		using namespace FormationOrderTokenTestLocal;
		FFormationOrderWorld Fixture(1);
		ASSERT_THAT(IsNotNull(Fixture.World));

		FSeinFormationLayout Layout;
		USeinFormationOrderToken* Token = nullptr;
		ASSERT_THAT(IsTrue(
			USeinCommandBrokerBPFL::SeinPlanFormationOrder(
				Fixture.World,
				Fixture.Player,
				Fixture.Members,
				GroundContext(),
				Position(1000),
				{},
				FGameplayTag(),
				false,
				Layout,
				Token)
			== ESeinFormationOrderTokenResult::Success));

		FSeinWorldSnapshot Snapshot;
		FSeinWorldSnapshotReferenceGuard SnapshotGuard(Snapshot);
		Fixture.World->CaptureSnapshot(Snapshot);
		Fixture.World->StopSimulation();
		FString Error;
		ASSERT_THAT(IsTrue(SeinTestSnapshotRestore::RestoreTrusted(
			*Fixture.World, Snapshot, &Error)));
		ASSERT_THAT(IsTrue(Fixture.World->IsSimulationRunning()));
		ASSERT_THAT(IsTrue(
			USeinCommandBrokerBPFL::SeinIssueFormationOrder(
				Fixture.World, Token)
			== ESeinFormationOrderTokenResult::StaleSession));
		ASSERT_THAT(AreEqual(0, Fixture.World->GetPendingCommands().Num()));
	}
}
