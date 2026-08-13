#include "SeinConsumerQualificationSubsystem.h"

#include "Containers/Ticker.h"
#include "Components/SeinMovementComponent.h"
#include "Core/SeinEntityPool.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Data/SeinRelationshipTypes.h"
#include "Input/SeinCommand.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "SeinLobbyState.h"
#include "SeinLobbySubsystem.h"
#include "SeinNetSubsystem.h"
#include "SeinReplayReader.h"
#include "SeinReplayWriter.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "StructUtils/InstancedStruct.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "Types/Vector.h"

#if defined(SEIN_CONSUMER_QUALIFY_MOVEMENT_PLUS)
#include "Lib/SeinMovementPlusBPFL.h"
#include "Movement/SeinMovement.h"
#include "Movement/SeinWheeledVehicleMovement.h"
#include "SeinConsumerMovementQualification.h"
#include "SeinMovementSubsystem.h"
#endif

namespace
{
	constexpr double QualificationTimeoutSeconds = 180.0;
	constexpr double ReconnectDelaySeconds = 2.0;
	constexpr int32 InitialResyncStartTick = 30;
	constexpr int32 ReplayTailAfterReconnectTicks = 60;
	constexpr int32 MovementCommandStartTick = 35;
	constexpr int64 QualificationRelationshipSourceInstanceID =
		0x5155414C;

	bool ResolveQualificationPair(
		const USeinWorldSubsystem& Sim,
		FSeinPlayerID& OutSource,
		FSeinPlayerID& OutTarget)
	{
		TArray<FSeinPlayerID> Players = Sim.GetRegisteredPlayerIDs();
		Players.RemoveAll([](const FSeinPlayerID Player)
		{
			return !Player.IsValid() || Player.IsNeutral();
		});
		Players.Sort();
		if (Players.Num() != 2)
		{
			return false;
		}
		OutSource = Players[0];
		OutTarget = Players[1];
		return true;
	}

	bool HasQualificationPairCapability(const USeinWorldSubsystem& Sim)
	{
		FSeinPlayerID Source;
		FSeinPlayerID Target;
		return ResolveQualificationPair(Sim, Source, Target)
			&& Sim.HasPairCapability(
				Source,
				Target,
				SeinARTSTags::Relationship_Capability_ShareVision);
	}

	FSeinCommand MakeQualificationPairCapabilityCommand(
		const USeinWorldSubsystem& Sim,
		bool bGrant)
	{
		FSeinPlayerID Source;
		FSeinPlayerID Target;
		if (!ResolveQualificationPair(Sim, Source, Target))
		{
			return FSeinCommand();
		}

		FSeinSetPairCapabilityCommandPayload Payload;
		Payload.SourcePlayer = Source;
		Payload.TargetPlayer = Target;
		Payload.CapabilityTag =
			SeinARTSTags::Relationship_Capability_ShareVision;
		Payload.SourceKindTag =
			SeinARTSTags::Relationship_Source_MatchAdministration;
		Payload.SourceInstanceID =
			QualificationRelationshipSourceInstanceID;
		Payload.bGrant = bGrant;

		FSeinCommand Command;
		Command.CommandType =
			SeinARTSTags::Command_Type_SetPairCapability;
		Command.SchemaVersion = 1;
		Command.Payload = FInstancedStruct::Make(Payload);
		return Command;
	}

	FString GuidDigits(const FGuid& Guid)
	{
		return Guid.ToString(EGuidFormats::Digits);
	}

#if defined(SEIN_CONSUMER_QUALIFY_MOVEMENT_PLUS)
	FFixedVector QualificationMovementDestination();

	struct FQualificationMovementObservation
	{
		FSeinEntityHandle Entity;
		FFixedVector Location;
		FFixedVector Velocity;
		FFixedVector Target;
		FString MovementClass;
		FSeinMovementPlusPresentationState Telemetry;
		bool bHasTarget = false;
		bool bExpectedTarget = false;
		bool bExpectedClass = false;
		bool bTelemetryValid = false;
		bool bTelemetryActive = false;
		bool bAdvanced = false;
	};

	FSeinEntityHandle FindQualificationMovementEntity(
		const USeinWorldSubsystem& Sim)
	{
		FSeinEntityHandle Result;
		const FSeinEntityPool& Pool = Sim.GetEntityPool();
		Pool.ForEachEntity(
			[&](FSeinEntityHandle Handle, const FSeinEntity& Entity)
			{
				(void)Entity;
				const FSeinMovementComponent* Movement =
					Sim.GetComponent<FSeinMovementComponent>(Handle);
				if (!Result.IsValid()
					&& Pool.GetOwner(Handle) == FSeinPlayerID(2)
					&& Movement
					&& Movement->MovementClass == FSoftClassPath(
						USeinWheeledVehicleMovement::StaticClass()))
				{
					Result = Handle;
				}
			});
		return Result;
	}

