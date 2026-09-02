/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         CoverFrozenDestinationReplayTests.cpp
 * @author       RJ Macklem
 * @created      22 Aug 2026
 * @latest       22 Aug 2026
 * @brief        Qualifies frozen Cover destinations through command replay.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Cover/CoverFrozenDestinationReplayTestTypes.h"
#include "Abilities/SeinMoveToProxy.h"
#include "Brokers/SeinBrokerTypes.h"
#include "Brokers/SeinDefaultCommandBrokerResolver.h"
#include "Components/SeinAbilityPayload.h"
#include "Components/SeinBrokerMembershipData.h"
#include "Components/SeinCommandBrokerData.h"
#include "Components/SeinCoverPayload.h"
#include "Components/SeinExtentsPayload.h"
#include "Components/SeinMovementPayload.h"
#include "Components/SeinNavigationPayload.h"
#include "Containers/Ticker.h"
#include "Data/SeinMatchSettings.h"
#include "Data/SeinReplayHeader.h"
#include "Data/SeinWorldSnapshot.h"
#include "HAL/FileManager.h"
#include "Input/SeinCommand.h"
#include "Lib/SeinAbilityBPFL.h"
#include "Lib/SeinCommandBrokerBPFL.h"
#include "Movement/SeinBasicUnitMovement.h"
#include "Movement/SeinVehicleGymTestTypes.h"
#include "SeinReplayReader.h"
#include "SeinReplayWriter.h"
#include "Settings/PluginSettings.h"
#include "Settings/SeinARTSCoverSettings.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSnapshotRestore.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "System/SeinCoverDefault.h"
#include "System/SeinCoverSubsystem.h"
#include "System/SeinCoverSystem.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "Tags/SeinCoverGameplayTags.h"

USeinCoverFrozenDestinationReplayMoveAbility::
	USeinCoverFrozenDestinationReplayMoveAbility()
{
	AbilityTag = SeinARTSTags::Command_Context_Target_Ground;
	TargetType = ESeinAbilityTargetType::Point;
	bIsMoveAbility = true;
}

void USeinCoverFrozenDestinationReplayMoveAbility::OnActivate_Implementation()
{
	USeinMoveToProxy* Proxy = USeinMoveToProxy::SeinMoveTo(this, TargetLocation);
	if (!Proxy)
	{
		EndAbility();
		return;
	}
	Proxy->Activate();
}

void USeinCoverFrozenDestinationReplayMoveAbility::OnTick_Implementation(
	FFixedPoint)
{
	const FSeinEntity* Entity = WorldSubsystem
		? WorldSubsystem->GetEntity(OwnerEntity)
		: nullptr;
	if (bIsActive && Entity
		&& Entity->Transform.GetLocation() == TargetLocation)
	{
		EndAbility();
	}
}

namespace
{
	const FSeinPlayerID CoverReplayPlayer(1);
	const FFixedVector DisplayedCoverSlot(
		FFixedPoint::FromInt(120) + FFixedPoint::Half,
		FFixedPoint::Zero,
		FFixedPoint::Zero);
	const FFixedVector MovedCoverSlot(
		FFixedPoint::FromInt(240) + FFixedPoint::Half,
		FFixedPoint::Zero,
		FFixedPoint::Zero);

