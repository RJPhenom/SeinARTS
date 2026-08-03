#include "SeinConsumerQualificationSubsystem.h"

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
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
#include "Types/Vector.h"

namespace
{
	constexpr double QualificationTimeoutSeconds = 180.0;
	constexpr double ReconnectDelaySeconds = 2.0;
	constexpr int32 InitialResyncStartTick = 30;
	constexpr int32 ReplayTailAfterReconnectTicks = 60;

	FString GuidDigits(const FGuid& Guid)
	{
		return Guid.ToString(EGuidFormats::Digits);
	}
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
	if (!bPingSubmitted && Sim->GetCurrentTick() >= 6
		&& Net->GetLocalPlayerID().IsValid())
	{
		Net->SubmitLocalCommand(FSeinCommand::MakePingCommand(
			Net->GetLocalPlayerID(), FFixedVector()));
		bPingSubmitted = true;
	}

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

	if (!bServerSawReconnect || bServerReplayPublished
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
	const FString PublishedPath = Writer->FinishRecording();
	if (PublishedPath.IsEmpty())
	{
		Fail(TEXT("server could not publish the streaming replay"));
		return;
	}

	bServerReplayPublished = true;
	const FString Marker = FString::Printf(
		TEXT("Path=%s\nEndTick=%d\nRoot=%s\n"),
		*PublishedPath,
		EndTick,
		*GuidDigits(FinalRoot));
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
	if (bReconnectTravelIssued && !bReconnectBound)
	{
		bReconnectBound = WriteMarker(
			TEXT("client-reconnect-bound.marker"),
			FString::Printf(
				TEXT("slot=%u\n"), Net->GetLocalPlayerID().Value));
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
		if (!bPingSubmitted && Sim->GetCurrentTick() >= 6)
		{
			Net->SubmitLocalCommand(FSeinCommand::MakePingCommand(
				Net->GetLocalPlayerID(), FFixedVector()));
			bPingSubmitted = true;
		}
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

	if (&World == InitialClientMatchWorld.Get()
		|| !Sim->IsSimulationRunning())
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
		FGuid Root;
		FString Error;
		if (!Sim->ComputeCanonicalStateRoot(Root, Error))
		{
			Fail(FString::Printf(
				TEXT("reconnected client root failed: %s"), *Error));
			return;
		}
		bReconnectCompleted = true;
		WriteMarker(
			TEXT("client-reconnect-complete.marker"),
			FString::Printf(
				TEXT("tick=%d\nRoot=%s\n"),
				Sim->GetCurrentTick(), *GuidDigits(Root)));
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
		const int32 SeekTick = FMath::Clamp(
			ExpectedReplayEndTick / 2, 1, ExpectedReplayEndTick - 1);
		if (!Reader->PlayFromTick(SeekTick))
		{
			Fail(FString::Printf(
				TEXT("checkpoint replay seek to tick %d was rejected"), SeekTick));
			return;
		}
		ActiveReplayReader = Reader;
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

	WriteMarker(
		TEXT("replay-complete.marker"),
		FString::Printf(
			TEXT("EndTick=%d\nRoot=%s\n"),
			ExpectedReplayEndTick, *ReplayRootText));
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