	bool ObserveQualificationMovement(
		const USeinWorldSubsystem& Sim,
		FQualificationMovementObservation& OutObservation)
	{
		OutObservation = FQualificationMovementObservation();
		const FSeinEntityHandle Handle =
			FindQualificationMovementEntity(Sim);
		const FSeinEntity* Entity = Sim.GetEntity(Handle);
		const FSeinMovementComponent* Movement =
			Sim.GetComponent<FSeinMovementComponent>(Handle);
		if (!Entity || !Movement)
		{
			return false;
		}

		OutObservation.Entity = Handle;
		OutObservation.Location = Entity->Transform.GetLocation();
		OutObservation.Velocity = Movement->Velocity;
		OutObservation.Target = Movement->TargetLocation;
		OutObservation.bHasTarget = Movement->bHasTarget;
		OutObservation.bExpectedTarget =
			OutObservation.bHasTarget
			&& OutObservation.Target == QualificationMovementDestination();

		UWorld* World = Sim.GetWorld();
		const USeinMovementSubsystem* MovementSubsystem = World
			? World->GetSubsystem<USeinMovementSubsystem>()
			: nullptr;
		const USeinMovement* MovementInstance = MovementSubsystem
			? MovementSubsystem->FindMovementInstance(Handle)
			: nullptr;
		if (MovementInstance)
		{
			OutObservation.MovementClass =
				MovementInstance->GetClass()->GetPathName();
			OutObservation.bExpectedClass =
				MovementInstance->GetClass()
				== USeinWheeledVehicleMovement::StaticClass();
		}

		FSeinMovementPlusPresentationDimensions Dimensions;
		OutObservation.Telemetry =
			USeinMovementPlusBPFL::SeinGetMovementPlusPresentationState(
				&Sim, Handle, Dimensions);
		const FSeinMovementPlusPresentationState& Telemetry =
			OutObservation.Telemetry;
		OutObservation.bTelemetryValid =
			FMath::IsFinite(Telemetry.SteeringAngleRadians)
			&& FMath::IsFinite(Telemetry.YawRateRadiansPerSecond)
			&& FMath::IsFinite(Telemetry.NormalizedThrottle)
			&& FMath::IsFinite(Telemetry.NormalizedBrake)
			&& FMath::IsFinite(Telemetry.WheelRotationRadians)
			&& FMath::IsFinite(Telemetry.LeftTrackVelocityCmPerSecond)
			&& FMath::IsFinite(Telemetry.RightTrackVelocityCmPerSecond)
			&& Telemetry.NormalizedThrottle >= 0.0f
			&& Telemetry.NormalizedThrottle <= 1.0f
			&& Telemetry.NormalizedBrake >= 0.0f
			&& Telemetry.NormalizedBrake <= 1.0f
			&& Telemetry.WheelRotationRadians >= 0.0f
			&& Telemetry.WheelRotationRadians < 2.0f * PI;
		OutObservation.bTelemetryActive =
			OutObservation.bTelemetryValid
			&& (FMath::Abs(Telemetry.WheelRotationRadians) > KINDA_SMALL_NUMBER
				|| FMath::Abs(Telemetry.LeftTrackVelocityCmPerSecond)
					> KINDA_SMALL_NUMBER
				|| FMath::Abs(Telemetry.RightTrackVelocityCmPerSecond)
					> KINDA_SMALL_NUMBER);
		OutObservation.bAdvanced =
			OutObservation.Location.X != FFixedPoint::FromInt(200)
			|| OutObservation.Location.Y != FFixedPoint::Zero;
		return true;
	}

	bool IsQualifiedMovementObservation(
		const FQualificationMovementObservation& Observation)
	{
		return Observation.bExpectedClass
			&& Observation.bExpectedTarget
			&& Observation.bTelemetryActive
			&& Observation.bAdvanced;
	}

	FString EncodeQualificationMovementState(
		const FQualificationMovementObservation& Observation)
	{
		return FString::Printf(
			TEXT("%d:%d:%lld:%lld:%lld:%lld:%lld:%lld:%d:%lld:%lld:%lld"),
			Observation.Entity.Index,
			Observation.Entity.Generation,
			static_cast<long long>(Observation.Location.X.Value),
			static_cast<long long>(Observation.Location.Y.Value),
			static_cast<long long>(Observation.Location.Z.Value),
			static_cast<long long>(Observation.Velocity.X.Value),
			static_cast<long long>(Observation.Velocity.Y.Value),
			static_cast<long long>(Observation.Velocity.Z.Value),
			Observation.bHasTarget ? 1 : 0,
			static_cast<long long>(Observation.Target.X.Value),
			static_cast<long long>(Observation.Target.Y.Value),
			static_cast<long long>(Observation.Target.Z.Value));
	}

	FString EncodeQualificationMovementEvidence(
		const FQualificationMovementObservation& Observation)
	{
		const FSeinMovementPlusPresentationState& Telemetry =
			Observation.Telemetry;
		return FString::Printf(
			TEXT("MovementState=%s\n")
			TEXT("MovementClass=%s\n")
			TEXT("MovementDestination=%lld:%lld:%lld\n")
			TEXT("MovementTargetWitness=%s\n")
			TEXT("MovementTelemetry=%.9g:%.9g:%.9g:%.9g:%.9g:%.9g:%.9g\n"),
			*EncodeQualificationMovementState(Observation),
			*Observation.MovementClass,
			static_cast<long long>(Observation.Target.X.Value),
			static_cast<long long>(Observation.Target.Y.Value),
			static_cast<long long>(Observation.Target.Z.Value),
			Observation.bExpectedTarget ? TEXT("Passed") : TEXT("Failed"),
			Telemetry.SteeringAngleRadians,
			Telemetry.YawRateRadiansPerSecond,
			Telemetry.NormalizedThrottle,
			Telemetry.NormalizedBrake,
			Telemetry.WheelRotationRadians,
			Telemetry.LeftTrackVelocityCmPerSecond,
			Telemetry.RightTrackVelocityCmPerSecond);
	}

	FFixedVector QualificationMovementDestination()
	{
		return FFixedVector(
			FFixedPoint::FromInt(50000),
			FFixedPoint::FromInt(5000),
			FFixedPoint::Zero);
	}
#endif

}