	struct FScopedCoverReplaySettings
	{
		FScopedCoverReplaySettings()
			: CoreSettings(GetMutableDefault<USeinARTSCoreSettings>())
			, CoverSettings(GetMutableDefault<USeinARTSCoverSettings>())
			, SavedNavigationClass(CoreSettings->NavigationClass)
			, SavedBrokerResolverClass(
				CoreSettings->DefaultBrokerResolverClass)
			, bSavedAsyncPathfinding(CoreSettings->bAsyncPathfinding)
			, SavedPathBudget(CoreSettings->PathRequestsPerTickBudget)
			, bSavedIdleReseek(CoreSettings->bIdleReseek)
			, SavedCoverSystemClass(CoverSettings->CoverSystemClass)
			, SavedCoverSnapRadius(CoverSettings->CoverSnapRadius)
		{
			FSeinVehicleGymNavigationRecipe Recipe;
			Recipe.ScenarioId = TEXT("CoverFrozenDestinationReplay");
			USeinVehicleGymNavigation::InstallRecipe(Recipe);
			CoreSettings->NavigationClass = FSoftClassPath(
				USeinVehicleGymNavigation::StaticClass());
			CoreSettings->DefaultBrokerResolverClass = FSoftClassPath(
				USeinDefaultCommandBrokerResolver::StaticClass());
			CoreSettings->bAsyncPathfinding = false;
			CoreSettings->PathRequestsPerTickBudget = 1024;
			CoreSettings->bIdleReseek = false;
			CoreSettings->ApplySimPerformanceCvars();
			CoverSettings->CoverSystemClass = FSoftClassPath(
				USeinCoverDefault::StaticClass());
			CoverSettings->CoverSnapRadius = FFixedPoint::FromInt(500);
		}

		~FScopedCoverReplaySettings()
		{
			CoreSettings->NavigationClass = SavedNavigationClass;
			CoreSettings->DefaultBrokerResolverClass =
				SavedBrokerResolverClass;
			CoreSettings->bAsyncPathfinding = bSavedAsyncPathfinding;
			CoreSettings->PathRequestsPerTickBudget = SavedPathBudget;
			CoreSettings->bIdleReseek = bSavedIdleReseek;
			CoreSettings->ApplySimPerformanceCvars();
			CoverSettings->CoverSystemClass = SavedCoverSystemClass;
			CoverSettings->CoverSnapRadius = SavedCoverSnapRadius;
			USeinVehicleGymNavigation::ResetRecipe();
		}

		USeinARTSCoreSettings* CoreSettings = nullptr;
		USeinARTSCoverSettings* CoverSettings = nullptr;
		FSoftClassPath SavedNavigationClass;
		TSoftClassPtr<USeinCommandBrokerResolver> SavedBrokerResolverClass;
		bool bSavedAsyncPathfinding = false;
		int32 SavedPathBudget = 0;
		bool bSavedIdleReseek = false;
		FSoftClassPath SavedCoverSystemClass;
		FFixedPoint SavedCoverSnapRadius;
	};

	struct FScopedCoverReplayFile
	{
		FString Path;

		~FScopedCoverReplayFile()
		{
			if (!Path.IsEmpty())
			{
				IFileManager::Get().Delete(*Path, false, true);
			}
		}
	};

	FSeinMatchSettings MakeCoverReplayMatchSettings()
	{
		FSeinMatchSettings Settings;
		FSeinMatchSlot& Slot = Settings.Slots.AddDefaulted_GetRef();
		Slot.SlotIndex = CoverReplayPlayer.Value;
		Slot.State = ESeinSlotState::Human;
		Slot.FactionID = FSeinFactionID(1);
		return Settings;
	}

	FSeinReplayHeader MakeCoverReplayHeader(USeinWorldSubsystem& World)
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

	FSeinCommand MakeCoverBrokerOrder(
		FSeinEntityHandle Member,
		const TArray<FSeinFrozenDestination>& Artifact,
		int32 CommandTick)
	{
		FSeinBrokerOrderPayload Payload;
		Payload.CommandContext.AddTag(
			SeinARTSTags::Command_Context_RightClick);
		Payload.CommandContext.AddTag(
			SeinARTSTags::Command_Context_Target_Ground);
		FSeinBrokerRecipientPlanSegment& Segment =
			Payload.RecipientPlan.AddDefaulted_GetRef();
		Segment.Recipient = Member;
		Segment.MemberCount = Artifact.Num();
		Payload.DestinationArtifact = Artifact;

		FSeinCommand Command;
		Command.Tick = CommandTick;
		Command.PlayerID = CoverReplayPlayer;
		Command.IssuerKind = ESeinCommandIssuerKind::Player;
		Command.CommandType = SeinARTSTags::Command_Type_BrokerOrder;
		Command.SchemaVersion = SeinBrokerOrderProtocol::SchemaVersion;
		Command.TargetLocation = DisplayedCoverSlot;
		Command.EntityList = {Member};
		Command.Payload = FInstancedStruct::Make(Payload);
		return Command;
	}