void USeinConsumerQualificationSubsystem::Initialize(
	FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	FParse::Value(
		FCommandLine::Get(), TEXT("SeinConsumerQualificationRole="), Role);
	if (Role.IsEmpty())
	{
		return;
	}

	FParse::Value(
		FCommandLine::Get(), TEXT("SeinConsumerMarkerDir="), MarkerDirectory);
	FParse::Value(
		FCommandLine::Get(), TEXT("SeinConsumerServerAddress="), ServerAddress);
	FParse::Value(
		FCommandLine::Get(), TEXT("SeinConsumerReplayPath="), ReplayPath);
	FParse::Value(
		FCommandLine::Get(), TEXT("SeinConsumerExpectedRoot="), ExpectedReplayRoot);
	FParse::Value(
		FCommandLine::Get(), TEXT("SeinConsumerExpectedMovementState="),
		ExpectedMovementState);
	FParse::Value(
		FCommandLine::Get(), TEXT("SeinConsumerExpectedEndTick="), ExpectedReplayEndTick);

	if (MarkerDirectory.IsEmpty())
	{
		MarkerDirectory = FPaths::ProjectSavedDir()
			/ TEXT("ConsumerRuntimeQualification");
	}
	FPaths::NormalizeDirectoryName(MarkerDirectory);
	IFileManager::Get().MakeDirectory(*MarkerDirectory, true);
	WriteMarker(
		*FString::Printf(TEXT("%s-initialized.marker"), *Role.ToLower()),
		FString::Printf(
			TEXT("role=%s\ncommandLine=%s\n"),
			*Role,
			FCommandLine::Get()));

	StartedAtSeconds = FPlatformTime::Seconds();
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(
			this, &USeinConsumerQualificationSubsystem::Tick));
}

void USeinConsumerQualificationSubsystem::Deinitialize()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}
	ActiveReplayReader.Reset();
	if (USeinWorldSubsystem* ReplayWorld = ReplayObserverWorld.Get())
	{
		ReplayWorld->OnCommandsProcessing.Remove(
			ReplayCommandObserverHandle);
	}
	ReplayCommandObserverHandle.Reset();
	ReplayObserverWorld.Reset();
	InitialClientMatchWorld.Reset();
	Super::Deinitialize();
}

bool USeinConsumerQualificationSubsystem::Tick(float DeltaSeconds)
{
	(void)DeltaSeconds;
	if (bFailed)
	{
		return false;
	}
	if (FPlatformTime::Seconds() - StartedAtSeconds
		> QualificationTimeoutSeconds)
	{
		Fail(TEXT("qualification timed out"));
		return false;
	}

	UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	if (!World)
	{
		return true;
	}
	if (!bWorldObservedWritten)
	{
		const UPackage* Package = World->GetOutermost();
		bWorldObservedWritten = WriteMarker(
			*FString::Printf(TEXT("%s-world.marker"), *Role.ToLower()),
			FString::Printf(
				TEXT("package=%s\nmap=%s\nnetMode=%d\n"),
				Package ? *Package->GetName() : TEXT("<none>"),
				*World->GetMapName(),
				static_cast<int32>(World->GetNetMode())));
	}

	if (Role.Equals(TEXT("Server"), ESearchCase::IgnoreCase))
	{
		TickServer(*World);
	}
	else if (Role.Equals(TEXT("Client"), ESearchCase::IgnoreCase))
	{
		TickClient(*World);
	}
	else if (Role.Equals(TEXT("Replay"), ESearchCase::IgnoreCase))
	{
		TickReplay(*World);
	}
	else
	{
		Fail(FString::Printf(TEXT("unknown qualification role '%s'"), *Role));
	}
	return !bFailed;
}

void USeinConsumerQualificationSubsystem::TickServer(UWorld& World)
{
	if (IsMap(World, TEXT("/Game/Maps/ConsumerLobbyMap")))
	{
		if (World.GetNetMode() == NM_Standalone && !bListenTravelIssued)
		{
			bListenTravelIssued = true;
			WriteMarker(
				TEXT("server-listen-travel.marker"),
				TEXT("reopening lobby with listen option\n"));
			UGameplayStatics::OpenLevel(
				&World,
				FName(TEXT("/Game/Maps/ConsumerLobbyMap")),
				true,
				TEXT("listen"));
			return;
		}
		if (World.GetNetMode() != NM_ListenServer)
		{
			return;
		}

		UGameInstance* GameInstance = World.GetGameInstance();
		USeinLobbySubsystem* Lobby = GameInstance
			? GameInstance->GetSubsystem<USeinLobbySubsystem>()
			: nullptr;
		if (!Lobby)
		{
			return;
		}
		if (!bServerHostClaimed)
		{
			APlayerController* HostController =
				GameInstance->GetFirstLocalPlayerController();
			if (!HostController)
			{
				return;
			}
			Lobby->InitializeLobby(2);
			if (!Lobby->ServerHandleSlotClaim(
				HostController, 1, FSeinFactionID::None()))
			{
				Fail(TEXT("listen host could not claim lobby slot 1"));
				return;
			}
			bServerHostClaimed = true;
			WriteMarker(
				TEXT("server-host-claimed.marker"),
				TEXT("slot=1\n"));
		}
		if (!bServerReadyWritten)
		{
			bServerReadyWritten = WriteMarker(
				TEXT("server-ready.marker"), TEXT("listen server ready\n"));
		}

		const ASeinLobbyState* LobbyState = Lobby
			? Lobby->GetLobbyState()
			: nullptr;
		if (!Lobby || !LobbyState || bMatchStartRequested)
		{
			return;
		}

		int32 ClaimedHumans = 0;
		for (const FSeinLobbySlotState& Slot : LobbyState->GetSlots())
		{
			if (Slot.bClaimed
				&& Slot.State == ESeinSlotState::Human
				&& !Slot.bDisconnected)
			{
				++ClaimedHumans;
			}
		}
		if (ClaimedHumans >= 2)
		{
			bMatchStartRequested = true;
			if (!Lobby->ServerStartMatch(true))
			{
				Fail(TEXT("listen server rejected the two-player lobby start"));
				return;
			}
			WriteMarker(
				TEXT("match-travel-requested.marker"),
				TEXT("two-player lobby snapshot accepted\n"));
		}
		return;
	}

	if (!IsMap(World, TEXT("/Game/Maps/ConsumerMap"))
		|| World.GetNetMode() != NM_ListenServer)
	{
		return;
	}

	UGameInstance* GameInstance = World.GetGameInstance();
	USeinNetSubsystem* Net = GameInstance
		? GameInstance->GetSubsystem<USeinNetSubsystem>()
		: nullptr;
	USeinWorldSubsystem* Sim = World.GetSubsystem<USeinWorldSubsystem>();
	if (Net && Sim && Net->HasDeterminismSessionFailure())
	{
		const FSeinDeterminismSessionFailure Failure =
			Net->GetDeterminismSessionFailure();
		Fail(FString::Printf(
			TEXT("server determinism failure kind=%d turn=%d participant=%s authority=%d; localDiagnostic=%s"),
			static_cast<int32>(Failure.Kind),
			Failure.Turn,
			*Failure.ParticipantID.ToCanonicalString(),
			Net->IsDeterminismSessionFailureAuthoritative() ? 1 : 0,
			*Net->GetLocalDeterminismFailureDiagnostic()));
		return;
	}
	if (!Net || !Sim || !Net->IsNetworkingActive()
		|| !Sim->IsSimulationRunning())
	{
		return;
	}

	if (!bServerMatchStarted)
	{
		bServerMatchStarted = true;
		WriteMarker(
			TEXT("server-match-started.marker"),
			FString::Printf(TEXT("tick=%d\n"), Sim->GetCurrentTick()));
	}
	if (Net->IsLocalDesyncDetected())
	{
		Fail(TEXT("server observed a determinism failure"));
		return;
	}
	if (!bServerRootGossipCompleted
		&& Net->GetLatestSuccessfulWorldStateRootReporterCount() >= 2)
	{
		bServerRootGossipCompleted = WriteMarker(
			TEXT("server-root-gossip-complete.marker"),
			FString::Printf(
				TEXT("Turn=%d\nReporters=%d\n"),
				Net->GetLatestSuccessfulWorldStateRootCheckTurn(),
				Net->GetLatestSuccessfulWorldStateRootReporterCount()));
		if (!bServerRootGossipCompleted)
		{
			Fail(TEXT("server could not publish root-gossip completion"));
			return;
		}
	}
	if (!bPingSubmitted && Sim->GetCurrentTick() >= 6
		&& Net->GetLocalPlayerID().IsValid())
	{
		Net->SubmitLocalCommand(FSeinCommand::MakePingCommand(
			Net->GetLocalPlayerID(), FFixedVector()));
		bPingSubmitted = true;
	}
	if (!bServerPairGrantSubmitted && Sim->GetCurrentTick() >= 6)
	{
		const FSeinCommand Grant =
			MakeQualificationPairCapabilityCommand(*Sim, true);
		if (!Grant.CommandType.IsValid())
		{
			return;
		}
		Net->SubmitLocalCommandDraft(
			Grant, /*bRequestMatchAdministration=*/true);
		bServerPairGrantSubmitted = true;
		WriteMarker(
			TEXT("server-pair-grant-submitted.marker"),
			FString::Printf(TEXT("tick=%d\n"), Sim->GetCurrentTick()));
	}
	if (bServerPairGrantSubmitted && !bServerPairGrantObserved
		&& HasQualificationPairCapability(*Sim))
	{
		bServerPairGrantObserved = WriteMarker(
			TEXT("server-pair-grant-observed.marker"),
			FString::Printf(TEXT("tick=%d\n"), Sim->GetCurrentTick()));
	}
#if defined(SEIN_CONSUMER_QUALIFY_MOVEMENT_PLUS)
	if (!bServerMovementObserved)
	{
		FQualificationMovementObservation Observation;
		if (ObserveQualificationMovement(*Sim, Observation)
			&& IsQualifiedMovementObservation(Observation))
		{
			bServerMovementObserved = WriteMarker(
				TEXT("server-movement-observed.marker"),
				FString::Printf(
					TEXT("tick=%d\n%s"),
					Sim->GetCurrentTick(),
					*EncodeQualificationMovementEvidence(Observation)));
			if (!bServerMovementObserved)
			{
				Fail(TEXT("server could not publish movement witness"));
				return;
			}
		}
	}
#endif

	int32 Connected = 0;
	bool bHasDropped = false;
	bool bHasReconnecting = false;
	for (const TPair<FSeinPlayerID, ESeinSlotLifecycle>& Pair
		: Net->GetSlotLifecycle())
	{
		switch (Pair.Value)
		{
		case ESeinSlotLifecycle::Connected:
			++Connected;
			break;
		case ESeinSlotLifecycle::Dropped:
			bHasDropped = true;
			break;
		case ESeinSlotLifecycle::Reconnecting:
			bHasReconnecting = true;
			break;
		default:
			break;
		}
	}

	if (!bServerSawDrop && bHasDropped)
	{
		bServerSawDrop = true;
		WriteMarker(
			TEXT("server-drop-observed.marker"),
			FString::Printf(TEXT("tick=%d\n"), Sim->GetCurrentTick()));
	}
	if (bServerSawDrop && !bServerSawReconnect
		&& !bHasDropped && !bHasReconnecting && Connected >= 2)
	{
		bServerSawReconnect = true;
		ServerReconnectTick = Sim->GetCurrentTick();
		WriteMarker(
			TEXT("server-reconnect-activated.marker"),
			FString::Printf(TEXT("tick=%d\n"), ServerReconnectTick));
	}
	if (bServerSawReconnect && !bServerPairRevokeSubmitted)
	{
		const FString PreservedMarker = FPaths::Combine(
			MarkerDirectory,
			TEXT("client-reconnect-capability-preserved.marker"));
		if (!IFileManager::Get().FileExists(*PreservedMarker)
			|| !HasQualificationPairCapability(*Sim))
		{
			return;
		}
#if defined(SEIN_CONSUMER_QUALIFY_MOVEMENT_PLUS)
		const FString MovementPreservedMarker = FPaths::Combine(
			MarkerDirectory,
			TEXT("client-reconnect-movement-preserved.marker"));
		if (!IFileManager::Get().FileExists(*MovementPreservedMarker))
		{
			return;
		}
#endif
		const FSeinCommand Revoke =
			MakeQualificationPairCapabilityCommand(*Sim, false);
		if (!Revoke.CommandType.IsValid())
		{
			Fail(TEXT("server could not resolve qualification pair for revoke"));
			return;
		}
		Net->SubmitLocalCommandDraft(
			Revoke, /*bRequestMatchAdministration=*/true);
		bServerPairRevokeSubmitted = true;
		WriteMarker(
			TEXT("server-pair-revoke-submitted.marker"),
			FString::Printf(TEXT("tick=%d\n"), Sim->GetCurrentTick()));
	}
	if (bServerPairRevokeSubmitted && !bServerPairRevokeObserved
		&& !HasQualificationPairCapability(*Sim))
	{
		bServerPairRevokeObserved = WriteMarker(
			TEXT("server-pair-revoke-observed.marker"),
			FString::Printf(TEXT("tick=%d\n"), Sim->GetCurrentTick()));
	}

	const FString ClientRevokeMarker = FPaths::Combine(
		MarkerDirectory, TEXT("client-pair-revoke-observed.marker"));
	if (!bServerSawReconnect || !bServerPairRevokeObserved
		|| !IFileManager::Get().FileExists(*ClientRevokeMarker)
		|| bServerReplayPublished
		|| Sim->GetCurrentTick()
			< ServerReconnectTick + ReplayTailAfterReconnectTicks)
	{
		return;
	}

	USeinReplayWriter* Writer = Net->GetReplayWriter();
	if (!Writer || !Writer->IsRecording()
		|| Writer->GetObservedEndTick() != Sim->GetCurrentTick())
	{
		return;
	}

	FGuid FinalRoot;
	FString RootError;
	if (!Sim->ComputeCanonicalStateRoot(FinalRoot, RootError))
	{
		Fail(FString::Printf(
			TEXT("server final canonical root failed: %s"), *RootError));
		return;
	}
	const int32 EndTick = Writer->GetObservedEndTick();
	FString MovementMarker;
#if defined(SEIN_CONSUMER_QUALIFY_MOVEMENT_PLUS)
	FQualificationMovementObservation MovementObservation;
	if (!ObserveQualificationMovement(*Sim, MovementObservation)
		|| !IsQualifiedMovementObservation(MovementObservation))
	{
		Fail(TEXT("server lost the active Movement+ fixture before replay publication"));
		return;
	}
	MovementMarker = EncodeQualificationMovementEvidence(
		MovementObservation);
#endif
	const FString PublishedPath = Writer->FinishRecording();
	if (PublishedPath.IsEmpty())
	{
		Fail(TEXT("server could not publish the streaming replay"));
		return;
	}

	bServerReplayPublished = true;
	const FString Marker = FString::Printf(
		TEXT("Path=%s\nEndTick=%d\nRoot=%s\n%s"),
		*PublishedPath,
		EndTick,
		*GuidDigits(FinalRoot),
		*MovementMarker);
	if (!WriteMarker(TEXT("server-complete.marker"), Marker))
	{
		Fail(TEXT("server could not publish its completion marker"));
	}
}