	bool ComputeCoverReplayRoot(USeinWorldSubsystem& World, FGuid& OutRoot)
	{
		FString Error;
		if (World.ComputeCanonicalStateRoot(OutRoot, Error))
		{
			return true;
		}
		UE_LOG(LogTemp, Error,
			TEXT("Cover replay canonical-root capture failed: %s"),
			*Error);
		return false;
	}
}

TEST(CoverFrozenBrokerOrderReplaysEveryReservationLifecycleTick,
	"SeinARTS.Determinism.Cover.FrozenDestinationReplay")
{
	FScopedCoverReplaySettings SettingsScope;
	FActorTestSpawner SourceSpawner;
	USeinWorldSubsystem* Source =
		SourceSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
	USeinCoverSubsystem* CoverSubsystem =
		SourceSpawner.GetWorld().GetSubsystem<USeinCoverSubsystem>();
	USeinCoverSystem* CoverSystem = CoverSubsystem
		? CoverSubsystem->GetCoverSystem()
		: nullptr;
	ASSERT_THAT(IsNotNull(Source));
	ASSERT_THAT(IsNotNull(CoverSystem));
	Source->NavProjectResolver.Unbind();
	Source->DynamicPassableResolver.Unbind();

	FSeinEntityHandle Member;
	FSeinEntityHandle Provider;
	int32 AbilityID = INDEX_NONE;
	FString Error;
	TArray<FSeinFrozenDestination> Artifact;
	bool bPlanSucceeded = false;
	ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
		*Source,
		[&]()
		{
			Source->RegisterPlayer(CoverReplayPlayer, FSeinFactionID(1));
			Member = Source->SpawnAbstractEntity(
				FFixedTransform(), CoverReplayPlayer);
			FSeinExtentsShape Shape;
			Shape.Shape = ESeinExtentsShape::Capsule;
			Shape.Radius = FFixedPoint::FromInt(20);
			FSeinExtentsPayload Extents;
			Extents.Shapes.Add(Shape);
			Source->AddComponent(Member, Extents);
			FSeinMovementPayload Movement;
			Movement.MovementClass = FSoftClassPath(
				USeinBasicUnitMovement::StaticClass());
			Movement.TopSpeed = FFixedPoint::FromInt(300);
			Movement.TurnRate = FFixedPoint::FromInt(10);
			Source->AddComponent(Member, Movement);
			FSeinNavigationPayload Navigation;
			Navigation.FallbackFootprintRadius = FFixedPoint::FromInt(20);
			Navigation.AcceptanceRadius = FFixedPoint::FromInt(1);
			Navigation.RepathMode = ESeinRepathMode::OffPathOnly;
			Navigation.OffPathThreshold = FFixedPoint::FromInt(10000);
			Source->AddComponent(Member, Navigation);
			FSeinAbilityPayload Abilities;
			Abilities.FallbackAbilityTag =
				SeinARTSTags::Command_Context_Target_Ground;
			Source->AddComponent(Member, Abilities);
			AbilityID = USeinAbilityBPFL::SeinGrantAbility(
				Source,
				Member,
				USeinCoverFrozenDestinationReplayMoveAbility::StaticClass());
			Source->GrantTag(Member, SeinCoverTags::Cover_UsesCover);

			Provider = Source->SpawnAbstractEntity(
				FFixedTransform(DisplayedCoverSlot),
				CoverReplayPlayer);
			FSeinCoverPayload Cover;
			Cover.QualityTag = SeinCoverTags::Cover_Light;
			Cover.SlotRadius = FFixedPoint::FromInt(10);
			Cover.Slots.Add(FFixedVector::ZeroVector);
			Source->AddComponent(Provider, Cover);
			CoverSystem->RegisterAuthoritativeProvider(Provider);

			USeinCommandBrokerBPFL::ComputeFormationDestinationArtifact(
				Source,
				{Member},
				DisplayedCoverSlot,
				{},
				FGameplayTag(),
				CoverReplayPlayer,
				false,
				Artifact,
				&bPlanSucceeded);
			if (FSeinEntity* MutableProvider =
				Source->GetEntityMutable(Provider))
			{
				MutableProvider->Transform.SetLocation(MovedCoverSlot);
			}
		},
		MakeCoverReplayMatchSettings(),
		0x4352504C,
		TEXT("SeinARTS.Cover.FrozenDestinationReplay"),
		&Error)));
	ASSERT_THAT(IsTrue(Member.IsValid()));
	ASSERT_THAT(IsTrue(Provider.IsValid()));
	ASSERT_THAT(IsTrue(AbilityID != INDEX_NONE));
	ASSERT_THAT(IsTrue(bPlanSucceeded));
	ASSERT_THAT(AreEqual(1, Artifact.Num()));
	ASSERT_THAT(IsTrue(Artifact[0].Member == Member));
	ASSERT_THAT(IsTrue(Artifact[0].WorldPosition == DisplayedCoverSlot));
	ASSERT_THAT(IsTrue(Artifact[0].bReserveFootprint));
	ASSERT_THAT(IsTrue(Artifact[0].SourceEntity == Provider));
	ASSERT_THAT(AreEqual(0, Artifact[0].SourceIndex));
	ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Authorize(*Source, &Error)));

	USeinReplayWriter* Writer = NewObject<USeinReplayWriter>(Source);
	ASSERT_THAT(IsNotNull(Writer));
	Writer->StartRecording(MakeCoverReplayHeader(*Source));
	ASSERT_THAT(IsTrue(Writer->IsRecording()));
	ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*Source, &Error)));
	ASSERT_THAT(IsTrue(Writer->CaptureCheckpoint(/*bRequired=*/true)));

	const USeinARTSCoreSettings* CoreSettings =
		GetDefault<USeinARTSCoreSettings>();
	ASSERT_THAT(IsNotNull(CoreSettings));
	const int32 TicksPerTurn = CoreSettings->TurnRate > 0
		? FMath::Max(
			1,
			CoreSettings->SimulationTickRate / CoreSettings->TurnRate)
		: 1;
	const int32 FirstTurn = CoreSettings->InputDelayTurns > 0
		? CoreSettings->InputDelayTurns
		: 3;
	const int32 CommandTick = FirstTurn * TicksPerTurn;
	const int32 MaxEndTick = CommandTick
		+ FMath::Max(60, CoreSettings->SimulationTickRate * 10);
	const FSeinCommand Command =
		MakeCoverBrokerOrder(Member, Artifact, CommandTick);
	Writer->RecordTurn(FirstTurn, {Command});
	ASSERT_THAT(IsTrue(Writer->IsRecording()));

	TArray<FGuid> SourceRoots;
	SourceRoots.SetNum(MaxEndTick + 1);
	FSeinWorldSnapshot MidMovementSnapshot;
	FSeinWorldSnapshotReferenceGuard MidMovementSnapshotGuard(
		MidMovementSnapshot);
	bool bSawReservedInFlight = false;
	int32 MidMovementTick = INDEX_NONE;
	int32 EndTick = INDEX_NONE;
	for (int32 ExpectedTick = 1; ExpectedTick <= MaxEndTick; ++ExpectedTick)
	{
		if (ExpectedTick > CommandTick
			&& (ExpectedTick - 1) % TicksPerTurn == 0)
		{
			const int32 Turn =
				(ExpectedTick - 1) / TicksPerTurn + 1;
			Writer->RecordTurn(Turn, {});
			ASSERT_THAT(IsTrue(Writer->IsRecording()));
		}
		if (ExpectedTick == CommandTick)
		{
			Source->SubmitLocalCommandDraft(Command);
		}
		FTSTicker::GetCoreTicker().Tick(
			Source->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(AreEqual(ExpectedTick, Source->GetCurrentTick()));
		ASSERT_THAT(IsTrue(ComputeCoverReplayRoot(
			*Source, SourceRoots[ExpectedTick])));
		Writer->ObserveCompletedTick(ExpectedTick);

		const FSeinEntity* MovingMember = Source->GetEntity(Member);
		ASSERT_THAT(IsNotNull(MovingMember));
		const FSeinBrokerMembershipData* Membership =
			Source->GetComponent<FSeinBrokerMembershipData>(Member);
		bool bSettledAtDisplayedSlot = false;
		if (Membership)
		{
			const FSeinCommandBrokerData* Broker =
				Source->GetComponent<FSeinCommandBrokerData>(
					Membership->CurrentBrokerHandle);
			if (Broker && !Broker->OrderQueue.IsEmpty()
				&& MovingMember->Transform.GetLocation()
					!= FFixedVector::ZeroVector
				&& MovingMember->Transform.GetLocation()
					!= DisplayedCoverSlot)
			{
				const FSeinBrokerQueuedOrder& Active = Broker->OrderQueue[0];
				const bool bReservedInFlight = Active.bIsExecuting
					&& Active.DestinationArtifact.Num() == 1
					&& Active.DestinationArtifact[0].bReserveFootprint
					&& Active.DestinationArtifact[0].WorldPosition
						== DisplayedCoverSlot
					&& Broker->SettledDestinationArtifact.IsEmpty()
					&& Source->IsDestinationFootprintReserved(
						DisplayedCoverSlot,
						Active.DestinationArtifact[0].FootprintRadius);
				bSawReservedInFlight = bSawReservedInFlight
					|| bReservedInFlight;
				if (bReservedInFlight && MidMovementTick == INDEX_NONE)
				{
					Source->CaptureSnapshot(MidMovementSnapshot);
					ASSERT_THAT(AreEqual(
						FSeinWorldSnapshot::CurrentVersion,
						MidMovementSnapshot.SnapshotVersion));
					MidMovementTick = ExpectedTick;
				}
			}
			bSettledAtDisplayedSlot = Broker
				&& Broker->OrderQueue.IsEmpty()
				&& Broker->SettledDestinationArtifact.Num() == 1
				&& Broker->SettledDestinationArtifact[0].Member == Member
				&& Broker->SettledDestinationArtifact[0].WorldPosition
					== DisplayedCoverSlot
				&& MovingMember->Transform.GetLocation()
					== DisplayedCoverSlot;
		}
		if (bSettledAtDisplayedSlot)
		{
			EndTick = ExpectedTick;
			break;
		}
	}
	ASSERT_THAT(IsTrue(bSawReservedInFlight));
	ASSERT_THAT(IsTrue(MidMovementTick != INDEX_NONE));
	if (EndTick == INDEX_NONE)
	{
		const FSeinEntity* TimedOutMember = Source->GetEntity(Member);
		UE_LOG(LogTemp, Error,
			TEXT("Cover replay source did not settle by tick %d (location=%s)."),
			MaxEndTick,
			TimedOutMember
				? *TimedOutMember->Transform.GetLocation().ToString()
				: TEXT("<missing>"));
	}
	ASSERT_THAT(IsTrue(EndTick != INDEX_NONE));

	const FSeinEntity* SettledMember = Source->GetEntity(Member);
	const FSeinEntity* MovedProvider = Source->GetEntity(Provider);
	const FSeinBrokerMembershipData* SourceMembership =
		Source->GetComponent<FSeinBrokerMembershipData>(Member);
	ASSERT_THAT(IsNotNull(SettledMember));
	ASSERT_THAT(IsNotNull(MovedProvider));
	ASSERT_THAT(IsNotNull(SourceMembership));
	ASSERT_THAT(IsTrue(
		SettledMember->Transform.GetLocation() == DisplayedCoverSlot));
	ASSERT_THAT(IsTrue(
		MovedProvider->Transform.GetLocation() == MovedCoverSlot));
	const FSeinCommandBrokerData* SourceBroker =
		Source->GetComponent<FSeinCommandBrokerData>(
			SourceMembership->CurrentBrokerHandle);
	ASSERT_THAT(IsNotNull(SourceBroker));
	ASSERT_THAT(IsTrue(SourceBroker->OrderQueue.IsEmpty()));
	ASSERT_THAT(AreEqual(1, SourceBroker->SettledDestinationArtifact.Num()));
	ASSERT_THAT(IsTrue(
		SourceBroker->SettledDestinationArtifact[0].WorldPosition
			== DisplayedCoverSlot));
	ASSERT_THAT(IsTrue(
		SourceBroker->SettledDestinationArtifact[0].SourceEntity == Provider));
	ASSERT_THAT(IsTrue(
		SourceBroker->SettledDestinationArtifact[0].bReserveFootprint));
	const int32 SourceHash = Source->ComputeStateHash();
	Source->StopSimulation();

	FScopedCoverReplayFile ReplayFile{Writer->FinishRecording()};
	ASSERT_THAT(IsFalse(ReplayFile.Path.IsEmpty()));
	ASSERT_THAT(IsTrue(IFileManager::Get().FileExists(*ReplayFile.Path)));

	FActorTestSpawner CheckpointSpawner;
	USeinWorldSubsystem* CheckpointTarget =
		CheckpointSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
	ASSERT_THAT(IsNotNull(CheckpointTarget));
	ASSERT_THAT(IsTrue(SeinTestSnapshotRestore::RestoreTrusted(
		*CheckpointTarget, MidMovementSnapshot, &Error)));
	ASSERT_THAT(AreEqual(
		MidMovementTick, CheckpointTarget->GetCurrentTick()));
	FGuid CheckpointRoot;
	ASSERT_THAT(IsTrue(ComputeCoverReplayRoot(
		*CheckpointTarget, CheckpointRoot)));
	ASSERT_THAT(IsTrue(SourceRoots[MidMovementTick] == CheckpointRoot));
	const FSeinBrokerMembershipData* RestoredMembership =
		CheckpointTarget->GetComponent<FSeinBrokerMembershipData>(Member);
	ASSERT_THAT(IsNotNull(RestoredMembership));
	const FSeinCommandBrokerData* RestoredBroker =
		CheckpointTarget->GetComponent<FSeinCommandBrokerData>(
			RestoredMembership->CurrentBrokerHandle);
	ASSERT_THAT(IsNotNull(RestoredBroker));
	ASSERT_THAT(AreEqual(1, RestoredBroker->OrderQueue.Num()));
	ASSERT_THAT(IsTrue(RestoredBroker->OrderQueue[0].bIsExecuting));
	ASSERT_THAT(AreEqual(
		1, RestoredBroker->OrderQueue[0].DestinationArtifact.Num()));
	ASSERT_THAT(IsTrue(
		RestoredBroker->SettledDestinationArtifact.IsEmpty()));
	ASSERT_THAT(IsTrue(CheckpointTarget->IsDestinationFootprintReserved(
		DisplayedCoverSlot,
		RestoredBroker->OrderQueue[0]
			.DestinationArtifact[0].FootprintRadius)));
	for (int32 ExpectedTick = MidMovementTick + 1;
		ExpectedTick <= EndTick;
		++ExpectedTick)
	{
		FTSTicker::GetCoreTicker().Tick(
			CheckpointTarget->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(AreEqual(
			ExpectedTick, CheckpointTarget->GetCurrentTick()));
		ASSERT_THAT(IsTrue(ComputeCoverReplayRoot(
			*CheckpointTarget, CheckpointRoot)));
		ASSERT_THAT(IsTrue(SourceRoots[ExpectedTick] == CheckpointRoot));
	}
	const FSeinEntity* CheckpointMember =
		CheckpointTarget->GetEntity(Member);
	RestoredMembership =
		CheckpointTarget->GetComponent<FSeinBrokerMembershipData>(Member);
	ASSERT_THAT(IsNotNull(CheckpointMember));
	ASSERT_THAT(IsNotNull(RestoredMembership));
	RestoredBroker = CheckpointTarget->GetComponent<FSeinCommandBrokerData>(
		RestoredMembership->CurrentBrokerHandle);
	ASSERT_THAT(IsNotNull(RestoredBroker));
	ASSERT_THAT(IsTrue(
		CheckpointMember->Transform.GetLocation() == DisplayedCoverSlot));
	ASSERT_THAT(IsTrue(RestoredBroker->OrderQueue.IsEmpty()));
	ASSERT_THAT(AreEqual(
		1, RestoredBroker->SettledDestinationArtifact.Num()));
	ASSERT_THAT(IsTrue(
		RestoredBroker->SettledDestinationArtifact[0].WorldPosition
			== DisplayedCoverSlot));
	CheckpointTarget->StopSimulation();

	for (int32 ExpectedTick = 1; ExpectedTick <= EndTick; ++ExpectedTick)
	{
		FActorTestSpawner TargetSpawner;
		USeinWorldSubsystem* Target =
			TargetSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Target));
		USeinReplayReader* Reader =
			NewObject<USeinReplayReader>(&TargetSpawner.GetWorld());
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
		ASSERT_THAT(AreEqual(ExpectedTick, Target->GetCurrentTick()));
		if (Reader->IsPlaying())
		{
			Reader->Stop();
		}
		ASSERT_THAT(IsFalse(Reader->IsPlaying()));
		if (!Target->IsSimulationRunning())
		{
			ASSERT_THAT(IsTrue(Target->StartSimulation()));
		}

		FGuid TargetRoot;
		ASSERT_THAT(IsTrue(ComputeCoverReplayRoot(*Target, TargetRoot)));
		ASSERT_THAT(IsTrue(SourceRoots[ExpectedTick] == TargetRoot));
		if (ExpectedTick == EndTick)
		{
			ASSERT_THAT(AreEqual(SourceHash, Target->ComputeStateHash()));
			const FSeinEntity* TargetMember = Target->GetEntity(Member);
			const FSeinEntity* TargetProvider = Target->GetEntity(Provider);
			const FSeinBrokerMembershipData* TargetMembership =
				Target->GetComponent<FSeinBrokerMembershipData>(Member);
			ASSERT_THAT(IsNotNull(TargetMember));
			ASSERT_THAT(IsNotNull(TargetProvider));
			ASSERT_THAT(IsNotNull(TargetMembership));
			ASSERT_THAT(IsTrue(
				TargetMember->Transform.GetLocation() == DisplayedCoverSlot));
			ASSERT_THAT(IsTrue(
				TargetProvider->Transform.GetLocation() == MovedCoverSlot));
			const FSeinCommandBrokerData* TargetBroker =
				Target->GetComponent<FSeinCommandBrokerData>(
					TargetMembership->CurrentBrokerHandle);
			ASSERT_THAT(IsNotNull(TargetBroker));
			ASSERT_THAT(IsTrue(TargetBroker->OrderQueue.IsEmpty()));
			ASSERT_THAT(AreEqual(
				1, TargetBroker->SettledDestinationArtifact.Num()));
			ASSERT_THAT(IsTrue(
				TargetBroker->SettledDestinationArtifact[0].WorldPosition
					== DisplayedCoverSlot));
			ASSERT_THAT(IsTrue(
				TargetBroker->SettledDestinationArtifact[0].SourceEntity
					== Provider));
		}
		Target->StopSimulation();
	}
}