void USeinConsumerQualificationSubsystem::TickClient(UWorld& World)
{
	const double Now = FPlatformTime::Seconds();
	if (!bInitialConnectTravelIssued && World.GetNetMode() == NM_Standalone)
	{
		if (ServerAddress.IsEmpty() || !GEngine)
		{
			Fail(TEXT("client has no initial server address"));
			return;
		}
		bInitialConnectTravelIssued = true;
		GEngine->SetClientTravel(&World, *ServerAddress, TRAVEL_Absolute);
		WriteMarker(
			TEXT("client-connect-travel.marker"),
			ServerAddress + TEXT("\n"));
		return;
	}
	if (bDisconnectIssued && !bReconnectTravelIssued)
	{
		if (Now - DisconnectIssuedAtSeconds >= ReconnectDelaySeconds
			&& World.GetNetMode() != NM_Client)
		{
			if (ServerAddress.IsEmpty() || !GEngine)
			{
				Fail(TEXT("client has no reconnect server address"));
				return;
			}
			bReconnectTravelIssued = true;
			GEngine->SetClientTravel(
				&World, *ServerAddress, TRAVEL_Absolute);
			WriteMarker(
				TEXT("client-reconnect-travel.marker"),
				ServerAddress + TEXT("\n"));
		}
		return;
	}

	if (!IsMap(World, TEXT("/Game/Maps/ConsumerMap"))
		|| World.GetNetMode() != NM_Client)
	{
		return;
	}
	if (bReconnectTravelIssued && !bReconnectNetworked)
	{
		bReconnectNetworked = WriteMarker(
			TEXT("client-reconnect-networked.marker"),
			TEXT("client returned to the authoritative match world\n"));
	}

	UGameInstance* GameInstance = World.GetGameInstance();
	USeinNetSubsystem* Net = GameInstance
		? GameInstance->GetSubsystem<USeinNetSubsystem>()
		: nullptr;
	USeinWorldSubsystem* Sim = World.GetSubsystem<USeinWorldSubsystem>();
	if (!Net || !Sim || !Net->IsNetworkingActive()
		|| !Net->GetLocalPlayerID().IsValid())
	{
		return;
	}
	if (!bReconnectTravelIssued)
	{
		if (!InitialClientMatchWorld.IsValid())
		{
			InitialClientMatchWorld = &World;
		}
		if (!Sim->IsSimulationRunning())
		{
			return;
		}
		if (!bClientPairGrantObserved)
		{
			if (!HasQualificationPairCapability(*Sim))
			{
				return;
			}
			bClientPairGrantObserved = WriteMarker(
				TEXT("client-pair-grant-observed.marker"),
				FString::Printf(TEXT("tick=%d\n"), Sim->GetCurrentTick()));
			if (!bClientPairGrantObserved)
			{
				Fail(TEXT("client could not publish pair-grant witness"));
				return;
			}
		}
#if defined(SEIN_CONSUMER_QUALIFY_MOVEMENT_PLUS)
		if (!bClientMovementCommandSubmitted
			&& Sim->GetCurrentTick() >= MovementCommandStartTick)
		{
			const FSeinEntityHandle Vehicle =
				FindQualificationMovementEntity(*Sim);
			if (!Vehicle.IsValid())
			{
				return;
			}
			const FGameplayTag MoveTag =
				GetDefault<USeinConsumerQualificationMoveAbility>()->AbilityTag;
			Net->SubmitLocalCommand(FSeinCommand::MakeAbilityCommand(
				Net->GetLocalPlayerID(),
				Vehicle,
				MoveTag,
				FSeinEntityHandle::Invalid(),
				QualificationMovementDestination()));
			bClientMovementCommandSubmitted = WriteMarker(
				TEXT("client-movement-command-submitted.marker"),
				FString::Printf(
					TEXT("tick=%d\nentity=%s\n"),
					Sim->GetCurrentTick(),
					*Vehicle.ToString()));
			if (!bClientMovementCommandSubmitted)
			{
				Fail(TEXT("client could not publish movement-command witness"));
				return;
			}
		}
		if (bClientMovementCommandSubmitted && !bClientMovementObserved)
		{
			FQualificationMovementObservation Observation;
			if (ObserveQualificationMovement(*Sim, Observation)
				&& IsQualifiedMovementObservation(Observation))
			{
				bClientMovementObserved = WriteMarker(
					TEXT("client-movement-observed.marker"),
					FString::Printf(
						TEXT("tick=%d\n%s"),
						Sim->GetCurrentTick(),
						*EncodeQualificationMovementEvidence(Observation)));
				if (!bClientMovementObserved)
				{
					Fail(TEXT("client could not publish movement-state witness"));
					return;
				}
			}
		}
#endif
		if (!bPingSubmitted && Sim->GetCurrentTick() >= 6)
		{
			Net->SubmitLocalCommand(FSeinCommand::MakePingCommand(
				Net->GetLocalPlayerID(), FFixedVector()));
			bPingSubmitted = true;
		}
		// Prove both initially connected simulation peers completed a canonical
		// root comparison before resync or disconnect changes reporter topology.
		const FString RootGossipMarker = FPaths::Combine(
			MarkerDirectory, TEXT("server-root-gossip-complete.marker"));
		if (!IFileManager::Get().FileExists(*RootGossipMarker))
		{
			return;
		}
#if defined(SEIN_CONSUMER_QUALIFY_MOVEMENT_PLUS)
		const FString ServerMovementMarker = FPaths::Combine(
			MarkerDirectory, TEXT("server-movement-observed.marker"));
		if (!bClientMovementObserved
			|| !IFileManager::Get().FileExists(*ServerMovementMarker))
		{
			return;
		}
#endif
		if (!bInitialResyncRequested
			&& Sim->GetCurrentTick() >= InitialResyncStartTick)
		{
			FString Error;
			if (!Net->RequestResync(Error))
			{
				Fail(FString::Printf(
					TEXT("initial resync request failed: %s"), *Error));
				return;
			}
			bInitialResyncRequested = true;
			InitialResyncRequestTick = Sim->GetCurrentTick();
		}
		if (bInitialResyncRequested
			&& Net->GetClientResyncPhase()
				!= USeinNetSubsystem::EClientResyncPhase::None)
		{
			bInitialResyncObserved = true;
		}
		if (bInitialResyncObserved && !bInitialResyncCompleted
			&& Net->GetClientResyncPhase()
				== USeinNetSubsystem::EClientResyncPhase::None
			&& Sim->IsSimulationRunning()
			&& Sim->GetCurrentTick() > InitialResyncRequestTick)
		{
			bInitialResyncCompleted = true;
			WriteMarker(
				TEXT("client-resync-complete.marker"),
				FString::Printf(TEXT("tick=%d\n"), Sim->GetCurrentTick()));
		}
		if (bInitialResyncCompleted && !bDisconnectIssued)
		{
			APlayerController* Controller =
				GameInstance->GetFirstLocalPlayerController();
			if (!Controller)
			{
				return;
			}
			bDisconnectIssued = true;
			DisconnectIssuedAtSeconds = Now;
			WriteMarker(
				TEXT("client-disconnect-issued.marker"),
				FString::Printf(TEXT("tick=%d\n"), Sim->GetCurrentTick()));
			Controller->ConsoleCommand(TEXT("disconnect"), true);
		}
		return;
	}

	if (&World == InitialClientMatchWorld.Get())
	{
		return;
	}
	if (!bReconnectResyncRequested)
	{
		FString Error;
		if (!Net->RequestResync(Error))
		{
			return;
		}
		bReconnectResyncRequested = true;
		ReconnectResyncRequestTick = Sim->GetCurrentTick();
	}
	if (bReconnectResyncRequested
		&& Net->GetClientResyncPhase()
			!= USeinNetSubsystem::EClientResyncPhase::None)
	{
		bReconnectResyncObserved = true;
	}
	if (bReconnectResyncObserved && !bReconnectCompleted
		&& Net->GetClientResyncPhase()
			== USeinNetSubsystem::EClientResyncPhase::None
		&& Sim->IsSimulationRunning()
		&& Sim->GetCurrentTick() > ReconnectResyncRequestTick)
	{
		if (!HasQualificationPairCapability(*Sim))
		{
			Fail(TEXT("pair capability was absent after production reconnect resync"));
			return;
		}
#if defined(SEIN_CONSUMER_QUALIFY_MOVEMENT_PLUS)
		FQualificationMovementObservation MovementObservation;
		if (!ObserveQualificationMovement(*Sim, MovementObservation)
			|| !IsQualifiedMovementObservation(MovementObservation))
		{
			return;
		}
		bReconnectMovementPreserved = WriteMarker(
			TEXT("client-reconnect-movement-preserved.marker"),
			FString::Printf(
				TEXT("tick=%d\n%s"),
				Sim->GetCurrentTick(),
				*EncodeQualificationMovementEvidence(MovementObservation)));
		if (!bReconnectMovementPreserved)
		{
			Fail(TEXT("client could not publish reconnect movement witness"));
			return;
		}
#endif
		FGuid Root;
		FString Error;
		if (!Sim->ComputeCanonicalStateRoot(Root, Error))
		{
			Fail(FString::Printf(
				TEXT("reconnected client root failed: %s"), *Error));
			return;
		}
		bReconnectPairCapabilityPreserved = WriteMarker(
			TEXT("client-reconnect-capability-preserved.marker"),
			FString::Printf(TEXT("tick=%d\n"), Sim->GetCurrentTick()));
		if (!bReconnectPairCapabilityPreserved)
		{
			Fail(TEXT("client could not publish reconnect capability witness"));
			return;
		}
		bReconnectCompleted = true;
		WriteMarker(
			TEXT("client-reconnect-complete.marker"),
			FString::Printf(
				TEXT("tick=%d\nRoot=%s\n"),
				Sim->GetCurrentTick(), *GuidDigits(Root)));
	}
	if (bReconnectCompleted && !bClientPairRevokeObserved
		&& !HasQualificationPairCapability(*Sim))
	{
		bClientPairRevokeObserved = WriteMarker(
			TEXT("client-pair-revoke-observed.marker"),
			FString::Printf(TEXT("tick=%d\n"), Sim->GetCurrentTick()));
	}
}

void USeinConsumerQualificationSubsystem::ObserveReplayCommands(
	int32 Tick,
	const TArray<FSeinCommand>& Commands)
{
	(void)Tick;
	for (const FSeinCommand& Command : Commands)
	{
#if defined(SEIN_CONSUMER_QUALIFY_MOVEMENT_PLUS)
		if (Command.CommandType
				== SeinARTSTags::Command_Type_ActivateAbility
			&& Command.AbilityTag
				== GetDefault<USeinConsumerQualificationMoveAbility>()->AbilityTag)
		{
			bReplayObservedMovementCommand = true;
		}
#endif
		if (Command.CommandType
			!= SeinARTSTags::Command_Type_SetPairCapability)
		{
			continue;
		}
		const FSeinSetPairCapabilityCommandPayload* Payload =
			Command.Payload.GetPtr<FSeinSetPairCapabilityCommandPayload>();
		if (!Payload
			|| Payload->SourceKindTag
				!= SeinARTSTags::Relationship_Source_MatchAdministration
			|| Payload->SourceInstanceID
				!= QualificationRelationshipSourceInstanceID)
		{
			continue;
		}
		if (Payload->bGrant)
		{
			bReplayObservedPairGrant = true;
		}
		else
		{
			bReplayObservedPairRevoke = true;
		}
	}
}

void USeinConsumerQualificationSubsystem::TickReplay(UWorld& World)
{
	if (!IsMap(World, TEXT("/Game/Maps/ConsumerMap")))
	{
		if (!bReplayTravelIssued && World.GetNetMode() == NM_Standalone)
		{
			bReplayTravelIssued = true;
			WriteMarker(
				TEXT("replay-travel.marker"),
				TEXT("opening pristine externally-orchestrated match world\n"));
			UGameplayStatics::OpenLevel(
				&World,
				FName(TEXT("/Game/Maps/ConsumerMap")),
				true,
				TEXT("SeinBootstrap=ExternalOrchestrator"));
		}
		return;
	}
	if (World.GetNetMode() != NM_Standalone)
	{
		return;
	}

	UGameInstance* GameInstance = World.GetGameInstance();
	USeinNetSubsystem* Net = GameInstance
		? GameInstance->GetSubsystem<USeinNetSubsystem>()
		: nullptr;
	USeinWorldSubsystem* Sim = World.GetSubsystem<USeinWorldSubsystem>();
	if (!Net || !Sim)
	{
		return;
	}

	if (!bReplayStarted)
	{
		if (ReplayPath.IsEmpty() || ExpectedReplayRoot.IsEmpty()
			|| ExpectedReplayEndTick <= 0)
		{
			Fail(TEXT("replay role is missing path/root/end-tick arguments"));
			return;
		}
#if defined(SEIN_CONSUMER_QUALIFY_MOVEMENT_PLUS)
		if (ExpectedMovementState.IsEmpty())
		{
			Fail(TEXT("replay role is missing expected Movement+ state"));
			return;
		}
#endif
		if (Sim->IsSimulationRunning() || Sim->GetCurrentTick() != 0)
		{
			Fail(TEXT("replay world was not pristine at qualification start"));
			return;
		}

		USeinReplayReader* Reader = Net->GetOrCreateReplayReader();
		if (!Reader || !Reader->LoadFromFile(ReplayPath))
		{
			Fail(TEXT("replay reader rejected the published journal"));
			return;
		}
		if (Reader->GetHeader().EndTick != ExpectedReplayEndTick)
		{
			Fail(FString::Printf(
				TEXT("replay end tick %d did not match server %d"),
				Reader->GetHeader().EndTick, ExpectedReplayEndTick));
			return;
		}
		ReplayObserverWorld = Sim;
		ReplayCommandObserverHandle =
			Sim->OnCommandsProcessing.AddUObject(
				this,
				&USeinConsumerQualificationSubsystem::ObserveReplayCommands);
		const int32 SeekTick = FMath::Clamp(
			InitialResyncStartTick, 1, ExpectedReplayEndTick - 1);
		if (!Reader->PlayFromTick(SeekTick))
		{
			Fail(FString::Printf(
				TEXT("checkpoint replay seek to tick %d was rejected"), SeekTick));
			return;
		}
		ActiveReplayReader = Reader;
		bReplayObservedPairGrant =
			bReplayObservedPairGrant
			|| HasQualificationPairCapability(*Sim);
		bReplayStarted = true;
		bReplayObservedPlaying = Reader->IsPlaying();
		WriteMarker(
			TEXT("replay-started.marker"),
			FString::Printf(TEXT("seek=%d\n"), SeekTick));
		return;
	}

	USeinReplayReader* Reader = ActiveReplayReader.Get();
	if (!Reader)
	{
		Fail(TEXT("replay reader disappeared during playback"));
		return;
	}
#if defined(SEIN_CONSUMER_QUALIFY_MOVEMENT_PLUS)
	FQualificationMovementObservation MovementObservation;
	if (ObserveQualificationMovement(*Sim, MovementObservation))
	{
		bReplayObservedMovementActive =
			bReplayObservedMovementActive
			|| (MovementObservation.bExpectedClass
				&& MovementObservation.bExpectedTarget);
		bReplayObservedMovementAdvanced =
			bReplayObservedMovementAdvanced
			|| (MovementObservation.bAdvanced
				&& MovementObservation.bTelemetryActive);
	}
#endif
	bReplayObservedPlaying = bReplayObservedPlaying || Reader->IsPlaying();
	if (!bReplayObservedPlaying || Reader->IsPlaying())
	{
		return;
	}
	if (Sim->GetCurrentTick() != ExpectedReplayEndTick)
	{
		Fail(FString::Printf(
			TEXT("replay stopped at tick %d instead of %d"),
			Sim->GetCurrentTick(), ExpectedReplayEndTick));
		return;
	}
	if (!bReplayObservedPairGrant || !bReplayObservedPairRevoke
		|| HasQualificationPairCapability(*Sim))
	{
		Fail(TEXT("replay did not witness the pair-capability grant/revoke lifecycle"));
		return;
	}
#if defined(SEIN_CONSUMER_QUALIFY_MOVEMENT_PLUS)
	if (!bReplayObservedMovementCommand
		|| !bReplayObservedMovementActive
		|| !bReplayObservedMovementAdvanced)
	{
		Fail(TEXT("replay did not witness the Movement+ command and active motion"));
		return;
	}
	if (!ObserveQualificationMovement(*Sim, MovementObservation)
		|| !IsQualifiedMovementObservation(MovementObservation))
	{
		Fail(TEXT("replay lost the qualified Movement+ fixture at its terminal frontier"));
		return;
	}
	const FString ReplayMovementState =
		EncodeQualificationMovementState(MovementObservation);
	if (!ReplayMovementState.Equals(
		ExpectedMovementState, ESearchCase::CaseSensitive))
	{
		Fail(FString::Printf(
			TEXT("replay Movement+ state %s did not match server %s"),
			*ReplayMovementState,
			*ExpectedMovementState));
		return;
	}
#endif

	// Natural replay completion releases its scheduler reservation. Re-arm the
	// consumed timeline without pumping another tick so the canonical-root API
	// can inspect the exact terminal boundary, then release it again. This is
	// the same public lifecycle required by the replay determinism regression.
	if (!Sim->StartSimulation())
	{
		Fail(TEXT("replay terminal timeline could not be re-armed for root proof"));
		return;
	}
	FGuid ReplayRoot;
	FString Error;
	const bool bRootComputed =
		Sim->ComputeCanonicalStateRoot(ReplayRoot, Error);
	Sim->StopSimulation();
	if (!bRootComputed)
	{
		Fail(FString::Printf(
			TEXT("replay final canonical root failed: %s"), *Error));
		return;
	}
	const FString ReplayRootText = GuidDigits(ReplayRoot);
	if (!ReplayRootText.Equals(ExpectedReplayRoot, ESearchCase::IgnoreCase))
	{
		Fail(FString::Printf(
			TEXT("replay root %s did not match server %s"),
			*ReplayRootText, *ExpectedReplayRoot));
		return;
	}

	FString MovementMarker;
#if defined(SEIN_CONSUMER_QUALIFY_MOVEMENT_PLUS)
	MovementMarker = FString::Printf(
		TEXT("MovementCommandWitness=Passed\n")
		TEXT("MovementStateWitness=Passed\n")
		TEXT("MovementFinalStateWitness=Passed\n%s"),
		*EncodeQualificationMovementEvidence(MovementObservation));
#endif
	WriteMarker(
		TEXT("replay-complete.marker"),
		FString::Printf(
			TEXT("EndTick=%d\nRoot=%s\nPairGrantWitness=Passed\nPairRevokeWitness=Passed\n%s"),
			ExpectedReplayEndTick, *ReplayRootText, *MovementMarker));
	FPlatformMisc::RequestExit(false);
}

bool USeinConsumerQualificationSubsystem::IsMap(
	const UWorld& World,
	const TCHAR* PackageName) const
{
	const UPackage* Package = World.GetOutermost();
	return Package && Package->GetName().Equals(PackageName);
}

bool USeinConsumerQualificationSubsystem::WriteMarker(
	const TCHAR* FileName,
	const FString& Body) const
{
	if (MarkerDirectory.IsEmpty())
	{
		return false;
	}
	IFileManager::Get().MakeDirectory(*MarkerDirectory, true);
	return FFileHelper::SaveStringToFile(
		Body,
		*(MarkerDirectory / FileName),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void USeinConsumerQualificationSubsystem::Fail(const FString& Reason)
{
	if (bFailed)
	{
		return;
	}
	bFailed = true;
	const FString FailureFile = FString::Printf(
		TEXT("%s-failed.marker"), *Role.ToLower());
	WriteMarker(*FailureFile, Reason + TEXT("\n"));
	FPlatformMisc::RequestExit(true);
}
