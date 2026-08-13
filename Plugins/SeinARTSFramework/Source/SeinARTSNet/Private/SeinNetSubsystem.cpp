/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNetSubsystem.cpp
 */

#include "SeinNetSubsystem.h"
#include "SeinARTSNet.h"
#include "SeinNetRelay.h"
#include "SeinLobbySubsystem.h"
#include "SeinReplayWriter.h"
#include "SeinReplayReader.h"
#include "SeinReplayFormat.h"
#include "SeinNetCommandWireCodec.h"
#include "Serialization/SeinSnapshotTransfer.h"
#include "Settings/PluginSettings.h"
#include "Data/SeinWorldSnapshot.h"
#include "Simulation/SeinSnapshotRestoreAuthority.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Input/SeinCommandSchemaRegistry.h"
#include "Serialization/SeinDeterministicValueDigest.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "AI/SeinAIController.h"
#include "AI/SeinNullAIController.h"
#include "UObject/SoftObjectPath.h"
#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "Misc/DateTime.h"
#include "HAL/PlatformTime.h"
#include "UObject/Class.h"

namespace
{
	// Valid peers submit only a few turns ahead (input delay plus at most a
	// small scheduling skew). A deliberately generous window keeps the wire
	// extensible while preventing malformed RPCs from allocating unbounded
	// per-turn maps. This is transport hygiene, not gameplay tuning.
	constexpr int32 GSeinMaxProtocolTurnLead = 256;
	constexpr int32 GSeinRetainedHistoryTurns = 256;
	// A burst may drain over several genuine turn boundaries, but it must not
	// become an unbounded process-local memory sink while transport is stalled.
	constexpr int32 GSeinMaxBufferedAuthorTurns = 4;
	// Destination actors bind the materializer during normal world startup.
	// Bound this non-simulation scheduler bridge so a missing integration fails
	// closed instead of retaining the game-instance subsystem forever.
	constexpr int32 GSeinMaxBootstrapMaterializerRetryTicks = 120;
	constexpr double GSeinBootstrapCoordinatorTimeoutSeconds = 30.0;
	const FName GSeinNetworkBootstrapAuthorityID(
		TEXT("SeinARTS.Net.LockstepBootstrap"));

	struct FSeinAuthorSubmissionBudget
	{
		int32 MaxCommands = 0;
		int32 MaxEncodedBytes = 0;
		uint64 MaxCanonicalCostBytes = 0;
	};

	int32 GetConfiguredMaxCommandsPerSubmission()
	{
		const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
		return FMath::Clamp(
			Settings ? Settings->MaxCommandsPerSubmission : 1,
			1,
			SeinNetProtocolLimits::MaxCommandsPerAuthor);
	}

	FSeinAuthorSubmissionBudget GetAuthorSubmissionBudget(
		int32 ExpectedAuthorCount,
		int32 FrozenMaxCommandsPerSubmission)
	{
		const int32 Authors = FMath::Clamp(
			ExpectedAuthorCount, 1,
			SeinNetProtocolLimits::MaxCommandAuthors);
		const int32 VariableWireBytes =
			static_cast<int32>(FSeinOpaqueCommandBatch::MaxBytes)
			- FSeinNetCommandWireCodec::FixedBatchHeaderBytes;
		FSeinAuthorSubmissionBudget Result;
		Result.MaxCommands = FMath::Min(
			FMath::Clamp(
				FrozenMaxCommandsPerSubmission,
				0,
				SeinNetProtocolLimits::MaxCommandsPerAuthor),
			SeinReplayFormat::MaxCommandsPerTurn / Authors);
		Result.MaxEncodedBytes = FSeinNetCommandWireCodec::FixedBatchHeaderBytes
			+ VariableWireBytes / Authors;
		Result.MaxCanonicalCostBytes =
			FSeinNetCommandWireCodec::MaxCanonicalCostBytes
			/ static_cast<uint64>(Authors);
		return Result;
	}

	bool FitsSingleTurnBudget(
		const FSeinQueuedCommandCost& Cost,
		const FSeinAuthorSubmissionBudget& Budget)
	{
		return Budget.MaxCommands > 0
			&& Cost.VariableWireBytes >= 0
			&& FSeinNetCommandWireCodec::FixedBatchHeaderBytes
				+ Cost.VariableWireBytes <= Budget.MaxEncodedBytes
			&& FSeinNetCommandWireCodec::FixedBatchHeaderBytes
				+ Cost.VariableCanonicalCostBytes
				<= Budget.MaxCanonicalCostBytes;
	}

	bool FitsBacklogBudget(
		int32 ExistingCount,
		int64 ExistingVariableWireBytes,
		uint64 ExistingVariableCanonicalCostBytes,
		const FSeinQueuedCommandCost& Added,
		const FSeinAuthorSubmissionBudget& PerTurn)
	{
		if (PerTurn.MaxEncodedBytes
			< FSeinNetCommandWireCodec::FixedBatchHeaderBytes
			|| PerTurn.MaxCanonicalCostBytes
				< FSeinNetCommandWireCodec::FixedBatchHeaderBytes)
		{
			return false;
		}
		const int64 MaxCount =
			static_cast<int64>(PerTurn.MaxCommands) * GSeinMaxBufferedAuthorTurns;
		const int64 MaxVariableWireBytes = static_cast<int64>(
			PerTurn.MaxEncodedBytes - FSeinNetCommandWireCodec::FixedBatchHeaderBytes)
			* GSeinMaxBufferedAuthorTurns;
		const uint64 MaxVariableCanonicalCostBytes =
			(PerTurn.MaxCanonicalCostBytes
				- FSeinNetCommandWireCodec::FixedBatchHeaderBytes)
			* GSeinMaxBufferedAuthorTurns;
		return Added.VariableWireBytes >= 0
			&& static_cast<int64>(ExistingCount) + 1 <= MaxCount
			&& ExistingVariableWireBytes <= MaxVariableWireBytes - Added.VariableWireBytes
			&& Added.VariableCanonicalCostBytes
				<= MaxVariableCanonicalCostBytes
			&& ExistingVariableCanonicalCostBytes
				<= MaxVariableCanonicalCostBytes
					- Added.VariableCanonicalCostBytes;
	}

	void RemoveOutgoingPrefix(FSeinOutgoingDraftBacklog& Backlog, int32 Count)
	{
		Count = FMath::Clamp(Count, 0, Backlog.Drafts.Num());
		Backlog.Drafts.RemoveAt(0, Count, EAllowShrinking::No);
		Backlog.Costs.RemoveAt(0, FMath::Min(Count, Backlog.Costs.Num()), EAllowShrinking::No);
		Backlog.VariableWireBytes = 0;
		Backlog.VariableCanonicalCostBytes = 0;
		for (const FSeinQueuedCommandCost& Cost : Backlog.Costs)
		{
			Backlog.VariableWireBytes += Cost.VariableWireBytes;
			Backlog.VariableCanonicalCostBytes +=
				Cost.VariableCanonicalCostBytes;
		}
	}

	void RemoveAICommandPrefix(FSeinAICommandBacklog& Backlog, int32 Count)
	{
		Count = FMath::Clamp(Count, 0, Backlog.Commands.Num());
		Backlog.Commands.RemoveAt(0, Count, EAllowShrinking::No);
		Backlog.Costs.RemoveAt(0, FMath::Min(Count, Backlog.Costs.Num()), EAllowShrinking::No);
		Backlog.VariableWireBytes = 0;
		Backlog.VariableCanonicalCostBytes = 0;
		for (const FSeinQueuedCommandCost& Cost : Backlog.Costs)
		{
			Backlog.VariableWireBytes += Cost.VariableWireBytes;
			Backlog.VariableCanonicalCostBytes +=
				Cost.VariableCanonicalCostBytes;
		}
	}

	int32 GetMaxCommandsPerCanonicalTurn()
	{
		return SeinReplayFormat::MaxCommandsPerTurn;
	}

	FSeinNetworkParticipantID MakeParticipantID(
		const FSeinMatchInstanceID& MatchID,
		const FString& StableRole)
	{
		const FString Identity = FString::Printf(
			TEXT("/SeinARTS/Network/%s/Participant/%s"),
			*MatchID.ToCanonicalString(),
			*StableRole);
		return FSeinNetworkParticipantID(FGuid::NewDeterministicGuid(
			Identity,
			0x5345494E4E455431ull));
	}

	bool AreMatchSettingsIdentical(
		const FSeinMatchSettings& A,
		const FSeinMatchSettings& B)
	{
		return FSeinMatchSettings::StaticStruct()->CompareScriptStruct(&A, &B, 0);
	}

	bool ComputeMatchSettingsDigest(
		FSeinMatchSettings& Settings,
		FGuid& OutDigest,
		const TCHAR* Operation)
	{
		FSeinDeterministicValueDigestError Error;
		if (SeinCanonicalizeAndDigestMatchSettings(Settings, OutDigest, &Error)
			&& OutDigest.IsValid())
		{
			return true;
		}

		OutDigest.Invalidate();
		UE_LOG(LogSeinNet, Error,
			TEXT("%s: canonical match-settings digest failed closed (field=%s error=%s)."),
			Operation, *Error.FieldPath, *Error.Message);
		return false;
	}
}

void USeinNetSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bModuleOwnedStateReleased = false;

	PostLoginHandle = FGameModeEvents::GameModePostLoginEvent.AddUObject(this, &USeinNetSubsystem::OnPostLogin);
	LogoutHandle = FGameModeEvents::GameModeLogoutEvent.AddUObject(this, &USeinNetSubsystem::OnLogout);

	// Hook world cleanup so we can reset lockstep state between worlds. This
	// subsystem is GameInstance-scoped — it survives map travel — but the
	// per-match lockstep state (turn buffers, completed turns, last-submitted
	// counter) is only valid within ONE simulation. If a sim runs in world A
	// (e.g. an auto-started ghost-sim in a menu map) and world B is then
	// loaded, the new sim should start from a clean lockstep state. Without
	// this hook, world B's fresh sim inherits world A's `LastSubmittedTurn`,
	// `ReceivedTurns`, etc., causing instant gate stalls (sim ticks to turn N
	// in world B but turn N's data is from world A's already-completed
	// pipeline). Relay tracking (`Relays`, `RelayToSlot`, `LocalRelay`) is
	// peer-identity, not lockstep state, and is preserved.
	WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &USeinNetSubsystem::OnWorldCleanup);
	if (GEngine)
	{
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(
			this, &USeinNetSubsystem::OnTravelFailure);
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(
			this, &USeinNetSubsystem::OnNetworkFailure);
	}

	UE_LOG(LogSeinNet, Log, TEXT("USeinNetSubsystem initialized."));
}

void USeinNetSubsystem::Deinitialize()
{
	ReleaseModuleOwnedStateForModuleUnload();
	Super::Deinitialize();
}

void USeinNetSubsystem::ReleaseModuleOwnedStateForModuleUnload()
{
	check(IsInGameThread());
	if (bModuleOwnedStateReleased) return;
	bModuleOwnedStateReleased = true;

	if (PostLoginHandle.IsValid())
	{
		FGameModeEvents::GameModePostLoginEvent.Remove(PostLoginHandle);
		PostLoginHandle.Reset();
	}
	if (LogoutHandle.IsValid())
	{
		FGameModeEvents::GameModeLogoutEvent.Remove(LogoutHandle);
		LogoutHandle.Reset();
	}
	if (WorldCleanupHandle.IsValid())
	{
		FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
		WorldCleanupHandle.Reset();
	}
	if (GEngine)
	{
		if (TravelFailureHandle.IsValid())
		{
			GEngine->OnTravelFailure().Remove(TravelFailureHandle);
		}
		if (NetworkFailureHandle.IsValid())
		{
			GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		}
	}
	TravelFailureHandle.Reset();
	NetworkFailureHandle.Reset();

	// Flush the replay log to disk on session teardown. PIE Stop = end of
	// match for our purposes, so we always write a file. If the writer isn't
	// recording (e.g. session never started), FinishRecording is a no-op.
	if (ReplayWriter && ReplayWriter->IsRecording())
	{
		const FString WrittenPath = ReplayWriter->FinishRecording();
		if (!WrittenPath.IsEmpty())
		{
			UE_LOG(LogSeinNet, Log, TEXT("Deinitialize: replay flushed -> %s"), *WrittenPath);
		}
	}
	ReplayWriter = nullptr;

	if (ReplayReader)
	{
		ReplayReader->Stop();
	}
	ReplayReader = nullptr;

	// nullptr deliberately means "detach whichever world is actually bound";
	// the GI's current world may differ during seamless-travel teardown.
	ResetMatchState(nullptr);
	ClearDeterminismSessionFailureSubmitter();

	Relays.Reset();
	LocalRelay.Reset();
	RelayToSlot.Reset();
	RelayToParticipant.Reset();
	PendingTravelIntent = ESeinMatchTravelIntent::NewMatch;

	OnDeterminismSessionFailure.Clear();
	OnDeterminismSessionFailureBP.Clear();
	OnTurnReceived.Clear();
	OnLocalSlotChanged.Clear();
	OnLocalSlotChangedBP.Clear();
	OnLocalCommandIssued.Clear();
	OnLocalCommandIssuedBP.Clear();

#if WITH_DEV_AUTOMATION_TESTS
	TestTurnSubmitOverride = nullptr;
	TestWorldStateRootSubmitOverride = nullptr;
	TestDeterminismSessionFailureSubmitOverride = nullptr;
	TestWorldStateRootResolverOverride = nullptr;
	TestServerOverride.Reset();
	TestDedicatedAuthorityOverride.Reset();
	TestParticipantManifestOverride.Reset();
	TestCommandProtocolDigestOverride.Reset();
	TestSimulationContentDigestOverride.Reset();
	TestCommandProtocolMaxCommandsOverride.Reset();
	TestNetworkingActiveOverride.Reset();
	TestDeterminismGossipEnabledOverride.Reset();
	TestDeterminismCheckIntervalOverride.Reset();
	TestCurrentTurnOverride.Reset();
	TestFindCommandSchemaOverride = nullptr;
#endif
}

void USeinNetSubsystem::OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	if (!World) return;

	// Only react to OUR GameInstance's world cleanup. PIE editor worlds for
	// other GIs (sub-PIE windows, asset browser previews, etc) shouldn't
	// touch our state.
	if (World->GetGameInstance() != GetGameInstance()) return;
	if (const USeinWorldSubsystem* BoundWorldSub = CachedWorldSub.Get())
	{
		// Seamless travel can initialize and bind the destination before the
		// source world finishes cleanup. A late source callback must not erase
		// the destination epoch or detach its hooks.
		if (BoundWorldSub->GetWorld() != World) return;
	}

	// A NON-lockstep map change (console `open`, editor travel) bypasses
	// RetireReplayEpochForCommittedTravel, which is the path that normally
	// closes the replay pair at travel commit. Without this, an active
	// reader stays IsPlaying against the dead world and refuses every
	// LoadFromFile until a manual Sein.Net.StopReplay, and an active writer
	// never publishes its journal. The GameInstance + bound-world guards
	// above already ensure this cleanup is for the world they're driving;
	// for committed lockstep travel both are null here (no-op).
	if (ReplayWriter && ReplayWriter->IsRecording())
	{
		const FString PublishedReplay = ReplayWriter->FinishRecording();
		if (PublishedReplay.IsEmpty())
		{
			UE_LOG(LogSeinNet, Warning,
				TEXT("OnWorldCleanup: could not publish the replay for world %s; its valid partial remains at %s."),
				*GetNameSafe(World), *ReplayWriter->GetActivePartialPath());
		}
		else
		{
			UE_LOG(LogSeinNet, Log,
				TEXT("OnWorldCleanup: replay flushed -> %s"), *PublishedReplay);
		}
	}
	ReplayWriter = nullptr;
	if (ReplayReader && ReplayReader->IsPlaying())
	{
		ReplayReader->Stop();
	}
	ReplayReader = nullptr;

	const bool bHadState =
		!TurnAggregator.GetPendingTurnIDs().IsEmpty() ||
		!ReceivedTurns.IsEmpty() ||
		!PendingOutgoingDrafts.IsEmpty() ||
		!PendingTurnSubmissions.IsEmpty() ||
		TurnAggregator.GetTurnRejectionFloor() >= 0 ||
		!ServerWorldStateRootReports.IsEmpty() ||
		!PendingWorldStateRootReports.IsEmpty() ||
		!AITakeoverControllers.IsEmpty() ||
		LastSubmittedTurn != -1 ||
		LastWorldStateRootReportedTurn != -1;

	ResetLockstepEpochState(World);

	if (bHadState)
	{
		UE_LOG(LogSeinNet, Log,
			TEXT("OnWorldCleanup: reset lockstep state for world %s (sessionEnded=%d)."),
			*GetNameSafe(World), bSessionEnded ? 1 : 0);
	}
}

bool USeinNetSubsystem::OwnsFailureWorld(const UWorld* World) const
{
	const UGameInstance* GameInstance = GetGameInstance();
	// Engine failure delegates are global. An unattributed null world must not
	// cancel every pending assignment in a multi-PIE process.
	return GameInstance && World
		&& World->GetGameInstance() == GameInstance;
}

void USeinNetSubsystem::CancelPendingLocalTravelFailure(
	const FString& Reason)
{
	if (!PendingLocalProtocolAssignment.IsSet()) return;
	const FSeinProtocolContext Context =
		PendingLocalProtocolAssignment.Context;
	ClientHandlePreparedMatchTravelCancelled(Context);
	UE_LOG(LogSeinNet, Error,
		TEXT("Prepared destination failed locally before activation; source epoch preserved: %s"),
		*Reason.Left(512));
}

void USeinNetSubsystem::OnTravelFailure(
	UWorld* World,
	ETravelFailure::Type FailureType,
	const FString& ErrorString)
{
	if ((!PendingAuthorityProtocolState.IsSet()
			&& !PendingLocalProtocolAssignment.IsSet())
		|| !OwnsFailureWorld(World))
	{
		return;
	}

	const FString Reason = FString::Printf(
		TEXT("Travel failure %s: %s"),
		ETravelFailure::ToString(FailureType),
		ErrorString.IsEmpty() ? TEXT("no engine diagnostic") : *ErrorString);
	if (PendingAuthorityProtocolState.IsSet())
	{
		// The callback can arrive after the engine has browsed to a fallback
		// world, so pending coordinator ownership—not current NetMode—is the
		// durable authority proof for this rollback.
		AbortPreparedMatchTravel(Reason);
		return;
	}
	CancelPendingLocalTravelFailure(Reason);
}

void USeinNetSubsystem::OnNetworkFailure(
	UWorld* World,
	UNetDriver* NetDriver,
	ENetworkFailure::Type FailureType,
	const FString& ErrorString)
{
	if (!PendingLocalProtocolAssignment.IsSet())
	{
		return;
	}
	UWorld* FailureWorld = World;
	if (!FailureWorld && NetDriver)
	{
		FailureWorld = NetDriver->GetWorld();
	}
	bool bOwnedPendingDriver = false;
	if (NetDriver && GEngine)
	{
		if (const FWorldContext* Context =
			GEngine->GetWorldContextFromPendingNetGameNetDriver(NetDriver))
		{
			bOwnedPendingDriver =
				Context->OwningGameInstance == GetGameInstance();
		}
	}
	const bool bOwnedFailure = OwnsFailureWorld(FailureWorld)
		|| bOwnedPendingDriver;
	if (!bOwnedFailure) return;

	const UWorld* SourceWorld =
		PendingLocalProtocolAssignment.SourceWorld.Get();
	const UWorld* CurrentWorld = GetWorld();
	const bool bClientDriver = NetDriver && NetDriver->ServerConnection;
	if (!bOwnedPendingDriver && !bClientDriver
		&& (!FailureWorld || FailureWorld->GetNetMode() != NM_Client)
		&& (!SourceWorld || SourceWorld->GetNetMode() != NM_Client)
		&& (!CurrentWorld || CurrentWorld->GetNetMode() != NM_Client))
	{
		return;
	}

	CancelPendingLocalTravelFailure(FString::Printf(
		TEXT("Network failure %s: %s"),
		ENetworkFailure::ToString(FailureType),
		ErrorString.IsEmpty() ? TEXT("no engine diagnostic") : *ErrorString));
}

void USeinNetSubsystem::ReleaseWorldOwnedAI(UWorld* RetiringWorld)
{
	USeinWorldSubsystem* RetiringWorldSub =
		RetiringWorld ? RetiringWorld->GetSubsystem<USeinWorldSubsystem>() : nullptr;

	for (TPair<FSeinPlayerID, TObjectPtr<USeinAIController>>& Pair : AITakeoverControllers)
	{
		USeinAIController* Controller = Pair.Value.Get();
		if (!IsValid(Controller)) continue;

		USeinWorldSubsystem* OwnerSub = Controller->WorldSubsystem.Get();
		if (IsValid(RetiringWorldSub) && OwnerSub == RetiringWorldSub)
		{
			RetiringWorldSub->UnregisterAIController(Controller);
		}
		else if (IsValid(OwnerSub) && (!RetiringWorld || OwnerSub->GetWorld() == RetiringWorld))
		{
			// Cleanup ordering can make GetSubsystem unavailable before this
			// GameInstance delegate runs. Preserve the controller lifecycle and,
			// most importantly, break its strong back-reference to the old world.
			Controller->OnUnregistered();
			Controller->WorldSubsystem = nullptr;
		}
	}

	AITakeoverControllers.Reset();
	PendingAICommands.Reset();
}

void USeinNetSubsystem::ResetLockstepEpochState(UWorld* RetiringWorld)
{
	CancelBootstrapMaterializerRetry();
	CancelBootstrapCoordinatorTimeout();

	// Detach from the world we actually bound, even when this is an in-place
	// epoch reset with no retiring-world pointer. Clearing only the cached
	// handle would otherwise leave live delegates behind across match/module
	// reloads and make the next bind stack another callback.
	USeinWorldSubsystem* BoundWorldSub = CachedWorldSub.Get();
	const bool bDetachBoundWorld =
		BoundWorldSub && (!RetiringWorld || BoundWorldSub->GetWorld() == RetiringWorld);
	if (bDetachBoundWorld)
	{
		BoundWorldSub->TurnReadyResolver.Unbind();
		BoundWorldSub->TurnConsumeNotifier.Unbind();
		BoundWorldSub->ClearAIEmitInterceptor();
		BoundWorldSub->ClearLocalCommandSubmitter();
		if (TickCompletedHandle.IsValid())
		{
			BoundWorldSub->OnSimTickCompleted.Remove(TickCompletedHandle);
		}
		if (ExecutionTopologyInvalidatedHandle.IsValid())
		{
			BoundWorldSub->OnExecutionTopologyInvalidated.Remove(
				ExecutionTopologyInvalidatedHandle);
		}
	}

	if (!BoundWorldSub || bDetachBoundWorld)
	{
		TickCompletedHandle.Reset();
		ExecutionTopologyInvalidatedHandle.Reset();
		CachedWorldSub.Reset();
	}
	ReleaseWorldOwnedAI(RetiringWorld);

	TurnAggregator.Reset();
	ConfigureTurnAggregator();
	ReceivedTurns.Reset();
	RetainedAssembledTurns.Reset();
	RetainedAssembledTurnFloor = -1;
	ServerResyncServes.Reset();
	HeartbeatCoverageThroughTurn.Reset();
	WorldRootReportExemptionThroughTurn.Reset();
	SlotReconnectingSinceTime.Reset();
	ClientResetResyncState(TEXT("protocol session reset"));
	PendingOutgoingDrafts.Reset();
	PendingTurnSubmissions.Reset();
	LastQueuedTurn = -1;
	LastSubmittedTurn = -1;
	bStartSessionRequested = false;
	bServerStartRequested = false;
	ServerWorldStateRootReports.Reset();
	CompletedWorldStateRootChecks.Reset();
	CompletedWorldStateRootRejectionFloor = -1;
	LatestSuccessfulWorldStateRootCheckTurn = -1;
	LatestSuccessfulWorldStateRootReporterCount = 0;
	PendingWorldStateRootReports.Reset();
	LastWorldStateRootQueuedTurn = -1;
	LastWorldStateRootReportedTurn = -1;
	DeterminismSessionFailure = FSeinDeterminismSessionFailure();
	bDeterminismSessionFailureAuthoritative = false;
	LocalDeterminismFailureDiagnostic.Reset();
	PendingDeterminismSessionFailureReport.Reset();
	PendingAuthenticatedDeterminismSessionFailures.Reset();
	BootstrapConsensus.Reset();
	LocalBootstrapReceipt.Reset();
	PendingLocalBootstrapReceiptReportContext.Reset();
	PendingLocalBootstrapAuthorizedReadyReportContext.Reset();
	DeferredBootstrapReceiptRequestContext.Reset();
	PendingBootstrapAuthorizationContext.Reset();
	PendingBootstrapAuthorizationReceipt.Reset();
	PendingBootstrapLaunchContext.Reset();
	PendingBootstrapLaunchReceipt.Reset();
	MatchBootstrapAuthority = FSeinMatchBootstrapAuthorityHandle();
	BootstrapSessionFailureReason.Reset();
	bBootstrapFailureReported = false;
	bBootstrapLaunchBarrierActive = false;
	bLocalBootstrapIngressClosed = false;

	LastStalledTurn = -1;
	FirstStalledAtTime = 0.0;
	LastStallLogTime = 0.0;
	bStallLogEscalated = false;
	IncompleteTurnDiagnostics.Reset();
}

void USeinNetSubsystem::ResetMatchState(UWorld* RetiringWorld)
{
	ResetLockstepEpochState(RetiringWorld);
	CancelPendingProtocolPromotion();
	PendingAuthorityProtocolState.Reset();
	PendingLocalProtocolAssignment.Reset();
	ParticipantBindings.Reset();
	SlotToParticipant.Reset();
	RelayToParticipant.Reset();
	CoordinatorParticipantID = FSeinNetworkParticipantID::Invalid();
	ActiveProtocolContext = FSeinProtocolContext();
	ActiveMatchSettings = FSeinMatchSettings();
	bHasActiveMatchSettings = false;
	FrozenMaxCommandsPerSubmission = 0;
	RequiredStartParticipants.Reset();
	BootstrapConsensus.Reset();
	LocalBootstrapReceipt.Reset();
	PendingLocalBootstrapReceiptReportContext.Reset();
	PendingLocalBootstrapAuthorizedReadyReportContext.Reset();
	DeferredBootstrapReceiptRequestContext.Reset();
	PendingBootstrapAuthorizationContext.Reset();
	PendingBootstrapAuthorizationReceipt.Reset();
	PendingBootstrapLaunchContext.Reset();
	PendingBootstrapLaunchReceipt.Reset();
	MatchBootstrapAuthority = FSeinMatchBootstrapAuthorityHandle();
	BootstrapSessionFailureReason.Reset();
	bBootstrapFailureReported = false;
	bBootstrapLaunchBarrierActive = false;
	bLocalBootstrapIngressClosed = false;
	AcceptedConfigFingerprints.Reset();
	TurnAggregator.Reset();
	LocalPlayerID = FSeinPlayerID::Neutral();
	LocalParticipantID = FSeinNetworkParticipantID::Invalid();
	bLocalParticipantSimulates = false;
	SessionSeed = 0;
	SlotLifecycle.Reset();
	SlotDroppedAtTime.Reset();
	StragglerCounts.Reset();
	TurnsCompletedCount = 0;
	bDesyncDetected = false;
	bDestinationStartPending = false;
}

int32 USeinNetSubsystem::GetTicksPerTurn() const
{
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	if (!Settings || Settings->TurnRate <= 0) return 1;
	return FMath::Max(1, Settings->SimulationTickRate / Settings->TurnRate);
}

int32 USeinNetSubsystem::GetInputDelayTurns() const
{
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	return (Settings && Settings->InputDelayTurns > 0) ? Settings->InputDelayTurns : 3;
}

int32 USeinNetSubsystem::GetCurrentTurn() const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (TestCurrentTurnOverride.IsSet())
	{
		return FMath::Max(0, TestCurrentTurnOverride.GetValue());
	}
#endif
	const UWorld* World = GetWorld();
	const USeinWorldSubsystem* WorldSub = World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	return WorldSub ? FMath::Max(0, WorldSub->GetCurrentTick() / GetTicksPerTurn()) : 0;
}

bool USeinNetSubsystem::IsCommandTurnWithinProtocolWindow(int32 TurnId, const TCHAR* Context) const
{
	if (TurnId < 0)
	{
		UE_LOG(LogSeinNet, Warning, TEXT("%s: rejecting negative TurnId=%d."), Context, TurnId);
		return false;
	}

	const int32 CurrentTurn = GetCurrentTurn();
	if (TurnId < CurrentTurn)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("%s: rejecting stale TurnId=%d (current=%d)."), Context, TurnId, CurrentTurn);
		return false;
	}

	const int64 MaxAccepted = static_cast<int64>(CurrentTurn) + GetInputDelayTurns() + GSeinMaxProtocolTurnLead;
	if (static_cast<int64>(TurnId) > MaxAccepted)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("%s: rejecting implausible future TurnId=%d (current=%d max=%lld)."),
			Context, TurnId, CurrentTurn, MaxAccepted);
		return false;
	}
	return true;
}

bool USeinNetSubsystem::IsDeterminismEvidenceTurnWithinProtocolWindow(
	int32 Turn,
	const TCHAR* Context) const
{
	if (Turn < 0)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("%s: rejecting negative determinism-evidence turn=%d."),
			Context, Turn);
		return false;
	}

	const int32 CurrentTurn = GetCurrentTurn();
	const int32 OldestAccepted = FMath::Max(0, CurrentTurn - GSeinRetainedHistoryTurns);
	const int64 MaxAccepted = static_cast<int64>(CurrentTurn) + GSeinMaxProtocolTurnLead;
	if (Turn < OldestAccepted || static_cast<int64>(Turn) > MaxAccepted)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("%s: rejecting determinism-evidence turn=%d outside [%d,%lld] (current=%d)."),
			Context, Turn, OldestAccepted, MaxAccepted, CurrentTurn);
		return false;
	}
	return true;
}

void USeinNetSubsystem::ExpireIncompleteWorldStateRootCheckpointsThrough(
	int32 Cutoff)
{
	if (!IsDeterminismGossipEnabled()
		|| !IsLocalProtocolCoordinator()
		|| bDeterminismSessionFailureAuthoritative
		|| Cutoff <= CompletedWorldStateRootRejectionFloor)
	{
		return;
	}

	TArray<FSeinNetworkParticipantID> ExpectedParticipants;
	GetExpectedWorldRootReporterParticipants(ExpectedParticipants);
	// A root has value only as a peer comparison. Capturing and reporting it
	// for a sole reporter performs the complete stop-the-world audit without
	// any possibility of detecting divergence.
	bool bForceSingleReporterObligationForTests = false;
#if WITH_DEV_AUTOMATION_TESTS
	bForceSingleReporterObligationForTests =
		TestDeterminismCheckIntervalOverride.IsSet();
#endif
	if (ExpectedParticipants.Num() <= 1
		&& !bForceSingleReporterObligationForTests)
	{
		return;
	}

	const int32 Interval = GetDeterminismCheckIntervalTurns();
	if (Interval <= 0) return;
	const int64 FirstPossible = FMath::Max<int64>(
		Interval,
		static_cast<int64>(CompletedWorldStateRootRejectionFloor) + 1);
	const int64 FirstDue =
		((FirstPossible + Interval - 1) / Interval) * Interval;

	for (int64 Due64 = FirstDue; Due64 <= Cutoff; Due64 += Interval)
	{
		const int32 DueTurn = static_cast<int32>(Due64);
		if (CompletedWorldStateRootChecks.Contains(DueTurn)) continue;

		const TMap<FSeinNetworkParticipantID, FGuid>* Reports =
			ServerWorldStateRootReports.Find(DueTurn);
		if (Reports && AreExpectedWorldRootReportsComplete(*Reports))
		{
			ServerCompareWorldStateRootsForTurn(DueTurn);
			if (CompletedWorldStateRootChecks.Contains(DueTurn))
			{
				continue;
			}
		}

		FSeinNetworkParticipantID FirstMissing =
			FSeinNetworkParticipantID::Invalid();
		bool bAnyExemptMissing = false;
		for (const FSeinNetworkParticipantID ParticipantID :
			ExpectedParticipants)
		{
			if (!Reports || !Reports->Contains(ParticipantID))
			{
				// A boundary this participant completed while its resync
				// suppression was active can never be back-reported; the
				// activation flip records an exemption so an unreportable
				// due turn cannot expire into an authoritative session kill
				// blaming a recovered peer.
				const int32* ExemptThrough =
					WorldRootReportExemptionThroughTurn.Find(ParticipantID);
				if (ExemptThrough && DueTurn <= *ExemptThrough)
				{
					bAnyExemptMissing = true;
					continue;
				}
				FirstMissing = ParticipantID;
				break;
			}
		}
		if (!FirstMissing.IsValid())
		{
			if (bAnyExemptMissing)
			{
				// Every non-exempt reporter delivered: judge the reduced set
				// now instead of letting the obligation wedge.
				ServerCompareWorldStateRootsForTurn(DueTurn);
			}
			continue;
		}

		FSeinDeterminismSessionFailure Failure;
		Failure.Kind =
			ESeinDeterminismSessionFailureKind::
				CanonicalRootCheckpointExpired;
		Failure.Turn = DueTurn;
		Failure.ParticipantID = FirstMissing;
		EnterDeterminismSessionFailure(
			Failure,
			/*bAuthoritative=*/true,
			/*bNotifyPeers=*/true);
		return;
	}
}

void USeinNetSubsystem::PruneProtocolState(int32 ReferenceTurn)
{
	ApplyDueAuthenticatedDeterminismSessionFailuresThrough(
		ReferenceTurn);
	const int32 Cutoff = ReferenceTurn - GSeinRetainedHistoryTurns;
	if (Cutoff <= -1) return;

	for (const int32 PendingTurn : TurnAggregator.GetPendingTurnIDs())
	{
		if (PendingTurn <= Cutoff)
		{
			UE_LOG(LogSeinNet, Warning,
				TEXT("[Server] pruning incomplete obsolete turn=%d."), PendingTurn);
		}
	}
	TurnAggregator.PruneThroughTurn(Cutoff);
	for (auto It = IncompleteTurnDiagnostics.CreateIterator(); It; ++It)
	{
		if (It.Key() <= TurnAggregator.GetTurnRejectionFloor()) It.RemoveCurrent();
	}
	for (auto It = ReceivedTurns.CreateIterator(); It; ++It)
	{
		if (It.Key() <= TurnAggregator.GetTurnRejectionFloor()) It.RemoveCurrent();
	}
	for (auto It = RetainedAssembledTurns.CreateIterator(); It; ++It)
	{
		if (It.Key() <= Cutoff) It.RemoveCurrent();
	}
	RetainedAssembledTurnFloor =
		FMath::Max(RetainedAssembledTurnFloor, Cutoff);

	// A due proof obligation must become an explicit terminal health result
	// before its evidence is aged out. This also catches the zero-report case,
	// for which no ServerWorldStateRootReports entry exists to warn about.
	ExpireIncompleteWorldStateRootCheckpointsThrough(Cutoff);
	CompletedWorldStateRootRejectionFloor =
		FMath::Max(CompletedWorldStateRootRejectionFloor, Cutoff);
	for (auto It = CompletedWorldStateRootChecks.CreateIterator(); It; ++It)
	{
		if (*It <= CompletedWorldStateRootRejectionFloor) It.RemoveCurrent();
	}
	for (auto It = ServerWorldStateRootReports.CreateIterator(); It; ++It)
	{
		if (It.Key() <= CompletedWorldStateRootRejectionFloor)
		{
			UE_LOG(LogSeinNet, Warning,
				TEXT("[DETERMINISM] pruning obsolete world-root evidence for turn=%d after session-health evaluation."),
				It.Key());
			It.RemoveCurrent();
		}
	}
	for (int32 Index = PendingWorldStateRootReports.Num() - 1;
		Index >= 0; --Index)
	{
		if (PendingWorldStateRootReports[Index].Turn
			<= CompletedWorldStateRootRejectionFloor)
		{
			const int32 ExpiredTurn =
				PendingWorldStateRootReports[Index].Turn;
			if (IsDueWorldStateRootCheckpoint(ExpiredTurn)
				&& LocalParticipantID.IsValid()
				&& !DeterminismSessionFailure.IsValid())
			{
				FSeinDeterminismSessionFailure Failure;
				Failure.Kind =
					ESeinDeterminismSessionFailureKind::
						CanonicalRootCheckpointExpired;
				Failure.Turn = ExpiredTurn;
				Failure.ParticipantID = LocalParticipantID;
				EnterDeterminismSessionFailure(
					Failure,
					/*bAuthoritative=*/false,
					/*bNotifyPeers=*/false);
			}
			UE_LOG(LogSeinNet, Warning,
				TEXT("[DETERMINISM] discarding locally pending obsolete world-root turn=%d after session-health evaluation."),
				ExpiredTurn);
			PendingWorldStateRootReports.RemoveAt(
				Index, 1, EAllowShrinking::No);
		}
	}
}

void USeinNetSubsystem::GetExpectedCommandSlots(TArray<FSeinPlayerID>& OutSlots) const
{
	OutSlots.Reset();
	for (const FSeinParticipantBinding& Binding : ParticipantBindings)
	{
		for (const FSeinPlayerID Slot : Binding.CommandSlots)
		{
			OutSlots.Add(Slot);
		}
	}
	OutSlots.Sort([](const FSeinPlayerID& A, const FSeinPlayerID& B)
	{
		return A.Value < B.Value;
	});
}

bool USeinNetSubsystem::IsCommandSubmissionLifecycleAllowed(FSeinPlayerID Slot) const
{
	const ESeinSlotLifecycle* Lifecycle = SlotLifecycle.Find(Slot);
	return Lifecycle && *Lifecycle == ESeinSlotLifecycle::Connected;
}

bool USeinNetSubsystem::IsAICommandSubmissionAllowed(
	FSeinPlayerID Slot,
	FString* OutError) const
{
	auto Reject = [OutError](const TCHAR* Error)
	{
		if (OutError) *OutError = Error;
		return false;
	};
	if (!Slot.IsValid())
	{
		return Reject(TEXT("slot identity is invalid"));
	}
	const FSeinNetworkParticipantID ParticipantID = FindParticipantForSlot(Slot);
	const FSeinParticipantBinding* Binding = FindParticipantBinding(ParticipantID);
	if (!ParticipantID.IsValid() || !Binding || !Binding->CommandSlots.Contains(Slot))
	{
		return Reject(TEXT("slot is absent from the frozen command-author manifest"));
	}
	const ESeinSlotLifecycle* Lifecycle = SlotLifecycle.Find(Slot);
	if (!Lifecycle || *Lifecycle != ESeinSlotLifecycle::AITakeover)
	{
		return Reject(TEXT("only an AI-takeover slot may author AI commands"));
	}
	return true;
}

USeinNetSubsystem::EFirstAcceptResult
USeinNetSubsystem::BufferWorldStateRootReportFirstWins(
	int32 Turn,
	FSeinNetworkParticipantID ParticipantID,
	const FGuid& WorldRoot)
{
	TMap<FSeinNetworkParticipantID, FGuid>& TurnBuffer =
		ServerWorldStateRootReports.FindOrAdd(Turn);
	if (const FGuid* Existing = TurnBuffer.Find(ParticipantID))
	{
		return *Existing == WorldRoot
			? EFirstAcceptResult::IdenticalDuplicate
			: EFirstAcceptResult::ConflictingDuplicate;
	}

	TurnBuffer.Add(ParticipantID, WorldRoot);
	return EFirstAcceptResult::Accepted;
}

void USeinNetSubsystem::GetExpectedWorldRootReporterParticipants(
	TArray<FSeinNetworkParticipantID>& OutParticipants) const
{
	OutParticipants.Reset();
	for (const FSeinParticipantBinding& Binding : ParticipantBindings)
	{
		if (Binding.bReportsWorldRoots
			&& IsParticipantConnected(Binding.ParticipantID))
		{
			OutParticipants.Add(Binding.ParticipantID);
		}
	}
	OutParticipants.Sort([](
		const FSeinNetworkParticipantID& A,
		const FSeinNetworkParticipantID& B)
	{
		return A.ToCanonicalString() < B.ToCanonicalString();
	});
}

bool USeinNetSubsystem::HasComparableWorldRootPeerInManifest() const
{
	int32 ReporterCount = 0;
	for (const FSeinParticipantBinding& Binding : ParticipantBindings)
	{
		if (Binding.bReportsWorldRoots && ++ReporterCount > 1)
		{
			return true;
		}
	}
	return false;
}

bool USeinNetSubsystem::AreExpectedWorldRootReportsComplete(
	const TMap<FSeinNetworkParticipantID, FGuid>& Reports) const
{
	TArray<FSeinNetworkParticipantID> ExpectedParticipants;
	GetExpectedWorldRootReporterParticipants(ExpectedParticipants);
	if (ExpectedParticipants.IsEmpty()) return false;
	for (const FSeinNetworkParticipantID ParticipantID : ExpectedParticipants)
	{
		if (!Reports.Contains(ParticipantID)) return false;
	}
	return true;
}

bool USeinNetSubsystem::IsDeterminismGossipEnabled() const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (TestDeterminismGossipEnabledOverride.IsSet())
	{
		return TestDeterminismGossipEnabledOverride.GetValue();
	}
#endif
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	return Settings && Settings->bDeterminismChecksEnabled;
}

bool USeinNetSubsystem::IsConfigParityCheckEnabled() const
{
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	return Settings && Settings->bConfigParityCheckEnabled;
}

int32 USeinNetSubsystem::GetDeterminismCheckIntervalTurns() const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (TestDeterminismCheckIntervalOverride.IsSet())
	{
		return FMath::Max(1, TestDeterminismCheckIntervalOverride.GetValue());
	}
#endif
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	return (Settings && Settings->DeterminismCheckIntervalTurns > 0) ? Settings->DeterminismCheckIntervalTurns : 10;
}

bool USeinNetSubsystem::IsDueWorldStateRootCheckpoint(int32 Turn) const
{
	const int32 Interval = GetDeterminismCheckIntervalTurns();
	return Turn > 0 && Interval > 0 && Turn % Interval == 0;
}

bool USeinNetSubsystem::IsServer() const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (TestServerOverride.IsSet()) return TestServerOverride.GetValue();
#endif
	const UWorld* World = GetWorld();
	if (!World) return false;
	const ENetMode Mode = World->GetNetMode();
	return Mode == NM_DedicatedServer || Mode == NM_ListenServer;
}

bool USeinNetSubsystem::IsDedicatedAuthority() const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (TestDedicatedAuthorityOverride.IsSet())
	{
		return TestDedicatedAuthorityOverride.GetValue();
	}
#endif
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() == NM_DedicatedServer;
}

bool USeinNetSubsystem::IsNetworkingActive() const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (TestNetworkingActiveOverride.IsSet())
	{
		return TestNetworkingActiveOverride.GetValue();
	}
#endif
	const UWorld* World = GetWorld();
	if (!World) return false;
	if (World->GetNetMode() == NM_Standalone) return false;

	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	return Settings && Settings->bNetworkingEnabled;
}

void USeinNetSubsystem::OnPostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer)
{
	if (!GameMode || !NewPlayer) return;
	if (!IsServer()) return;

	UWorld* World = GameMode->GetWorld();
	if (!World) return;
	if (World->GetGameInstance() != GetGameInstance())
	{
		// Some other GI's GameMode (e.g. a sub-PIE world) — ignore.
		return;
	}

	// Relay spawn moved into ServerSpawnRelayForController, which is invoked
	// only after an authority has chosen the controller's exact slot: either
	// ASeinGameMode's frozen match manifest or the lobby's final launch
	// bindings. This eliminates the dual-source-of-truth bug where the old
	// auto-spawn-here path independently sequenced slots
	// (NextSlotToAssign++) while GameMode independently picked from match
	// settings — they could disagree if connection order ≠ slot order.
	//
	// We still ensure the session seed is locked at first PostLogin so
	// that whenever the relay spawns, the seed is already stable.
	EnsureSessionSeed();

	UE_LOG(LogSeinNet, Verbose, TEXT("OnPostLogin: PC=%s noted (relay spawn deferred to GameMode binding flow)."),
		*GetNameSafe(NewPlayer));
}

void USeinNetSubsystem::ServerSpawnRelayForController(APlayerController* PC, FSeinPlayerID Slot)
{
	if (!IsServer()) return;
	if (!PC)
	{
		UE_LOG(LogSeinNet, Warning, TEXT("ServerSpawnRelayForController: null PC."));
		return;
	}
	if (!Slot.IsValid())
	{
		UE_LOG(LogSeinNet, Warning, TEXT("ServerSpawnRelayForController: invalid slot for PC %s — no relay spawned (legacy auto-assign path?)."),
			*GetNameSafe(PC));
		return;
	}

	UWorld* World = PC->GetWorld();
	if (!World) return;
	TryPromotePendingAuthorityProtocolState();

	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	if (!Settings || !Settings->bNetworkingEnabled)
	{
		UE_LOG(LogSeinNet, Verbose, TEXT("ServerSpawnRelayForController: networking disabled — skipping."));
		return;
	}
	const FSeinNetworkParticipantID ParticipantID = FindParticipantForSlot(Slot);
	if (ActiveProtocolContext.IsValid() && !ParticipantID.IsValid())
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ServerSpawnRelayForController: slot=%u is absent from active membership %s — fail-closed."),
			Slot.Value, *ActiveProtocolContext.ToCanonicalDebugString());
		return;
	}

	// SLOT COLLISION GUARD / RECONNECT RECLAIM. A Connected slot may never be
	// rebound to a different controller: that would produce two relays for one
	// command author and wedge the lockstep gate. A Dropped/AITakeover/
	// Reconnecting slot is different: OnLogout deliberately retains its relay
	// as the server-side continuity anchor, so a legitimate returning
	// controller must take ownership of that exact actor instead of being
	// rejected as a collision or spawning a duplicate.
	ASeinNetRelay* ReclaimableRelay = nullptr;
	for (const TWeakObjectPtr<ASeinNetRelay>& Wp : Relays)
	{
		ASeinNetRelay* R = Wp.Get();
		if (!R) continue;
		if (R->GetOwner() == PC) continue; // same PC → handled by idempotence below
		const FSeinPlayerID* ExistingSlot = RelayToSlot.Find(R);
		if (ExistingSlot && *ExistingSlot == Slot)
		{
			const ESeinSlotLifecycle* Lifecycle = SlotLifecycle.Find(Slot);
			if (Lifecycle && *Lifecycle != ESeinSlotLifecycle::Connected)
			{
				ReclaimableRelay = R;
				break;
			}
			UE_LOG(LogSeinNet, Error,
				TEXT("[SLOT COLLISION] ServerSpawnRelayForController: slot %u is ALREADY bound to %s (relay %s). Refusing to bind %s to the same slot — would produce dual-binding and freeze the lockstep gate. GameMode bug: two controllers were routed to the same SeinPlayerStart. Investigate ChoosePlayerStart_Implementation and ClaimedSlots tracking."),
				Slot.Value, *GetNameSafe(R->GetOwner()), *GetNameSafe(R), *GetNameSafe(PC));
			return;
		}
	}

	if (ReclaimableRelay)
	{
		const ESeinSlotLifecycle PriorLifecycle =
			SlotLifecycle.FindRef(Slot);
		const bool bWasAITakeover =
			PriorLifecycle == ESeinSlotLifecycle::AITakeover;
		if (bWasAITakeover)
		{
			TeardownAIForSlot(Slot);
		}

		// A returning process must prove config parity again. The participant ID
		// is frozen by the match manifest and remains stable for this slot, but
		// acceptance belongs to the vanished connection, not the slot itself.
		const FSeinNetworkParticipantID PreviousParticipant =
			RelayToParticipant.FindRef(ReclaimableRelay);
		if (PreviousParticipant.IsValid())
		{
			AcceptedConfigFingerprints.Remove(PreviousParticipant);
		}
		if (ParticipantID.IsValid())
		{
			AcceptedConfigFingerprints.Remove(ParticipantID);
		}

		ReclaimableRelay->SetOwner(PC);
		ReclaimableRelay->AssignedPlayerID = Slot;
		ReclaimableRelay->AssignedParticipantID = ParticipantID;
		ReclaimableRelay->ProtocolContext = ActiveProtocolContext;
		ReclaimableRelay->SessionSeed = SessionSeed;
		RelayToSlot.Add(ReclaimableRelay, Slot);
		if (ParticipantID.IsValid())
		{
			RelayToParticipant.Add(ReclaimableRelay, ParticipantID);
		}

		const USeinWorldSubsystem* LaunchSim = GetWorld()
			? GetWorld()->GetSubsystem<USeinWorldSubsystem>() : nullptr;
		const bool bMatchLaunched = LaunchSim
			&& LaunchSim->GetMatchBootstrapState()
				== ESeinMatchBootstrapState::Consumed;
		SlotLifecycle.Add(Slot, bMatchLaunched
			? ESeinSlotLifecycle::Reconnecting
			: ESeinSlotLifecycle::Connected);
		SlotDroppedAtTime.Remove(Slot);
		if (bMatchLaunched)
		{
			SlotReconnectingSinceTime.Add(Slot, FPlatformTime::Seconds());
		}
		else
		{
			SlotReconnectingSinceTime.Remove(Slot);
		}

		ReclaimableRelay->ForceNetUpdate();
		if (ParticipantID.IsValid() && ActiveProtocolContext.IsValid()
			&& bHasActiveMatchSettings)
		{
			ReclaimableRelay->Client_PrepareMatchBootstrap(
				Slot,
				ParticipantID,
				ActiveProtocolContext,
				SessionSeed,
				FindParticipantBinding(ParticipantID)
					&& FindParticipantBinding(ParticipantID)->bSimulates,
				/*bAllowCurrentWorldActivation=*/true,
				ParticipantBindings,
				ActiveMatchSettings);
		}
		UE_LOG(LogSeinNet, Log,
			TEXT("[Resync] slot=%u reclaimed retained relay %s for returning controller %s; lifecycle=%s."),
			Slot.Value,
			*GetNameSafe(ReclaimableRelay),
			*GetNameSafe(PC),
			bMatchLaunched ? TEXT("Reconnecting") : TEXT("Connected"));
		TryRearmPreparedDestinationStart();
		TryDispatchLockstepSessionStart();
		return;
	}

	// Idempotence: if this PC already has a relay (re-bind / seamless travel),
	// re-stamp it instead of spawning a duplicate. Replicates the new slot
	// value to the owning client via OnRep_AssignedPlayerID.
	for (const TWeakObjectPtr<ASeinNetRelay>& Wp : Relays)
	{
		if (ASeinNetRelay* R = Wp.Get())
		{
			if (R->GetOwner() == PC)
			{
				const FSeinPlayerID* PrevSlot = RelayToSlot.Find(R);
				const uint8 PrevSlotValue = PrevSlot ? PrevSlot->Value : 0;
				const FSeinNetworkParticipantID PreviousParticipant =
					RelayToParticipant.FindRef(R);
				if (PreviousParticipant.IsValid() && PreviousParticipant != ParticipantID)
				{
					AcceptedConfigFingerprints.Remove(PreviousParticipant);
				}
				R->AssignedPlayerID = Slot;
				R->AssignedParticipantID = ParticipantID;
				R->ProtocolContext = ActiveProtocolContext;
				R->SessionSeed = SessionSeed;
				RelayToSlot.Add(R, Slot);
				if (ParticipantID.IsValid()) RelayToParticipant.Add(R, ParticipantID);
				R->ForceNetUpdate();
				if (PC->IsLocalController())
				{
					NotifyLocalLobbySlotAssigned(R, Slot);
					if (ParticipantID.IsValid() && ActiveProtocolContext.IsValid()
						&& bHasActiveMatchSettings)
					{
						NotifyLocalProtocolAssigned(
							R,
							Slot,
							ParticipantID,
							ActiveProtocolContext,
							SessionSeed,
							FindParticipantBinding(ParticipantID)
								&& FindParticipantBinding(ParticipantID)->bSimulates,
							ParticipantBindings,
							ActiveMatchSettings,
							ESeinPreparedWorldActivation::AllowCurrentWorld);
					}
				}
				else if (ParticipantID.IsValid() && ActiveProtocolContext.IsValid()
					&& bHasActiveMatchSettings)
				{
					R->Client_PrepareMatchBootstrap(
						Slot,
						ParticipantID,
						ActiveProtocolContext,
						SessionSeed,
						FindParticipantBinding(ParticipantID)
							&& FindParticipantBinding(ParticipantID)->bSimulates,
						/*bAllowCurrentWorldActivation=*/true,
						ParticipantBindings,
						ActiveMatchSettings);
				}
				UE_LOG(LogSeinNet, Log,
					TEXT("ServerSpawnRelayForController: existing relay %s re-stamped slot=%u (was %u) for %s"),
					*GetNameSafe(R), Slot.Value, PrevSlotValue, *GetNameSafe(PC));
				TryRearmPreparedDestinationStart();
				TryDispatchLockstepSessionStart();
				return;
			}
		}
	}

	// WYSIWYG. None/empty => the net relay is OFF: spawn nothing, so this controller gets no relay and
	// lockstep traffic can't flow. A set-but-unloadable class is a mistake, not an off-switch: fall
	// back to the shipped default with a logged error.
	if (Settings->RelayActorClass.IsNull())
	{
		USeinARTSCoreSettings::ReportDisabledSystem(TEXT("Net Relay"),
			TEXT("No relay is spawned, so lockstep networking can't send or receive commands."), /*bHighSeverity*/ true);
		return;
	}
	UClass* RelayClass = Settings->RelayActorClass.TryLoadClass<ASeinNetRelay>();
	if (!RelayClass)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("RelayActorClass '%s' could not be loaded — falling back to the shipped default."),
			*Settings->RelayActorClass.ToString());
		RelayClass = ASeinNetRelay::StaticClass();
	}

	FActorSpawnParameters Params;
	Params.Owner = PC;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	// Deferred spawn so we can stamp AssignedPlayerID + SessionSeed BEFORE
	// BeginPlay runs (and before initial replication ships). On a Listen
	// Server, BeginPlay fires synchronously inside SpawnActor — if we stamp
	// after, RegisterRelay sees a zero slot and the host's LocalPlayerID
	// never latches (server-side has no OnRep callback).
	Params.bDeferConstruction = true;

	ASeinNetRelay* Relay = World->SpawnActor<ASeinNetRelay>(RelayClass, FTransform::Identity, Params);
	if (!Relay)
	{
		UE_LOG(LogSeinNet, Error, TEXT("ServerSpawnRelayForController: failed to spawn relay for %s slot=%u"),
			*GetNameSafe(PC), Slot.Value);
		return;
	}

	EnsureSessionSeed();
	Relay->AssignedPlayerID = Slot;
	Relay->AssignedParticipantID = ParticipantID;
	Relay->ProtocolContext = ActiveProtocolContext;
	Relay->SessionSeed = SessionSeed;
	// A genuinely new relay occupant must prove parity itself; never inherit
	// an earlier process's acceptance merely because the slot was reused.
	if (ParticipantID.IsValid())
	{
		AcceptedConfigFingerprints.Remove(ParticipantID);
	}
	RelayToSlot.Add(Relay, Slot);
	if (ParticipantID.IsValid()) RelayToParticipant.Add(Relay, ParticipantID);

	// Drop-in/drop-out: resolve this slot's lifecycle. Before the match
	// launches, a (re)joining PC is simply Connected. Once the bootstrap has
	// been consumed, a returning PC's world holds STALE (or no) match state —
	// granting instant authorship would inject commands computed from a
	// pre-frontier timeline. The slot therefore lands in Reconnecting
	// (heartbeats keep the gate healthy, authorship withheld) until the peer
	// completes the checkpoint+tail resync and activates. If it was
	// AITakeover, tear down the AI either way so it doesn't fight the
	// returning human for the slot.
	const ESeinSlotLifecycle* Prior = SlotLifecycle.Find(Slot);
	const bool bWasAITakeover = Prior && *Prior == ESeinSlotLifecycle::AITakeover;
	// "Launched" means the SIM's bootstrap authorization was consumed — not
	// merely that match settings were promoted for travel. Gating on the
	// settings flag would strand a pre-launch rejoin in Reconnecting and
	// deadlock the whole session start (launch requires Connected slots).
	const USeinWorldSubsystem* LaunchSim = GetWorld()
		? GetWorld()->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	const bool bMatchLaunched = LaunchSim
		&& LaunchSim->GetMatchBootstrapState()
			== ESeinMatchBootstrapState::Consumed;
	const bool bReturningToLaunchedMatch = bMatchLaunched
		&& Prior
		&& (*Prior == ESeinSlotLifecycle::Dropped || bWasAITakeover);
	SlotLifecycle.Add(Slot, bReturningToLaunchedMatch
		? ESeinSlotLifecycle::Reconnecting
		: ESeinSlotLifecycle::Connected);
	SlotDroppedAtTime.Remove(Slot);
	if (bReturningToLaunchedMatch)
	{
		SlotReconnectingSinceTime.Add(Slot, FPlatformTime::Seconds());
	}
	if (bWasAITakeover)
	{
		TeardownAIForSlot(Slot);
	}
	if (bReturningToLaunchedMatch)
	{
		UE_LOG(LogSeinNet, Log,
			TEXT("[Resync] slot=%u returned to a launched match: authorship withheld until the checkpoint+tail resync activates (auto-requested by the client on its first out-of-window turn)."),
			Slot.Value);
	}

	Relay->FinishSpawning(FTransform::Identity);
	if (ActiveProtocolContext.IsValid() && ParticipantID.IsValid()
		&& bHasActiveMatchSettings)
	{
		const APlayerController* OwnerPC = Cast<APlayerController>(Relay->GetOwner());
		if (!OwnerPC || !OwnerPC->IsLocalController())
		{
			Relay->Client_PrepareMatchBootstrap(
				Slot,
				ParticipantID,
				ActiveProtocolContext,
				SessionSeed,
				FindParticipantBinding(ParticipantID)
					&& FindParticipantBinding(ParticipantID)->bSimulates,
				/*bAllowCurrentWorldActivation=*/true,
				ParticipantBindings,
				ActiveMatchSettings);
		}
	}

	UE_LOG(LogSeinNet, Log,
		TEXT("ServerSpawnRelayForController: spawned %s for %s  slot=%u  seed=%lld  lifecycle=%s"),
		*GetNameSafe(Relay), *GetNameSafe(PC), Slot.Value, SessionSeed,
		bReturningToLaunchedMatch ? TEXT("Reconnecting") : TEXT("Connected"));
	TryRearmPreparedDestinationStart();
	TryDispatchLockstepSessionStart();
}

void USeinNetSubsystem::EnsureSessionSeed()
{
	if (SessionSeed != 0) return;

	// Debug override: when settings.DebugFixedSessionSeed is nonzero, lock the
	// seed to that exact value so successive PIE Plays produce bit-identical
	// sim runs (any PRNG-driven variance disappears, leaving only true
	// determinism bugs visible). Production builds leave it at 0 and the
	// random path runs.
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	if (Settings && Settings->DebugFixedSessionSeed != 0)
	{
		SessionSeed = Settings->DebugFixedSessionSeed;
		UE_LOG(LogSeinNet, Warning, TEXT("EnsureSessionSeed: using DebugFixedSessionSeed=%lld (DEBUG; clear in production)."), SessionSeed);
		return;
	}

	// Server-generated random seed combining wall clock + cycle counter. The
	// lockstep invariant only requires that ALL clients agree on the seed
	// before tick 0 — how it was generated is private to the host.
	const int64 NowCycles = (int64)FPlatformTime::Cycles64();
	const int64 NowTicks = FDateTime::UtcNow().GetTicks();
	SessionSeed = (NowCycles ^ NowTicks);
	if (SessionSeed == 0) SessionSeed = 1; // 0 reads as "uninitialized" elsewhere.
	UE_LOG(LogSeinNet, Log, TEXT("EnsureSessionSeed: generated SessionSeed=%lld"), SessionSeed);
}

const FSeinParticipantBinding* USeinNetSubsystem::FindParticipantBinding(
	FSeinNetworkParticipantID ParticipantID) const
{
	return ParticipantBindings.FindByPredicate(
		[ParticipantID](const FSeinParticipantBinding& Binding)
		{
			return Binding.ParticipantID == ParticipantID;
		});
}

FSeinNetworkParticipantID USeinNetSubsystem::FindParticipantForSlot(FSeinPlayerID Slot) const
{
	return SlotToParticipant.FindRef(Slot);
}

bool USeinNetSubsystem::IsParticipantConnected(
	FSeinNetworkParticipantID ParticipantID) const
{
	if (!ParticipantID.IsValid()) return false;
	if (IsDedicatedAuthority() && ParticipantID == CoordinatorParticipantID) return true;

	for (const TPair<TWeakObjectPtr<ASeinNetRelay>, FSeinNetworkParticipantID>& Pair
		: RelayToParticipant)
	{
		if (!Pair.Key.IsValid() || Pair.Value != ParticipantID) continue;
		const FSeinPlayerID Slot = RelayToSlot.FindRef(Pair.Key);
		const ESeinSlotLifecycle* Lifecycle = SlotLifecycle.Find(Slot);
		if (Slot.IsValid() && Lifecycle && *Lifecycle == ESeinSlotLifecycle::Connected)
		{
			return true;
		}
	}
	return false;
}

bool USeinNetSubsystem::AreRequiredStartParticipantsBound(
	TArray<FSeinNetworkParticipantID>* OutMissingParticipants) const
{
	if (OutMissingParticipants) OutMissingParticipants->Reset();
	const UWorld* CurrentWorld = GetWorld();
	bool bAllReady = CurrentWorld != nullptr;

	for (const FSeinNetworkParticipantID ParticipantID : RequiredStartParticipants)
	{
		bool bBoundInCurrentWorld =
			IsDedicatedAuthority() && ParticipantID == CoordinatorParticipantID;
		if (!bBoundInCurrentWorld)
		{
			for (const TPair<TWeakObjectPtr<ASeinNetRelay>,
				FSeinNetworkParticipantID>& Pair : RelayToParticipant)
			{
				ASeinNetRelay* Relay = Pair.Key.Get();
				if (!Relay || Pair.Value != ParticipantID
					|| Relay->GetWorld() != CurrentWorld)
				{
					continue;
				}
				const FSeinPlayerID Slot = RelayToSlot.FindRef(Pair.Key);
				const ESeinSlotLifecycle* Lifecycle = SlotLifecycle.Find(Slot);
				if (Slot.IsValid() && Lifecycle
					&& *Lifecycle == ESeinSlotLifecycle::Connected)
				{
					bBoundInCurrentWorld = true;
					break;
				}
			}
		}

		if (!bBoundInCurrentWorld)
		{
			bAllReady = false;
			if (OutMissingParticipants)
			{
				OutMissingParticipants->Add(ParticipantID);
			}
		}
	}

	if (OutMissingParticipants)
	{
		OutMissingParticipants->Sort([](
			const FSeinNetworkParticipantID& A,
			const FSeinNetworkParticipantID& B)
		{
			return A.ToCanonicalString() < B.ToCanonicalString();
		});
	}
	return bAllReady;
}

bool USeinNetSubsystem::IsCurrentWorldPreparedDestination(
	FString* OutError) const
{
	if (OutError) OutError->Reset();
	const UWorld* World = GetWorld();
	const FString PackageName = World && World->GetOutermost()
		? World->GetOutermost()->GetName()
		: FString();
	const FGuid LoadedDigest = SeinComputeDestinationWorldDigest(PackageName);
	if (ActiveProtocolContext.IsValid()
		&& LoadedDigest.IsValid()
		&& LoadedDigest == ActiveProtocolContext.DestinationWorldDigest)
	{
		return true;
	}
	if (OutError)
	{
		*OutError = TEXT("The loaded world does not match the prepared destination identity.");
	}
	return false;
}

bool USeinNetSubsystem::IsPreparedWorldActivationEligible(
	const UWorld* CurrentWorld,
	const UWorld* SourceWorld,
	ESeinPreparedWorldActivation Activation,
	const FGuid& LoadedWorldDigest,
	const FGuid& DestinationWorldDigest)
{
	return CurrentWorld
		&& LoadedWorldDigest.IsValid()
		&& LoadedWorldDigest == DestinationWorldDigest
		&& (Activation == ESeinPreparedWorldActivation::AllowCurrentWorld
			|| CurrentWorld != SourceWorld);
}

bool USeinNetSubsystem::IsCurrentProtocolContext(
	const FSeinProtocolContext& MessageContext,
	const TCHAR* Operation) const
{
	if (ActiveProtocolContext.IsValid() && MessageContext == ActiveProtocolContext)
	{
		return true;
	}
	UE_LOG(LogSeinNet, Warning,
		TEXT("%s: protocol context mismatch; active={%s} message={%s}."),
		Operation,
		*ActiveProtocolContext.ToCanonicalDebugString(),
		*MessageContext.ToCanonicalDebugString());
	return false;
}

bool USeinNetSubsystem::ConfigureTurnAggregator()
{
	if (!ActiveProtocolContext.IsValid() || ParticipantBindings.IsEmpty()) return false;
	const ESeinTurnAggregatorConfigResult Result =
		TurnAggregator.Configure(ActiveProtocolContext, ParticipantBindings);
	if (Result != ESeinTurnAggregatorConfigResult::Configured)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ConfigureTurnAggregator: rejected manifest/context (result=%d context=%s)."),
			static_cast<int32>(Result),
			*ActiveProtocolContext.ToCanonicalDebugString());
		return false;
	}
	if (bHasActiveMatchSettings)
	{
		const int32 FrozenAuthorCount = GetFrozenExpectedAuthorCount();
		if (FrozenAuthorCount != TurnAggregator.GetExpectedAuthors().Num())
		{
			UE_LOG(LogSeinNet, Error,
				TEXT("ConfigureTurnAggregator: frozen active-slot count %d does not match manifest author count %d."),
				FrozenAuthorCount, TurnAggregator.GetExpectedAuthors().Num());
			TurnAggregator.Reset();
			return false;
		}
		if (!FreezeAuthorSubmissionPolicy(TEXT("ConfigureTurnAggregator")))
		{
			TurnAggregator.Reset();
			return false;
		}
	}
	return true;
}

bool USeinNetSubsystem::ConfigureBootstrapConsensus()
{
	RequiredStartParticipants.Reset();
	TArray<FSeinNetworkParticipantID> SimulatingParticipants;
	for (const FSeinParticipantBinding& Binding : ParticipantBindings)
	{
		if (!Binding.bSimulates) continue;
		RequiredStartParticipants.Add(Binding.ParticipantID);
		SimulatingParticipants.Add(Binding.ParticipantID);
	}

	const ESeinBootstrapConsensusConfigResult Result =
		BootstrapConsensus.Configure(ActiveProtocolContext, SimulatingParticipants);
	if (Result != ESeinBootstrapConsensusConfigResult::Configured)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("ConfigureBootstrapConsensus: rejected frozen simulation membership (result=%d context=%s)."),
			static_cast<int32>(Result),
			*ActiveProtocolContext.ToCanonicalDebugString());
		return false;
	}
	return true;
}

bool USeinNetSubsystem::FreezeAuthorSubmissionPolicy(const TCHAR* Operation)
{
	if (FrozenMaxCommandsPerSubmission > 0) return true;
	if (!bHasActiveMatchSettings || GetFrozenExpectedAuthorCount() <= 0)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("%s: cannot freeze author submission policy without canonical active match settings."),
			Operation);
		return false;
	}
#if WITH_DEV_AUTOMATION_TESTS
	if (TestCommandProtocolMaxCommandsOverride.IsSet())
	{
		FrozenMaxCommandsPerSubmission = FMath::Clamp(
			TestCommandProtocolMaxCommandsOverride.GetValue(), 1,
			SeinNetProtocolLimits::MaxCommandsPerAuthor);
	}
	else
#endif
	{
		const UWorld* World = GetWorld();
		const USeinWorldSubsystem* WorldSub =
			World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
		FrozenMaxCommandsPerSubmission = WorldSub
			? WorldSub->GetCommandProtocolMaxCommandsPerSubmission()
			: 0;
	}
	if (FrozenMaxCommandsPerSubmission <= 0)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("%s: the world has no command submission cap frozen into its protocol identity."),
			Operation);
		return false;
	}
	UE_LOG(LogSeinNet, Verbose,
		TEXT("%s: froze MaxCommandsPerSubmission=%d for this match."),
		Operation, FrozenMaxCommandsPerSubmission);
	return true;
}

int32 USeinNetSubsystem::GetFrozenExpectedAuthorCount() const
{
	if (!bHasActiveMatchSettings) return 0;
	int32 Count = 0;
	for (const FSeinMatchSlot& Slot : ActiveMatchSettings.Slots)
	{
		if (Slot.State == ESeinSlotState::Human || Slot.State == ESeinSlotState::AI)
			++Count;
	}
	return Count;
}

bool USeinNetSubsystem::ValidateAuthorSubmissionBudget(
	int32 CommandCount,
	const FSeinOpaqueCommandBatch& EncodedBatch,
	uint64 CanonicalCostBytes,
	FString& OutError) const
{
	const int32 AuthorCount = GetFrozenExpectedAuthorCount();
	if (AuthorCount <= 0
		|| AuthorCount > SeinNetProtocolLimits::MaxCommandAuthors)
	{
		OutError = TEXT("frozen command-author count is unavailable or outside the 1..16 match cap");
		return false;
	}
	if (FrozenMaxCommandsPerSubmission <= 0)
	{
		OutError = TEXT("per-match author submission policy is not frozen");
		return false;
	}
	if (TurnAggregator.IsConfigured()
		&& TurnAggregator.GetExpectedAuthors().Num() != AuthorCount)
	{
		OutError = TEXT("frozen active-slot count disagrees with the configured command-author manifest");
		return false;
	}
	const FSeinAuthorSubmissionBudget Budget = GetAuthorSubmissionBudget(
		AuthorCount, FrozenMaxCommandsPerSubmission);
	if (CommandCount > Budget.MaxCommands
		|| EncodedBatch.Bytes.Num() > Budget.MaxEncodedBytes
		|| CanonicalCostBytes > Budget.MaxCanonicalCostBytes)
	{
		OutError = FString::Printf(
			TEXT("author submission exceeds deterministic %d-way share (commands=%d/%d wire=%d/%d canonical-cost=%llu/%llu)"),
			AuthorCount,
			CommandCount, Budget.MaxCommands,
			EncodedBatch.Bytes.Num(), Budget.MaxEncodedBytes,
			static_cast<unsigned long long>(CanonicalCostBytes),
			static_cast<unsigned long long>(Budget.MaxCanonicalCostBytes));
		return false;
	}
	return true;
}

bool USeinNetSubsystem::PreflightCanonicalTurnBatch(
	TConstArrayView<FSeinCommand> Commands,
	FString& OutError) const
{
	FSeinOpaqueCommandBatch Encoded;
	const int32 MaxCommands = GetMaxCommandsPerCanonicalTurn();
	auto FindSchema = [this](
		FGameplayTag Type, int32 Version, FSeinCommandSchemaDescriptor& Out)
	{
		return FindFrozenCommandSchema(Type, Version, Out);
	};
	if (!FSeinNetCommandWireCodec::EncodeCommands(
		Commands,
		MaxCommands,
		FindSchema,
		Encoded,
		OutError))
	{
		return false;
	}

	TArray<FSeinCommand> Decoded;
	if (!FSeinNetCommandWireCodec::DecodeCommands(
		Encoded, MaxCommands, FindSchema, Decoded, OutError))
	{
		return false;
	}
	if (Decoded.Num() != Commands.Num())
	{
		OutError = TEXT("canonical command preflight round-trip changed command count");
		return false;
	}
	const UScriptStruct* CommandStruct = FSeinCommand::StaticStruct();
	for (int32 Index = 0; Index < Decoded.Num(); ++Index)
	{
		if (!CommandStruct->CompareScriptStruct(&Commands[Index], &Decoded[Index], 0))
		{
			OutError = FString::Printf(
				TEXT("canonical command preflight round-trip changed command index %d"),
				Index);
			return false;
		}
	}
	return true;
}

bool USeinNetSubsystem::ResolveLocalCommandProtocolDigest(FGuid& OutDigest) const
{
	OutDigest.Invalidate();
#if WITH_DEV_AUTOMATION_TESTS
	if (TestCommandProtocolDigestOverride.IsSet())
	{
		OutDigest = TestCommandProtocolDigestOverride.GetValue();
		return OutDigest.IsValid();
	}
#endif

	const UWorld* World = GetWorld();
	const USeinWorldSubsystem* WorldSub =
		World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	if (!WorldSub) return false;
	OutDigest = WorldSub->GetCommandProtocolDigest();
	return OutDigest.IsValid();
}

bool USeinNetSubsystem::ResolveLocalSimulationContentDigest(
	FGuid& OutDigest) const
{
	OutDigest.Invalidate();
#if WITH_DEV_AUTOMATION_TESTS
	if (TestSimulationContentDigestOverride.IsSet())
	{
		OutDigest = TestSimulationContentDigestOverride.GetValue();
		return OutDigest.IsValid();
	}
#endif

	const UWorld* World = GetWorld();
	const USeinWorldSubsystem* WorldSub =
		World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	if (!WorldSub || !WorldSub->IsSimulationContentReady())
	{
		return false;
	}
	OutDigest = WorldSub->GetSimulationContentDigest();
	return OutDigest.IsValid();
}

bool USeinNetSubsystem::BuildCanonicalParticipantManifest(
	const FSeinMatchInstanceID& MatchInstanceID,
	TArray<FSeinParticipantBinding>& OutBindings,
	TMap<FSeinPlayerID, FSeinNetworkParticipantID>& OutSlotToParticipant,
	FSeinNetworkParticipantID& OutCoordinatorParticipantID,
	TMap<FSeinPlayerID, ESeinSlotLifecycle>& OutSlotLifecycle)
{
	OutBindings.Reset();
	OutSlotToParticipant.Reset();
	OutCoordinatorParticipantID = FSeinNetworkParticipantID::Invalid();
	OutSlotLifecycle.Reset();

	if (!MatchInstanceID.IsValid()) return false;

#if WITH_DEV_AUTOMATION_TESTS
	if (TestParticipantManifestOverride.IsSet())
	{
		OutBindings = TestParticipantManifestOverride.GetValue();
		if (OutBindings.IsEmpty()
			|| OutBindings.Num() > SeinNetProtocolLimits::MaxParticipants)
		{
			return false;
		}
		int32 CommandSlotCount = 0;
		for (const FSeinParticipantBinding& Binding : OutBindings)
		{
			if (!Binding.IsValid()) return false;
			if (!OutCoordinatorParticipantID.IsValid() && Binding.bCanCoordinate)
			{
				OutCoordinatorParticipantID = Binding.ParticipantID;
			}
			for (const FSeinPlayerID Slot : Binding.CommandSlots)
			{
				if (OutSlotToParticipant.Contains(Slot)) return false;
				OutSlotToParticipant.Add(Slot, Binding.ParticipantID);
				OutSlotLifecycle.Add(Slot, ESeinSlotLifecycle::Connected);
				++CommandSlotCount;
			}
		}
		OutBindings.Sort([](
			const FSeinParticipantBinding& A,
			const FSeinParticipantBinding& B)
		{
			return A.ParticipantID.ToCanonicalString() < B.ParticipantID.ToCanonicalString();
		});
		return CommandSlotCount > 0 && OutCoordinatorParticipantID.IsValid();
	}
#endif

	TSet<FSeinPlayerID> HumanSlotSet;
	TSet<FSeinPlayerID> AISlotSet;
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (const USeinLobbySubsystem* Lobby = GI->GetSubsystem<USeinLobbySubsystem>())
		{
			if (Lobby->HasPublishedSnapshot())
			{
				for (const FSeinMatchSlot& Slot : Lobby->GetPublishedSnapshot().Slots)
				{
					if (Slot.SlotIndex <= 0 || Slot.SlotIndex > MAX_uint8) continue;
					const FSeinPlayerID PlayerSlot(static_cast<uint8>(Slot.SlotIndex));
					if (Slot.State == ESeinSlotState::Human) HumanSlotSet.Add(PlayerSlot);
					else if (Slot.State == ESeinSlotState::AI) AISlotSet.Add(PlayerSlot);
				}
			}
		}
	}
	for (const TPair<TWeakObjectPtr<ASeinNetRelay>, FSeinPlayerID>& Pair : RelayToSlot)
	{
		if (Pair.Key.IsValid() && Pair.Value.IsValid()) HumanSlotSet.Add(Pair.Value);
	}

	TArray<FSeinPlayerID> HumanSlots = HumanSlotSet.Array();
	TArray<FSeinPlayerID> AISlots = AISlotSet.Array();
	HumanSlots.Sort([](const FSeinPlayerID A, const FSeinPlayerID B) { return A.Value < B.Value; });
	AISlots.Sort([](const FSeinPlayerID A, const FSeinPlayerID B) { return A.Value < B.Value; });
	if (HumanSlots.IsEmpty() && AISlots.IsEmpty())
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("BuildCanonicalParticipantManifest: no Human or AI command slots; refusing an auto-completing empty match."));
		return false;
	}

	if (IsDedicatedAuthority())
	{
		OutCoordinatorParticipantID = MakeParticipantID(
			MatchInstanceID,
			TEXT("DedicatedAuthority"));
		FSeinParticipantBinding& Dedicated = OutBindings.Emplace_GetRef();
		Dedicated.ParticipantID = OutCoordinatorParticipantID;
		Dedicated.bSimulates = true;
		Dedicated.bReportsWorldRoots = true;
		Dedicated.bCanCoordinate = true;
	}

	for (const FSeinPlayerID Slot : HumanSlots)
	{
		const FSeinNetworkParticipantID ParticipantID = MakeParticipantID(
			MatchInstanceID,
			FString::Printf(TEXT("HumanSlot.%u"), Slot.Value));
		FSeinParticipantBinding& Binding = OutBindings.Emplace_GetRef();
		Binding.ParticipantID = ParticipantID;
		Binding.CommandSlots.Add(Slot);
		Binding.bSimulates = true;
		Binding.bReportsWorldRoots = true;
		OutSlotToParticipant.Add(Slot, ParticipantID);

		bool bLiveRelay = false;
		for (const TPair<TWeakObjectPtr<ASeinNetRelay>, FSeinPlayerID>& RelayPair : RelayToSlot)
		{
			ASeinNetRelay* Relay = RelayPair.Key.Get();
			if (!Relay || RelayPair.Value != Slot) continue;
			bLiveRelay = true;
			APlayerController* PC = Cast<APlayerController>(Relay->GetOwner());
			if (const UGameInstance* GI = GetGameInstance())
			{
				if (const USeinLobbySubsystem* Lobby =
					GI->GetSubsystem<USeinLobbySubsystem>())
				{
					// Match administration comes from the lobby authority policy.
					// Transport coordination below is an independent capability.
					Binding.bCanAdministerMatch |= Lobby->IsHostController(PC);
				}
			}
			if (!IsDedicatedAuthority())
			{
				if (PC && PC->IsLocalController())
				{
					OutCoordinatorParticipantID = ParticipantID;
				}
			}
		}
		OutSlotLifecycle.Add(
			Slot,
			bLiveRelay ? ESeinSlotLifecycle::Connected : ESeinSlotLifecycle::Dropped);
	}

	if (!OutCoordinatorParticipantID.IsValid())
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("BuildCanonicalParticipantManifest: no coordinator participant is available."));
		return false;
	}
	FSeinParticipantBinding* Coordinator = OutBindings.FindByPredicate(
		[OutCoordinatorParticipantID](const FSeinParticipantBinding& Binding)
		{
			return Binding.ParticipantID == OutCoordinatorParticipantID;
		});
	if (!Coordinator) return false;
	Coordinator->bCanCoordinate = true;
	for (const FSeinPlayerID Slot : AISlots)
	{
		if (OutSlotToParticipant.Contains(Slot))
		{
			UE_LOG(LogSeinNet, Error,
				TEXT("BuildCanonicalParticipantManifest: slot=%u is both Human and AI."), Slot.Value);
			return false;
		}
		Coordinator->CommandSlots.Add(Slot);
		OutSlotToParticipant.Add(Slot, OutCoordinatorParticipantID);
		OutSlotLifecycle.Add(Slot, ESeinSlotLifecycle::AITakeover);
	}

	OutBindings.Sort([](
		const FSeinParticipantBinding& A,
		const FSeinParticipantBinding& B)
	{
		return A.ParticipantID.ToCanonicalString() < B.ParticipantID.ToCanonicalString();
	});
	if (OutBindings.Num() > SeinNetProtocolLimits::MaxParticipants)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("BuildCanonicalParticipantManifest: participant count %d exceeds protocol cap %d."),
			OutBindings.Num(), SeinNetProtocolLimits::MaxParticipants);
		return false;
	}
	return true;
}

bool USeinNetSubsystem::ValidateParticipantManifestAssignment(
	const FSeinProtocolContext& Context,
	FSeinPlayerID LocalSlot,
	FSeinNetworkParticipantID AssignedParticipantID,
	bool bLocalSimulates,
	const TArray<FSeinParticipantBinding>& InBindings,
	const FSeinMatchSettings& MatchSettings,
	TArray<FSeinParticipantBinding>& OutCanonicalBindings,
	TMap<FSeinPlayerID, FSeinNetworkParticipantID>& OutSlotToParticipant,
	FString& OutError) const
{
	OutCanonicalBindings.Reset();
	OutSlotToParticipant.Reset();
	OutError.Reset();

	FSeinTurnAggregator ManifestValidator;
	const ESeinTurnAggregatorConfigResult ValidationResult =
		ManifestValidator.Configure(Context, InBindings);
	if (ValidationResult != ESeinTurnAggregatorConfigResult::Configured)
	{
		OutError = FString::Printf(
			TEXT("participant manifest failed protocol validation (result=%d)"),
			static_cast<int32>(ValidationResult));
		return false;
	}

	TMap<FSeinPlayerID, FSeinNetworkParticipantID> CandidateSlotMap;
	for (const FSeinParticipantBinding& Binding : InBindings)
	{
		for (const FSeinPlayerID Slot : Binding.CommandSlots)
		{
			CandidateSlotMap.Add(Slot, Binding.ParticipantID);
		}
	}

	TSet<FSeinPlayerID> SettingsCommandSlots;
	for (const FSeinMatchSlot& MatchSlot : MatchSettings.Slots)
	{
		if (MatchSlot.State != ESeinSlotState::Human
			&& MatchSlot.State != ESeinSlotState::AI)
		{
			continue;
		}
		if (MatchSlot.SlotIndex <= 0 || MatchSlot.SlotIndex > MAX_uint8)
		{
			OutError = FString::Printf(
				TEXT("active match slot index %d is outside protocol range"),
				MatchSlot.SlotIndex);
			return false;
		}
		const FSeinPlayerID CommandSlot(
			static_cast<uint8>(MatchSlot.SlotIndex));
		if (SettingsCommandSlots.Contains(CommandSlot))
		{
			OutError = FString::Printf(
				TEXT("active match settings repeat command slot %u"),
				CommandSlot.Value);
			return false;
		}
		SettingsCommandSlots.Add(CommandSlot);
	}
	if (SettingsCommandSlots.IsEmpty()
		|| SettingsCommandSlots.Num() != CandidateSlotMap.Num())
	{
		OutError = FString::Printf(
			TEXT("participant manifest command-author count %d disagrees with active match settings count %d"),
			CandidateSlotMap.Num(), SettingsCommandSlots.Num());
		return false;
	}
	for (const FSeinPlayerID CommandSlot : SettingsCommandSlots)
	{
		if (!CandidateSlotMap.Contains(CommandSlot))
		{
			OutError = FString::Printf(
				TEXT("participant manifest omits active command slot %u"),
				CommandSlot.Value);
			return false;
		}
	}

	const FSeinNetworkParticipantID* SlotOwner =
		CandidateSlotMap.Find(LocalSlot);
	if (!SlotOwner || *SlotOwner != AssignedParticipantID)
	{
		OutError = FString::Printf(
			TEXT("local slot %u is not owned by assigned participant %s"),
			LocalSlot.Value, *AssignedParticipantID.ToCanonicalString());
		return false;
	}
	const FSeinParticipantBinding* LocalBinding =
		InBindings.FindByPredicate(
			[AssignedParticipantID](const FSeinParticipantBinding& Binding)
			{
				return Binding.ParticipantID == AssignedParticipantID;
			});
	if (!LocalBinding || LocalBinding->bSimulates != bLocalSimulates)
	{
		OutError = TEXT("local simulation capability disagrees with the authenticated participant manifest");
		return false;
	}

	OutCanonicalBindings = InBindings;
	OutCanonicalBindings.Sort([](
		const FSeinParticipantBinding& A,
		const FSeinParticipantBinding& B)
	{
		return A.ParticipantID.ToCanonicalString()
			< B.ParticipantID.ToCanonicalString();
	});
	OutSlotToParticipant = MoveTemp(CandidateSlotMap);
	return true;
}

void USeinNetSubsystem::ApplyProtocolAssignmentToRelays()
{
	const bool bUsePending = PendingAuthorityProtocolState.IsSet();
	const FSeinProtocolContext& AssignmentContext = bUsePending
		? PendingAuthorityProtocolState.Context
		: ActiveProtocolContext;
	const FSeinMatchSettings& AssignmentSettings = bUsePending
		? PendingAuthorityProtocolState.MatchSettings
		: ActiveMatchSettings;
	const int64 AssignmentSeed = bUsePending
		? PendingAuthorityProtocolState.Seed
		: SessionSeed;
	const TMap<FSeinPlayerID, FSeinNetworkParticipantID>& AssignmentSlots =
		bUsePending
			? PendingAuthorityProtocolState.SlotToParticipant
			: SlotToParticipant;
	const TArray<FSeinParticipantBinding>& AssignmentBindings = bUsePending
		? PendingAuthorityProtocolState.ParticipantBindings
		: ParticipantBindings;
	const ESeinPreparedWorldActivation AssignmentActivation = bUsePending
		? PendingAuthorityProtocolState.Activation
		: ESeinPreparedWorldActivation::AllowCurrentWorld;
	for (const TWeakObjectPtr<ASeinNetRelay>& WeakRelay : Relays)
	{
		ASeinNetRelay* Relay = WeakRelay.Get();
		if (!Relay) continue;
		const FSeinPlayerID Slot = RelayToSlot.FindRef(Relay);
		const FSeinNetworkParticipantID ParticipantID =
			AssignmentSlots.FindRef(Slot);
		if (!Slot.IsValid() || !ParticipantID.IsValid())
		{
			UE_LOG(LogSeinNet, Error,
				TEXT("ApplyProtocolAssignmentToRelays: relay=%s has no canonical slot/participant binding."),
				*GetNameSafe(Relay));
			continue;
		}
		Relay->AssignedPlayerID = Slot;
		Relay->AssignedParticipantID = ParticipantID;
		Relay->ProtocolContext = AssignmentContext;
		Relay->SessionSeed = AssignmentSeed;
		Relay->ForceNetUpdate();
		const FSeinParticipantBinding* AssignmentBinding =
			AssignmentBindings.FindByPredicate(
				[ParticipantID](const FSeinParticipantBinding& Binding)
				{
					return Binding.ParticipantID == ParticipantID;
				});
		const bool bSimulates = AssignmentBinding
			&& AssignmentBinding->bSimulates;

		const APlayerController* PC = Cast<APlayerController>(Relay->GetOwner());
		if (PC && PC->IsLocalController())
		{
			if (bUsePending)
			{
				PendingLocalProtocolAssignment.Relay = Relay;
				PendingLocalProtocolAssignment.Slot = Slot;
				PendingLocalProtocolAssignment.ParticipantID = ParticipantID;
				PendingLocalProtocolAssignment.Context = AssignmentContext;
				PendingLocalProtocolAssignment.Seed = AssignmentSeed;
				PendingLocalProtocolAssignment.bSimulates = bSimulates;
				PendingLocalProtocolAssignment.ParticipantBindings =
					AssignmentBindings;
				PendingLocalProtocolAssignment.MatchSettings =
					AssignmentSettings;
				PendingLocalProtocolAssignment.SourceWorld =
					PendingAuthorityProtocolState.SourceWorld;
				PendingLocalProtocolAssignment.Activation =
					AssignmentActivation;
				SchedulePendingProtocolPromotion();
			}
			else
			{
				NotifyLocalProtocolAssigned(
					Relay,
					Slot,
					ParticipantID,
					AssignmentContext,
					AssignmentSeed,
					bSimulates,
					AssignmentBindings,
					AssignmentSettings,
					AssignmentActivation);
			}
		}
		else
		{
			Relay->Client_PrepareMatchBootstrap(
				Slot,
				ParticipantID,
				AssignmentContext,
				AssignmentSeed,
				bSimulates,
				AssignmentActivation
					== ESeinPreparedWorldActivation::AllowCurrentWorld,
				AssignmentBindings,
				AssignmentSettings);
		}
	}
}

bool USeinNetSubsystem::PrepareMatchTravel(
	ESeinMatchTravelIntent Intent,
	FName DestinationWorldPackage,
	ESeinPreparedWorldActivation Activation)
{
	if (!IsServer())
	{
		UE_LOG(LogSeinNet, Warning, TEXT("PrepareMatchTravel: server-only."));
		return false;
	}
	const FGuid DestinationWorldDigest =
		SeinComputeDestinationWorldDigest(DestinationWorldPackage.ToString());
	if (!DestinationWorldDigest.IsValid())
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("PrepareMatchTravel: destination world package '%s' is invalid."),
			*DestinationWorldPackage.ToString());
		return false;
	}
	if (bDestinationStartPending)
	{
		if (PendingTravelIntent == Intent
			&& PendingAuthorityProtocolState.IsSet()
			&& PendingAuthorityProtocolState.Activation == Activation
			&& PendingAuthorityProtocolState.SourceWorld.Get() == GetWorld()
			&& PendingAuthorityProtocolState.Context.DestinationWorldDigest
				== DestinationWorldDigest)
		{
			return true;
		}
		UE_LOG(LogSeinNet, Error,
			TEXT("PrepareMatchTravel: conflicting intent while destination start is pending."));
		return false;
	}

	FGuid LocalCommandProtocolDigest;
	if (!ResolveLocalCommandProtocolDigest(LocalCommandProtocolDigest))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("PrepareMatchTravel: local command protocol is unavailable; refusing to create or continue an incompatible match."));
		return false;
	}
	FGuid LocalSimulationContentDigest;
	if (!ResolveLocalSimulationContentDigest(
			LocalSimulationContentDigest))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("PrepareMatchTravel: local simulation-content identity is unavailable; refusing to create or continue an unproven match."));
		return false;
	}

	FSeinPendingAuthorityProtocolState Prepared;
	Prepared.Intent = Intent;
	Prepared.SourceWorld = GetWorld();
	Prepared.Activation = Activation;
	if (Intent == ESeinMatchTravelIntent::NewMatch)
	{
		bool bPreparedSettings = false;
		if (const UGameInstance* GI = GetGameInstance())
		{
			if (const USeinLobbySubsystem* Lobby = GI->GetSubsystem<USeinLobbySubsystem>())
			{
				if (Lobby->HasPublishedSnapshot()
					&& !Lobby->GetPublishedSnapshot().Slots.IsEmpty())
				{
					Prepared.MatchSettings = Lobby->GetPublishedSnapshot();
					bPreparedSettings = true;
				}
			}
		}
		if (!bPreparedSettings && bHasActiveMatchSettings)
		{
			Prepared.MatchSettings = ActiveMatchSettings;
			bPreparedSettings = true;
		}
		if (!bPreparedSettings)
		{
			UE_LOG(LogSeinNet, Error,
				TEXT("PrepareMatchTravel(NewMatch): no canonical match settings snapshot is available."));
			return false;
		}
		FGuid MatchSettingsDigest;
		if (!ComputeMatchSettingsDigest(
			Prepared.MatchSettings,
			MatchSettingsDigest,
			TEXT("PrepareMatchTravel(NewMatch)")))
		{
			return false;
		}

		const FSeinMatchInstanceID MatchInstanceID(FGuid::NewGuid());
		const int64 PreviousSeed = SessionSeed;
		SessionSeed = 0;
		EnsureSessionSeed();
		Prepared.Seed = SessionSeed;
		SessionSeed = PreviousSeed;
		if (!BuildCanonicalParticipantManifest(
				MatchInstanceID,
				Prepared.ParticipantBindings,
				Prepared.SlotToParticipant,
				Prepared.CoordinatorParticipantID,
				Prepared.SlotLifecycle))
		{
			return false;
		}
		Prepared.Context = FSeinProtocolContext(
			MatchInstanceID,
			1,
			Prepared.CoordinatorParticipantID,
			1,
			1,
			SeinComputeMembershipDigest(Prepared.ParticipantBindings),
			DestinationWorldDigest,
			MatchSettingsDigest,
			LocalSimulationContentDigest,
			LocalCommandProtocolDigest);
	}
	else
	{
		if (!ActiveProtocolContext.IsValid() || ParticipantBindings.IsEmpty()
			|| !bHasActiveMatchSettings
			|| ActiveProtocolContext.LockstepEpoch == MAX_int64)
		{
			UE_LOG(LogSeinNet, Error,
				TEXT("PrepareMatchTravel(ContinueMatch): no valid durable match or epoch exhausted."));
			return false;
		}
		Prepared.MatchSettings = ActiveMatchSettings;
		FGuid CurrentMatchSettingsDigest;
		if (!ComputeMatchSettingsDigest(
				Prepared.MatchSettings,
				CurrentMatchSettingsDigest,
				TEXT("PrepareMatchTravel(ContinueMatch)"))
			|| CurrentMatchSettingsDigest != ActiveProtocolContext.MatchSettingsDigest
			|| LocalSimulationContentDigest
				!= ActiveProtocolContext.SimulationContentDigest
			|| LocalCommandProtocolDigest != ActiveProtocolContext.CommandProtocolDigest
			|| CoordinatorParticipantID != ActiveProtocolContext.CoordinatorParticipantID)
		{
			UE_LOG(LogSeinNet, Error,
				TEXT("PrepareMatchTravel(ContinueMatch): durable settings, simulation content, command protocol, or coordinator identity changed; refusing epoch transition."));
			return false;
		}
		Prepared.Context = ActiveProtocolContext;
		++Prepared.Context.LockstepEpoch;
		Prepared.Context.DestinationWorldDigest = DestinationWorldDigest;
		Prepared.Seed = SessionSeed;
		Prepared.ParticipantBindings = ParticipantBindings;
		Prepared.SlotToParticipant = SlotToParticipant;
		Prepared.CoordinatorParticipantID = CoordinatorParticipantID;
		Prepared.SlotLifecycle = SlotLifecycle;
		Prepared.FrozenMaxCommandsPerSubmission =
			FrozenMaxCommandsPerSubmission;
	}

	PendingAuthorityProtocolState = MoveTemp(Prepared);
	bDestinationStartPending = true;
	PendingTravelIntent = Intent;
	ApplyProtocolAssignmentToRelays();
	SchedulePendingProtocolPromotion();

	int32 CommandSlotCount = 0;
	for (const FSeinParticipantBinding& Binding
		: PendingAuthorityProtocolState.ParticipantBindings)
	{
		CommandSlotCount += Binding.CommandSlots.Num();
	}

	UE_LOG(LogSeinNet, Log,
		TEXT("PrepareMatchTravel: %s prepared context={%s} participants=%d commandSlots=%d."),
		Intent == ESeinMatchTravelIntent::NewMatch ? TEXT("NewMatch") : TEXT("ContinueMatch"),
		*PendingAuthorityProtocolState.Context.ToCanonicalDebugString(),
		PendingAuthorityProtocolState.ParticipantBindings.Num(),
		CommandSlotCount);
	return true;
}

void USeinNetSubsystem::AbortPreparedMatchTravel(const FString& Reason)
{
	if (!PendingAuthorityProtocolState.IsSet()) return;
	const FSeinProtocolContext CancelledContext =
		PendingAuthorityProtocolState.Context;
	PendingAuthorityProtocolState.Reset();
	bDestinationStartPending = false;
	ClientHandlePreparedMatchTravelCancelled(CancelledContext);
	CancelPendingProtocolPromotion();

	for (const TWeakObjectPtr<ASeinNetRelay>& WeakRelay : Relays)
	{
		ASeinNetRelay* Relay = WeakRelay.Get();
		if (!Relay) continue;
		const APlayerController* PC = Cast<APlayerController>(Relay->GetOwner());
		if (!PC || !PC->IsLocalController())
		{
			Relay->Client_CancelPreparedMatchTravel(CancelledContext);
		}
		const FSeinPlayerID Slot = RelayToSlot.FindRef(Relay);
		const FSeinNetworkParticipantID ParticipantID =
			SlotToParticipant.FindRef(Slot);
		Relay->AssignedParticipantID = ParticipantID;
		Relay->ProtocolContext = ActiveProtocolContext;
		Relay->SessionSeed = SessionSeed;
		Relay->ForceNetUpdate();
	}
	UE_LOG(LogSeinNet, Error,
		TEXT("Prepared match travel aborted without mutating the source epoch: %s"),
		*Reason.Left(512));
}

bool USeinNetSubsystem::TryPromotePendingAuthorityProtocolState()
{
	if (!IsServer() || !PendingAuthorityProtocolState.IsSet()) return false;
	UWorld* World = GetWorld();
	const FString PackageName = World && World->GetOutermost()
		? World->GetOutermost()->GetName()
		: FString();
	if (!World || World->bIsTearingDown
		|| !IsPreparedWorldActivationEligible(
			World,
			PendingAuthorityProtocolState.SourceWorld.Get(),
			PendingAuthorityProtocolState.Activation,
			SeinComputeDestinationWorldDigest(PackageName),
			PendingAuthorityProtocolState.Context.DestinationWorldDigest))
	{
		return false;
	}
	USeinWorldSubsystem* DestinationWorldSub =
		World->GetSubsystem<USeinWorldSubsystem>();
	if (!DestinationWorldSub
		|| !DestinationWorldSub->GetCommandProtocolDigest().IsValid()
		|| !DestinationWorldSub->IsSimulationContentReady())
	{
		return false;
	}
	if (DestinationWorldSub->GetCommandProtocolDigest()
			!= PendingAuthorityProtocolState.Context.CommandProtocolDigest
		|| DestinationWorldSub->GetSimulationContentDigest()
			!= PendingAuthorityProtocolState.Context.SimulationContentDigest)
	{
		AbortPreparedMatchTravel(
			TEXT("destination command protocol or simulation content differs from the prepared context"));
		return false;
	}

	FSeinPendingAuthorityProtocolState Prepared =
		MoveTemp(PendingAuthorityProtocolState);
	PendingAuthorityProtocolState.Reset();
	FSeinPendingLocalProtocolAssignment PendingLocal =
		MoveTemp(PendingLocalProtocolAssignment);
	PendingLocalProtocolAssignment.Reset();

	RetireReplayEpochForCommittedTravel();

	if (Prepared.Intent == ESeinMatchTravelIntent::NewMatch)
	{
		// A prepared travel is still reversible. Retire match-scoped state only
		// after destination identity has committed locally.
		ResetMatchState(nullptr);
	}
	else
	{
		ResetLockstepEpochState(nullptr);
	}

	ActiveProtocolContext = Prepared.Context;
	ActiveMatchSettings = MoveTemp(Prepared.MatchSettings);
	bHasActiveMatchSettings = true;
	SessionSeed = Prepared.Seed;
	ParticipantBindings = MoveTemp(Prepared.ParticipantBindings);
	SlotToParticipant = MoveTemp(Prepared.SlotToParticipant);
	CoordinatorParticipantID = Prepared.CoordinatorParticipantID;
	SlotLifecycle = MoveTemp(Prepared.SlotLifecycle);
	FrozenMaxCommandsPerSubmission =
		Prepared.FrozenMaxCommandsPerSubmission;
	AcceptedConfigFingerprints.Reset();
	RelayToParticipant.Reset();
	for (const TWeakObjectPtr<ASeinNetRelay>& WeakRelay : Relays)
	{
		ASeinNetRelay* Relay = WeakRelay.Get();
		if (!Relay || Relay->GetWorld() != World) continue;
		const FSeinNetworkParticipantID ParticipantID =
			SlotToParticipant.FindRef(RelayToSlot.FindRef(Relay));
		if (ParticipantID.IsValid())
		{
			RelayToParticipant.Add(Relay, ParticipantID);
		}
	}

	TurnAggregator.Reset();
	BootstrapConsensus.Reset();
	if (!ConfigureTurnAggregator() || !ConfigureBootstrapConsensus())
	{
		FailBootstrapSession(
			TEXT("Destination world rejected the prepared manifest or bootstrap membership."),
			/*bNotifyPeers=*/true);
		return false;
	}
	bDestinationStartPending = true;
	PendingTravelIntent = Prepared.Intent;
	if (PendingLocal.IsSet())
	{
		PendingLocalProtocolAssignment = MoveTemp(PendingLocal);
		SchedulePendingProtocolPromotion();
	}
	if (IsDedicatedAuthority() && CoordinatorParticipantID.IsValid())
	{
		LocalParticipantID = CoordinatorParticipantID;
		if (const USeinWorldSubsystem* WorldSub =
			World->GetSubsystem<USeinWorldSubsystem>())
		{
			AcceptedConfigFingerprints.Add(
				CoordinatorParticipantID,
				WorldSub->GetConfigFingerprint());
		}
	}
	TryPromotePendingLocalProtocolAssignment();
	UE_LOG(LogSeinNet, Log,
		TEXT("Promoted prepared protocol context only after destination-world identity matched."));
	return true;
}

void USeinNetSubsystem::RetireReplayEpochForCommittedTravel()
{
	// Replay journals are lockstep-epoch artifacts. Committed travel ends the
	// source epoch for both NewMatch and ContinueMatch; retaining its writer
	// would carry old tick/turn/checkpoint counters into destination tick zero.
	// The writer pins the source-world identity captured at StartRecording, so
	// late retirement cannot accidentally validate against the destination.
	if (ReplayWriter && ReplayWriter->IsRecording())
	{
		const FString PublishedReplay = ReplayWriter->FinishRecording();
		if (PublishedReplay.IsEmpty())
		{
			UE_LOG(LogSeinNet, Warning,
				TEXT("Committed travel could not publish the source replay epoch; its valid partial remains at %s."),
				*ReplayWriter->GetActivePartialPath());
		}
	}
	ReplayWriter = nullptr;
	if (ReplayReader && ReplayReader->IsPlaying())
	{
		ReplayReader->Stop();
	}
	ReplayReader = nullptr;
}

bool USeinNetSubsystem::TryPromotePendingLocalProtocolAssignment()
{
	if (!PendingLocalProtocolAssignment.IsSet()) return false;
	UWorld* World = GetWorld();
	const FString PackageName = World && World->GetOutermost()
		? World->GetOutermost()->GetName()
		: FString();
	if (!World || World->bIsTearingDown
		|| !IsPreparedWorldActivationEligible(
			World,
			PendingLocalProtocolAssignment.SourceWorld.Get(),
			PendingLocalProtocolAssignment.Activation,
			SeinComputeDestinationWorldDigest(PackageName),
			PendingLocalProtocolAssignment.Context.DestinationWorldDigest))
	{
		return false;
	}

	FSeinPendingLocalProtocolAssignment Assignment =
		PendingLocalProtocolAssignment;
	ASeinNetRelay* Relay = Assignment.Relay.Get();
	if (!Relay || Relay->GetWorld() != World)
	{
		Relay = LocalRelay.Get();
	}
	if (!Relay || Relay->GetWorld() != World) return false;

	NotifyLocalProtocolAssigned(
		Relay,
		Assignment.Slot,
		Assignment.ParticipantID,
		Assignment.Context,
		Assignment.Seed,
		Assignment.bSimulates,
		Assignment.ParticipantBindings,
		Assignment.MatchSettings,
		Assignment.Activation);
	return ActiveProtocolContext == Assignment.Context
		&& !PendingLocalProtocolAssignment.IsSet();
}

void USeinNetSubsystem::SchedulePendingProtocolPromotion()
{
	if (PendingProtocolPromotionHandle.IsValid()) return;
	PendingProtocolPromotionHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(
			this, &USeinNetSubsystem::TickPendingProtocolPromotion));
	if (!PendingProtocolPromotionHandle.IsValid())
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("Failed to schedule pending destination protocol promotion."));
	}
}

bool USeinNetSubsystem::TickPendingProtocolPromotion(float DeltaSeconds)
{
	(void)DeltaSeconds;
	TryPromotePendingAuthorityProtocolState();
	TryPromotePendingLocalProtocolAssignment();
	if (bDestinationStartPending && IsServer())
	{
		TryRearmPreparedDestinationStart();
	}
	if (PendingAuthorityProtocolState.IsSet()
		|| PendingLocalProtocolAssignment.IsSet())
	{
		return true;
	}
	PendingProtocolPromotionHandle.Reset();
	return false;
}

void USeinNetSubsystem::CancelPendingProtocolPromotion()
{
	if (PendingProtocolPromotionHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(
			PendingProtocolPromotionHandle);
		PendingProtocolPromotionHandle.Reset();
	}
}

void USeinNetSubsystem::TryRearmPreparedDestinationStart()
{
	if (!bDestinationStartPending || !IsServer()) return;
	UWorld* World = GetWorld();
	if (!World || World->bIsTearingDown) return;
	if (PendingAuthorityProtocolState.IsSet()
		&& !TryPromotePendingAuthorityProtocolState())
	{
		return;
	}
	if (!IsCurrentWorldPreparedDestination()) return;
	if (!BootstrapConsensus.IsConfigured()
		&& !ConfigureBootstrapConsensus())
	{
		FailBootstrapSession(
			TEXT("Destination world could not re-arm frozen bootstrap consensus."),
			/*bNotifyPeers=*/true);
		return;
	}
	if (IsDedicatedAuthority() && CoordinatorParticipantID.IsValid())
	{
		LocalParticipantID = CoordinatorParticipantID;
		if (const USeinWorldSubsystem* WorldSub =
			World->GetSubsystem<USeinWorldSubsystem>())
		{
			AcceptedConfigFingerprints.Add(
				CoordinatorParticipantID,
				WorldSub->GetConfigFingerprint());
		}
	}
	StartLockstepSession();
}

void USeinNetSubsystem::OnLogout(AGameModeBase* GameMode, AController* Exiting)
{
	if (!Exiting || !IsServer()) return;

	// Drop-in/drop-out (Phase 4): instead of destroying the relay on logout,
	// mark the owning slot as Dropped + retain the relay actor. The server
	// will inject empty heartbeats on the slot's behalf so the gate doesn't
	// stall, and (after timeout) transition the slot to AITakeover. If the
	// player reconnects, the slot returns to Connected and the relay resumes
	// normally.
	const double NowSec = FPlatformTime::Seconds();
	bool bAnyMarkedDropped = false;
	for (const TWeakObjectPtr<ASeinNetRelay>& Wp : Relays)
	{
		ASeinNetRelay* Relay = Wp.Get();
		if (!Relay || Relay->GetOwner() != Exiting) continue;

		const FSeinPlayerID Slot = Relay->AssignedPlayerID;
		if (!Slot.IsValid()) continue;
		const FSeinNetworkParticipantID ParticipantID =
			RelayToParticipant.FindRef(Relay);
		if (bBootstrapLaunchBarrierActive
			&& RequiredStartParticipants.Contains(ParticipantID)
			&& BootstrapConsensus.IsLaunchInFlight())
		{
			FailBootstrapSession(
				TEXT("A frozen participant disconnected after bootstrap receipt collection began."),
				/*bNotifyPeers=*/true);
		}

		SlotLifecycle.Add(Slot, ESeinSlotLifecycle::Dropped);
		SlotDroppedAtTime.Add(Slot, NowSec);
		bAnyMarkedDropped = true;

		// A peer that vanishes mid-resync abandons its serve; a fresh request
		// restarts from a fresh checkpoint when it returns. Its heartbeat
		// coverage guarantee dies with the serve.
		if (ServerResyncServes.Remove(Slot) > 0)
		{
			HeartbeatCoverageThroughTurn.Remove(Slot);
			UE_LOG(LogSeinNet, Log,
				TEXT("[Resync] slot=%u dropped mid-resync; serve abandoned."),
				Slot.Value);
		}

		UE_LOG(LogSeinNet, Log,
			TEXT("OnLogout: slot %u marked DROPPED (owner=%s left). Server will inject heartbeats; AI takeover scheduled in %.1fs."),
			Slot.Value, *GetNameSafe(Exiting), GetDroppedToAITakeoverSeconds());
	}

	// Cover both existing partial turns and unopened zero-author turns. A drop
	// immediately after resync activation can occur before either author has
	// opened the next gate turn; GetPendingTurnIDs alone cannot see that gap.
	if (bAnyMarkedDropped)
	{
		BackfillSuppressedSlotHeartbeatsThroughPipelineWindow();
	}

	// World-root gossip compares live simulation peers, not occupied gameplay slots.
	// A dropped slot still receives command heartbeats from the server, but its
	// vanished process can no longer report roots. Re-evaluate outstanding
	// checks against the remaining connected reporter set so they neither
	// wedge nor accumulate forever.
	if (bAnyMarkedDropped && !ServerWorldStateRootReports.IsEmpty())
	{
		TArray<int32> PendingTurns;
		ServerWorldStateRootReports.GetKeys(PendingTurns);
		for (const int32 Turn : PendingTurns)
		{
			const TMap<FSeinNetworkParticipantID, FGuid>* Reports =
				ServerWorldStateRootReports.Find(Turn);
			if (Reports && AreExpectedWorldRootReportsComplete(*Reports))
			{
				ServerCompareWorldStateRootsForTurn(Turn);
			}
		}
	}

	// Retry only to update diagnostics before receipt collection. Frozen
	// bootstrap membership never shrinks; loss after dispatch failed above.
	if (bAnyMarkedDropped)
	{
		TryDispatchLockstepSessionStart();
	}
}

void USeinNetSubsystem::RegisterRelay(ASeinNetRelay* Relay)
{
	if (!Relay) return;
	Relays.AddUnique(Relay);

	// On the client side (and on the host's own PC), latch the local relay.
	// Identification: the relay's owner is a PlayerController whose IsLocalController() is true.
	if (APlayerController* PC = Cast<APlayerController>(Relay->GetOwner()))
	{
		if (PC->IsLocalController())
		{
			LocalRelay = Relay;
			UE_LOG(LogSeinNet, Log, TEXT("RegisterRelay: latched LOCAL relay %s (PC=%s)"),
				*GetNameSafe(Relay), *GetNameSafe(PC));

			// On a Listen Server, AssignedPlayerID + SessionSeed were stamped
			// before FinishSpawning (deferred-spawn path in OnPostLogin), so
			// they're valid here. OnRep_AssignedPlayerID does NOT fire on the
			// server, so without this eager latch the host's LocalPlayerID
			// would stay zero. On a remote client, AssignedPlayerID is still
			// neutral here (initial rep hasn't arrived yet) — OnRep latches it.
			if (Relay->AssignedPlayerID.IsValid())
			{
				NotifyLocalLobbySlotAssigned(Relay, Relay->AssignedPlayerID);
			}
			if (Relay->AssignedPlayerID.IsValid()
				&& Relay->AssignedParticipantID.IsValid()
				&& Relay->ProtocolContext.IsValid()
				&& bHasActiveMatchSettings)
			{
				NotifyLocalProtocolAssigned(
					Relay,
					Relay->AssignedPlayerID,
					Relay->AssignedParticipantID,
					Relay->ProtocolContext,
					Relay->SessionSeed,
					bLocalParticipantSimulates,
					ParticipantBindings,
					ActiveMatchSettings);
			}
		}
	}
	TryRearmPreparedDestinationStart();
	TryDispatchLockstepSessionStart();
}

void USeinNetSubsystem::UnregisterRelay(ASeinNetRelay* Relay)
{
	if (!Relay) return;
	const FSeinPlayerID RemovedSlot = RelayToSlot.FindRef(Relay);
	const FSeinNetworkParticipantID RemovedParticipant = RelayToParticipant.FindRef(Relay);
	const bool bVerifyFrozenParticipantStillBound =
		bBootstrapLaunchBarrierActive
		&& RequiredStartParticipants.Contains(RemovedParticipant)
		&& BootstrapConsensus.IsLaunchInFlight();
	const ESeinSlotLifecycle* RemovedLifecycle = SlotLifecycle.Find(RemovedSlot);
	const bool bRemovedConnectedParticipant =
		RemovedSlot.IsValid() && RemovedLifecycle &&
		*RemovedLifecycle == ESeinSlotLifecycle::Connected;
	Relays.RemoveAllSwap([Relay](const TWeakObjectPtr<ASeinNetRelay>& Wp) { return Wp.Get() == Relay; });
	RelayToSlot.Remove(Relay);
	RelayToParticipant.Remove(Relay);
	if (bVerifyFrozenParticipantStillBound)
	{
		TArray<FSeinNetworkParticipantID> MissingParticipants;
		AreRequiredStartParticipantsBound(&MissingParticipants);
		if (MissingParticipants.Contains(RemovedParticipant))
		{
			FailBootstrapSession(
				TEXT("A frozen participant relay disappeared after bootstrap receipt collection began."),
				/*bNotifyPeers=*/true);
		}
	}
	if (LocalRelay.Get() == Relay)
	{
		LocalRelay.Reset();
		// Note: keep LocalPlayerID + SessionSeed cached. A relay swap (e.g.
		// seamless travel) would re-stamp them; clearing here would briefly
		// flash a "no slot" state for any UI binding to GetLocalPlayerID.
	}

	// EndPlay can unregister a relay before GameMode's Logout delegate finds
	// its owner. Retry a not-yet-dispatched start for diagnostics/rebinding;
	// frozen receipt membership is never reduced. Do not retry during teardown:
	// that request belongs to the retiring epoch and ResetLockstepEpochState
	// will discard it. Acceptance/lifecycle are deliberately preserved for
	// same-match travel; a genuinely new relay occupant clears acceptance in
	// ServerSpawnRelayForController before it can satisfy the barrier.
	UWorld* RelayWorld = Relay->GetWorld();
	if (bRemovedConnectedParticipant && RemovedParticipant.IsValid()
		&& RelayWorld && !RelayWorld->bIsTearingDown)
	{
		TryDispatchLockstepSessionStart();
	}
}

USeinWorldSubsystem* USeinNetSubsystem::BindLockstepHooksForCurrentWorld()
{
	UWorld* World = GetWorld();
	USeinWorldSubsystem* WorldSub = World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	if (!WorldSub) return nullptr;

	const bool bWorldChanged = CachedWorldSub.Get() != WorldSub;
	if (bWorldChanged)
	{
		if (USeinWorldSubsystem* Previous = CachedWorldSub.Get())
		{
			Previous->TurnReadyResolver.Unbind();
			Previous->TurnConsumeNotifier.Unbind();
			Previous->ClearAIEmitInterceptor();
			Previous->ClearLocalCommandSubmitter();
			if (TickCompletedHandle.IsValid())
			{
				Previous->OnSimTickCompleted.Remove(TickCompletedHandle);
			}
			if (ExecutionTopologyInvalidatedHandle.IsValid())
			{
				Previous->OnExecutionTopologyInvalidated.Remove(
					ExecutionTopologyInvalidatedHandle);
			}
		}
		TickCompletedHandle.Reset();
		ExecutionTopologyInvalidatedHandle.Reset();
		CachedWorldSub = WorldSub;
	}

	TWeakObjectPtr<USeinNetSubsystem> WeakSelf(this);
	WorldSub->TurnReadyResolver.BindLambda([WeakSelf](int32 Turn)
	{
		USeinNetSubsystem* Self = WeakSelf.Get();
		return Self ? Self->ResolveTurnReady(Turn) : true;
	});
	WorldSub->TurnConsumeNotifier.BindLambda([WeakSelf](int32 Turn)
	{
		if (USeinNetSubsystem* Self = WeakSelf.Get())
		{
			Self->ConsumeTurn(Turn);
		}
	});
	FSeinAIEmitInterceptor AIInterceptor;
	AIInterceptor.BindLambda([WeakSelf](FSeinPlayerID Slot, const FSeinCommand& Cmd) -> bool
	{
		USeinNetSubsystem* Self = WeakSelf.Get();
		return Self ? Self->HandleAIEmit(Slot, Cmd) : false;
	});
	WorldSub->SetAIEmitInterceptor(MoveTemp(AIInterceptor));
	FSeinLocalCommandSubmitter LocalSubmitter;
	LocalSubmitter.BindLambda(
		[WeakSelf](const FSeinCommand& Draft, bool bRequestMatchAdministration)
		{
			if (USeinNetSubsystem* Self = WeakSelf.Get())
			{
				Self->SubmitLocalCommandDraft(Draft, bRequestMatchAdministration);
			}
		});
	WorldSub->SetLocalCommandSubmitter(MoveTemp(LocalSubmitter));

	if (!TickCompletedHandle.IsValid())
	{
		TickCompletedHandle = WorldSub->OnSimTickCompleted.AddUObject(this, &USeinNetSubsystem::OnSimTickCompleted);
	}
	if (!ExecutionTopologyInvalidatedHandle.IsValid())
	{
		ExecutionTopologyInvalidatedHandle =
			WorldSub->OnExecutionTopologyInvalidated.AddUObject(
				this,
				&USeinNetSubsystem::HandleExecutionTopologyInvalidated);
	}

	if (bWorldChanged)
	{
		UE_LOG(LogSeinNet, Log,
			TEXT("BindLockstepHooksForCurrentWorld: bound %s (TicksPerTurn=%d InputDelay=%d)."),
			*GetNameSafe(WorldSub), GetTicksPerTurn(), GetInputDelayTurns());
	}
	return WorldSub;
}

bool USeinNetSubsystem::AreNetworkStartPrerequisitesReady(bool bHooksReady) const
{
	const bool bHasLocalParticipant =
		(IsDedicatedAuthority() && LocalParticipantID.IsValid())
		|| (LocalRelay.IsValid() && LocalPlayerID.IsValid() && LocalParticipantID.IsValid());
	return ActiveProtocolContext.IsValid()
		&& bHasActiveMatchSettings
		&& SessionSeed != 0 && bHooksReady && bHasLocalParticipant;
}

bool USeinNetSubsystem::AreConfigFingerprintsComplete(
	const TArray<FSeinNetworkParticipantID>& ExpectedParticipants,
	const TMap<FSeinNetworkParticipantID, int32>& AcceptedFingerprints,
	int32 RequiredFingerprint)
{
	for (const FSeinNetworkParticipantID ParticipantID : ExpectedParticipants)
	{
		const int32* Accepted = AcceptedFingerprints.Find(ParticipantID);
		if (!Accepted || *Accepted != RequiredFingerprint)
		{
			return false;
		}
	}
	return true;
}

bool USeinNetSubsystem::IsConfigParityStartBarrierSatisfied(
	TArray<FSeinNetworkParticipantID>* OutMissingParticipants) const
{
	if (OutMissingParticipants) OutMissingParticipants->Reset();
	if (!IsConfigParityCheckEnabled()) return true;

	const UWorld* World = GetWorld();
	const USeinWorldSubsystem* WorldSub =
		World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	if (!WorldSub) return false;
	const int32 RequiredFingerprint = WorldSub->GetConfigFingerprint();

	TArray<FSeinNetworkParticipantID> ExpectedParticipants;
	GetExpectedWorldRootReporterParticipants(ExpectedParticipants);
	if (OutMissingParticipants)
	{
		for (const FSeinNetworkParticipantID ParticipantID : ExpectedParticipants)
		{
			const int32* Accepted = AcceptedConfigFingerprints.Find(ParticipantID);
			if (!Accepted || *Accepted != RequiredFingerprint)
			{
				OutMissingParticipants->Add(ParticipantID);
			}
		}
	}

	return AreConfigFingerprintsComplete(
		ExpectedParticipants, AcceptedConfigFingerprints, RequiredFingerprint);
}

void USeinNetSubsystem::NotifyLocalLobbySlotAssigned(
	ASeinNetRelay* Relay,
	FSeinPlayerID Slot)
{
	if (!Relay || !Slot.IsValid()) return;
	LocalRelay = Relay;
	if (LocalPlayerID == Slot) return;

	LocalPlayerID = Slot;
	OnLocalSlotChanged.Broadcast(LocalPlayerID);
	OnLocalSlotChangedBP.Broadcast(LocalPlayerID);
}

void USeinNetSubsystem::NotifyLocalProtocolAssigned(
	ASeinNetRelay* Relay,
	FSeinPlayerID Slot,
	FSeinNetworkParticipantID ParticipantID,
	const FSeinProtocolContext& Context,
	int64 Seed,
	bool bSimulates,
	const TArray<FSeinParticipantBinding>& InParticipantBindings,
	const FSeinMatchSettings& MatchSettings,
	ESeinPreparedWorldActivation Activation)
{
	if (!Relay || !Slot.IsValid() || !ParticipantID.IsValid()
		|| !Context.IsValid() || Seed == 0 || MatchSettings.Slots.IsEmpty())
	{
		return;
	}
	FGuid LocalCommandProtocolDigest;
	FGuid LocalMatchSettingsDigest;
	FGuid LocalSimulationContentDigest;
	FSeinMatchSettings CanonicalMatchSettings = MatchSettings;
	TArray<FSeinParticipantBinding> CanonicalParticipantBindings;
	TMap<FSeinPlayerID, FSeinNetworkParticipantID>
		CanonicalSlotToParticipant;
	FString ParticipantManifestError;
	if (!ValidateParticipantManifestAssignment(
			Context,
			Slot,
			ParticipantID,
			bSimulates,
			InParticipantBindings,
			CanonicalMatchSettings,
			CanonicalParticipantBindings,
			CanonicalSlotToParticipant,
			ParticipantManifestError))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("NotifyLocalProtocolAssigned: rejected unauthenticated or inconsistent participant manifest: %s."),
			*ParticipantManifestError);
		return;
	}
	const bool bCommandDigestReady =
		ResolveLocalCommandProtocolDigest(LocalCommandProtocolDigest);
	const bool bSimulationContentDigestReady =
		ResolveLocalSimulationContentDigest(
			LocalSimulationContentDigest);
	const bool bMatchDigestReady = ComputeMatchSettingsDigest(
			CanonicalMatchSettings,
			LocalMatchSettingsDigest,
			TEXT("NotifyLocalProtocolAssigned"));
	UWorld* LoadedWorld = GetWorld();
	const FString LoadedPackage = LoadedWorld && LoadedWorld->GetOutermost()
		? LoadedWorld->GetOutermost()->GetName()
		: FString();
	const FGuid LoadedWorldDigest =
		SeinComputeDestinationWorldDigest(LoadedPackage);
	const bool bSamePendingAssignment = PendingLocalProtocolAssignment.IsSet()
		&& PendingLocalProtocolAssignment.Context == Context
		&& PendingLocalProtocolAssignment.ParticipantID == ParticipantID
		&& PendingLocalProtocolAssignment.Slot == Slot
		&& PendingLocalProtocolAssignment.Seed == Seed;
	UWorld* AssignmentSourceWorld = bSamePendingAssignment
		? PendingLocalProtocolAssignment.SourceWorld.Get()
		: LoadedWorld;
	const ESeinPreparedWorldActivation EffectiveActivation =
		bSamePendingAssignment
			? PendingLocalProtocolAssignment.Activation
			: Activation;
	const bool bAlreadyActive = ActiveProtocolContext == Context;
	const bool bActivationDeferred = !bAlreadyActive
		&& !IsPreparedWorldActivationEligible(
			LoadedWorld,
			AssignmentSourceWorld,
			EffectiveActivation,
			LoadedWorldDigest,
			Context.DestinationWorldDigest);
	const bool bDigestsMatch = bCommandDigestReady && bMatchDigestReady
		&& bSimulationContentDigestReady
		&& LocalCommandProtocolDigest == Context.CommandProtocolDigest
		&& LocalMatchSettingsDigest == Context.MatchSettingsDigest
		&& LocalSimulationContentDigest
			== Context.SimulationContentDigest;
	if (!bDigestsMatch)
	{
		if (bActivationDeferred)
		{
			UE_LOG(LogSeinNet, Error,
				TEXT("NotifyLocalProtocolAssigned: rejected incompatible prepared assignment before destination activation."));
			return;
		}
		const USeinWorldSubsystem* WorldSub =
			LoadedWorld
				? LoadedWorld->GetSubsystem<USeinWorldSubsystem>()
				: nullptr;
		if (IsNetworkingActive() && WorldSub)
		{
			Relay->Server_ReportConfigFingerprint(
				Context,
				WorldSub->GetConfigFingerprint(),
				LocalCommandProtocolDigest,
				LocalMatchSettingsDigest,
				LocalSimulationContentDigest);
		}
		PendingLocalProtocolAssignment.Reset();
		if (!PendingAuthorityProtocolState.IsSet())
		{
			CancelPendingProtocolPromotion();
		}
		UE_LOG(LogSeinNet, Error,
			TEXT("NotifyLocalProtocolAssigned: local command protocol, canonical match settings, or simulation content differs from the authority context — rejected."));
		return;
	}
	if (ActiveProtocolContext.IsValid()
		&& ActiveProtocolContext.MatchInstanceID == Context.MatchInstanceID)
	{
		if ((bHasActiveMatchSettings
				&& !AreMatchSettingsIdentical(
					ActiveMatchSettings, CanonicalMatchSettings))
			|| Context.LockstepEpoch < ActiveProtocolContext.LockstepEpoch
			|| Context.CoordinatorTerm < ActiveProtocolContext.CoordinatorTerm
			|| Context.MembershipRevision
				!= ActiveProtocolContext.MembershipRevision
			|| Context.MembershipDigest != ActiveProtocolContext.MembershipDigest
			|| Context.CommandProtocolDigest
				!= ActiveProtocolContext.CommandProtocolDigest
			|| Context.MatchSettingsDigest
				!= ActiveProtocolContext.MatchSettingsDigest
			|| Context.SimulationContentDigest
				!= ActiveProtocolContext.SimulationContentDigest
			|| (Context.LockstepEpoch == ActiveProtocolContext.LockstepEpoch
				&& Context.DestinationWorldDigest
					!= ActiveProtocolContext.DestinationWorldDigest)
			|| (Context.CoordinatorParticipantID
					!= ActiveProtocolContext.CoordinatorParticipantID
				&& Context.CoordinatorTerm
					<= ActiveProtocolContext.CoordinatorTerm)
			|| (SessionSeed != 0 && SessionSeed != Seed))
		{
			UE_LOG(LogSeinNet, Error,
				TEXT("NotifyLocalProtocolAssigned: prepared context violates the active match's monotonic contract — rejected."));
			return;
		}
	}
	if (bActivationDeferred)
	{
		if (PendingLocalProtocolAssignment.IsSet()
			&& (PendingLocalProtocolAssignment.Context != Context
				|| PendingLocalProtocolAssignment.ParticipantID != ParticipantID
				|| PendingLocalProtocolAssignment.Slot != Slot
				|| PendingLocalProtocolAssignment.Seed != Seed
				|| PendingLocalProtocolAssignment.bSimulates != bSimulates
				|| PendingLocalProtocolAssignment.Activation != Activation
				|| !AreMatchSettingsIdentical(
					PendingLocalProtocolAssignment.MatchSettings,
					CanonicalMatchSettings)))
		{
			UE_LOG(LogSeinNet, Error,
				TEXT("NotifyLocalProtocolAssigned: conflicting destination assignment arrived while travel was pending — rejected."));
			return;
		}
		PendingLocalProtocolAssignment.Relay = Relay;
		PendingLocalProtocolAssignment.Slot = Slot;
		PendingLocalProtocolAssignment.ParticipantID = ParticipantID;
		PendingLocalProtocolAssignment.Context = Context;
		PendingLocalProtocolAssignment.Seed = Seed;
		PendingLocalProtocolAssignment.bSimulates = bSimulates;
		PendingLocalProtocolAssignment.ParticipantBindings =
			CanonicalParticipantBindings;
		PendingLocalProtocolAssignment.MatchSettings =
			MoveTemp(CanonicalMatchSettings);
		PendingLocalProtocolAssignment.SourceWorld = AssignmentSourceWorld;
		PendingLocalProtocolAssignment.Activation = EffectiveActivation;
		SchedulePendingProtocolPromotion();
		UE_LOG(LogSeinNet, Log,
			TEXT("NotifyLocalProtocolAssigned: cached destination context without touching the loaded source-world epoch."));
		return;
	}
	if (ActiveProtocolContext.IsValid()
		&& ActiveProtocolContext.MatchInstanceID == Context.MatchInstanceID
		&& bHasActiveMatchSettings
		&& !AreMatchSettingsIdentical(ActiveMatchSettings, CanonicalMatchSettings))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("NotifyLocalProtocolAssigned: match settings changed inside one match identity — rejected."));
		return;
	}

	if (ActiveProtocolContext.IsValid() && ActiveProtocolContext != Context)
	{
		if (ActiveProtocolContext.MatchInstanceID != Context.MatchInstanceID)
		{
			ResetMatchState(nullptr);
		}
		else
		{
			if (Context.LockstepEpoch < ActiveProtocolContext.LockstepEpoch
				|| Context.CoordinatorTerm < ActiveProtocolContext.CoordinatorTerm)
			{
				UE_LOG(LogSeinNet, Warning,
					TEXT("NotifyLocalProtocolAssigned: stale context {%s} rejected; active={%s}."),
					*Context.ToCanonicalDebugString(),
					*ActiveProtocolContext.ToCanonicalDebugString());
				return;
			}
			if (Context.MembershipRevision != ActiveProtocolContext.MembershipRevision
				|| Context.MembershipDigest != ActiveProtocolContext.MembershipDigest)
			{
				UE_LOG(LogSeinNet, Error,
					TEXT("NotifyLocalProtocolAssigned: membership changed without an authenticated manifest transition — rejected."));
				return;
			}
			if (Context.CommandProtocolDigest != ActiveProtocolContext.CommandProtocolDigest
				|| Context.MatchSettingsDigest != ActiveProtocolContext.MatchSettingsDigest
				|| Context.SimulationContentDigest
					!= ActiveProtocolContext.SimulationContentDigest)
			{
				UE_LOG(LogSeinNet, Error,
					TEXT("NotifyLocalProtocolAssigned: compatibility digests changed inside one match identity — rejected."));
				return;
			}
			if (Context.LockstepEpoch == ActiveProtocolContext.LockstepEpoch
				&& Context.DestinationWorldDigest
					!= ActiveProtocolContext.DestinationWorldDigest)
			{
				UE_LOG(LogSeinNet, Error,
					TEXT("NotifyLocalProtocolAssigned: destination identity changed inside one lockstep epoch — rejected."));
				return;
			}
			if (Context.CoordinatorParticipantID
					!= ActiveProtocolContext.CoordinatorParticipantID
				&& Context.CoordinatorTerm <= ActiveProtocolContext.CoordinatorTerm)
			{
				UE_LOG(LogSeinNet, Error,
					TEXT("NotifyLocalProtocolAssigned: coordinator identity changed without a newer term — rejected."));
				return;
			}
			if (Context.LockstepEpoch != ActiveProtocolContext.LockstepEpoch)
			{
				ResetLockstepEpochState(nullptr);
			}
		}
		CancelBootstrapMaterializerRetry();
		DeferredBootstrapReceiptRequestContext.Reset();
	}
	if (ActiveProtocolContext.IsValid()
		&& ActiveProtocolContext.MatchInstanceID == Context.MatchInstanceID
		&& SessionSeed != 0 && SessionSeed != Seed)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("NotifyLocalProtocolAssigned: seed changed inside match — rejected."));
		return;
	}

	NotifyLocalLobbySlotAssigned(Relay, Slot);
	LocalParticipantID = ParticipantID;
	bLocalParticipantSimulates = bSimulates;
	CoordinatorParticipantID = Context.CoordinatorParticipantID;
	ActiveProtocolContext = Context;
	ActiveMatchSettings = MoveTemp(CanonicalMatchSettings);
	bHasActiveMatchSettings = true;
	ParticipantBindings = MoveTemp(CanonicalParticipantBindings);
	SlotToParticipant = MoveTemp(CanonicalSlotToParticipant);
	if (!FreezeAuthorSubmissionPolicy(TEXT("NotifyLocalProtocolAssigned")))
	{
		ResetMatchState(nullptr);
		return;
	}
	SessionSeed = Seed;
	if (PendingLocalProtocolAssignment.IsSet()
		&& PendingLocalProtocolAssignment.Context == Context)
	{
		PendingLocalProtocolAssignment.Reset();
	}
	if (UGameInstance* GI = GetGameInstance())
	{
		if (USeinLobbySubsystem* Lobby = GI->GetSubsystem<USeinLobbySubsystem>())
		{
			Lobby->InstallPreparedMatchSettingsSnapshot(ActiveMatchSettings);
		}
	}
	UE_LOG(LogSeinNet, Log,
		TEXT("NotifyLocalProtocolAssigned: slot=%u participant=%s seed=%lld context={%s}"),
		LocalPlayerID.Value,
		*LocalParticipantID.ToCanonicalString(),
		SessionSeed,
		*ActiveProtocolContext.ToCanonicalDebugString());

	// Bind before simulation start so the first tick is gated. This shared path
	// is also called directly by authority startup; dedicated servers never
	// receive a local-slot notification.
	BindLockstepHooksForCurrentWorld();

	// Every networked participant reports, regardless of its LOCAL parity
	// preference. Enforcement is a host policy: otherwise a client could turn
	// its own check off, send nothing, and bypass or indefinitely wedge the
	// host's pre-start barrier. The listen host's relay round-trips harmlessly.
	if (IsNetworkingActive() && LocalParticipantID.IsValid() && Relay)
	{
		const UWorld* World = GetWorld();
		if (const USeinWorldSubsystem* WorldSub =
			World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr)
		{
			Relay->Server_ReportConfigFingerprint(
				ActiveProtocolContext,
				WorldSub->GetConfigFingerprint(),
				LocalCommandProtocolDigest,
				LocalMatchSettingsDigest,
				LocalSimulationContentDigest);
		}
	}

	// Relay replacement/late assignment is the retry edge for locally frozen
	// work. Flush exact batches/checkpoints before honoring deferred bootstrap
	// protocol messages.
	FlushPendingTurnSubmissions();
	FlushPendingWorldStateRootReports();
	FlushPendingDeterminismSessionFailure();
	if (PendingLocalBootstrapReceiptReportContext.IsSet()
		&& LocalBootstrapReceipt.IsSet())
	{
		SubmitLocalBootstrapReceipt(
			PendingLocalBootstrapReceiptReportContext.GetValue(),
			LocalBootstrapReceipt.GetValue());
	}
	if (PendingLocalBootstrapAuthorizedReadyReportContext.IsSet()
		&& LocalBootstrapReceipt.IsSet())
	{
		SubmitLocalBootstrapAuthorizedReady(
			PendingLocalBootstrapAuthorizedReadyReportContext.GetValue(),
			LocalBootstrapReceipt.GetValue());
	}
	if (DeferredBootstrapReceiptRequestContext.IsSet())
	{
		const FSeinProtocolContext Deferred =
			DeferredBootstrapReceiptRequestContext.GetValue();
		if (!BootstrapMaterializerRetryHandle.IsValid())
		{
			DeferredBootstrapReceiptRequestContext.Reset();
			ClientHandleBootstrapReceiptRequest(Deferred);
		}
	}
	TryAuthorizeLocalBootstrap();
	TryLaunchLocalBootstrap();
}

void USeinNetSubsystem::StartLocalSession()
{
	if (IsNetworkingActive())
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("StartLocalSession: network launch is coordinator-controlled; request rejected."));
		return;
	}
	bStartSessionRequested = true;
	TryStartLocalSession();
}

bool USeinNetSubsystem::IsLocalSimulatingParticipant() const
{
	const FSeinParticipantBinding* Binding =
		FindParticipantBinding(LocalParticipantID);
	return Binding ? Binding->bSimulates : bLocalParticipantSimulates;
}

bool USeinNetSubsystem::TryMaterializeLocalBootstrapReceipt(
	const FSeinProtocolContext& Context,
	FSeinMatchBootstrapReceipt& OutReceipt,
	FString& OutError)
{
	OutReceipt = FSeinMatchBootstrapReceipt();
	OutError.Reset();
	if (!Context.IsValid() || !ActiveProtocolContext.IsValid()
		|| Context != ActiveProtocolContext)
	{
		OutError = TEXT("Bootstrap receipt request does not match the prepared protocol context.");
		return false;
	}
	if (!IsLocalSimulatingParticipant() || !bHasActiveMatchSettings
		|| SessionSeed == 0)
	{
		OutError = TEXT("Local simulation identity, match settings, or session seed is unavailable.");
		return false;
	}
	if (!IsCurrentWorldPreparedDestination(&OutError))
	{
		return false;
	}

	USeinWorldSubsystem* WorldSub = BindLockstepHooksForCurrentWorld();
	if (!WorldSub)
	{
		OutError = TEXT("The destination simulation world is unavailable.");
		return false;
	}
	if (WorldSub->GetCommandProtocolDigest() != Context.CommandProtocolDigest)
	{
		OutError = TEXT("The destination world's command protocol differs from the prepared context.");
		return false;
	}
	if (!WorldSub->IsSimulationContentReady()
		|| WorldSub->GetSimulationContentDigest()
			!= Context.SimulationContentDigest)
	{
		OutError =
			TEXT("The destination world's simulation content differs from the prepared context.");
		return false;
	}

	FSeinMatchBootstrapAuthorityHandle ClaimedAuthority;
	if (!WorldSub->ClaimMatchBootstrapAuthority(
			GSeinNetworkBootstrapAuthorityID,
			this,
			ClaimedAuthority,
			OutError))
	{
		return false;
	}
	MatchBootstrapAuthority = ClaimedAuthority;

	const FGuid AuthorizationContextDigest =
		SeinComputeBootstrapAuthorizationContextDigest(Context, SessionSeed);
	if (!AuthorizationContextDigest.IsValid())
	{
		OutError = TEXT("The bootstrap authorization-context digest is invalid.");
		return false;
	}

	// Session seed is deterministic bootstrap state. Apply it before the first
	// Ensure call; sealed-state retries must never rewind PRNG state.
	if (WorldSub->GetMatchBootstrapState()
		== ESeinMatchBootstrapState::Awaiting)
	{
		if (!WorldSub->SeedSimRandom(
				MatchBootstrapAuthority, SessionSeed, OutError))
		{
			return false;
		}
	}
	if (!WorldSub->EnsureMatchBootstrapLocallyReady(
		MatchBootstrapAuthority,
		ActiveMatchSettings,
		AuthorizationContextDigest,
		OutReceipt,
		OutError))
	{
		return false;
	}
	LocalBootstrapReceipt = OutReceipt;
	return true;
}

void USeinNetSubsystem::ClientHandleBootstrapReceiptRequest(
	const FSeinProtocolContext& Context)
{
	if (bLocalBootstrapIngressClosed)
	{
		UE_LOG(LogSeinNet, Verbose,
			TEXT("Ignoring bootstrap receipt request after local bootstrap ingress closed."));
		return;
	}
	if (!Context.IsValid()) return;
	if (PendingLocalProtocolAssignment.IsSet()
		&& PendingLocalProtocolAssignment.Context == Context
		&& ActiveProtocolContext != Context)
	{
		DeferredBootstrapReceiptRequestContext = Context;
		UE_LOG(LogSeinNet, Verbose,
			TEXT("Deferring bootstrap receipt request until the prepared destination world is active."));
		return;
	}
	if (!ActiveProtocolContext.IsValid())
	{
		DeferredBootstrapReceiptRequestContext = Context;
		return;
	}
	if (!IsCurrentProtocolContext(
		Context, TEXT("ClientHandleBootstrapReceiptRequest")))
	{
		FailBootstrapSession(
			TEXT("Bootstrap receipt request carried the wrong protocol context."),
			/*bNotifyPeers=*/IsServer());
		ReportLocalBootstrapFailure(Context);
		return;
	}
	if (BootstrapMaterializerRetryHandle.IsValid()
		&& DeferredBootstrapReceiptRequestContext.IsSet()
		&& DeferredBootstrapReceiptRequestContext.GetValue() == Context)
	{
		// An exact retry cannot extend the bounded wait window. The scheduled
		// callback will observe a newly bound materializer on the next frame.
		return;
	}
	CancelBootstrapMaterializerRetry();
	DeferredBootstrapReceiptRequestContext.Reset();

	FSeinMatchBootstrapReceipt Receipt;
	FString Error;
	if (!TryMaterializeLocalBootstrapReceipt(Context, Receipt, Error))
	{
		UWorld* World = GetWorld();
		USeinWorldSubsystem* WorldSub =
			World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
		if (WorldSub
			&& WorldSub->GetMatchBootstrapState()
				== ESeinMatchBootstrapState::Awaiting
			&& !WorldSub->MatchBootstrapMaterializer.IsBound())
		{
			DeferredBootstrapReceiptRequestContext = Context;
			ScheduleBootstrapMaterializerRetry(Context);
			UE_LOG(LogSeinNet, Verbose,
				TEXT("Bootstrap receipt request is waiting for the destination materializer."));
			return;
		}

		FailBootstrapSession(
			Error.IsEmpty()
				? TEXT("Local match bootstrap materialization failed.")
				: Error,
			/*bNotifyPeers=*/IsServer());
		ReportLocalBootstrapFailure(Context);
		return;
	}
	SubmitLocalBootstrapReceipt(Context, Receipt);
}

void USeinNetSubsystem::ScheduleBootstrapMaterializerRetry(
	const FSeinProtocolContext& Context)
{
	if (BootstrapMaterializerRetryHandle.IsValid())
	{
		if (DeferredBootstrapReceiptRequestContext.IsSet()
			&& DeferredBootstrapReceiptRequestContext.GetValue() == Context)
		{
			return;
		}
		CancelBootstrapMaterializerRetry();
	}

	DeferredBootstrapReceiptRequestContext = Context;
	BootstrapMaterializerRetryAttempts = 0;
	BootstrapMaterializerRetryHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(
			this, &USeinNetSubsystem::TickBootstrapMaterializerRetry));
	if (BootstrapMaterializerRetryHandle.IsValid()) return;

	DeferredBootstrapReceiptRequestContext.Reset();
	FailBootstrapSession(
		TEXT("Failed to schedule destination bootstrap materializer retry."),
		/*bNotifyPeers=*/IsServer());
	ReportLocalBootstrapFailure(Context);
}

bool USeinNetSubsystem::TickBootstrapMaterializerRetry(float DeltaSeconds)
{
	(void)DeltaSeconds;
	if (!DeferredBootstrapReceiptRequestContext.IsSet()
		|| !BootstrapSessionFailureReason.IsEmpty())
	{
		BootstrapMaterializerRetryHandle.Reset();
		BootstrapMaterializerRetryAttempts = 0;
		return false;
	}

	const FSeinProtocolContext Context =
		DeferredBootstrapReceiptRequestContext.GetValue();
	if (!ActiveProtocolContext.IsValid() || Context != ActiveProtocolContext)
	{
		BootstrapMaterializerRetryHandle.Reset();
		BootstrapMaterializerRetryAttempts = 0;
		DeferredBootstrapReceiptRequestContext.Reset();
		FailBootstrapSession(
			TEXT("Deferred bootstrap materializer request no longer matches the active context."),
			/*bNotifyPeers=*/IsServer());
		ReportLocalBootstrapFailure(Context);
		return false;
	}

	UWorld* World = GetWorld();
	USeinWorldSubsystem* WorldSub =
		World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	if (!WorldSub || WorldSub->GetMatchBootstrapState()
		== ESeinMatchBootstrapState::Failed)
	{
		BootstrapMaterializerRetryHandle.Reset();
		BootstrapMaterializerRetryAttempts = 0;
		DeferredBootstrapReceiptRequestContext.Reset();
		FailBootstrapSession(
			TEXT("Destination bootstrap became unavailable while waiting for its materializer."),
			/*bNotifyPeers=*/IsServer());
		ReportLocalBootstrapFailure(Context);
		return false;
	}

	const bool bStillWaiting = WorldSub->GetMatchBootstrapState()
		== ESeinMatchBootstrapState::Awaiting
		&& !WorldSub->MatchBootstrapMaterializer.IsBound();
	if (!bStillWaiting)
	{
		// Return false before re-entering the request path; that path may schedule
		// a new bounded retry if a replacement world is still initializing.
		BootstrapMaterializerRetryHandle.Reset();
		BootstrapMaterializerRetryAttempts = 0;
		DeferredBootstrapReceiptRequestContext.Reset();
		ClientHandleBootstrapReceiptRequest(Context);
		return false;
	}

	++BootstrapMaterializerRetryAttempts;
	if (BootstrapMaterializerRetryAttempts
		< GSeinMaxBootstrapMaterializerRetryTicks)
	{
		return true;
	}

	BootstrapMaterializerRetryHandle.Reset();
	BootstrapMaterializerRetryAttempts = 0;
	DeferredBootstrapReceiptRequestContext.Reset();
	FailBootstrapSession(
		TEXT("Destination bootstrap materializer did not bind within the bounded startup window."),
		/*bNotifyPeers=*/IsServer());
	ReportLocalBootstrapFailure(Context);
	return false;
}

void USeinNetSubsystem::CancelBootstrapMaterializerRetry()
{
	if (BootstrapMaterializerRetryHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(
			BootstrapMaterializerRetryHandle);
		BootstrapMaterializerRetryHandle.Reset();
	}
	BootstrapMaterializerRetryAttempts = 0;
}

void USeinNetSubsystem::SubmitLocalBootstrapReceipt(
	const FSeinProtocolContext& Context,
	const FSeinMatchBootstrapReceipt& Receipt)
{
	if (bLocalBootstrapIngressClosed) return;
	if (IsServer())
	{
		SubmitBootstrapReceiptForParticipant(
			LocalParticipantID, Context, Receipt);
		return;
	}
	if (ASeinNetRelay* Relay = LocalRelay.Get())
	{
		Relay->Server_ReportMatchBootstrapReceipt(Context, Receipt);
		PendingLocalBootstrapReceiptReportContext.Reset();
		return;
	}
	PendingLocalBootstrapReceiptReportContext = Context;
}

void USeinNetSubsystem::SubmitLocalBootstrapAuthorizedReady(
	const FSeinProtocolContext& Context,
	const FSeinMatchBootstrapReceipt& Receipt)
{
	if (bLocalBootstrapIngressClosed) return;
	if (IsServer())
	{
		SubmitBootstrapAuthorizedReadyForParticipant(
			LocalParticipantID, Context, Receipt);
		return;
	}
	if (ASeinNetRelay* Relay = LocalRelay.Get())
	{
		Relay->Server_ReportMatchBootstrapAuthorizedReady(Context, Receipt);
		PendingLocalBootstrapAuthorizedReadyReportContext.Reset();
		return;
	}
	PendingLocalBootstrapAuthorizedReadyReportContext = Context;
}

void USeinNetSubsystem::ClientHandleBootstrapAuthorization(
	const FSeinProtocolContext& Context,
	const FSeinMatchBootstrapReceipt& Receipt)
{
	if (bLocalBootstrapIngressClosed)
	{
		UE_LOG(LogSeinNet, Verbose,
			TEXT("Ignoring bootstrap authorization after local bootstrap ingress closed."));
		return;
	}
	if (!Context.IsValid() || !Receipt.IsValid()
		|| Receipt.ContractDigest != Context.MatchSettingsDigest
		|| Receipt.SimulationContentDigest
			!= Context.SimulationContentDigest)
	{
		FailBootstrapSession(
			TEXT("Coordinator sent an invalid bootstrap authorization."),
			/*bNotifyPeers=*/IsServer());
		ReportLocalBootstrapFailure(Context);
		return;
	}
	if (PendingBootstrapAuthorizationContext.IsSet()
		!= PendingBootstrapAuthorizationReceipt.IsSet()
		|| (PendingBootstrapAuthorizationContext.IsSet()
			&& (PendingBootstrapAuthorizationContext.GetValue() != Context
				|| PendingBootstrapAuthorizationReceipt.GetValue() != Receipt)))
	{
		FailBootstrapSession(
			TEXT("Coordinator equivocated between bootstrap authorizations."),
			/*bNotifyPeers=*/IsServer());
		ReportLocalBootstrapFailure(Context);
		return;
	}
	PendingBootstrapAuthorizationContext = Context;
	PendingBootstrapAuthorizationReceipt = Receipt;
	if (PendingLocalProtocolAssignment.IsSet()
		&& PendingLocalProtocolAssignment.Context == Context
		&& ActiveProtocolContext != Context)
	{
		return;
	}
	if (!ActiveProtocolContext.IsValid())
	{
		return;
	}
	if (!IsCurrentProtocolContext(
		Context, TEXT("ClientHandleBootstrapAuthorization")))
	{
		FailBootstrapSession(
			TEXT("Bootstrap authorization carried the wrong protocol context."),
			/*bNotifyPeers=*/IsServer());
		ReportLocalBootstrapFailure(Context);
		return;
	}

	TryAuthorizeLocalBootstrap();
	TryLaunchLocalBootstrap();
}

void USeinNetSubsystem::ClientHandleBootstrapLaunch(
	const FSeinProtocolContext& Context,
	const FSeinMatchBootstrapReceipt& Receipt)
{
	if (bLocalBootstrapIngressClosed)
	{
		UE_LOG(LogSeinNet, Verbose,
			TEXT("Ignoring bootstrap launch after local bootstrap ingress closed."));
		return;
	}
	if (!Context.IsValid() || !Receipt.IsValid()
		|| Receipt.ContractDigest != Context.MatchSettingsDigest
		|| Receipt.SimulationContentDigest
			!= Context.SimulationContentDigest)
	{
		FailLocalBootstrapAfterCommit(
			TEXT("Coordinator sent an invalid bootstrap launch."));
		return;
	}
	if (PendingBootstrapLaunchContext.IsSet()
		!= PendingBootstrapLaunchReceipt.IsSet()
		|| (PendingBootstrapLaunchContext.IsSet()
			&& (PendingBootstrapLaunchContext.GetValue() != Context
				|| PendingBootstrapLaunchReceipt.GetValue() != Receipt)))
	{
		FailLocalBootstrapAfterCommit(
			TEXT("Coordinator equivocated between bootstrap launch envelopes."));
		return;
	}
	PendingBootstrapLaunchContext = Context;
	PendingBootstrapLaunchReceipt = Receipt;
	if (PendingLocalProtocolAssignment.IsSet()
		&& PendingLocalProtocolAssignment.Context == Context
		&& ActiveProtocolContext != Context)
	{
		return;
	}
	if (!ActiveProtocolContext.IsValid()) return;
	if (!IsCurrentProtocolContext(
		Context, TEXT("ClientHandleBootstrapLaunch")))
	{
		FailLocalBootstrapAfterCommit(
			TEXT("Bootstrap launch carried the wrong protocol context."));
		return;
	}

	TryLaunchLocalBootstrap();
}

void USeinNetSubsystem::ClientHandleBootstrapFailure(
	const FSeinProtocolContext& Context,
	const FString& Reason)
{
	if (PendingLocalProtocolAssignment.IsSet()
		&& PendingLocalProtocolAssignment.Context == Context
		&& ActiveProtocolContext != Context)
	{
		ClientHandlePreparedMatchTravelCancelled(Context);
		UE_LOG(LogSeinNet, Error,
			TEXT("Prepared destination bootstrap failed before activation; source epoch preserved: %s"),
			*Reason.Left(512));
		return;
	}
	if (bLocalBootstrapIngressClosed)
	{
		UE_LOG(LogSeinNet, Verbose,
			TEXT("Ignoring bootstrap failure after local bootstrap ingress closed."));
		return;
	}
	if (!ActiveProtocolContext.IsValid())
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("Ignoring an unbound bootstrap-failure message before protocol assignment."));
		return;
	}
	if (Context != ActiveProtocolContext)
	{
		FailBootstrapSession(
			TEXT("Coordinator bootstrap-failure message carried the wrong context."),
			/*bNotifyPeers=*/false);
		return;
	}
	FailBootstrapSession(Reason, /*bNotifyPeers=*/false);
}

void USeinNetSubsystem::ClientHandlePreparedMatchTravelCancelled(
	const FSeinProtocolContext& Context)
{
	if (!PendingLocalProtocolAssignment.IsSet()
		|| PendingLocalProtocolAssignment.Context != Context)
	{
		return;
	}
	PendingLocalProtocolAssignment.Reset();
	if (DeferredBootstrapReceiptRequestContext.IsSet()
		&& DeferredBootstrapReceiptRequestContext.GetValue() == Context)
	{
		DeferredBootstrapReceiptRequestContext.Reset();
	}
	if (PendingBootstrapAuthorizationContext.IsSet()
		&& PendingBootstrapAuthorizationContext.GetValue() == Context)
	{
		PendingBootstrapAuthorizationContext.Reset();
		PendingBootstrapAuthorizationReceipt.Reset();
	}
	if (PendingBootstrapLaunchContext.IsSet()
		&& PendingBootstrapLaunchContext.GetValue() == Context)
	{
		PendingBootstrapLaunchContext.Reset();
		PendingBootstrapLaunchReceipt.Reset();
	}
	if (!PendingAuthorityProtocolState.IsSet())
	{
		CancelPendingProtocolPromotion();
	}
	UE_LOG(LogSeinNet, Log,
		TEXT("Cancelled an unactivated prepared destination assignment."));
}

void USeinNetSubsystem::TryStartLocalSession()
{
	if (!bStartSessionRequested) return;
	if (IsNetworkingActive())
	{
		bStartSessionRequested = false;
		UE_LOG(LogSeinNet, Warning,
			TEXT("TryStartLocalSession: network launch is coordinator-controlled; request rejected."));
		return;
	}
	UWorld* World = GetWorld();
	USeinWorldSubsystem* WorldSub =
		World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	if (!WorldSub) return;
	if (WorldSub->IsSimulationRunning())
	{
		bStartSessionRequested = false;
		return;
	}
	if (WorldSub->StandaloneBootstrapLauncher.IsBound()
		&& WorldSub->StandaloneBootstrapLauncher.Execute())
	{
		bStartSessionRequested = false;
	}
}

void USeinNetSubsystem::TryAuthorizeLocalBootstrap()
{
	if (!IsNetworkingActive()
		|| !PendingBootstrapAuthorizationContext.IsSet()
		|| !PendingBootstrapAuthorizationReceipt.IsSet()
		|| !BootstrapSessionFailureReason.IsEmpty())
	{
		return;
	}

	USeinWorldSubsystem* WorldSub = BindLockstepHooksForCurrentWorld();
	if (!WorldSub) return;
	const bool bHooksReady = CachedWorldSub.Get() == WorldSub
		&& TickCompletedHandle.IsValid()
		&& ExecutionTopologyInvalidatedHandle.IsValid()
		&& WorldSub->TurnReadyResolver.IsBound()
		&& WorldSub->TurnConsumeNotifier.IsBound()
		&& WorldSub->HasAIEmitInterceptor()
		&& WorldSub->HasLocalCommandSubmitter();
	if (!AreNetworkStartPrerequisitesReady(bHooksReady)) return;

	const FSeinProtocolContext AuthorizationContext =
		PendingBootstrapAuthorizationContext.GetValue();
	const FSeinMatchBootstrapReceipt AuthorizationReceipt =
		PendingBootstrapAuthorizationReceipt.GetValue();
	if (AuthorizationContext != ActiveProtocolContext
		|| AuthorizationReceipt.ContractDigest
			!= AuthorizationContext.MatchSettingsDigest
		|| AuthorizationReceipt.SimulationContentDigest
			!= AuthorizationContext.SimulationContentDigest)
	{
		FailBootstrapSession(
			TEXT("Deferred bootstrap authorization no longer matches the active protocol context."),
			/*bNotifyPeers=*/IsServer());
		ReportLocalBootstrapFailure(AuthorizationContext);
		return;
	}

	FSeinMatchBootstrapReceipt LocalReceipt;
	FString Error;
	if (!TryMaterializeLocalBootstrapReceipt(
			AuthorizationContext, LocalReceipt, Error)
		|| LocalReceipt != AuthorizationReceipt)
	{
		if (Error.IsEmpty())
		{
			Error = TEXT("Coordinator authorization differs from the local sealed bootstrap receipt.");
		}
		FailBootstrapSession(Error, /*bNotifyPeers=*/IsServer());
		ReportLocalBootstrapFailure(AuthorizationContext);
		return;
	}

	const FGuid AuthorizationContextDigest =
		SeinComputeBootstrapAuthorizationContextDigest(
			AuthorizationContext, SessionSeed);
	if (!WorldSub->AuthorizeMatchBootstrap(
			MatchBootstrapAuthority,
			AuthorizationReceipt,
			AuthorizationContextDigest,
			Error))
	{
		if (Error.IsEmpty())
		{
			Error = TEXT("Core rejected bootstrap authorization.");
		}
		FailBootstrapSession(Error, /*bNotifyPeers=*/IsServer());
		ReportLocalBootstrapFailure(AuthorizationContext);
		return;
	}

	PendingBootstrapAuthorizationContext.Reset();
	PendingBootstrapAuthorizationReceipt.Reset();
	SubmitLocalBootstrapAuthorizedReady(
		AuthorizationContext, AuthorizationReceipt);
	UE_LOG(LogSeinNet, Log,
		TEXT("Network bootstrap is locally authorized and awaiting the coordinator launch barrier."));

}

void USeinNetSubsystem::TryLaunchLocalBootstrap()
{
	if (!IsNetworkingActive()
		|| !PendingBootstrapLaunchContext.IsSet()
		|| !PendingBootstrapLaunchReceipt.IsSet()
		|| !BootstrapSessionFailureReason.IsEmpty())
	{
		return;
	}
	if (PendingBootstrapAuthorizationContext.IsSet())
	{
		TryAuthorizeLocalBootstrap();
		if (PendingBootstrapAuthorizationContext.IsSet()) return;
	}
	if (PendingBootstrapAuthorizationContext.IsSet()
		|| PendingBootstrapAuthorizationReceipt.IsSet())
	{
		FailLocalBootstrapAfterCommit(
			TEXT("Local bootstrap authorization cache is internally inconsistent."));
		return;
	}

	USeinWorldSubsystem* WorldSub = BindLockstepHooksForCurrentWorld();
	if (!WorldSub) return;
	const bool bHooksReady = CachedWorldSub.Get() == WorldSub
		&& TickCompletedHandle.IsValid()
		&& ExecutionTopologyInvalidatedHandle.IsValid()
		&& WorldSub->TurnReadyResolver.IsBound()
		&& WorldSub->TurnConsumeNotifier.IsBound()
		&& WorldSub->HasAIEmitInterceptor()
		&& WorldSub->HasLocalCommandSubmitter();
	if (!AreNetworkStartPrerequisitesReady(bHooksReady)) return;

	const FSeinProtocolContext LaunchContext =
		PendingBootstrapLaunchContext.GetValue();
	const FSeinMatchBootstrapReceipt LaunchReceipt =
		PendingBootstrapLaunchReceipt.GetValue();
	if (LaunchContext != ActiveProtocolContext
		|| LaunchReceipt.ContractDigest != LaunchContext.MatchSettingsDigest
		|| LaunchReceipt.SimulationContentDigest
			!= LaunchContext.SimulationContentDigest
		|| !LocalBootstrapReceipt.IsSet()
		|| LocalBootstrapReceipt.GetValue() != LaunchReceipt)
	{
		FailLocalBootstrapAfterCommit(
			TEXT("Deferred bootstrap launch no longer matches the locally authorized receipt/context."));
		return;
	}

	FString Error;
	if (!WorldSub->LaunchAuthorizedMatchBootstrap(
			MatchBootstrapAuthority, Error))
	{
		if (Error.IsEmpty())
		{
			Error = TEXT("Core rejected the coordinator's tick-zero launch.");
		}
		FailLocalBootstrapAfterCommit(Error);
		return;
	}

	// V9 replays restore the original network bootstrap identity instead of
	// synthesizing a replay-specific tick-zero world. Launch only arms the
	// dormant scheduler, so this is the one exact quiescent boundary where the
	// mandatory initial checkpoint can be captured before tick 1 executes.
	if (ReplayWriter && ReplayWriter->IsRecording()
		&& !ReplayWriter->CaptureCheckpoint(/*bRequired=*/true))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("Replay recording stopped because its mandatory tick-zero checkpoint could not be captured; the match will continue."));
	}

	// Prime the incremental world-root cache at the already-quiescent tick-zero
	// launch boundary. This moves the one unavoidable full cache construction
	// into match startup (beside the mandatory replay checkpoint) rather than
	// letting the first three-second gossip checkpoint become an in-play hitch.
	// Later checkpoints update only leaves mutated since the preceding report.
	if (IsDeterminismGossipEnabled()
		&& LocalParticipantID.IsValid()
		&& GetDeterminismCheckIntervalTurns() > 0
		&& HasComparableWorldRootPeerInManifest())
	{
		FGuid PrimedRoot;
		FString PrimeError;
		if (!WorldSub->SealRoutineCanonicalStateRoot(
				WorldSub->GetCurrentTick(),
				/*bForceFullRebuild=*/false,
				PrimedRoot,
				PrimeError))
		{
			UE_LOG(
				LogSeinNet,
				Warning,
				TEXT("Tick-zero routine world-root cache priming was deferred: %s"),
				PrimeError.IsEmpty()
					? TEXT("canonical root unavailable")
					: *PrimeError);
		}
	}

	PendingBootstrapLaunchContext.Reset();
	PendingBootstrapLaunchReceipt.Reset();
	bLocalBootstrapIngressClosed = true;
	CancelBootstrapMaterializerRetry();
	DeferredBootstrapReceiptRequestContext.Reset();
	PendingLocalBootstrapReceiptReportContext.Reset();
	PendingLocalBootstrapAuthorizedReadyReportContext.Reset();
	PendingBootstrapAuthorizationContext.Reset();
	PendingBootstrapAuthorizationReceipt.Reset();
	UE_LOG(LogSeinNet, Log,
		TEXT("Network bootstrap launch consumed the exact authorized receipt."));

	// Pre-fire the first non-grace heartbeat. Only a successful handoff advances
	// the queue ledger; relay-assignment races retain this exact heartbeat.
	if (LocalPlayerID.IsValid())
	{
		const int32 InputDelay = GetInputDelayTurns();
		if (LastQueuedTurn < InputDelay)
		{
			QueueTurnSubmissionsThrough(
				InputDelay, /*bAttachCurrentCommands=*/false);
			FlushPendingTurnSubmissions();
		}
	}
}

void USeinNetSubsystem::ServerHandleBootstrapReceipt(
	ASeinNetRelay* SourceRelay,
	const FSeinProtocolContext& Context,
	const FSeinMatchBootstrapReceipt& Receipt)
{
	if (!IsServer() || !SourceRelay) return;
	if (!bBootstrapLaunchBarrierActive)
	{
		UE_LOG(LogSeinNet, Verbose,
			TEXT("Ignoring bootstrap receipt outside the coordinator prepare barrier."));
		return;
	}
	const FSeinNetworkParticipantID ParticipantID =
		RelayToParticipant.FindRef(SourceRelay);
	if (!ParticipantID.IsValid()) return;
	SubmitBootstrapReceiptForParticipant(ParticipantID, Context, Receipt);
}

void USeinNetSubsystem::ServerHandleBootstrapAuthorizedReady(
	ASeinNetRelay* SourceRelay,
	const FSeinProtocolContext& Context,
	const FSeinMatchBootstrapReceipt& Receipt)
{
	if (!IsServer() || !SourceRelay) return;
	if (!bBootstrapLaunchBarrierActive)
	{
		UE_LOG(LogSeinNet, Verbose,
			TEXT("Ignoring bootstrap authorization-ready evidence outside the coordinator prepare barrier."));
		return;
	}
	const FSeinNetworkParticipantID ParticipantID =
		RelayToParticipant.FindRef(SourceRelay);
	if (!ParticipantID.IsValid()) return;
	SubmitBootstrapAuthorizedReadyForParticipant(
		ParticipantID, Context, Receipt);
}

void USeinNetSubsystem::ServerHandleBootstrapFailure(
	ASeinNetRelay* SourceRelay,
	const FSeinProtocolContext& Context)
{
	if (!IsServer() || !SourceRelay) return;
	if (!bBootstrapLaunchBarrierActive)
	{
		UE_LOG(LogSeinNet, Verbose,
			TEXT("Ignoring bootstrap failure outside the coordinator prepare barrier."));
		return;
	}
	const FSeinNetworkParticipantID ParticipantID =
		RelayToParticipant.FindRef(SourceRelay);
	const FSeinParticipantBinding* Binding =
		FindParticipantBinding(ParticipantID);
	if (!Binding || !Binding->bSimulates) return;
	if (Context != ActiveProtocolContext)
	{
		FailBootstrapSession(
			TEXT("A simulating participant reported bootstrap failure under the wrong context."),
			/*bNotifyPeers=*/true);
		return;
	}
	FailBootstrapSession(
		FString::Printf(
			TEXT("Simulating participant %s failed local match bootstrap."),
			*ParticipantID.ToCanonicalString()),
		/*bNotifyPeers=*/true);
}

void USeinNetSubsystem::SubmitBootstrapReceiptForParticipant(
	FSeinNetworkParticipantID ParticipantID,
	const FSeinProtocolContext& Context,
	const FSeinMatchBootstrapReceipt& Receipt)
{
	if (!IsServer() || !BootstrapSessionFailureReason.IsEmpty()) return;
	if (!bBootstrapLaunchBarrierActive)
	{
		UE_LOG(LogSeinNet, Verbose,
			TEXT("Ignoring bootstrap receipt submission after coordinator ingress closed."));
		return;
	}
	const FSeinParticipantBinding* Binding =
		FindParticipantBinding(ParticipantID);
	if (!Binding || !Binding->bSimulates) return;

	const ESeinBootstrapConsensusSubmitResult Result =
		BootstrapConsensus.Submit(Context, ParticipantID, Receipt);
	switch (Result)
	{
	case ESeinBootstrapConsensusSubmitResult::Accepted:
	case ESeinBootstrapConsensusSubmitResult::IdenticalRetry:
		return;
	case ESeinBootstrapConsensusSubmitResult::AgreementReached:
		DispatchBootstrapAuthorization();
		return;
	case ESeinBootstrapConsensusSubmitResult::AlreadyAgreed:
		return;
	case ESeinBootstrapConsensusSubmitResult::AlreadyFailed:
		FailBootstrapSession(
			TEXT("Tick-zero bootstrap consensus previously failed."),
			/*bNotifyPeers=*/true);
		return;
	case ESeinBootstrapConsensusSubmitResult::InvalidContext:
		FailBootstrapSession(
			TEXT("A bootstrap receipt carried the wrong protocol context."),
			/*bNotifyPeers=*/true);
		return;
	case ESeinBootstrapConsensusSubmitResult::InvalidReceipt:
		FailBootstrapSession(
			TEXT("A simulating participant reported an invalid bootstrap receipt."),
			/*bNotifyPeers=*/true);
		return;
	case ESeinBootstrapConsensusSubmitResult::ContractDigestMismatch:
		FailBootstrapSession(
			TEXT("A bootstrap receipt does not bind the prepared match-settings contract."),
			/*bNotifyPeers=*/true);
		return;
	case ESeinBootstrapConsensusSubmitResult::
		SimulationContentDigestMismatch:
		FailBootstrapSession(
			TEXT("A bootstrap receipt does not bind the prepared simulation content."),
			/*bNotifyPeers=*/true);
		return;
	case ESeinBootstrapConsensusSubmitResult::InvalidParticipant:
	case ESeinBootstrapConsensusSubmitResult::UnexpectedParticipant:
		FailBootstrapSession(
			TEXT("An unauthenticated participant submitted bootstrap evidence."),
			/*bNotifyPeers=*/true);
		return;
	case ESeinBootstrapConsensusSubmitResult::ConflictingRetry:
		FailBootstrapSession(
			TEXT("A simulating participant equivocated between bootstrap receipts."),
			/*bNotifyPeers=*/true);
		return;
	case ESeinBootstrapConsensusSubmitResult::ReceiptDisagreement:
		FailBootstrapSession(
			TEXT("Simulating participants produced different tick-zero bootstrap receipts."),
			/*bNotifyPeers=*/true);
		return;
	case ESeinBootstrapConsensusSubmitResult::InvalidPhase:
		FailBootstrapSession(
			TEXT("Bootstrap receipt evidence arrived in an invalid phase."),
			/*bNotifyPeers=*/true);
		return;
	default:
		return;
	}
}

void USeinNetSubsystem::SubmitBootstrapAuthorizedReadyForParticipant(
	FSeinNetworkParticipantID ParticipantID,
	const FSeinProtocolContext& Context,
	const FSeinMatchBootstrapReceipt& Receipt)
{
	if (!IsServer() || !BootstrapSessionFailureReason.IsEmpty()) return;
	if (!bBootstrapLaunchBarrierActive)
	{
		UE_LOG(LogSeinNet, Verbose,
			TEXT("Ignoring authorization-ready submission after coordinator ingress closed."));
		return;
	}
	const FSeinParticipantBinding* Binding =
		FindParticipantBinding(ParticipantID);
	if (!Binding || !Binding->bSimulates) return;

	const ESeinBootstrapConsensusSubmitResult Result =
		BootstrapConsensus.SubmitAuthorizedReady(
			Context, ParticipantID, Receipt);
	switch (Result)
	{
	case ESeinBootstrapConsensusSubmitResult::Accepted:
	case ESeinBootstrapConsensusSubmitResult::IdenticalRetry:
		return;
	case ESeinBootstrapConsensusSubmitResult::AuthorizationReady:
		DispatchBootstrapLaunch();
		return;
	case ESeinBootstrapConsensusSubmitResult::ConflictingRetry:
		FailBootstrapSession(
			TEXT("A simulating participant equivocated after bootstrap authorization."),
			/*bNotifyPeers=*/true);
		return;
	case ESeinBootstrapConsensusSubmitResult::ReceiptDisagreement:
		FailBootstrapSession(
			TEXT("An authorization-ready acknowledgement does not match the agreed receipt."),
			/*bNotifyPeers=*/true);
		return;
	case ESeinBootstrapConsensusSubmitResult::ContractDigestMismatch:
		FailBootstrapSession(
			TEXT("An authorization-ready acknowledgement does not bind the prepared match contract."),
			/*bNotifyPeers=*/true);
		return;
	case ESeinBootstrapConsensusSubmitResult::
		SimulationContentDigestMismatch:
		FailBootstrapSession(
			TEXT("An authorization-ready acknowledgement does not bind the prepared simulation content."),
			/*bNotifyPeers=*/true);
		return;
	case ESeinBootstrapConsensusSubmitResult::AlreadyFailed:
		FailBootstrapSession(
			TEXT("Bootstrap consensus failed before authorization readiness completed."),
			/*bNotifyPeers=*/true);
		return;
	default:
		FailBootstrapSession(
			TEXT("Invalid authorization-ready bootstrap evidence was rejected."),
			/*bNotifyPeers=*/true);
		return;
	}
}

void USeinNetSubsystem::ReportLocalBootstrapFailure(
	const FSeinProtocolContext& Context)
{
	if (bLocalBootstrapIngressClosed)
	{
		UE_LOG(LogSeinNet, Verbose,
			TEXT("Ignoring local bootstrap failure after bootstrap ingress closed."));
		return;
	}
	if (bBootstrapFailureReported) return;
	bBootstrapFailureReported = true;
	if (IsServer())
	{
		FailBootstrapSession(
			TEXT("The coordinator process failed local match bootstrap."),
			/*bNotifyPeers=*/true);
	}
	else if (ASeinNetRelay* Relay = LocalRelay.Get())
	{
		Relay->Server_ReportMatchBootstrapFailure(Context);
	}
}

void USeinNetSubsystem::FailBootstrapSession(
	const FString& Reason,
	bool bNotifyPeers)
{
	if ((IsServer() && BootstrapConsensus.IsLaunchComplete())
		|| (!IsServer() && bLocalBootstrapIngressClosed))
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("Ignoring bootstrap rollback request after launch commit: %s"),
			*Reason.Left(512));
		return;
	}
	if (!BootstrapSessionFailureReason.IsEmpty()) return;
	BootstrapSessionFailureReason = (Reason.IsEmpty()
		? TEXT("Match bootstrap failed without a diagnostic.")
		: Reason).Left(512);
	bServerStartRequested = false;
	bStartSessionRequested = false;
	bBootstrapLaunchBarrierActive = false;
	bLocalBootstrapIngressClosed = true;
	CancelPendingProtocolPromotion();
	PendingAuthorityProtocolState.Reset();
	PendingLocalProtocolAssignment.Reset();
	CancelBootstrapMaterializerRetry();
	CancelBootstrapCoordinatorTimeout();
	DeferredBootstrapReceiptRequestContext.Reset();
	PendingLocalBootstrapReceiptReportContext.Reset();
	PendingLocalBootstrapAuthorizedReadyReportContext.Reset();
	PendingBootstrapAuthorizationContext.Reset();
	PendingBootstrapAuthorizationReceipt.Reset();
	PendingBootstrapLaunchContext.Reset();
	PendingBootstrapLaunchReceipt.Reset();

	if (UWorld* World = GetWorld())
	{
		if (USeinWorldSubsystem* WorldSub =
			World->GetSubsystem<USeinWorldSubsystem>())
		{
			WorldSub->StopSimulation();
			if (MatchBootstrapAuthority.IsValid())
			{
				FString FailureError;
				if (!WorldSub->FailMatchBootstrap(
						MatchBootstrapAuthority,
						BootstrapSessionFailureReason,
						FailureError))
				{
					UE_LOG(LogSeinNet, Error,
						TEXT("Core rejected Net bootstrap failure authority: %s"),
						*FailureError);
				}
			}
		}
	}
	UE_LOG(LogSeinNet, Error, TEXT("Match bootstrap failed: %s"),
		*BootstrapSessionFailureReason);

	if (bNotifyPeers && IsServer() && ActiveProtocolContext.IsValid())
	{
		for (const TWeakObjectPtr<ASeinNetRelay>& WeakRelay : Relays)
		{
			if (ASeinNetRelay* Relay = WeakRelay.Get())
			{
				Relay->Client_FailMatchBootstrap(
					ActiveProtocolContext, BootstrapSessionFailureReason);
			}
		}
	}
}

void USeinNetSubsystem::FailLocalBootstrapAfterCommit(const FString& Reason)
{
	if (!BootstrapSessionFailureReason.IsEmpty()) return;
	BootstrapSessionFailureReason = (Reason.IsEmpty()
		? TEXT("Local tick-zero launch failed after coordinator commit.")
		: Reason).Left(512);
	bServerStartRequested = false;
	bStartSessionRequested = false;
	bBootstrapLaunchBarrierActive = false;
	bLocalBootstrapIngressClosed = true;
	CancelPendingProtocolPromotion();
	PendingAuthorityProtocolState.Reset();
	PendingLocalProtocolAssignment.Reset();
	CancelBootstrapMaterializerRetry();
	CancelBootstrapCoordinatorTimeout();
	DeferredBootstrapReceiptRequestContext.Reset();
	PendingLocalBootstrapReceiptReportContext.Reset();
	PendingLocalBootstrapAuthorizedReadyReportContext.Reset();
	PendingBootstrapAuthorizationContext.Reset();
	PendingBootstrapAuthorizationReceipt.Reset();
	PendingBootstrapLaunchContext.Reset();
	PendingBootstrapLaunchReceipt.Reset();

	if (UWorld* World = GetWorld())
	{
		if (USeinWorldSubsystem* WorldSub =
			World->GetSubsystem<USeinWorldSubsystem>())
		{
			WorldSub->StopSimulation();
			if (MatchBootstrapAuthority.IsValid())
			{
				FString FailureError;
				if (!WorldSub->FailMatchBootstrap(
						MatchBootstrapAuthority,
						BootstrapSessionFailureReason,
						FailureError))
				{
					UE_LOG(LogSeinNet, Error,
						TEXT("Core rejected local post-commit bootstrap failure: %s"),
						*FailureError);
				}
			}
		}
	}
	UE_LOG(LogSeinNet, Error,
		TEXT("Local bootstrap process failed after launch commit: %s"),
		*BootstrapSessionFailureReason);
}

void USeinNetSubsystem::DispatchBootstrapReceiptRequests()
{
	if (BootstrapConsensus.GetState()
		!= ESeinBootstrapConsensusState::CollectingReceipts)
	{
		return;
	}
	bBootstrapLaunchBarrierActive = true;
	StartBootstrapCoordinatorTimeout();
	if (!BootstrapSessionFailureReason.IsEmpty()) return;
	const TArray<FSeinNetworkParticipantID> Missing =
		BootstrapConsensus.GetMissingParticipants();
	TSet<FSeinNetworkParticipantID> MissingSet;
	for (const FSeinNetworkParticipantID ParticipantID : Missing)
	{
		MissingSet.Add(ParticipantID);
	}

	if (MissingSet.Contains(LocalParticipantID)
		&& IsLocalSimulatingParticipant())
	{
		// The coordinator uses the exact same local request/submit path as peers.
		ClientHandleBootstrapReceiptRequest(ActiveProtocolContext);
	}

	TSet<FSeinNetworkParticipantID> Requested;
	for (const TWeakObjectPtr<ASeinNetRelay>& WeakRelay : Relays)
	{
		ASeinNetRelay* Relay = WeakRelay.Get();
		if (!Relay) continue;
		const FSeinNetworkParticipantID ParticipantID =
			RelayToParticipant.FindRef(Relay);
		if (ParticipantID == LocalParticipantID
			|| !MissingSet.Contains(ParticipantID)
			|| Requested.Contains(ParticipantID))
		{
			continue;
		}
		Requested.Add(ParticipantID);
		Relay->Client_RequestMatchBootstrapReceipt(ActiveProtocolContext);
	}
}

void USeinNetSubsystem::DispatchBootstrapAuthorization()
{
	if (!IsServer() || !bServerStartRequested
		|| !BootstrapSessionFailureReason.IsEmpty())
	{
		return;
	}
	FSeinMatchBootstrapReceipt AgreedReceipt;
	if (BootstrapConsensus.GetState()
			!= ESeinBootstrapConsensusState::ReceiptAgreed
		|| !BootstrapConsensus.GetAgreedReceipt(AgreedReceipt)
		|| !BootstrapConsensus.BeginAuthorization())
	{
		FailBootstrapSession(
			TEXT("Bootstrap authorization was requested without receipt agreement."),
			/*bNotifyPeers=*/true);
		return;
	}
	if (IsLocalSimulatingParticipant()
		&& (!LocalBootstrapReceipt.IsSet()
			|| LocalBootstrapReceipt.GetValue() != AgreedReceipt))
	{
		FailBootstrapSession(
			TEXT("Coordinator local receipt differs from the agreed bootstrap receipt."),
			/*bNotifyPeers=*/true);
		return;
	}

	USeinWorldSubsystem* AuthorityWorld = IsLocalSimulatingParticipant()
		? BindLockstepHooksForCurrentWorld()
		: nullptr;
	if (AuthorityWorld)
	{
		if (!ReplayWriter) ReplayWriter = NewObject<USeinReplayWriter>(this);
		if (ReplayWriter && !ReplayWriter->IsRecording())
		{
			FSeinReplayHeader Header;
			Header.CommandProtocolDigest =
				AuthorityWorld->GetCommandProtocolDigest();
			Header.MatchSettingsDigest =
				AuthorityWorld->GetMatchSettingsDigest();
			Header.BootstrapReceipt = AgreedReceipt;
			Header.ConfigFingerprint = AuthorityWorld->GetConfigFingerprint();
			SeinReplayCompatibility::StampCurrent(Header, GetWorld());
			Header.RandomSeed = AuthorityWorld->GetSessionSeed();
			Header.SettingsSnapshot = AuthorityWorld->GetMatchSettings();
			for (const FSeinMatchSlot& Slot : Header.SettingsSnapshot.Slots)
			{
				if ((Slot.State != ESeinSlotState::Human
						&& Slot.State != ESeinSlotState::AI)
					|| Slot.SlotIndex <= 0 || Slot.SlotIndex > MAX_uint8)
				{
					continue;
				}
				FSeinPlayerRegistration& Player =
					Header.Players.Emplace_GetRef();
				Player.PlayerID = FSeinPlayerID(
					static_cast<uint8>(Slot.SlotIndex));
				Player.FactionID = Slot.FactionID;
				Player.TeamID = Slot.TeamID;
				Player.bIsAI = Slot.State == ESeinSlotState::AI;
			}
			Header.StartTick = AuthorityWorld->GetCurrentTick();
			Header.RecordedAt = FDateTime::UtcNow();
			ReplayWriter->StartRecording(Header);
		}
	}

	for (const TWeakObjectPtr<ASeinNetRelay>& WeakRelay : Relays)
	{
		ASeinNetRelay* Relay = WeakRelay.Get();
		if (!Relay) continue;
		const FSeinNetworkParticipantID ParticipantID =
			RelayToParticipant.FindRef(Relay);
		const FSeinParticipantBinding* Binding =
			FindParticipantBinding(ParticipantID);
		if (!Binding || !Binding->bSimulates
			|| ParticipantID == LocalParticipantID)
		{
			continue;
		}
		Relay->Client_AuthorizeMatchBootstrap(
			ActiveProtocolContext, AgreedReceipt);
	}
	if (IsLocalSimulatingParticipant())
	{
		ClientHandleBootstrapAuthorization(
			ActiveProtocolContext, AgreedReceipt);
	}
}

void USeinNetSubsystem::DispatchBootstrapLaunch()
{
	if (!IsServer() || !bServerStartRequested
		|| !BootstrapSessionFailureReason.IsEmpty())
	{
		return;
	}
	FSeinMatchBootstrapReceipt AgreedReceipt;
	if (BootstrapConsensus.GetState()
			!= ESeinBootstrapConsensusState::AuthorizedReady
		|| !BootstrapConsensus.GetAgreedReceipt(AgreedReceipt)
		|| !BootstrapConsensus.BeginLaunch())
	{
		FailBootstrapSession(
			TEXT("Bootstrap launch was requested before every peer acknowledged authorization."),
			/*bNotifyPeers=*/true);
		return;
	}

	// This transition is the irreversible distributed commit. Close every
	// coordinator-side prepare ingress before issuing any remote or local launch
	// so late evidence cannot roll back peers that have already started tick zero.
	bServerStartRequested = false;
	bStartSessionRequested = false;
	bBootstrapLaunchBarrierActive = false;
	CancelBootstrapCoordinatorTimeout();
	UE_LOG(LogSeinNet, Log,
		TEXT("Lockstep tick-zero launch committed after unanimous authorization readiness from %d simulating participant(s)."),
		BootstrapConsensus.GetAuthorizedReadyParticipantCount());

	for (const TWeakObjectPtr<ASeinNetRelay>& WeakRelay : Relays)
	{
		ASeinNetRelay* Relay = WeakRelay.Get();
		if (!Relay) continue;
		const FSeinNetworkParticipantID ParticipantID =
			RelayToParticipant.FindRef(Relay);
		const FSeinParticipantBinding* Binding =
			FindParticipantBinding(ParticipantID);
		if (!Binding || !Binding->bSimulates
			|| ParticipantID == LocalParticipantID)
		{
			continue;
		}
		Relay->Client_LaunchMatchBootstrap(
			ActiveProtocolContext, AgreedReceipt);
	}
	if (IsLocalSimulatingParticipant())
	{
		ClientHandleBootstrapLaunch(
			ActiveProtocolContext, AgreedReceipt);
	}
}

void USeinNetSubsystem::StartBootstrapCoordinatorTimeout()
{
	if (BootstrapCoordinatorTimeoutHandle.IsValid()) return;
	BootstrapCoordinatorDeadlineSeconds = FPlatformTime::Seconds()
		+ GSeinBootstrapCoordinatorTimeoutSeconds;
	BootstrapCoordinatorTimeoutHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(
			this, &USeinNetSubsystem::TickBootstrapCoordinatorTimeout));
	if (BootstrapCoordinatorTimeoutHandle.IsValid()) return;

	BootstrapCoordinatorDeadlineSeconds = 0.0;
	FailBootstrapSession(
		TEXT("Failed to schedule the bounded bootstrap coordinator timeout."),
		/*bNotifyPeers=*/true);
}

bool USeinNetSubsystem::TickBootstrapCoordinatorTimeout(float DeltaSeconds)
{
	(void)DeltaSeconds;
	if (!bBootstrapLaunchBarrierActive
		|| !BootstrapSessionFailureReason.IsEmpty()
		|| BootstrapConsensus.IsLaunchComplete())
	{
		BootstrapCoordinatorTimeoutHandle.Reset();
		BootstrapCoordinatorDeadlineSeconds = 0.0;
		return false;
	}
	if (FPlatformTime::Seconds() < BootstrapCoordinatorDeadlineSeconds)
	{
		return true;
	}

	const ESeinBootstrapConsensusState TimedOutState =
		BootstrapConsensus.GetState();
	const TArray<FSeinNetworkParticipantID> Missing =
		BootstrapConsensus.GetMissingParticipants();
	TArray<FString> MissingNames;
	MissingNames.Reserve(Missing.Num());
	for (const FSeinNetworkParticipantID ParticipantID : Missing)
	{
		MissingNames.Add(ParticipantID.ToCanonicalString());
	}
	BootstrapCoordinatorTimeoutHandle.Reset();
	BootstrapCoordinatorDeadlineSeconds = 0.0;
	FailBootstrapSession(
		FString::Printf(
			TEXT("Bootstrap coordinator timed out in phase %d awaiting participant(s) [%s]."),
			static_cast<int32>(TimedOutState),
			*FString::Join(MissingNames, TEXT(","))),
		/*bNotifyPeers=*/true);
	return false;
}

void USeinNetSubsystem::CancelBootstrapCoordinatorTimeout()
{
	if (BootstrapCoordinatorTimeoutHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(
			BootstrapCoordinatorTimeoutHandle);
		BootstrapCoordinatorTimeoutHandle.Reset();
	}
	BootstrapCoordinatorDeadlineSeconds = 0.0;
}

void USeinNetSubsystem::StartLockstepSession()
{
	if (!IsServer())
	{
		UE_LOG(LogSeinNet, Warning, TEXT("StartLockstepSession: callable only on server — ignored."));
		return;
	}
	if (PendingAuthorityProtocolState.IsSet())
	{
		TryPromotePendingAuthorityProtocolState();
		if (PendingAuthorityProtocolState.IsSet())
		{
			UE_LOG(LogSeinNet, Log,
				TEXT("StartLockstepSession: destination protocol remains pending until its world identity is loaded."));
			return;
		}
	}
	if (!ActiveProtocolContext.IsValid())
	{
		const UWorld* World = GetWorld();
		const FName DestinationWorldPackage = World && World->GetOutermost()
			? World->GetOutermost()->GetFName()
			: NAME_None;
		if (!PrepareMatchTravel(
			ESeinMatchTravelIntent::NewMatch,
			DestinationWorldPackage,
			ESeinPreparedWorldActivation::AllowCurrentWorld))
		{
			UE_LOG(LogSeinNet, Error,
				TEXT("StartLockstepSession: protocol preparation failed; start rejected."));
			return;
		}
		if (!TryPromotePendingAuthorityProtocolState())
		{
			UE_LOG(LogSeinNet, Error,
				TEXT("StartLockstepSession: current-world protocol preparation did not become active."));
			return;
		}
	}
	if (!TurnAggregator.IsConfigured() || ParticipantBindings.IsEmpty()
		|| !bHasActiveMatchSettings || !BootstrapConsensus.IsConfigured())
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("StartLockstepSession: invalid/empty protocol manifest; start rejected."));
		return;
	}
	if (!BootstrapSessionFailureReason.IsEmpty())
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("StartLockstepSession: bootstrap previously failed (%s)."),
			*BootstrapSessionFailureReason);
		return;
	}

	bDestinationStartPending = false;
	bServerStartRequested = true;
	bBootstrapLaunchBarrierActive = true;
	StartBootstrapCoordinatorTimeout();
	if (!BootstrapSessionFailureReason.IsEmpty()) return;
	TryDispatchLockstepSessionStart();
}

void USeinNetSubsystem::TryDispatchLockstepSessionStart()
{
	if (!bServerStartRequested || !IsServer()) return;
	if (PendingAuthorityProtocolState.IsSet()) return;
	if (!BootstrapSessionFailureReason.IsEmpty()) return;

	TArray<FSeinNetworkParticipantID> MissingStartParticipants;
	if (!AreRequiredStartParticipantsBound(&MissingStartParticipants))
	{
		TArray<FString> MissingNames;
		MissingNames.Reserve(MissingStartParticipants.Num());
		for (const FSeinNetworkParticipantID ParticipantID : MissingStartParticipants)
		{
			MissingNames.Add(ParticipantID.ToCanonicalString());
		}
		UE_LOG(LogSeinNet, Log,
			TEXT("StartLockstepSession: awaiting destination relay/bootstrap for participant(s) [%s]."),
			*FString::Join(MissingNames, TEXT(",")));
		return;
	}

	TArray<FSeinNetworkParticipantID> MissingParticipants;
	if (!IsConfigParityStartBarrierSatisfied(&MissingParticipants))
	{
		TArray<FString> MissingNames;
		MissingNames.Reserve(MissingParticipants.Num());
		for (const FSeinNetworkParticipantID ParticipantID : MissingParticipants)
		{
			MissingNames.Add(ParticipantID.ToCanonicalString());
		}
		UE_LOG(LogSeinNet, Log,
			TEXT("StartLockstepSession: deferred by config parity barrier; awaiting participant(s) [%s]."),
			*FString::Join(MissingNames, TEXT(",")));
		return;
	}

	FString DestinationError;
	if (!IsCurrentWorldPreparedDestination(&DestinationError))
	{
		FailBootstrapSession(DestinationError, /*bNotifyPeers=*/true);
		return;
	}

	EnsureSessionSeed();
	switch (BootstrapConsensus.GetState())
	{
	case ESeinBootstrapConsensusState::CollectingReceipts:
		DispatchBootstrapReceiptRequests();
		return;
	case ESeinBootstrapConsensusState::ReceiptAgreed:
		DispatchBootstrapAuthorization();
		return;
	case ESeinBootstrapConsensusState::CollectingAuthorizedReady:
		return;
	case ESeinBootstrapConsensusState::AuthorizedReady:
		DispatchBootstrapLaunch();
		return;
	case ESeinBootstrapConsensusState::Launched:
		return;
	default:
		FailBootstrapSession(
			TEXT("Bootstrap consensus is not available for the active context."),
			/*bNotifyPeers=*/true);
		return;
	}
}

bool USeinNetSubsystem::ResolveTurnReady(int32 Turn)
{
	// Root gossip is a proof obligation, not an optional diagnostic. Once the
	// epoch can no longer produce or complete a due checkpoint, no grace turn,
	// buffered fan-out, or later restart may advance it.
	if (DeterminismSessionFailure.IsValid()) return false;

	// Grace period: the first InputDelayTurns turns can never have submissions
	// (no input could have been issued before sim started), so unconditionally
	// pass them. This matches the heartbeat schedule below — first heartbeat
	// is for turn `0 + InputDelay`, fired after sim's first turn worth of
	// ticks complete, so turn `InputDelay` is the first gated turn.
	if (Turn < GetInputDelayTurns()) return true;

	const bool bReady = ReceivedTurns.Contains(Turn);
	if (!bReady)
	{
		// Only a turn that reaches its execution boundary without fan-out is
		// evidence that the configured input delay failed to absorb latency.
		// Merely observing the first of several author submissions is normal
		// ordering and must not be reported as a straggler event.
		if (TurnAggregator.IsConfigured()
			&& !TurnAggregator.IsTurnCommitted(Turn)
			&& !TurnAggregator.IsTurnRetired(Turn))
		{
			const double NowSec = FPlatformTime::Seconds();
			FSeinIncompleteTurnDiagnostic& Diagnostic =
				IncompleteTurnDiagnostics.FindOrAdd(Turn);
			if (Diagnostic.FirstObservedAt <= 0.0)
			{
				Diagnostic.FirstObservedAt = NowSec;
				Diagnostic.LastLoggedAt = NowSec;
			}
			Diagnostic.bReachedExecutionGate = true;
		}

		// Per-tick stall noise — Verbose.
		UE_LOG(LogSeinNet, Verbose, TEXT("ResolveTurnReady: turn %d NOT ready — sim stalls."), Turn);

		// Persistent-stall escalation: most "not ready" hits are transient
		// pipeline blips (one peer's heartbeat is in flight). Only escalate
		// to Log level when the SAME turn has been stuck for ≥2 seconds —
		// that's a real stall, worth surfacing without verbose. Transient
		// blips stay at Verbose.
		const double NowSec = FPlatformTime::Seconds();
		if (Turn != LastStalledTurn)
		{
			// New unready turn — start the timer, log at Verbose.
			LastStalledTurn = Turn;
			FirstStalledAtTime = NowSec;
			LastStallLogTime = NowSec;
			bStallLogEscalated = false;
			UE_LOG(LogSeinNet, Verbose,
				TEXT("[GATE STALL transient] turn=%d not ready (waiting for fan-out)."),
				Turn);
		}
		else
		{
			const double StalledFor = NowSec - FirstStalledAtTime;
			if (StalledFor >= 2.0)
			{
				// Persistent stall — log at Log level, but rate-limit to
				// once every 2 seconds so the log stays readable on a
				// long-frozen sim.
				if (!bStallLogEscalated || (NowSec - LastStallLogTime) >= 2.0)
				{
					UE_LOG(LogSeinNet, Log,
						TEXT("[GATE STALL persistent] turn=%d not ready for %.1fs  LocalSlot=%u  ReceivedTurns=%d entries  PendingOutgoing=%d  LastSubmittedTurn=%d. Sim is frozen — one peer's heartbeat dropped."),
						Turn, StalledFor, LocalPlayerID.Value, ReceivedTurns.Num(), PendingOutgoingDrafts.Num(), LastSubmittedTurn);
					LastStallLogTime = NowSec;
					bStallLogEscalated = true;
				}
			}
		}
	}
	else
	{
		// Turn became ready — reset the persistence tracker so the next
		// transient blip starts fresh.
		if (Turn == LastStalledTurn)
		{
			LastStalledTurn = -1;
			FirstStalledAtTime = 0.0;
			bStallLogEscalated = false;
		}
	}
	return bReady;
}

void USeinNetSubsystem::ConsumeTurn(int32 Turn)
{
	TArray<FSeinCommand> Drained;
	if (!ReceivedTurns.RemoveAndCopyValue(Turn, Drained))
	{
		// Grace turn (no entry stored) or already drained — nothing to do.
		return;
	}

	UWorld* World = GetWorld();
	USeinWorldSubsystem* WorldSub = World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	if (!WorldSub) return;

	for (const FSeinCommand& Cmd : Drained)
	{
		WorldSub->EnqueueAuthenticatedCommand(
			Cmd, Cmd.PlayerID, Cmd.IssuerKind);
	}
	UE_LOG(LogSeinNet, Verbose, TEXT("ConsumeTurn: drained turn %d (%d cmd(s)) into PendingCommands."),
		Turn, Drained.Num());
}

void USeinNetSubsystem::OnSimTickCompleted(int32 CompletedTick)
{
	if (ReplayWriter && ReplayWriter->IsRecording())
	{
		ReplayWriter->ObserveCompletedTick(CompletedTick);
	}
	if (!IsNetworkingActive()) return;
	const int32 TicksPerTurn = GetTicksPerTurn();
	const int32 NextTick = CompletedTick + 1;
	const bool bCompletedTurnBoundary =
		TicksPerTurn > 0 && NextTick % TicksPerTurn == 0;

	// Fire only on turn boundaries — i.e., the tick we just completed was the
	// last tick of a turn (so the next tick starts a new turn).
	if (!bCompletedTurnBoundary) return;

	// Compute outgoing turn: commands accumulated during the turn we just
	// finished apply at `current_turn + InputDelay`. Idempotent guard against
	// multiple OnSimTickCompleted fires within the same boundary.
	const int32 JustFinishedTurn = CompletedTick / TicksPerTurn;

	// Routine gossip asks for an exact root only at its configured checkpoint
	// cadence (30 turns / 3 seconds by default). The incremental cache already
	// coalesces every mutation since the preceding checkpoint into the exact
	// latest leaf value, so sealing the same dirty leaves after every intervening
	// turn multiplied reflection and tree-update work without adding evidence.
	//
	// Reporter eligibility comes from the frozen participant manifest. A client
	// does not own the coordinator's complete RelayToParticipant lifecycle map;
	// consulting that server-only view here made clients incorrectly skip every
	// checkpoint and eventually triggered an "expired incomplete" session stop.
	bool bMaintainRoutineRootForTests = false;
#if WITH_DEV_AUTOMATION_TESTS
	bMaintainRoutineRootForTests =
		TestDeterminismCheckIntervalOverride.IsSet();
#endif
	if (IsDeterminismGossipEnabled()
		&& LocalParticipantID.IsValid()
		&& IsDueWorldStateRootCheckpoint(JustFinishedTurn)
		&& (HasComparableWorldRootPeerInManifest()
			|| bMaintainRoutineRootForTests))
	{
		UWorld* World = GetWorld();
		USeinWorldSubsystem* WorldSub = World
			? World->GetSubsystem<USeinWorldSubsystem>()
			: nullptr;
		FGuid MaintainedRoot;
		FString MaintenanceError;
		if (!WorldSub
			|| !WorldSub->SealRoutineCanonicalStateRoot(
				CompletedTick,
				/*bForceFullRebuild=*/false,
				MaintainedRoot,
				MaintenanceError))
		{
			UE_LOG(
				LogSeinNet,
				VeryVerbose,
				TEXT("Routine world-root maintenance unavailable at tick %d: %s"),
				CompletedTick,
				MaintenanceError.IsEmpty()
					? TEXT("no simulation subsystem")
					: *MaintenanceError);
		}
	}
	ApplyDueAuthenticatedDeterminismSessionFailuresThrough(
		JustFinishedTurn);
	if (DeterminismSessionFailure.IsValid()) return;
	const int32 InputDelay = GetInputDelayTurns();
	const int32 OutgoingTurn = JustFinishedTurn + InputDelay;

	// A resyncing peer replays the past: it must neither author turns the
	// coordinator already committed nor report roots for boundaries the
	// session has moved beyond. Suppression begins at ADOPTION — during the
	// transfer the local sim is still on its old timeline and keeps
	// submitting normally (the coordinator's Reconnecting flip rejects and
	// heartbeat-covers those turns), which closes the request-to-flip RTT
	// window that would otherwise leave open turns with neither a
	// submission nor a heartbeat.
	const bool bResyncSuppressed =
		ClientResyncPhase != EClientResyncPhase::None
		&& ClientResyncPhase != EClientResyncPhase::Transferring;
	ClientAdvanceResyncCatchUp(JustFinishedTurn);

	// Only a local gameplay slot authors ordinary command batches. World-root
	// identity is participant-scoped, so a dedicated authority reports too.
	if (LocalPlayerID.IsValid() && !bResyncSuppressed)
	{
		// Catch up any skipped non-grace turns with heartbeats, attaching user
		// commands only to the latest outgoing turn.
		if (OutgoingTurn > LastSubmittedTurn)
		{
			QueueTurnSubmissionsThrough(OutgoingTurn, /*bAttachCurrentCommands=*/true);
			FlushPendingTurnSubmissions();
		}

	}
	if (LocalParticipantID.IsValid() && !bResyncSuppressed)
	{
		MaybeSubmitWorldStateRootCheck(JustFinishedTurn);
	}
	if (DeterminismSessionFailure.IsValid()) return;

	PruneProtocolState(JustFinishedTurn);
	if (DeterminismSessionFailure.IsValid()) return;

	// Drop-in/drop-out (Phase 4): server-only per-turn polling. Inject
	// heartbeats for dropped slots so the gate doesn't stall, then evaluate
	// whether any dropped slot has timed out into AI takeover. Cheap walks
	// over a small map; safe per-turn cost.
	if (IsServer())
	{
		// Heartbeats target the OUTGOING turn (= JustFinishedTurn + InputDelay).
		// That's the turn the next gate completion will need a slot from.
		InjectDroppedSlotHeartbeats(OutgoingTurn, /*bAllowAICommands=*/true);
		ServerCheckTurnComplete(OutgoingTurn);
		EvaluateDroppedSlots();
		ServerAdvanceResyncTransfers();
		ServerAdvanceResyncActivation(JustFinishedTurn);
	}
}

void USeinNetSubsystem::QueueTurnSubmissionsThrough(int32 FinalTurn, bool bAttachCurrentCommands)
{
	const int32 InputDelay = GetInputDelayTurns();
	const int32 FirstTurn = FMath::Max3(LastQueuedTurn + 1, LastSubmittedTurn + 1, InputDelay);
	if (FirstTurn > FinalTurn) return;

	for (int32 Turn = FirstTurn; Turn <= FinalTurn; ++Turn)
	{
		FSeinPendingTurnSubmission& Pending = PendingTurnSubmissions.Emplace_GetRef();
		Pending.TurnId = Turn;
		if (bAttachCurrentCommands && Turn == FinalTurn)
		{
			FreezeLargestOutgoingDraftPrefix(Turn, Pending.Drafts);
		}
	}
	LastQueuedTurn = FinalTurn;
}

void USeinNetSubsystem::FlushPendingTurnSubmissions()
{
	while (!PendingTurnSubmissions.IsEmpty())
	{
		const FSeinPendingTurnSubmission& Pending = PendingTurnSubmissions[0];
		bool bSent = false;
#if WITH_DEV_AUTOMATION_TESTS
		if (TestTurnSubmitOverride)
		{
			TArray<FSeinCommand> Commands;
			Commands.Reserve(Pending.Drafts.Num());
			for (const FSeinCommandSubmissionDraft& Draft : Pending.Drafts)
			{
				Commands.Add(Draft.Command);
			}
			bSent = TestTurnSubmitOverride(Pending.TurnId, Commands);
		}
		else
#endif
		{
			bSent = SubmitLocalDraftsAtTurn(Pending.TurnId, Pending.Drafts);
		}

		if (!bSent)
		{
			UE_LOG(LogSeinNet, Warning,
				TEXT("FlushPendingTurnSubmissions: retaining turn=%d cmds=%d for retry."),
				Pending.TurnId, Pending.Drafts.Num());
			return;
		}

		UE_LOG(LogSeinNet, Verbose,
			TEXT("FlushPendingTurnSubmissions: handed off turn=%d cmds=%d heartbeat=%d."),
			Pending.TurnId, Pending.Drafts.Num(), Pending.Drafts.IsEmpty() ? 1 : 0);
		LastSubmittedTurn = FMath::Max(LastSubmittedTurn, Pending.TurnId);
		PendingTurnSubmissions.RemoveAt(0, 1, EAllowShrinking::No);
	}
}

void USeinNetSubsystem::SubmitLocalCommand(const FSeinCommand& Command)
{
	SubmitLocalCommandDraft(Command, /*bRequestMatchAdministration=*/false);
}

void USeinNetSubsystem::SubmitLocalCommandDraft(
	const FSeinCommand& Draft,
	bool bRequestMatchAdministration)
{
	UWorld* World = GetWorld();
	USeinWorldSubsystem* WorldSub =
		World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	const bool bNetworkingActive = IsNetworkingActive();
	if (bNetworkingActive)
	{
		FSeinCommandSchemaDescriptor Schema;
		const bool bHasSchema = FindFrozenCommandSchema(
			Draft.CommandType, Draft.SchemaVersion, Schema);
		if (IsUnsupportedNetworkPauseCommand(
				Draft, bHasSchema ? &Schema : nullptr))
		{
			UE_LOG(LogSeinNet, Error,
				TEXT("SubmitLocalCommandDraft: rejected unsupported network pause-control '%s'; the canonical coordinator/digest lane is not installed, so this command must not enter the ordinary turn queue."),
				*Draft.CommandType.ToString());
			return;
		}
	}

	if (!WorldSub
		|| WorldSub->GetMatchBootstrapState()
			!= ESeinMatchBootstrapState::Consumed
		|| !WorldSub->IsSimulationRunning())
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("SubmitLocalCommandDraft: rejected '%s' because ordinary ingress requires a launched, running match."),
			*Draft.CommandType.ToString());
		return;
	}

	if (bNetworkingActive)
	{
		FSeinCommand Untrusted = Draft;
		Untrusted.PlayerID = FSeinPlayerID::Neutral();
		Untrusted.IssuerKind = ESeinCommandIssuerKind::Unauthenticated;
		Untrusted.DerivedResourcePayer = FSeinPlayerID::Neutral();
		Untrusted.Tick = 0;
		if (!TryBufferOutgoingDraft(FSeinCommandSubmissionDraft(
				Untrusted, bRequestMatchAdministration)))
		{
			return;
		}
	}

	OnLocalCommandIssued.Broadcast(Draft);
	OnLocalCommandIssuedBP.Broadcast(Draft);

	if (!bNetworkingActive)
	{
		if (!WorldSub) return;

		FSeinCommand Canonical = Draft;
		TArray<FSeinCommand> Single{Canonical};
		StampAuthoritativeCommandBatch(
			Single,
			Draft.PlayerID,
			bRequestMatchAdministration,
			/*TurnId=*/0);
		Single[0].Tick = WorldSub->GetCurrentTick();
		WorldSub->EnqueueAuthenticatedCommand(
			Single[0], Single[0].PlayerID, Single[0].IssuerKind);
	}
}

void USeinNetSubsystem::SubmitLocalCommands(const TArray<FSeinCommand>& Commands)
{
	for (const FSeinCommand& Command : Commands)
	{
		SubmitLocalCommandDraft(
			Command, /*bRequestMatchAdministration=*/false);
	}
}

bool USeinNetSubsystem::SubmitLocalCommandAtTurn(int32 TurnId, const FSeinCommand& Command)
{
	TArray<FSeinCommand> Single;
	Single.Add(Command);
	return SubmitLocalCommandsAtTurn(TurnId, Single);
}

bool USeinNetSubsystem::SubmitLocalCommandsAtTurn(int32 TurnId, const TArray<FSeinCommand>& Commands)
{
	TArray<FSeinCommandSubmissionDraft> Drafts;
	Drafts.Reserve(Commands.Num());
	for (const FSeinCommand& Command : Commands)
	{
		Drafts.Emplace(Command, false);
	}
	return SubmitLocalDraftsAtTurn(TurnId, Drafts);
}

bool USeinNetSubsystem::SubmitLocalDraftsAtTurn(
	int32 TurnId,
	const TArray<FSeinCommandSubmissionDraft>& Drafts)
{
	// NOTE: empty `Commands` is intentionally allowed — the heartbeat path
	// relies on a per-turn submission even when no input was issued, so the
	// server's gate can complete for that turn. Skipping empties would stall
	// every peer.
	const int32 MaxCommandsPerSubmission = IsNetworkingActive()
		? FrozenMaxCommandsPerSubmission
		: GetConfiguredMaxCommandsPerSubmission();
	if (MaxCommandsPerSubmission <= 0)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("SubmitLocalCommandsAtTurn: per-match author submission policy is not frozen."));
		return false;
	}
	if (Drafts.Num() > MaxCommandsPerSubmission)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("SubmitLocalCommandsAtTurn: %d commands exceeds protocol cap %d."),
			Drafts.Num(), MaxCommandsPerSubmission);
		return false;
	}

	// Standalone / networking-disabled path: skip the relay entirely and
	// drop straight into the world subsystem's command buffer. Single-player
	// is zero-network-overhead; the lockstep wire is purely opt-in.
	if (!IsNetworkingActive())
	{
		if (Drafts.Num() == 0) return true; // no heartbeat needed in Standalone (treat as "sent")
		UWorld* World = GetWorld();
		USeinWorldSubsystem* WorldSub = World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
		if (!WorldSub)
		{
			UE_LOG(LogSeinNet, Warning, TEXT("SubmitLocalCommandsAtTurn: Standalone but no USeinWorldSubsystem — dropping %d cmd(s)."), Drafts.Num());
			return false;
		}
		UE_LOG(LogSeinNet, Verbose, TEXT("SubmitLocalCommandsAtTurn [Standalone]: enqueuing %d cmd(s) directly to WorldSubsystem."), Drafts.Num());
		for (const FSeinCommandSubmissionDraft& Draft : Drafts)
		{
			TArray<FSeinCommand> Stamped{Draft.Command};
			StampAuthoritativeCommandBatch(
				Stamped, Draft.Command.PlayerID,
				Draft.bRequestMatchAdministration,
				TurnId);
			WorldSub->EnqueueAuthenticatedCommand(
				Stamped[0], Stamped[0].PlayerID, Stamped[0].IssuerKind);
		}
		return true;
	}
	for (const FSeinCommandSubmissionDraft& Draft : Drafts)
	{
		FSeinCommandSchemaDescriptor Schema;
		const bool bHasSchema = FindFrozenCommandSchema(
			Draft.Command.CommandType, Draft.Command.SchemaVersion, Schema);
		if (IsUnsupportedNetworkPauseCommand(
				Draft.Command, bHasSchema ? &Schema : nullptr))
		{
			UE_LOG(LogSeinNet, Error,
				TEXT("SubmitLocalCommandsAtTurn: rejected unsupported network pause-control '%s'; the canonical coordinator/digest lane is not installed."),
				*Draft.Command.CommandType.ToString());
			return false;
		}
	}

	if (!IsCommandTurnWithinProtocolWindow(TurnId, TEXT("SubmitLocalCommandsAtTurn")))
	{
		return false;
	}
	if (!ActiveProtocolContext.IsValid() || !LocalParticipantID.IsValid())
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("SubmitLocalCommandsAtTurn: protocol assignment not ready."));
		return false;
	}

	ASeinNetRelay* Relay = LocalRelay.Get();
	if (!Relay)
	{
		// Promoted to Warning level (was already Warning) — but caller MUST
		// observe the false return and not advance any "last submitted"
		// tracking, otherwise the missed turn is lost forever. See
		// StartLocalSession's pre-fire guard.
		UE_LOG(LogSeinNet, Warning,
			TEXT("SubmitLocalCommandsAtTurn: no LocalRelay yet (replication pending?) — dropping %d cmd(s) for TurnId=%d. Caller MUST treat this as not-submitted."),
			Drafts.Num(), TurnId);
		return false;
	}

	UE_LOG(LogSeinNet, Verbose, TEXT("SubmitLocalCommandsAtTurn: TurnId=%d  Count=%d  Slot=%u  Relay=%s%s"),
		TurnId, Drafts.Num(), LocalPlayerID.Value, *GetNameSafe(Relay),
		Drafts.IsEmpty() ? TEXT("  [HEARTBEAT]") : TEXT(""));
	TArray<FSeinCommandSubmissionDraft> UntrustedDrafts = Drafts;
	for (FSeinCommandSubmissionDraft& Draft : UntrustedDrafts)
	{
		Draft.Command.PlayerID = FSeinPlayerID::Neutral();
		Draft.Command.IssuerKind = ESeinCommandIssuerKind::Unauthenticated;
		Draft.Command.DerivedResourcePayer = FSeinPlayerID::Neutral();
		Draft.Command.Tick = 0;
	}
	FSeinOpaqueCommandBatch OpaqueDrafts;
	FString WireError;
	const int32 FrozenAuthorCount = GetFrozenExpectedAuthorCount();
	if (FrozenAuthorCount <= 0)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("SubmitLocalCommandsAtTurn: frozen command-author count is unavailable."));
		return false;
	}
	const FSeinAuthorSubmissionBudget AuthorBudget =
		GetAuthorSubmissionBudget(
			FrozenAuthorCount, FrozenMaxCommandsPerSubmission);
	FSeinWireCost WireCost;
	if (!FSeinNetCommandWireCodec::EncodeDraftsWithCost(
		UntrustedDrafts,
		AuthorBudget.MaxCommands,
		[this](FGameplayTag Type, int32 Version, FSeinCommandSchemaDescriptor& Out)
		{
			return FindFrozenCommandSchema(Type, Version, Out);
		},
		OpaqueDrafts,
		WireError,
		WireCost)
		|| !ValidateAuthorSubmissionBudget(
			UntrustedDrafts.Num(), OpaqueDrafts,
			WireCost.CanonicalCostBytes, WireError))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("SubmitLocalCommandsAtTurn: opaque encoding failed for turn=%d: %s."),
			TurnId, *WireError);
		return false;
	}
	Relay->Server_SubmitCommands(ActiveProtocolContext, TurnId, OpaqueDrafts);
	return true;
}

void USeinNetSubsystem::StampAuthoritativeCommandBatch(
	TArray<FSeinCommand>& Commands,
	FSeinPlayerID Slot,
	bool bGrantMatchAdministration,
	int32 TurnId) const
{
	const int32 AuthoritativeTick = TurnId * GetTicksPerTurn();
	for (FSeinCommand& Command : Commands)
	{
		FSeinCommandSchemaDescriptor Schema;
		const bool bIsRegisteredMatchControl =
			bGrantMatchAdministration
			&& FindFrozenCommandSchema(
				Command.CommandType, Command.SchemaVersion, Schema)
			&& Schema.AuthorityScope == ESeinCommandAuthorityScope::MatchControl;
		Command.IssuerKind = bIsRegisteredMatchControl
			? ESeinCommandIssuerKind::MatchAdministrator
			: ESeinCommandIssuerKind::Player;
		Command.PlayerID = bIsRegisteredMatchControl
			? FSeinPlayerID::Neutral()
			: Slot;
		Command.DerivedResourcePayer = FSeinPlayerID::Neutral();
		Command.Tick = AuthoritativeTick;
	}
}

bool USeinNetSubsystem::FindFrozenCommandSchema(
	FGameplayTag CommandType,
	int32 SchemaVersion,
	FSeinCommandSchemaDescriptor& OutSchema) const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (TestFindCommandSchemaOverride)
	{
		return TestFindCommandSchemaOverride(
			CommandType, SchemaVersion, OutSchema);
	}
#endif
	const UWorld* World = GetWorld();
	const USeinWorldSubsystem* WorldSub =
		World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	return WorldSub
		&& WorldSub->FindCommandSchema(CommandType, SchemaVersion, OutSchema);
}

bool USeinNetSubsystem::IsUnsupportedNetworkPauseCommand(
	const FSeinCommand& Command,
	const FSeinCommandSchemaDescriptor* Schema)
{
	return Command.CommandType == SeinARTSTags::Command_Type_PauseMatchRequest
		|| (Schema
			&& (Schema->AllowedExecutionContexts
				& static_cast<int32>(ESeinCommandExecutionAllowance::FrozenPauseControl)) != 0);
}

bool USeinNetSubsystem::MeasureOutgoingDraftCost(
	const FSeinCommandSubmissionDraft& Draft,
	FSeinQueuedCommandCost& OutCost,
	FString& OutError) const
{
	OutCost = {};
	FSeinOpaqueCommandBatch Encoded;
	FSeinWireCost WireCost;
	if (!FSeinNetCommandWireCodec::EncodeDraftsWithCost(
		MakeArrayView(&Draft, 1),
		1,
		[this](FGameplayTag Type, int32 Version, FSeinCommandSchemaDescriptor& Out)
		{
			return FindFrozenCommandSchema(Type, Version, Out);
		},
		Encoded,
		OutError,
		WireCost)
		|| Encoded.Bytes.Num() < FSeinNetCommandWireCodec::FixedBatchHeaderBytes)
	{
		if (OutError.IsEmpty()) OutError = TEXT("invalid single-draft opaque frame");
		return false;
	}
	OutCost.VariableWireBytes =
		Encoded.Bytes.Num() - FSeinNetCommandWireCodec::FixedBatchHeaderBytes;
	if (WireCost.CanonicalCostBytes
		< static_cast<uint64>(FSeinNetCommandWireCodec::FixedBatchHeaderBytes))
	{
		OutError = TEXT("single-draft canonical cost omits its batch header");
		return false;
	}
	OutCost.VariableCanonicalCostBytes = WireCost.CanonicalCostBytes
		- FSeinNetCommandWireCodec::FixedBatchHeaderBytes;
	return true;
}

bool USeinNetSubsystem::MeasureAICommandCost(
	const FSeinCommand& Command,
	FSeinQueuedCommandCost& OutCost,
	FString& OutError) const
{
	OutCost = {};
	FSeinOpaqueCommandBatch Encoded;
	FSeinWireCost WireCost;
	if (!FSeinNetCommandWireCodec::EncodeCommandsWithCost(
		MakeArrayView(&Command, 1),
		1,
		[this](FGameplayTag Type, int32 Version, FSeinCommandSchemaDescriptor& Out)
		{
			return FindFrozenCommandSchema(Type, Version, Out);
		},
		Encoded,
		OutError,
		WireCost)
		|| Encoded.Bytes.Num() < FSeinNetCommandWireCodec::FixedBatchHeaderBytes)
	{
		if (OutError.IsEmpty()) OutError = TEXT("invalid single-command opaque frame");
		return false;
	}
	OutCost.VariableWireBytes =
		Encoded.Bytes.Num() - FSeinNetCommandWireCodec::FixedBatchHeaderBytes;
	if (WireCost.CanonicalCostBytes
		< static_cast<uint64>(FSeinNetCommandWireCodec::FixedBatchHeaderBytes))
	{
		OutError = TEXT("single-command canonical cost omits its batch header");
		return false;
	}
	OutCost.VariableCanonicalCostBytes = WireCost.CanonicalCostBytes
		- FSeinNetCommandWireCodec::FixedBatchHeaderBytes;
	return true;
}

bool USeinNetSubsystem::TryBufferOutgoingDraft(
	const FSeinCommandSubmissionDraft& Draft)
{
	if (!FreezeAuthorSubmissionPolicy(TEXT("SubmitLocalCommandDraft")))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("SubmitLocalCommandDraft: dropping command because no frozen match submission policy is installed."));
		return false;
	}
	const FSeinAuthorSubmissionBudget Budget = GetAuthorSubmissionBudget(
		GetFrozenExpectedAuthorCount(), FrozenMaxCommandsPerSubmission);
	FSeinQueuedCommandCost Cost;
	FString Error;
	if (!MeasureOutgoingDraftCost(Draft, Cost, Error)
		|| !FitsSingleTurnBudget(Cost, Budget))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("SubmitLocalCommandDraft: dropping command that cannot fit an empty frozen author share (type=%s version=%d): %s."),
			*Draft.Command.CommandType.ToString(), Draft.Command.SchemaVersion,
			Error.IsEmpty() ? TEXT("wire or decoded-allocation share exceeded") : *Error);
		return false;
	}
	if (PendingOutgoingDrafts.Drafts.Num() != PendingOutgoingDrafts.Costs.Num())
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("SubmitLocalCommandDraft: dropping command because outgoing backlog accounting is inconsistent."));
		return false;
	}
	if (!FitsBacklogBudget(
		PendingOutgoingDrafts.Num(),
		PendingOutgoingDrafts.VariableWireBytes,
		PendingOutgoingDrafts.VariableCanonicalCostBytes,
		Cost,
		Budget))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("SubmitLocalCommandDraft: dropping newest command because the bounded outgoing backlog is full (pending=%d, max-turn-shares=%d)."),
			PendingOutgoingDrafts.Num(), GSeinMaxBufferedAuthorTurns);
		return false;
	}

	PendingOutgoingDrafts.Drafts.Add(Draft);
	PendingOutgoingDrafts.Costs.Add(Cost);
	PendingOutgoingDrafts.VariableWireBytes += Cost.VariableWireBytes;
	PendingOutgoingDrafts.VariableCanonicalCostBytes +=
		Cost.VariableCanonicalCostBytes;
	return true;
}

void USeinNetSubsystem::FreezeLargestOutgoingDraftPrefix(
	int32 TurnId,
	TArray<FSeinCommandSubmissionDraft>& OutDrafts)
{
	OutDrafts.Reset();
	if (PendingOutgoingDrafts.IsEmpty()) return;
	if (PendingOutgoingDrafts.Drafts.Num() != PendingOutgoingDrafts.Costs.Num())
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("FreezeLargestOutgoingDraftPrefix: inconsistent backlog accounting; retaining input and submitting heartbeat for turn=%d."),
			TurnId);
		return;
	}
	const FSeinAuthorSubmissionBudget Budget = GetAuthorSubmissionBudget(
		GetFrozenExpectedAuthorCount(), FrozenMaxCommandsPerSubmission);
	while (!PendingOutgoingDrafts.IsEmpty()
		&& !FitsSingleTurnBudget(PendingOutgoingDrafts.Costs[0], Budget))
	{
		const FSeinCommand& Rejected = PendingOutgoingDrafts.Drafts[0].Command;
		UE_LOG(LogSeinNet, Error,
			TEXT("FreezeLargestOutgoingDraftPrefix: dropping head command that cannot fit an empty frozen author share (type=%s version=%d turn=%d)."),
			*Rejected.CommandType.ToString(), Rejected.SchemaVersion, TurnId);
		RemoveOutgoingPrefix(PendingOutgoingDrafts, 1);
	}
	if (PendingOutgoingDrafts.IsEmpty()) return;

	int32 PrefixCount = 0;
	int64 VariableWireBytes = 0;
	uint64 VariableCanonicalCostBytes = 0;
	const uint64 MaxVariableCanonicalCostBytes =
		Budget.MaxCanonicalCostBytes
		- FSeinNetCommandWireCodec::FixedBatchHeaderBytes;
	for (const FSeinQueuedCommandCost& Cost : PendingOutgoingDrafts.Costs)
	{
		if (PrefixCount >= Budget.MaxCommands
			|| Cost.VariableWireBytes > Budget.MaxEncodedBytes
				- FSeinNetCommandWireCodec::FixedBatchHeaderBytes - VariableWireBytes
			|| Cost.VariableCanonicalCostBytes
				> MaxVariableCanonicalCostBytes
					- VariableCanonicalCostBytes)
		{
			break;
		}
		VariableWireBytes += Cost.VariableWireBytes;
		VariableCanonicalCostBytes += Cost.VariableCanonicalCostBytes;
		++PrefixCount;
	}
	if (PrefixCount <= 0) return;

	OutDrafts.Append(PendingOutgoingDrafts.Drafts.GetData(), PrefixCount);
	FSeinOpaqueCommandBatch Verification;
	FString Error;
	FSeinWireCost VerifiedCost;
	if (!FSeinNetCommandWireCodec::EncodeDraftsWithCost(
		OutDrafts,
		Budget.MaxCommands,
		[this](FGameplayTag Type, int32 Version, FSeinCommandSchemaDescriptor& Out)
		{
			return FindFrozenCommandSchema(Type, Version, Out);
		},
		Verification,
		Error,
		VerifiedCost)
		|| !ValidateAuthorSubmissionBudget(
			OutDrafts.Num(), Verification,
			VerifiedCost.CanonicalCostBytes, Error))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("FreezeLargestOutgoingDraftPrefix: cached-cost invariant failed for turn=%d; dropping selected prefix so it cannot block future heartbeats: %s."),
			TurnId, *Error);
		RemoveOutgoingPrefix(PendingOutgoingDrafts, PrefixCount);
		OutDrafts.Reset();
		return;
	}

	RemoveOutgoingPrefix(PendingOutgoingDrafts, PrefixCount);
	UE_LOG(LogSeinNet, Verbose,
		TEXT("FreezeLargestOutgoingDraftPrefix: froze %d command(s) for turn=%d; retained suffix=%d."),
		PrefixCount, TurnId, PendingOutgoingDrafts.Num());
}

bool USeinNetSubsystem::TryBufferAICommand(
	FSeinPlayerID Slot,
	const FSeinCommand& Command)
{
	FString IngressError;
	if (!IsAICommandSubmissionAllowed(Slot, &IngressError))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("HandleAIEmit: dropping slot=%u command before queue allocation: %s."),
			Slot.Value, *IngressError);
		return false;
	}
	FSeinCommandSchemaDescriptor Schema;
	const bool bHasSchema = FindFrozenCommandSchema(
		Command.CommandType, Command.SchemaVersion, Schema);
	if (IsUnsupportedNetworkPauseCommand(
			Command, bHasSchema ? &Schema : nullptr))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("HandleAIEmit: dropping unsupported network pause-control slot=%u type=%s; the canonical coordinator/digest lane is not installed."),
			Slot.Value, *Command.CommandType.ToString());
		return false;
	}
	if (!FreezeAuthorSubmissionPolicy(TEXT("HandleAIEmit")))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("HandleAIEmit: dropping slot=%u command because no frozen match submission policy is installed."),
			Slot.Value);
		return false;
	}
	const FSeinAuthorSubmissionBudget Budget = GetAuthorSubmissionBudget(
		GetFrozenExpectedAuthorCount(), FrozenMaxCommandsPerSubmission);
	FSeinQueuedCommandCost Cost;
	FString Error;
	if (!MeasureAICommandCost(Command, Cost, Error)
		|| !FitsSingleTurnBudget(Cost, Budget))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("HandleAIEmit: dropping slot=%u command that cannot fit an empty frozen author share (type=%s version=%d): %s."),
			Slot.Value, *Command.CommandType.ToString(), Command.SchemaVersion,
			Error.IsEmpty() ? TEXT("wire or decoded-allocation share exceeded") : *Error);
		return false;
	}

	FSeinAICommandBacklog& Backlog = PendingAICommands.FindOrAdd(Slot);
	if (Backlog.Commands.Num() != Backlog.Costs.Num())
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("HandleAIEmit: dropping slot=%u command because AI backlog accounting is inconsistent."),
			Slot.Value);
		return false;
	}
	if (!FitsBacklogBudget(
		Backlog.Num(), Backlog.VariableWireBytes,
		Backlog.VariableCanonicalCostBytes, Cost, Budget))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("HandleAIEmit: dropping newest slot=%u command because the bounded AI backlog is full (pending=%d, max-turn-shares=%d)."),
			Slot.Value, Backlog.Num(), GSeinMaxBufferedAuthorTurns);
		return false;
	}
	Backlog.Commands.Add(Command);
	Backlog.Costs.Add(Cost);
	Backlog.VariableWireBytes += Cost.VariableWireBytes;
	Backlog.VariableCanonicalCostBytes += Cost.VariableCanonicalCostBytes;
	return true;
}

void USeinNetSubsystem::ConsumeAICommandPrefix(FSeinPlayerID Slot, int32 Count)
{
	FSeinAICommandBacklog* Backlog = PendingAICommands.Find(Slot);
	if (!Backlog) return;
	RemoveAICommandPrefix(*Backlog, Count);
	if (Backlog->IsEmpty()) PendingAICommands.Remove(Slot);
}

bool USeinNetSubsystem::PeekPendingAICommandsForTurn(
	FSeinPlayerID Slot, int32 TurnId, TArray<FSeinCommand>& OutCommands)
{
	OutCommands.Reset();
	FSeinAICommandBacklog* Pending = PendingAICommands.Find(Slot);
	if (!Pending || Pending->IsEmpty()) return false;
	if (Pending->Commands.Num() != Pending->Costs.Num())
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("PeekPendingAICommandsForTurn: slot=%u has inconsistent backlog accounting; retaining commands and submitting heartbeat for turn=%d."),
			Slot.Value, TurnId);
		return false;
	}
	const FSeinAuthorSubmissionBudget Budget = GetAuthorSubmissionBudget(
		GetFrozenExpectedAuthorCount(), FrozenMaxCommandsPerSubmission);
	while (!Pending->IsEmpty() && !FitsSingleTurnBudget(Pending->Costs[0], Budget))
	{
		const FSeinCommand& Rejected = Pending->Commands[0];
		UE_LOG(LogSeinNet, Error,
			TEXT("PeekPendingAICommandsForTurn: dropping slot=%u head command that cannot fit an empty frozen author share (type=%s version=%d turn=%d)."),
			Slot.Value, *Rejected.CommandType.ToString(), Rejected.SchemaVersion, TurnId);
		RemoveAICommandPrefix(*Pending, 1);
	}
	if (Pending->IsEmpty())
	{
		PendingAICommands.Remove(Slot);
		return false;
	}

	int32 PrefixCount = 0;
	int64 VariableWireBytes = 0;
	uint64 VariableCanonicalCostBytes = 0;
	const uint64 MaxVariableCanonicalCostBytes =
		Budget.MaxCanonicalCostBytes
		- FSeinNetCommandWireCodec::FixedBatchHeaderBytes;
	for (const FSeinQueuedCommandCost& Cost : Pending->Costs)
	{
		if (PrefixCount >= Budget.MaxCommands
			|| Cost.VariableWireBytes > Budget.MaxEncodedBytes
				- FSeinNetCommandWireCodec::FixedBatchHeaderBytes - VariableWireBytes
			|| Cost.VariableCanonicalCostBytes
				> MaxVariableCanonicalCostBytes
					- VariableCanonicalCostBytes)
		{
			break;
		}
		VariableWireBytes += Cost.VariableWireBytes;
		VariableCanonicalCostBytes += Cost.VariableCanonicalCostBytes;
		++PrefixCount;
	}
	if (PrefixCount <= 0) return false;

	// Transactional peek: admission/aggregation may reject this batch. The
	// source queue is consumed only after the author submission is accepted.
	OutCommands.Append(Pending->Commands.GetData(), PrefixCount);
	StampAuthoritativeCommandBatch(
		OutCommands, Slot, /*bParticipantCanAdministerMatch=*/false, TurnId);
	for (const FSeinCommand& Command : OutCommands)
	{
		FSeinCommandSchemaDescriptor Schema;
		const bool bHasSchema = FindFrozenCommandSchema(
			Command.CommandType, Command.SchemaVersion, Schema);
		if (!bHasSchema || IsUnsupportedNetworkPauseCommand(Command, &Schema))
		{
			UE_LOG(LogSeinNet, Error,
				TEXT("PeekPendingAICommandsForTurn: buffered slot=%u prefix failed defensive schema/pause validation at turn=%d; dropping the invalid prefix before aggregation."),
				Slot.Value, TurnId);
			RemoveAICommandPrefix(*Pending, PrefixCount);
			if (Pending->IsEmpty()) PendingAICommands.Remove(Slot);
			OutCommands.Reset();
			return false;
		}
	}
	FSeinOpaqueCommandBatch Verification;
	FString Error;
	FSeinWireCost VerifiedCost;
	if (!FSeinNetCommandWireCodec::EncodeCommandsWithCost(
		OutCommands,
		Budget.MaxCommands,
		[this](FGameplayTag Type, int32 Version, FSeinCommandSchemaDescriptor& Out)
		{
			return FindFrozenCommandSchema(Type, Version, Out);
		},
		Verification,
		Error,
		VerifiedCost)
		|| !ValidateAuthorSubmissionBudget(
			OutCommands.Num(), Verification,
			VerifiedCost.CanonicalCostBytes, Error))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("PeekPendingAICommandsForTurn: cached-cost invariant failed for slot=%u turn=%d; dropping selected prefix so it cannot block later heartbeats: %s."),
			Slot.Value, TurnId, *Error);
		RemoveAICommandPrefix(*Pending, PrefixCount);
		if (Pending->IsEmpty()) PendingAICommands.Remove(Slot);
		OutCommands.Reset();
		return false;
	}
	return true;
}

bool USeinNetSubsystem::BuildDroppedSlotSubmission(
	FSeinPlayerID Slot, int32 TurnId, bool bAllowAICommands,
	TArray<FSeinCommand>& OutCommands)
{
	OutCommands.Reset();
	return bAllowAICommands &&
		PeekPendingAICommandsForTurn(Slot, TurnId, OutCommands);
}

void USeinNetSubsystem::ServerHandleSubmission(
	ASeinNetRelay* SourceRelay,
	const FSeinProtocolContext& Context,
	int32 TurnId,
	const FSeinOpaqueCommandBatch& OpaqueDrafts)
{
	if (!IsServer() || !SourceRelay) return;
	if (!IsCurrentProtocolContext(Context, TEXT("ServerHandleSubmission"))) return;

	const FSeinPlayerID* SlotPtr = RelayToSlot.Find(SourceRelay);
	const FSeinNetworkParticipantID* ParticipantPtr = RelayToParticipant.Find(SourceRelay);
	if (!SlotPtr || !SlotPtr->IsValid() || !ParticipantPtr || !ParticipantPtr->IsValid())
	{
		UE_LOG(LogSeinNet, Warning, TEXT("[Server] opaque submission from unmapped relay %s — rejecting."),
			*GetNameSafe(SourceRelay));
		return;
	}
	const FSeinPlayerID Slot = *SlotPtr;
	const FSeinNetworkParticipantID ParticipantID = *ParticipantPtr;
	const FSeinParticipantBinding* Binding = FindParticipantBinding(ParticipantID);
	if (!Binding || !Binding->CommandSlots.Contains(Slot))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("[Server] participant %s is not authorized to author slot=%u turn=%d."),
			*ParticipantID.ToCanonicalString(), Slot.Value, TurnId);
		return;
	}
	if (!IsCommandSubmissionLifecycleAllowed(Slot))
	{
		const ESeinSlotLifecycle* Lifecycle = SlotLifecycle.Find(Slot);
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Server] rejecting command submission from slot=%u turn=%d lifecycle=%d; only Connected slots may author relay commands."),
			Slot.Value, TurnId, Lifecycle ? static_cast<int32>(*Lifecycle) : INDEX_NONE);
		return;
	}

	PruneProtocolState(GetCurrentTurn());
	if (!IsCommandTurnWithinProtocolWindow(TurnId, TEXT("ServerHandleSubmission")))
	{
		return;
	}

	// Stamp authoritative sender on each command. Caller's PlayerID is
	// untrusted — server overrides.
	UWorld* World = GetWorld();
	USeinWorldSubsystem* WorldSub =
		World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	if (!WorldSub)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("[Server] command schema snapshot unavailable; rejecting participant=%s turn=%d."),
			*ParticipantID.ToCanonicalString(), TurnId);
		return;
	}
	const int32 FrozenAuthorCount = GetFrozenExpectedAuthorCount();
	if (FrozenAuthorCount <= 0)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("[Server] frozen command-author count unavailable; rejecting participant=%s turn=%d."),
			*ParticipantID.ToCanonicalString(), TurnId);
		return;
	}
	const FSeinAuthorSubmissionBudget AuthorBudget =
		GetAuthorSubmissionBudget(
			FrozenAuthorCount, FrozenMaxCommandsPerSubmission);
	if (OpaqueDrafts.Bytes.Num() > AuthorBudget.MaxEncodedBytes)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Server] rejecting over-share opaque submission participant=%s turn=%d wire=%d/%d."),
			*ParticipantID.ToCanonicalString(), TurnId,
			OpaqueDrafts.Bytes.Num(), AuthorBudget.MaxEncodedBytes);
		return;
	}
	TArray<FSeinCommandSubmissionDraft> Drafts;
	FString WireError;
	FSeinWireCost DraftCost;
	if (!FSeinNetCommandWireCodec::DecodeDraftsWithCost(
		OpaqueDrafts,
		AuthorBudget.MaxCommands,
		[WorldSub](FGameplayTag Type, int32 Version, FSeinCommandSchemaDescriptor& Out)
		{
			return WorldSub->FindCommandSchema(Type, Version, Out);
		},
		Drafts,
		WireError,
		DraftCost,
		FSeinNetCommandWireCodec::MaxNativeAllocationBytes)
		|| !ValidateAuthorSubmissionBudget(
			Drafts.Num(), OpaqueDrafts,
			DraftCost.CanonicalCostBytes, WireError))
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Server] rejecting malformed opaque submission participant=%s turn=%d: %s."),
			*ParticipantID.ToCanonicalString(), TurnId, *WireError);
		return;
	}
	TArray<FSeinCommand> Stamped;
	Stamped.Reserve(Drafts.Num());
	for (const FSeinCommandSubmissionDraft& Draft : Drafts)
	{
		TArray<FSeinCommand> Single{Draft.Command};
		StampAuthoritativeCommandBatch(
			Single,
			Slot,
			Draft.bRequestMatchAdministration && Binding->bCanAdministerMatch,
			TurnId);
		FSeinCommandSchemaDescriptor Schema;
		if (WorldSub->ValidateCommandStructure(Single[0], &Schema)
			!= ESeinCommandStructureResult::Valid)
		{
			UE_LOG(LogSeinNet, Warning,
				TEXT("[Server] dropping malformed command draft participant=%s slot=%u turn=%d type=%s version=%d."),
				*ParticipantID.ToCanonicalString(), Slot.Value, TurnId,
				*Single[0].CommandType.ToString(), Single[0].SchemaVersion);
			continue;
		}
		// This drop is what keeps pause-control out of aggregated turns; the
		// replay writer FAILS RECORDING if one ever reaches it (v9 has no
		// frozen-time journal — see USeinReplayWriter::RecordEncodedTurn).
		// Installing the canonical pause lane means extending the replay
		// format in the same change, or every paused match loses its replay.
		if (IsUnsupportedNetworkPauseCommand(Single[0], &Schema))
		{
			UE_LOG(LogSeinNet, Warning,
				TEXT("[Server] dropping unsupported network pause-control participant=%s slot=%u turn=%d type=%s until the canonical pause lane is installed."),
				*ParticipantID.ToCanonicalString(), Slot.Value, TurnId,
				*Single[0].CommandType.ToString());
			continue;
		}
		Stamped.Add(MoveTemp(Single[0]));
	}
	FSeinOpaqueCommandBatch AuthorCanonicalBatch;
	FSeinWireCost AuthorCanonicalCost;
	if (!FSeinNetCommandWireCodec::EncodeCommandsWithCost(
			Stamped,
			AuthorBudget.MaxCommands,
			[WorldSub](FGameplayTag Type, int32 Version, FSeinCommandSchemaDescriptor& Out)
			{
				return WorldSub->FindCommandSchema(Type, Version, Out);
			},
			AuthorCanonicalBatch,
			WireError,
			AuthorCanonicalCost)
		|| !ValidateAuthorSubmissionBudget(
			Stamped.Num(), AuthorCanonicalBatch,
			AuthorCanonicalCost.CanonicalCostBytes, WireError))
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Server] rejecting over-share author batch participant=%s slot=%u turn=%d: %s."),
			*ParticipantID.ToCanonicalString(), Slot.Value, TurnId, *WireError);
		return;
	}

	const FSeinTurnAuthor Author(ParticipantID, Slot);
	FString AggregateWireError;
	const ESeinTurnSubmitResult SubmitResult =
		TurnAggregator.Submit(
			Context, TurnId, Author, Stamped,
			[this, &AggregateWireError](TConstArrayView<FSeinCommand> Prospective)
			{
				return PreflightCanonicalTurnBatch(Prospective, AggregateWireError);
			});
	if (SubmitResult == ESeinTurnSubmitResult::IdenticalRetry)
	{
		UE_LOG(LogSeinNet, Verbose,
			TEXT("[Server] identical retry participant=%s slot=%u turn=%d — idempotent no-op."),
			*ParticipantID.ToCanonicalString(), Slot.Value, TurnId);
		return;
	}
	if (SubmitResult == ESeinTurnSubmitResult::ConflictingRetry)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("[Server] conflicting retry participant=%s slot=%u turn=%d — rejected; first accepted batch is immutable."),
			*ParticipantID.ToCanonicalString(), Slot.Value, TurnId);
		return;
	}
	if (SubmitResult == ESeinTurnSubmitResult::AggregateRejected)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("[Server] rejecting aggregate-overflow submission participant=%s slot=%u turn=%d: %s."),
			*ParticipantID.ToCanonicalString(), Slot.Value, TurnId,
			*AggregateWireError);
		return;
	}
	if (SubmitResult != ESeinTurnSubmitResult::Accepted)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Server] rejected submission participant=%s slot=%u turn=%d result=%d."),
			*ParticipantID.ToCanonicalString(), Slot.Value, TurnId,
			static_cast<int32>(SubmitResult));
		return;
	}

	UE_LOG(LogSeinNet, Verbose,
		TEXT("[Server] buffered participant=%s slot=%u turn=%d commands=%d (have %d/%d authors)."),
		*ParticipantID.ToCanonicalString(), Slot.Value, TurnId, Drafts.Num(),
		TurnAggregator.GetSubmittedAuthorCount(TurnId), GetActiveSlotCount());

	ServerCheckTurnComplete(TurnId, Slot);
}

void USeinNetSubsystem::InjectDroppedSlotHeartbeats(
	int32 Turn, bool bAllowAICommands)
{
	if (!IsServer()) return;
	if (!TurnAggregator.IsConfigured()
		|| TurnAggregator.IsTurnRetired(Turn)
		|| TurnAggregator.IsTurnCommitted(Turn))
	{
		return;
	}

	TArray<FSeinPlayerID> ExpectedSlots;
	GetExpectedCommandSlots(ExpectedSlots);

	for (const FSeinPlayerID Slot : ExpectedSlots)
	{
		const ESeinSlotLifecycle* Status = SlotLifecycle.Find(Slot);
		if (!Status) continue;

		// Only inject for slots that aren't going to submit themselves:
		// dropped, AI-owned, resyncing (authorship withheld until activation),
		// and freshly activated slots inside their guaranteed coverage window
		// (their first authored turn is past it by construction).
		const int32* CoverageThrough =
			HeartbeatCoverageThroughTurn.Find(Slot);
		const bool bInsideCoverageWindow =
			CoverageThrough && Turn <= *CoverageThrough;
		if (*Status != ESeinSlotLifecycle::Dropped
			&& *Status != ESeinSlotLifecycle::AITakeover
			&& *Status != ESeinSlotLifecycle::Reconnecting
			&& !bInsideCoverageWindow) continue;

		if (!Slot.IsValid()) continue;
		const FSeinNetworkParticipantID ParticipantID = FindParticipantForSlot(Slot);
		if (!ParticipantID.IsValid())
		{
			UE_LOG(LogSeinNet, Error,
				TEXT("[Server] cannot inject slot=%u turn=%d: no manifest participant owns the slot."),
				Slot.Value, Turn);
			continue;
		}
		const FSeinTurnAuthor Author(ParticipantID, Slot);
		if (TurnAggregator.HasSubmission(Turn, Author)) continue;

		// Peek any AI-emitted commands buffered since the last boundary. The
		// source queue remains intact until admission succeeds.
		TArray<FSeinCommand> AICommands;
		const int32 QueuedCommandCount = PendingAICommands.Find(Slot)
			? PendingAICommands.FindChecked(Slot).Num() : 0;
		BuildDroppedSlotSubmission(Slot, Turn, bAllowAICommands, AICommands);
		const int32 BufferedCommandCount = AICommands.Num();
		bool bSubmittingBufferedCommands = BufferedCommandCount > 0;
		const FSeinAuthorSubmissionBudget AuthorBudget =
			GetAuthorSubmissionBudget(
				GetFrozenExpectedAuthorCount(),
				FrozenMaxCommandsPerSubmission);
		FString AdmissionError;
		auto FitsAuthorBudget = [this, &AuthorBudget, &AdmissionError](
			const TArray<FSeinCommand>& Candidate)
		{
			AdmissionError.Reset();
			FSeinOpaqueCommandBatch Encoded;
			FSeinWireCost WireCost;
			return FSeinNetCommandWireCodec::EncodeCommandsWithCost(
				Candidate,
				AuthorBudget.MaxCommands,
				[this](FGameplayTag Type, int32 Version, FSeinCommandSchemaDescriptor& Out)
				{
					return FindFrozenCommandSchema(Type, Version, Out);
				},
				Encoded,
				AdmissionError,
				WireCost)
				&& ValidateAuthorSubmissionBudget(
					Candidate.Num(), Encoded,
					WireCost.CanonicalCostBytes, AdmissionError);
		};
		if (!FitsAuthorBudget(AICommands))
		{
			UE_LOG(LogSeinNet, Error,
				TEXT("[Server] AI batch over share participant=%s slot=%u turn=%d; retaining %d command(s) and submitting heartbeat: %s."),
				*ParticipantID.ToCanonicalString(), Slot.Value, Turn,
				BufferedCommandCount, *AdmissionError);
			AICommands.Reset();
			bSubmittingBufferedCommands = false;
			if (!FitsAuthorBudget(AICommands))
			{
				UE_LOG(LogSeinNet, Error,
					TEXT("[Server] empty injected heartbeat failed admission participant=%s slot=%u turn=%d: %s."),
					*ParticipantID.ToCanonicalString(), Slot.Value, Turn, *AdmissionError);
				continue;
			}
		}
		auto SubmitCandidate = [this, &AdmissionError, Turn, &Author](
			const TArray<FSeinCommand>& Candidate)
		{
			AdmissionError.Reset();
			return TurnAggregator.Submit(
				ActiveProtocolContext, Turn, Author, Candidate,
				[this, &AdmissionError](TConstArrayView<FSeinCommand> Prospective)
				{
					return PreflightCanonicalTurnBatch(Prospective, AdmissionError);
				});
		};
		ESeinTurnSubmitResult Result = SubmitCandidate(AICommands);
		if (Result == ESeinTurnSubmitResult::AggregateRejected
			&& bSubmittingBufferedCommands)
		{
			UE_LOG(LogSeinNet, Error,
				TEXT("[Server] AI batch failed aggregate preflight participant=%s slot=%u turn=%d; retaining %d command(s) and submitting heartbeat: %s."),
				*ParticipantID.ToCanonicalString(), Slot.Value, Turn,
				BufferedCommandCount, *AdmissionError);
			AICommands.Reset();
			bSubmittingBufferedCommands = false;
			Result = SubmitCandidate(AICommands);
		}
		if (Result != ESeinTurnSubmitResult::Accepted
			&& Result != ESeinTurnSubmitResult::IdenticalRetry)
		{
			UE_LOG(LogSeinNet, Warning,
				TEXT("[Server] rejected injected submission participant=%s slot=%u turn=%d result=%d: %s."),
				*ParticipantID.ToCanonicalString(), Slot.Value, Turn,
				static_cast<int32>(Result), *AdmissionError);
			continue;
		}

		if (Result == ESeinTurnSubmitResult::Accepted && bSubmittingBufferedCommands)
		{
			ConsumeAICommandPrefix(Slot, BufferedCommandCount);
			UE_LOG(LogSeinNet, Verbose,
				TEXT("[Server] admitted and consumed %d AI command(s) for participant=%s slot=%u into turn=%d."),
				BufferedCommandCount, *ParticipantID.ToCanonicalString(), Slot.Value, Turn);
		}
		else
		{
			UE_LOG(LogSeinNet, Verbose,
				TEXT("[Server] injected heartbeat for participant=%s slot=%u turn=%d (retained AI commands=%d)."),
				*ParticipantID.ToCanonicalString(), Slot.Value, Turn,
				bSubmittingBufferedCommands ? 0 : QueuedCommandCount);
		}
	}
}

void USeinNetSubsystem::
	BackfillSuppressedSlotHeartbeatsThroughPipelineWindow()
{
	if (!IsServer() || !TurnAggregator.IsConfigured())
	{
		return;
	}

	TArray<int32> Turns = TurnAggregator.GetPendingTurnIDs();
	const int32 CurrentTurn = GetCurrentTurn();
	const int32 PipelineEndTurn = CurrentTurn + GetInputDelayTurns();
	for (int32 Turn = CurrentTurn; Turn <= PipelineEndTurn; ++Turn)
	{
		Turns.AddUnique(Turn);
	}
	Turns.Sort();
	for (const int32 Turn : Turns)
	{
		InjectDroppedSlotHeartbeats(Turn, /*bAllowAICommands=*/false);
		ServerCheckTurnComplete(Turn);
	}
}

bool USeinNetSubsystem::HandleAIEmit(FSeinPlayerID OwnedSlot, const FSeinCommand& Command)
{
	// This adapter is normally unbound outside an active server topology. If a
	// topology transition leaves it briefly bound, false is a fail-closed drop;
	// USeinAIController only uses direct enqueue when no adapter is bound.
	if (!IsServer() || !IsNetworkingActive()) return false;

	if (!OwnedSlot.IsValid())
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("HandleAIEmit: AI controller emitted with invalid OwnedPlayerID — dropping command."));
		return true; // we still claim ownership of the routing decision
	}

	// Buffer until the next turn boundary, where InjectDroppedSlotHeartbeats
	// drains us into the OutgoingTurn buffer slot for this player. Stamping
	// the turn here would race with what the heartbeat injector picks; we
	// let the injector own both turn assignment and authority stamping so an
	// AI-authored PlayerID/Tick can never leak into the canonical stream.
	if (!TryBufferAICommand(OwnedSlot, Command)) return true;
	UE_LOG(LogSeinNet, Verbose,
		TEXT("[Server] AI emit buffered: slot=%u  (pending=%d)"),
		OwnedSlot.Value, PendingAICommands[OwnedSlot].Num());
	return true;
}

void USeinNetSubsystem::EvaluateDroppedSlots()
{
	if (!IsServer()) return;
	const double NowSec = FPlatformTime::Seconds();

	for (auto It = SlotDroppedAtTime.CreateIterator(); It; ++It)
	{
		const FSeinPlayerID Slot = It.Key();
		const double DroppedAt = It.Value();

		ESeinSlotLifecycle* StatusPtr = SlotLifecycle.Find(Slot);
		if (!StatusPtr || *StatusPtr != ESeinSlotLifecycle::Dropped)
		{
			// Slot reconnected or already transitioned; clean up.
			It.RemoveCurrent();
			continue;
		}

		const double Elapsed = NowSec - DroppedAt;
		if (Elapsed < GetDroppedToAITakeoverSeconds()) continue;

		// Transition: Dropped → AITakeover. The framework flips the lifecycle
		// bit (server keeps injecting heartbeats so the gate doesn't stall)
		// and — when SlotDropPolicy is BasicAI — auto-instantiates the
		// configured AI controller class + registers it with the sim. Per
		// DESIGN §16 the AI internal reasoning runs on the host only; only
		// emitted commands cross the lockstep boundary.
		*StatusPtr = ESeinSlotLifecycle::AITakeover;
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Drop] slot=%u transition Dropped → AITakeover (was dropped %.1fs)."),
			Slot.Value, Elapsed);

		TryAutoRegisterAIForSlot(Slot);
		It.RemoveCurrent();
	}

	// A slot parked in Reconnecting with NO serve in flight (its peer never
	// requested, or a resync failed and was never retried) must not idle its
	// units forever: demote to Dropped after the same takeover window so the
	// existing AI fallback resumes. A live serve resets the clock; a later
	// resync request can always re-enter Reconnecting.
	for (auto It = SlotReconnectingSinceTime.CreateIterator(); It; ++It)
	{
		const FSeinPlayerID Slot = It.Key();
		ESeinSlotLifecycle* StatusPtr = SlotLifecycle.Find(Slot);
		if (!StatusPtr || *StatusPtr != ESeinSlotLifecycle::Reconnecting)
		{
			It.RemoveCurrent();
			continue;
		}
		if (ServerResyncServes.Contains(Slot))
		{
			It.Value() = NowSec;
			continue;
		}
		if (NowSec - It.Value() < GetDroppedToAITakeoverSeconds()) continue;

		*StatusPtr = ESeinSlotLifecycle::Dropped;
		SlotDroppedAtTime.Add(Slot, NowSec);
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Resync] slot=%u idled in Reconnecting %.1fs with no serve; demoted to Dropped so AI fallback can resume."),
			Slot.Value, NowSec - It.Value());
		It.RemoveCurrent();
	}
}

double USeinNetSubsystem::GetDroppedToAITakeoverSeconds() const
{
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	return Settings ? Settings->DroppedToAITakeoverSeconds : 30.0;
}

void USeinNetSubsystem::TryAutoRegisterAIForSlot(FSeinPlayerID Slot)
{
	if (!IsServer() || !Slot.IsValid()) return;

	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	if (!Settings) return;

	// Policy gate. KeepUnitsAlive: just leave the slot in AITakeover — units
	// idle, no AI registered, the framework's empty-heartbeat injection
	// keeps the gate green. RemovePlayer: forfeit semantics — currently
	// behaves identically to KeepUnitsAlive at the AI layer (the unit-
	// teardown path is reserved for a follow-up; doing the destroy here
	// cleanly requires walking the slot's entities and we want that gated
	// on a deliberate API rather than a side effect of disconnect).
	if (Settings->SlotDropPolicy != ESeinSlotDropPolicy::BasicAI)
	{
		UE_LOG(LogSeinNet, Log,
			TEXT("[Drop] slot=%u AITakeover: SlotDropPolicy=%d, no AI auto-spawn."),
			Slot.Value, (int32)Settings->SlotDropPolicy);
		return;
	}

	// Already have a controller for this slot? (Edge case: simulate-disconnect
	// fired twice without reconnect in between.) Don't double-register.
	if (AITakeoverControllers.Contains(Slot))
	{
		UE_LOG(LogSeinNet, Verbose,
			TEXT("[Drop] slot=%u already has an AI controller registered; skipping re-register."),
			Slot.Value);
		return;
	}

	// Resolve the configured class. Empty path or failed load → fall back to
	// the framework-shipped null controller. Both branches log; designers
	// see in the log what class actually got picked.
	// WYSIWYG. None/empty => drop-takeover AI is OFF: register no controller, so the dropped slot's
	// units simply idle (the server's empty heartbeats keep the lockstep gate from stalling). A
	// set-but-unloadable class is a mistake, not an off-switch: fall back to the shipped null
	// controller with a logged warning.
	if (Settings->DefaultAIControllerClass.IsNull())
	{
		USeinARTSCoreSettings::ReportDisabledSystem(TEXT("Default AI Controller"),
			TEXT("A dropped player's slot gets no AI takeover; its units stay idle."), /*bHighSeverity*/ false);
		return;
	}
	UClass* AIClass = Settings->DefaultAIControllerClass.TryLoadClass<USeinAIController>();
	if (!AIClass)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Drop] slot=%u DefaultAIControllerClass='%s' failed to load — falling back to USeinNullAIController."),
			Slot.Value, *Settings->DefaultAIControllerClass.ToString());
		AIClass = USeinNullAIController::StaticClass();
	}

	// Need the world subsystem to register against.
	UWorld* World = GetWorld();
	USeinWorldSubsystem* WorldSub = World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	if (!WorldSub)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Drop] slot=%u AITakeover: no USeinWorldSubsystem — skipping AI auto-register."),
			Slot.Value);
		return;
	}

	// Outer the controller on the world subsystem so its lifetime is bound
	// to the same scope the registration array uses (matches how the
	// resolver pool entries are outered). NewObject also fires PostInit /
	// any UCLASS-level cdo-init the subclass needs.
	USeinAIController* Controller = NewObject<USeinAIController>(WorldSub, AIClass);
	if (!Controller)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("[Drop] slot=%u failed to NewObject<USeinAIController> from class %s."),
			Slot.Value, *GetNameSafe(AIClass));
		return;
	}

	WorldSub->RegisterAIController(Controller, Slot);
	AITakeoverControllers.Add(Slot, Controller);

	UE_LOG(LogSeinNet, Log,
		TEXT("[Drop] slot=%u registered AI controller %s (class=%s)."),
		Slot.Value, *GetNameSafe(Controller), *GetNameSafe(AIClass));
}

void USeinNetSubsystem::TeardownAIForSlot(FSeinPlayerID Slot)
{
	if (!IsServer() || !Slot.IsValid()) return;

	TObjectPtr<USeinAIController>* Found = AITakeoverControllers.Find(Slot);
	USeinAIController* OwnedController = Found ? Found->Get() : nullptr;
	if (UWorld* World = GetWorld())
	{
		if (USeinWorldSubsystem* WorldSub = World->GetSubsystem<USeinWorldSubsystem>())
		{
			// Designer-registered takeover controllers intentionally need not live
			// in AITakeoverControllers. Remove every controller for the reclaimed
			// slot from the public world registry, not only the default instance.
			const TArray<TObjectPtr<USeinAIController>> Registered =
				WorldSub->GetAIControllers();
			for (USeinAIController* Controller : Registered)
			{
				if (Controller && Controller->OwnedPlayerID == Slot)
				{
					WorldSub->UnregisterAIController(Controller);
				}
			}
		}
	}
	if (OwnedController)
	{
		// MarkAsGarbage isn't strictly required — losing the strong ref via
		// the map removal below makes it eligible — but it makes the
		// teardown intent explicit for the framework-owned transient instance.
		OwnedController->MarkAsGarbage();
	}

	AITakeoverControllers.Remove(Slot);

	// Drop any AI-emitted commands buffered for this slot but not yet drained
	// into a turn — once the human reclaims the slot, the lockstep wire will
	// carry their submissions, and stale AI commands surfacing one tick later
	// would silently overwrite their first input.
	PendingAICommands.Remove(Slot);

	UE_LOG(LogSeinNet, Log,
		TEXT("[Drop] slot=%u AI controller/queue state torn down (slot reconnected or session ended)."),
		Slot.Value);
}

// ==================== Resync (FEAT-01) ====================
// Bounded checkpoint + exact command-tail catch-up. The coordinator serves a
// freshly captured boundary checkpoint (chunked snapshot envelope) plus the
// retained assembled-turn tail; the peer adopts stopped under the one-shot
// restore authority, free-runs the normal gate to the frontier, and activates
// only after an exact canonical-root handshake at an agreed boundary.

ASeinNetRelay* USeinNetSubsystem::FindRelayForSlot(FSeinPlayerID Slot) const
{
	for (const TPair<TWeakObjectPtr<ASeinNetRelay>, FSeinPlayerID>& Pair :
		RelayToSlot)
	{
		if (Pair.Value == Slot)
		{
			if (ASeinNetRelay* Relay = Pair.Key.Get())
			{
				return Relay;
			}
		}
	}
	return nullptr;
}

void USeinNetSubsystem::ServerFailResync(
	FSeinPlayerID Slot,
	ASeinNetRelay* Relay,
	const FString& Reason)
{
	UE_LOG(LogSeinNet, Error,
		TEXT("[Resync] serve failed for slot=%u: %s"),
		Slot.Value, *Reason);
	ServerResyncServes.Remove(Slot);
	// The slot stays Reconnecting (heartbeats keep the gate healthy); the
	// peer may request a fresh resync.
	if (Relay)
	{
		Relay->Client_NotifyResyncActivation(
			ActiveProtocolContext, /*bActivated=*/false,
			/*FirstAuthoredTurn=*/-1, Reason);
	}
}

void USeinNetSubsystem::ServerHandleResyncRequest(
	ASeinNetRelay* SourceRelay,
	const FSeinProtocolContext& Context)
{
	if (!IsServer() || !SourceRelay) return;
	if (!IsCurrentProtocolContext(Context, TEXT("ServerHandleResyncRequest")))
	{
		return;
	}
	const FSeinPlayerID* Slot = RelayToSlot.Find(SourceRelay);
	if (!Slot || !Slot->IsValid())
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("[Resync] request from an unmapped relay was refused."));
		return;
	}
	if (ServerResyncServes.Contains(*Slot))
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Resync] slot=%u already has a serve in flight; duplicate request ignored."),
			Slot->Value);
		return;
	}
	if (DeterminismSessionFailure.IsValid())
	{
		ServerFailResync(*Slot, SourceRelay,
			TEXT("This lockstep epoch is terminally failed; a resync cannot recover it."));
		return;
	}

	UWorld* World = GetWorld();
	USeinWorldSubsystem* WorldSub =
		World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	if (!WorldSub)
	{
		ServerFailResync(*Slot, SourceRelay,
			TEXT("The coordinator has no simulation world to checkpoint."));
		return;
	}

	// Withhold authorship for the whole resync: heartbeats keep the gate
	// healthy (the Reconnecting lifecycle is included in injection), and any
	// AI takeover ends now — the human is back and catching up.
	if (const ESeinSlotLifecycle* Lifecycle = SlotLifecycle.Find(*Slot))
	{
		if (*Lifecycle == ESeinSlotLifecycle::AITakeover)
		{
			TeardownAIForSlot(*Slot);
		}
	}
	SlotLifecycle.Add(*Slot, ESeinSlotLifecycle::Reconnecting);
	SlotDroppedAtTime.Remove(*Slot);
	SlotReconnectingSinceTime.Add(*Slot, FPlatformTime::Seconds());

	// The slot may already owe submissions to OPEN turns (a stalled or
	// suppressed peer is exactly why a resync happens) — back-fill every
	// pending turn with heartbeats NOW, or the whole session's gate stays
	// wedged on this slot forever and the sim boundary that drives the rest
	// of this flow never fires again. Mirrors the OnLogout drop path.
	BackfillSuppressedSlotHeartbeatsThroughPipelineWindow();
	// And re-evaluate outstanding world-root checks against the reduced
	// reporter set — a due checkpoint waiting on this now-suppressed peer
	// would otherwise wedge until it expires as an authoritative session
	// failure long after the peer recovered.
	if (!ServerWorldStateRootReports.IsEmpty())
	{
		TArray<int32> PendingRootTurns;
		ServerWorldStateRootReports.GetKeys(PendingRootTurns);
		for (const int32 Turn : PendingRootTurns)
		{
			const TMap<FSeinNetworkParticipantID, FGuid>* Reports =
				ServerWorldStateRootReports.Find(Turn);
			if (Reports && AreExpectedWorldRootReportsComplete(*Reports))
			{
				ServerCompareWorldStateRootsForTurn(Turn);
			}
		}
	}

	// A relay RPC handler runs on the game thread between fixed ticks, and
	// the deferred queues drain at every PostTick — so this is a legal
	// quiescent capture boundary. Capture refusal fails the serve loudly.
	FSeinWorldSnapshot Checkpoint;
	FSeinWorldSnapshotReferenceGuard CheckpointGCGuard(Checkpoint);
	WorldSub->CaptureSnapshot(Checkpoint);
	if (Checkpoint.SnapshotVersion != FSeinWorldSnapshot::CurrentVersion)
	{
		ServerFailResync(*Slot, SourceRelay,
			TEXT("The coordinator could not capture a checkpoint at this boundary."));
		return;
	}

	TArray<uint8> EnvelopeBytes;
	FSeinSnapshotEnvelopeMetadata Metadata;
	FString EncodeError;
	if (!SeinSnapshotTransfer::EncodeCheckpointEnvelope(
		Checkpoint, EnvelopeBytes, Metadata, EncodeError))
	{
		ServerFailResync(*Slot, SourceRelay, EncodeError);
		return;
	}

	const int32 TicksPerTurn = GetTicksPerTurn();
	const int32 CheckpointTurn = TicksPerTurn > 0
		? Checkpoint.CurrentTick / TicksPerTurn
		: 0;
	FServerResyncServe& Serve = ServerResyncServes.Add(*Slot);
	Serve.TransferId = NextResyncTransferId++;
	Serve.CheckpointTurn = CheckpointTurn;
	Serve.PendingEnvelopeBytes = MoveTemp(EnvelopeBytes);
	Serve.TotalChunks = FMath::DivideAndRoundUp(
		Serve.PendingEnvelopeBytes.Num(), ResyncCheckpointChunkBytes);
	Serve.NextChunkIndex = 0;
	Serve.StartedAtSeconds = FPlatformTime::Seconds();

	UE_LOG(LogSeinNet, Log,
		TEXT("[Resync] serving slot=%u checkpoint tick=%d turn=%d bytes=%d chunks=%d (paced)."),
		Slot->Value, Checkpoint.CurrentTick, CheckpointTurn,
		Serve.PendingEnvelopeBytes.Num(), Serve.TotalChunks);

	// Announce now; chunks are PACED across turn boundaries by
	// ServerAdvanceResyncTransfers — a one-frame reliable-RPC burst would
	// overflow the connection's reliable buffer and disconnect the peer.
	SourceRelay->Client_BeginCheckpointTransfer(
		ActiveProtocolContext, Serve.TransferId, CheckpointTurn,
		Serve.TotalChunks, Serve.PendingEnvelopeBytes.Num());
}

void USeinNetSubsystem::ServerAdvanceResyncTransfers()
{
	if (ServerResyncServes.IsEmpty()) return;
	const double NowSeconds = FPlatformTime::Seconds();
	TArray<FSeinPlayerID> TimedOutSlots;
	for (TPair<FSeinPlayerID, FServerResyncServe>& Pair : ServerResyncServes)
	{
		FServerResyncServe& Serve = Pair.Value;
		if (NowSeconds - Serve.StartedAtSeconds > ResyncServeTimeoutSeconds)
		{
			TimedOutSlots.Add(Pair.Key);
			continue;
		}
		if (Serve.bTransferComplete)
		{
			continue;
		}
		ASeinNetRelay* Relay = FindRelayForSlot(Pair.Key);
		if (!Relay)
		{
			continue; // OnLogout abandons the serve when the relay dies.
		}
		int32 SentThisBoundary = 0;
		while (Serve.NextChunkIndex < Serve.TotalChunks
			&& SentThisBoundary < ResyncChunksPerBoundary)
		{
			const int32 Offset =
				Serve.NextChunkIndex * ResyncCheckpointChunkBytes;
			const int32 Count = FMath::Min(
				ResyncCheckpointChunkBytes,
				Serve.PendingEnvelopeBytes.Num() - Offset);
			TArray<uint8> Chunk(
				Serve.PendingEnvelopeBytes.GetData() + Offset, Count);
			Relay->Client_ReceiveCheckpointChunk(
				ActiveProtocolContext, Serve.TransferId,
				Serve.NextChunkIndex, Chunk);
			++Serve.NextChunkIndex;
			++SentThisBoundary;
		}
		if (Serve.NextChunkIndex >= Serve.TotalChunks)
		{
			Serve.bTransferComplete = true;
			Serve.PendingEnvelopeBytes.Empty();
			Relay->Client_EndCheckpointTransfer(
				ActiveProtocolContext, Serve.TransferId);
		}
	}
	for (const FSeinPlayerID Slot : TimedOutSlots)
	{
		ServerFailResync(Slot, FindRelayForSlot(Slot), FString::Printf(
			TEXT("The resync serve exceeded %.0f seconds and was abandoned."),
			ResyncServeTimeoutSeconds));
	}
}

void USeinNetSubsystem::ServerHandleResyncAbort(
	ASeinNetRelay* SourceRelay,
	const FSeinProtocolContext& Context)
{
	if (!IsServer() || !SourceRelay) return;
	if (!IsCurrentProtocolContext(Context, TEXT("ServerHandleResyncAbort")))
	{
		return;
	}
	const FSeinPlayerID* Slot = RelayToSlot.Find(SourceRelay);
	if (Slot && ServerResyncServes.Remove(*Slot) > 0)
	{
		UE_LOG(LogSeinNet, Log,
			TEXT("[Resync] slot=%u aborted its resync; serve freed."),
			Slot->Value);
	}
}

void USeinNetSubsystem::ServerHandleResyncTailRequest(
	ASeinNetRelay* SourceRelay,
	const FSeinProtocolContext& Context,
	int32 FromTurn)
{
	if (!IsServer() || !SourceRelay) return;
	if (!IsCurrentProtocolContext(
		Context, TEXT("ServerHandleResyncTailRequest")))
	{
		return;
	}
	const FSeinPlayerID* Slot = RelayToSlot.Find(SourceRelay);
	FServerResyncServe* Serve =
		Slot ? ServerResyncServes.Find(*Slot) : nullptr;
	if (!Slot || !Serve || FromTurn != Serve->CheckpointTurn + 1)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("[Resync] tail request refused (no serve in flight or wrong FromTurn=%d)."),
			FromTurn);
		return;
	}
	if (FromTurn <= RetainedAssembledTurnFloor)
	{
		ServerFailResync(*Slot, SourceRelay, FString::Printf(
			TEXT("The retained turn window no longer covers turn %d (floor %d); request a fresh resync."),
			FromTurn, RetainedAssembledTurnFloor));
		return;
	}

	// Serve every retained committed turn from the checkpoint frontier
	// onward through the SAME delivery path as live fan-out. Gaps are
	// terminal for this serve: a missing committed turn cannot be
	// reconstructed. (Turns not yet committed simply arrive live.)
	int32 Sent = 0;
	for (int32 Turn = FromTurn;; ++Turn)
	{
		const FSeinOpaqueCommandBatch* Retained =
			RetainedAssembledTurns.Find(Turn);
		if (!Retained)
		{
			if (TurnAggregator.IsTurnCommitted(Turn))
			{
				ServerFailResync(*Slot, SourceRelay, FString::Printf(
					TEXT("Committed turn %d is missing from the retained tail."),
					Turn));
				return;
			}
			break;
		}
		SourceRelay->Client_ReceiveTurn(
			ActiveProtocolContext, Turn, *Retained);
		++Sent;
	}
	UE_LOG(LogSeinNet, Log,
		TEXT("[Resync] slot=%u tail served: %d turn(s) from %d."),
		Slot->Value, Sent, FromTurn);
}

void USeinNetSubsystem::ServerHandleResyncReady(
	ASeinNetRelay* SourceRelay,
	const FSeinProtocolContext& Context)
{
	if (!IsServer() || !SourceRelay) return;
	if (!IsCurrentProtocolContext(Context, TEXT("ServerHandleResyncReady")))
	{
		return;
	}
	const FSeinPlayerID* Slot = RelayToSlot.Find(SourceRelay);
	FServerResyncServe* Serve =
		Slot ? ServerResyncServes.Find(*Slot) : nullptr;
	if (!Slot || !Serve)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("[Resync] ready report refused: no serve in flight."));
		return;
	}
	// A repeated ready report RESCHEDULES the handshake: the peer sends it
	// again when a scheduled boundary already passed on its side (it may run
	// up to InputDelay turns ahead of the coordinator's execution turn).
	// The boundary must clear the peer's maximum legal lead plus headroom.
	Serve->ActivationCheckTurn =
		GetCurrentTurn() + GetInputDelayTurns() + 2;
	Serve->LocalActivationRoot.Reset();
	Serve->PeerActivationRoot.Reset();
	SourceRelay->Client_NotifyResyncActivationCheck(
		ActiveProtocolContext, Serve->ActivationCheckTurn);
	UE_LOG(LogSeinNet, Log,
		TEXT("[Resync] slot=%u activation handshake scheduled at turn=%d."),
		Slot->Value, Serve->ActivationCheckTurn);
}

void USeinNetSubsystem::ServerHandleResyncActivationRoot(
	ASeinNetRelay* SourceRelay,
	const FSeinProtocolContext& Context,
	int32 CheckTurn,
	FGuid WorldRoot)
{
	if (!IsServer() || !SourceRelay) return;
	if (!IsCurrentProtocolContext(
		Context, TEXT("ServerHandleResyncActivationRoot")))
	{
		return;
	}
	const FSeinPlayerID* Slot = RelayToSlot.Find(SourceRelay);
	FServerResyncServe* Serve =
		Slot ? ServerResyncServes.Find(*Slot) : nullptr;
	if (!Slot || !Serve || CheckTurn != Serve->ActivationCheckTurn
		|| !WorldRoot.IsValid())
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("[Resync] activation root refused (turn=%d)."), CheckTurn);
		return;
	}
	Serve->PeerActivationRoot = WorldRoot;
	ServerTryCompleteResyncActivation(*Slot);
}

void USeinNetSubsystem::ServerTryCompleteResyncActivation(FSeinPlayerID Slot)
{
	FServerResyncServe* Serve = ServerResyncServes.Find(Slot);
	if (!Serve
		|| !Serve->LocalActivationRoot.IsSet()
		|| !Serve->PeerActivationRoot.IsSet())
	{
		return;
	}
	ASeinNetRelay* Relay = FindRelayForSlot(Slot);
	if (Serve->LocalActivationRoot.GetValue()
		!= Serve->PeerActivationRoot.GetValue())
	{
		ServerFailResync(Slot, Relay, FString::Printf(
			TEXT("Canonical roots diverged at the activation boundary (turn %d); the adopted timeline is not exact."),
			Serve->ActivationCheckTurn));
		return;
	}

	// Exact agreement: the peer is provably on the live timeline. Guarantee
	// heartbeat coverage through FirstAuthoredTurn - 1 so the peer's first
	// authored turn can never collide with an injected heartbeat — measured
	// from the LATER of the scheduled boundary and the coordinator's turn at
	// completion (a laggy root report must not shrink the coverage below
	// turns already heartbeat-injected while the slot was Reconnecting).
	const int32 FirstAuthoredTurn =
		FMath::Max(Serve->ActivationCheckTurn, GetCurrentTurn())
		+ GetInputDelayTurns() + 2;
	HeartbeatCoverageThroughTurn.Add(Slot, FirstAuthoredTurn - 1);
	// Waive root-report obligations for boundaries the peer completed while
	// suppressed — they can never be back-reported (see the expiry loop).
	const FSeinNetworkParticipantID ActivatedParticipant =
		FindParticipantForSlot(Slot);
	if (ActivatedParticipant.IsValid())
	{
		WorldRootReportExemptionThroughTurn.Add(
			ActivatedParticipant, FirstAuthoredTurn - 1);
	}
	SlotLifecycle.Add(Slot, ESeinSlotLifecycle::Connected);
	SlotReconnectingSinceTime.Remove(Slot);
	UE_LOG(LogSeinNet, Log,
		TEXT("[Resync] slot=%u ACTIVATED at turn=%d; authorship resumes at turn=%d."),
		Slot.Value, Serve->ActivationCheckTurn, FirstAuthoredTurn);
	if (Relay)
	{
		Relay->Client_NotifyResyncActivation(
			ActiveProtocolContext, /*bActivated=*/true,
			FirstAuthoredTurn, FString());
	}
	ServerResyncServes.Remove(Slot);
}

void USeinNetSubsystem::ServerAdvanceResyncActivation(int32 JustFinishedTurn)
{
	// Exemptions age out only past the retention window — the expiry loop
	// they guard evaluates turns up to a full window behind the frontier.
	for (auto It = WorldRootReportExemptionThroughTurn.CreateIterator();
		It; ++It)
	{
		if (It.Value() < JustFinishedTurn - GSeinRetainedHistoryTurns)
		{
			It.RemoveCurrent();
		}
	}
	if (ServerResyncServes.IsEmpty())
	{
		for (auto It = HeartbeatCoverageThroughTurn.CreateIterator(); It; ++It)
		{
			if (It.Value() < JustFinishedTurn) It.RemoveCurrent();
		}
		return;
	}
	TArray<FSeinPlayerID> ReadySlots;
	for (TPair<FSeinPlayerID, FServerResyncServe>& Pair : ServerResyncServes)
	{
		FServerResyncServe& Serve = Pair.Value;
		if (Serve.ActivationCheckTurn != JustFinishedTurn
			|| Serve.LocalActivationRoot.IsSet())
		{
			continue;
		}
		FGuid LocalRoot;
		FString RootError;
		if (!ResolveLocalWorldStateRoot(LocalRoot, RootError))
		{
			ServerFailResync(Pair.Key, FindRelayForSlot(Pair.Key),
				FString::Printf(
					TEXT("The coordinator could not compute its activation root: %s"),
					*RootError));
			continue;
		}
		Serve.LocalActivationRoot = LocalRoot;
		ReadySlots.Add(Pair.Key);
	}
	for (const FSeinPlayerID Slot : ReadySlots)
	{
		ServerTryCompleteResyncActivation(Slot);
	}
	for (auto It = HeartbeatCoverageThroughTurn.CreateIterator(); It; ++It)
	{
		if (It.Value() < JustFinishedTurn) It.RemoveCurrent();
	}
}

bool USeinNetSubsystem::RequestResync(FString& OutError)
{
	OutError.Reset();
	if (IsServer())
	{
		OutError =
			TEXT("The coordinator IS the authoritative timeline; resync applies only to owning peers.");
		return false;
	}
	if (ClientResyncPhase != EClientResyncPhase::None)
	{
		OutError = TEXT("A resync is already in flight for this peer.");
		return false;
	}
	ASeinNetRelay* Relay = LocalRelay.Get();
	if (!Relay)
	{
		OutError = TEXT("No owned relay: this peer is not connected to a coordinator.");
		return false;
	}
	ClientResyncPhase = EClientResyncPhase::Transferring;
	ClientResyncTransferId = -1;
	ClientResyncCheckpointTurn = -1;
	ClientResyncTotalChunks = 0;
	ClientResyncTotalBytes = 0;
	ClientResyncReceivedChunks = 0;
	ClientResyncEnvelopeBytes.Reset();
	ClientResyncActivationCheckTurn = -1;
	ClientResyncHighestReceivedTurn = -1;
	Relay->Server_RequestResync(ActiveProtocolContext);
	UE_LOG(LogSeinNet, Log, TEXT("[Resync] requested from the coordinator."));
	return true;
}

void USeinNetSubsystem::ClientResetResyncState(const TCHAR* Reason)
{
	if (ClientResyncPhase == EClientResyncPhase::None) return;
	UE_LOG(LogSeinNet, Log, TEXT("[Resync] local state reset: %s"),
		Reason ? Reason : TEXT("unspecified"));
	const bool bAdopted =
		ClientResyncPhase == EClientResyncPhase::CatchingUp
		|| ClientResyncPhase == EClientResyncPhase::ActivationPending;
	if (UWorld* World = GetWorld())
	{
		if (USeinWorldSubsystem* WorldSub =
			World->GetSubsystem<USeinWorldSubsystem>())
		{
			WorldSub->EndResyncCatchUpWindow();
			if (bAdopted)
			{
				// The world is on the adopted timeline: keep its scheduler
				// alive (the gate paces it; authorship stays withheld
				// server-side) and reconcile the authorship cursors to the
				// adopted frontier so a later boundary cannot burst-submit
				// thousands of stale-timeline turns.
				const int32 TicksPerTurn = GetTicksPerTurn();
				const int32 AdoptedTurn = TicksPerTurn > 0
					? WorldSub->GetCurrentTick() / TicksPerTurn
					: 0;
				const int32 ReconciledCursor =
					AdoptedTurn + GetInputDelayTurns();
				LastQueuedTurn =
					FMath::Max(LastQueuedTurn, ReconciledCursor);
				LastSubmittedTurn =
					FMath::Max(LastSubmittedTurn, ReconciledCursor);
				if (!WorldSub->IsSimulationRunning())
				{
					WorldSub->StartSimulation();
				}
			}
		}
	}
	// Free the coordinator-side serve so a fresh request is not refused.
	if (ASeinNetRelay* Relay = LocalRelay.Get())
	{
		Relay->Server_AbortResync(ActiveProtocolContext);
	}
	ClientResyncPhase = EClientResyncPhase::None;
	ClientResyncTransferId = -1;
	ClientResyncEnvelopeBytes.Reset();
}

void USeinNetSubsystem::ClientHandleBeginCheckpointTransfer(
	const FSeinProtocolContext& Context,
	int32 TransferId,
	int32 CheckpointTurn,
	int32 TotalChunks,
	int64 TotalBytes)
{
	if (!IsCurrentProtocolContext(
		Context, TEXT("ClientHandleBeginCheckpointTransfer")))
	{
		return;
	}
	if (ClientResyncPhase != EClientResyncPhase::Transferring)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Resync] unexpected checkpoint transfer begin ignored."));
		return;
	}
	if (TotalBytes <= 0 || TotalChunks <= 0
		|| TotalBytes > static_cast<int64>(
			FSeinSnapshotEnvelopeCodec::MaxBodyBytes)
			+ FSeinSnapshotEnvelopeCodec::PrefixBytes
			+ static_cast<int64>(
				FSeinSnapshotEnvelopeCodec::MaxDirectoryBytes)
		|| TotalChunks != FMath::DivideAndRoundUp(
			static_cast<int32>(TotalBytes), ResyncCheckpointChunkBytes))
	{
		ClientResetResyncState(
			TEXT("checkpoint transfer announced out-of-bounds framing"));
		return;
	}
	ClientResyncTransferId = TransferId;
	ClientResyncCheckpointTurn = CheckpointTurn;
	ClientResyncTotalChunks = TotalChunks;
	ClientResyncTotalBytes = TotalBytes;
	ClientResyncReceivedChunks = 0;
	ClientResyncEnvelopeBytes.Reset();
	ClientResyncEnvelopeBytes.Reserve(static_cast<int32>(TotalBytes));
}

void USeinNetSubsystem::ClientHandleCheckpointChunk(
	const FSeinProtocolContext& Context,
	int32 TransferId,
	int32 ChunkIndex,
	const TArray<uint8>& Bytes)
{
	if (!IsCurrentProtocolContext(
		Context, TEXT("ClientHandleCheckpointChunk")))
	{
		return;
	}
	if (ClientResyncPhase != EClientResyncPhase::Transferring
		|| TransferId != ClientResyncTransferId
		|| ChunkIndex != ClientResyncReceivedChunks
		|| Bytes.IsEmpty()
		|| Bytes.Num() > ResyncCheckpointChunkBytes
		|| ClientResyncEnvelopeBytes.Num() + Bytes.Num()
			> ClientResyncTotalBytes)
	{
		ClientResetResyncState(
			TEXT("checkpoint chunk arrived out of order or out of bounds"));
		return;
	}
	ClientResyncEnvelopeBytes.Append(Bytes);
	++ClientResyncReceivedChunks;
}

void USeinNetSubsystem::ClientHandleEndCheckpointTransfer(
	const FSeinProtocolContext& Context,
	int32 TransferId)
{
	if (!IsCurrentProtocolContext(
		Context, TEXT("ClientHandleEndCheckpointTransfer")))
	{
		return;
	}
	if (ClientResyncPhase != EClientResyncPhase::Transferring
		|| TransferId != ClientResyncTransferId
		|| ClientResyncReceivedChunks != ClientResyncTotalChunks
		|| ClientResyncEnvelopeBytes.Num() != ClientResyncTotalBytes)
	{
		ClientResetResyncState(
			TEXT("checkpoint transfer ended incomplete"));
		return;
	}

	ClientResyncPhase = EClientResyncPhase::Adopting;

	FSeinWorldSnapshot Checkpoint;
	FSeinWorldSnapshotReferenceGuard CheckpointGCGuard(Checkpoint);
	FSeinSnapshotEnvelopeMetadata Metadata;
	FString DecodeError;
	if (!SeinSnapshotTransfer::DecodeCheckpointEnvelope(
		ClientResyncEnvelopeBytes, Checkpoint, Metadata, DecodeError))
	{
		ClientResetResyncState(*DecodeError);
		return;
	}
	ClientResyncEnvelopeBytes.Reset();

	UWorld* World = GetWorld();
	USeinWorldSubsystem* WorldSub =
		World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	if (!WorldSub)
	{
		ClientResetResyncState(TEXT("no local simulation world to adopt into"));
		return;
	}

	// The gate keeps a stalled peer's sim waiting between ticks, but adoption
	// itself requires the scheduler idle. Stop explicitly (idempotent) so the
	// adopted world resumes only when WE start it for catch-up.
	WorldSub->StopSimulation();

	FSeinSnapshotRestoreAuthorityHandle Authority;
	FString ClaimError;
	if (!WorldSub->ClaimSnapshotRestoreAuthority(
		FName(TEXT("SeinARTSNet.Resync")), this, Authority, ClaimError))
	{
		ClientResetResyncState(*ClaimError);
		return;
	}
	if (!WorldSub->RestoreSnapshot(
		MoveTemp(Authority),
		Checkpoint,
		FSeinSnapshotRestoreOptions(
			ESeinSnapshotLocalStateRestorePolicy::PreserveCurrent,
			ESeinSnapshotResumePolicy::RemainStopped)))
	{
		ClientResetResyncState(
			TEXT("the transferred checkpoint failed exact adoption"));
		return;
	}

	FString WindowError;
	if (!WorldSub->BeginResyncCatchUpWindow(WindowError))
	{
		ClientResetResyncState(*WindowError);
		return;
	}

	// Turns delivered before adoption belong to a timeline this world has
	// left: entries at or below the checkpoint turn can never be consumed
	// (the sim is already past them) and would block the ready condition
	// for up to a full retention window.
	for (auto It = ReceivedTurns.CreateIterator(); It; ++It)
	{
		if (It.Key() <= ClientResyncCheckpointTurn) It.RemoveCurrent();
	}

	// Catch up on the tail through the normal delivery path: request the
	// retained turns, start the dormant reservation, and let the standard
	// completeness gate free-run to the frontier under the core's catch-up
	// burst (the accumulator is topped to MaxTicksPerFrame while the window
	// is open, so the wall-clock deficit actually closes).
	ClientResyncPhase = EClientResyncPhase::CatchingUp;
	ClientResyncHighestReceivedTurn = -1;
	if (ASeinNetRelay* Relay = LocalRelay.Get())
	{
		Relay->Server_RequestResyncTail(
			ActiveProtocolContext, ClientResyncCheckpointTurn + 1);
	}
	if (!WorldSub->StartSimulation())
	{
		ClientResetResyncState(
			TEXT("the adopted world's scheduler could not start for catch-up"));
		return;
	}
	UE_LOG(LogSeinNet, Log,
		TEXT("[Resync] checkpoint adopted at turn=%d; catching up."),
		ClientResyncCheckpointTurn);
}

void USeinNetSubsystem::ClientHandleResyncActivationCheck(
	const FSeinProtocolContext& Context,
	int32 CheckTurn)
{
	if (!IsCurrentProtocolContext(
		Context, TEXT("ClientHandleResyncActivationCheck")))
	{
		return;
	}
	if (ClientResyncPhase != EClientResyncPhase::ActivationPending)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Resync] unexpected activation check (turn=%d) ignored."),
			CheckTurn);
		return;
	}
	// This peer may legally run ahead of the coordinator's execution turn;
	// if the scheduled boundary already completed locally, its root can no
	// longer be computed — ask for a fresh boundary instead of wedging.
	const int32 TicksPerTurn = GetTicksPerTurn();
	UWorld* World = GetWorld();
	const USeinWorldSubsystem* WorldSub =
		World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	const int32 LastCompletedTurn = (WorldSub && TicksPerTurn > 0)
		? (WorldSub->GetCurrentTick() / TicksPerTurn) - 1
		: -1;
	if (CheckTurn <= LastCompletedTurn)
	{
		UE_LOG(LogSeinNet, Log,
			TEXT("[Resync] activation boundary turn=%d already passed locally (at %d); requesting a reschedule."),
			CheckTurn, LastCompletedTurn);
		if (ASeinNetRelay* Relay = LocalRelay.Get())
		{
			Relay->Server_ReportResyncReady(ActiveProtocolContext);
		}
		return;
	}
	ClientResyncActivationCheckTurn = CheckTurn;
}

void USeinNetSubsystem::ClientHandleResyncActivation(
	const FSeinProtocolContext& Context,
	bool bActivated,
	int32 FirstAuthoredTurn,
	const FString& Reason)
{
	if (!IsCurrentProtocolContext(
		Context, TEXT("ClientHandleResyncActivation")))
	{
		return;
	}
	if (!bActivated)
	{
		ClientResetResyncState(*FString::Printf(
			TEXT("the coordinator failed this resync: %s"), *Reason));
		return;
	}
	if (ClientResyncPhase != EClientResyncPhase::ActivationPending)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Resync] activation verdict arrived in an unexpected phase."));
		return;
	}

	// Rejoin the authorship ledger exactly where the coordinator's heartbeat
	// coverage ends: our first submission is FirstAuthoredTurn, gap-free and
	// collision-free by construction.
	LastQueuedTurn = FirstAuthoredTurn - 1;
	LastSubmittedTurn = FirstAuthoredTurn - 1;
	if (UWorld* World = GetWorld())
	{
		if (USeinWorldSubsystem* WorldSub =
			World->GetSubsystem<USeinWorldSubsystem>())
		{
			WorldSub->EndResyncCatchUpWindow();
		}
	}
	ClientResyncPhase = EClientResyncPhase::None;
	UE_LOG(LogSeinNet, Log,
		TEXT("[Resync] ACTIVATED; authorship resumes at turn=%d."),
		FirstAuthoredTurn);
}

void USeinNetSubsystem::MaybeAutoRequestResync(int32 RejectedLiveTurn)
{
	if (IsServer() || ClientResyncPhase != EClientResyncPhase::None)
	{
		return;
	}
	// Only turns AHEAD of the window imply a stale timeline (behind-window
	// turns are ordinary late deliveries). Require a real gap so a boundary
	// race cannot trigger a spurious resync.
	const int32 Lead = RejectedLiveTurn - GetCurrentTurn();
	if (Lead <= GetInputDelayTurns() + GSeinMaxProtocolTurnLead)
	{
		return;
	}
	FString Error;
	if (RequestResync(Error))
	{
		UE_LOG(LogSeinNet, Log,
			TEXT("[Resync] auto-requested: live turn %d is %d turn(s) beyond this peer's window."),
			RejectedLiveTurn, Lead);
	}
	else
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Resync] auto-request refused: %s"), *Error);
	}
}

void USeinNetSubsystem::ClientAdvanceResyncCatchUp(int32 JustFinishedTurn)
{
	if (ClientResyncPhase == EClientResyncPhase::CatchingUp)
	{
		// Caught up when every received turn is consumed and the sim reached
		// the highest turn the wire has shown us.
		if (ReceivedTurns.IsEmpty()
			&& ClientResyncHighestReceivedTurn >= 0
			&& JustFinishedTurn >= ClientResyncHighestReceivedTurn)
		{
			ClientResyncPhase = EClientResyncPhase::ActivationPending;
			if (ASeinNetRelay* Relay = LocalRelay.Get())
			{
				Relay->Server_ReportResyncReady(ActiveProtocolContext);
			}
			UE_LOG(LogSeinNet, Log,
				TEXT("[Resync] frontier reached at turn=%d; awaiting activation handshake."),
				JustFinishedTurn);
		}
		return;
	}
	if (ClientResyncPhase == EClientResyncPhase::ActivationPending
		&& ClientResyncActivationCheckTurn == JustFinishedTurn)
	{
		FGuid LocalRoot;
		FString RootError;
		if (!ResolveLocalWorldStateRoot(LocalRoot, RootError))
		{
			ClientResetResyncState(*FString::Printf(
				TEXT("could not compute the activation root: %s"),
				*RootError));
			return;
		}
		if (ASeinNetRelay* Relay = LocalRelay.Get())
		{
			Relay->Server_ReportResyncActivationRoot(
				ActiveProtocolContext,
				ClientResyncActivationCheckTurn, LocalRoot);
		}
	}
}

void USeinNetSubsystem::SimulateSlotDisconnect(FSeinPlayerID Slot)
{
	if (!IsServer())
	{
		UE_LOG(LogSeinNet, Warning, TEXT("SimulateSlotDisconnect: server-only — ignored on client."));
		return;
	}
	if (!Slot.IsValid()) return;

	SlotLifecycle.Add(Slot, ESeinSlotLifecycle::Dropped);
	SlotDroppedAtTime.Add(Slot, FPlatformTime::Seconds());

	UE_LOG(LogSeinNet, Warning,
		TEXT("[Drop] SimulateSlotDisconnect: slot %u marked DROPPED. Heartbeat injection active; AI takeover scheduled in %.1fs."),
		Slot.Value, GetDroppedToAITakeoverSeconds());

	// Cover both already-open turns and the full input-delay pipeline. A slot
	// can disconnect before any author has opened a future gate turn.
	BackfillSuppressedSlotHeartbeatsThroughPipelineWindow();

	if (!ServerWorldStateRootReports.IsEmpty())
	{
		TArray<int32> PendingTurns;
		ServerWorldStateRootReports.GetKeys(PendingTurns);
		for (const int32 Turn : PendingTurns)
		{
			const TMap<FSeinNetworkParticipantID, FGuid>* Reports =
				ServerWorldStateRootReports.Find(Turn);
			if (Reports && AreExpectedWorldRootReportsComplete(*Reports))
			{
				ServerCompareWorldStateRootsForTurn(Turn);
			}
		}
	}

	// The parity barrier is defined over connected relay processes. A
	// simulated drop shrinks that set just like a real logout.
	TryDispatchLockstepSessionStart();
}

void USeinNetSubsystem::SimulateSlotReconnect(FSeinPlayerID Slot)
{
	if (!IsServer())
	{
		UE_LOG(LogSeinNet, Warning, TEXT("SimulateSlotReconnect: server-only — ignored on client."));
		return;
	}
	if (!Slot.IsValid()) return;

	const ESeinSlotLifecycle* Prev = SlotLifecycle.Find(Slot);
	if (!Prev)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Drop] SimulateSlotReconnect: slot %u has no lifecycle entry — was it ever connected?"),
			Slot.Value);
		return;
	}

	const bool bWasAITakeover = (*Prev == ESeinSlotLifecycle::AITakeover);
	SlotLifecycle.Add(Slot, ESeinSlotLifecycle::Connected);
	SlotDroppedAtTime.Remove(Slot);
	if (bWasAITakeover)
	{
		TeardownAIForSlot(Slot);
	}

	UE_LOG(LogSeinNet, Log,
		TEXT("[Drop] SimulateSlotReconnect: slot %u back to CONNECTED. NOTE: full snapshot+tail catch-up is a follow-up phase — slot resumes from current sim state, not from where it disconnected."),
		Slot.Value);
}

void USeinNetSubsystem::ServerCheckTurnComplete(
	int32 TurnId, FSeinPlayerID CompletingSubmitter)
{
	if (!TurnAggregator.IsConfigured()
		|| TurnAggregator.IsTurnRetired(TurnId)
		|| TurnAggregator.IsTurnCommitted(TurnId))
	{
		return;
	}

	const TArray<FSeinTurnAuthor> MissingAuthors =
		TurnAggregator.GetMissingAuthors(TurnId);
	const int32 ReceivedExpectedCount =
		TurnAggregator.GetSubmittedAuthorCount(TurnId);
	const int32 ExpectedAuthorCount = TurnAggregator.GetExpectedAuthors().Num();
	if (!MissingAuthors.IsEmpty())
	{
		// Persistent-incomplete escalation: most incompletes are transient
		// pipeline blips. State is per turn because future/open turns can be
		// examined in an interleaved order as RPCs and disconnect recovery run.
		const double NowSec = FPlatformTime::Seconds();
		FSeinIncompleteTurnDiagnostic* Diagnostic =
			IncompleteTurnDiagnostics.Find(TurnId);
		if (!Diagnostic)
		{
			FSeinIncompleteTurnDiagnostic& Added =
				IncompleteTurnDiagnostics.Add(TurnId);
			Added.FirstObservedAt = NowSec;
			Added.LastLoggedAt = NowSec;
			UE_LOG(LogSeinNet, Verbose,
				TEXT("[BUFFER INCOMPLETE transient] turn=%d have=%d/%d authors."),
				TurnId, ReceivedExpectedCount, ExpectedAuthorCount);
			return;
		}

		const double IncompleteFor = NowSec - Diagnostic->FirstObservedAt;
		if (IncompleteFor >= 2.0 &&
			(!Diagnostic->bEscalated || (NowSec - Diagnostic->LastLoggedAt) >= 2.0))
		{
			TArray<FString> Have, Missing;
			for (const FSeinTurnAuthor& Author : TurnAggregator.GetExpectedAuthors())
			{
				const FString Label = FString::Printf(
					TEXT("%s/slot-%u"),
					*Author.ParticipantID.ToCanonicalString(), Author.CommandSlot.Value);
				if (TurnAggregator.HasSubmission(TurnId, Author))
				{
					Have.Add(Label);
				}
				else
				{
					Missing.Add(Label);
				}
			}
			UE_LOG(LogSeinNet, Log,
				TEXT("[BUFFER INCOMPLETE persistent] turn=%d incomplete for %.1fs have=%d/%d authors [%s] missing=[%s]. Server is holding."),
				TurnId, IncompleteFor, ReceivedExpectedCount, ExpectedAuthorCount,
				*FString::Join(Have, TEXT(",")),
				*FString::Join(Missing, TEXT(",")));
			Diagnostic->LastLoggedAt = NowSec;
			Diagnostic->bEscalated = true;
		}
		return;
	}

	TArray<FSeinCommand> Assembled;
	const ESeinTurnCommitResult CommitResult =
		TurnAggregator.TryCommit(ActiveProtocolContext, TurnId, Assembled);
	if (CommitResult != ESeinTurnCommitResult::Committed)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Server] ready turn=%d failed canonical commit result=%d."),
			TurnId, static_cast<int32>(CommitResult));
		return;
	}

	// Per-turn chatter — Verbose. Routine completion isn't worth a log line
	// every ~100ms in a healthy session. Bump LogSeinNet to Verbose to see.
	UE_LOG(LogSeinNet, Verbose,
		TEXT("[Server] turn complete: turn=%d authors=%d commands=%d — fanning to %d relays."),
		TurnId, ExpectedAuthorCount, Assembled.Num(), Relays.Num());

	FinalizeCompletedTurnDiagnostics(TurnId, CompletingSubmitter);

	FSeinOpaqueCommandBatch OpaqueAssembled;
	FString WireError;
	if (!FSeinNetCommandWireCodec::EncodeCommands(
		Assembled,
		GetMaxCommandsPerCanonicalTurn(),
		[this](FGameplayTag Type, int32 Version, FSeinCommandSchemaDescriptor& Out)
		{
			return FindFrozenCommandSchema(Type, Version, Out);
		},
		OpaqueAssembled,
		WireError))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("[Server] committed turn=%d cannot be encoded for fan-out: %s."),
			TurnId, *WireError);
		return;
	}
	// Dedicated authorities do not receive their own client RPC. Decode the
	// exact fan-out bytes once locally so their sim and the replay observe the
	// same canonical representation as every remote peer (including container
	// normalization performed by the bounded decoder).
	TArray<FSeinCommand> WireCanonicalAssembled;
	if (!FSeinNetCommandWireCodec::DecodeCommands(
		OpaqueAssembled,
		GetMaxCommandsPerCanonicalTurn(),
		[this](FGameplayTag Type, int32 Version, FSeinCommandSchemaDescriptor& Out)
		{
			return FindFrozenCommandSchema(Type, Version, Out);
		},
		WireCanonicalAssembled,
		WireError))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("[Server] committed turn=%d failed local canonical wire decode: %s."),
			TurnId, *WireError);
		return;
	}

	// Capture the exact canonical fan-out bytes into the replay journal BEFORE
	// fan-out. Recording at the assembly step gives one authoritative copy free
	// of duplicates or ordering ambiguity, while retaining byte identity with
	// every peer's bounded decoder.
	if (ReplayWriter && ReplayWriter->IsRecording())
	{
		ReplayWriter->RecordEncodedTurn(TurnId, OpaqueAssembled);
	}

	// Retain the EXACT fan-out bytes so a resync tail is byte-identical to
	// live delivery (same opaque batch through the same Client_ReceiveTurn).
	// Bounded by the shared protocol history window; pruned alongside the
	// other per-turn ledgers in PruneProtocolState. This remains FEAT-01's
	// short-lived recovery-tail source; persistent replay storage is independent.
	RetainedAssembledTurns.Add(TurnId, OpaqueAssembled);

	// Feed a co-located authority directly from the exact decoded fan-out bytes.
	// Depending on a local Client RPC can strand a listen host when a turn is
	// committed synchronously from logout/reconnect recovery.
	const bool bBufferedLocalAuthority =
		BufferAssembledTurnForLocalAuthority(
			TurnId, WireCanonicalAssembled);

	for (const TWeakObjectPtr<ASeinNetRelay>& Wp : Relays)
	{
		if (ASeinNetRelay* Target = Wp.Get())
		{
			const APlayerController* OwnerController =
				Cast<APlayerController>(Target->GetOwner());
			if (bBufferedLocalAuthority
				&& OwnerController
				&& OwnerController->IsLocalController())
			{
				continue;
			}
			Target->Client_ReceiveTurn(ActiveProtocolContext, TurnId, OpaqueAssembled);
		}
	}
}

bool USeinNetSubsystem::BufferAssembledTurnForLocalAuthority(
	int32 TurnId, const TArray<FSeinCommand>& Commands)
{
	if (IsServer())
	{
		BufferReceivedTurn(TurnId, Commands);
		return true;
	}
	return false;
}

void USeinNetSubsystem::BufferReceivedTurn(int32 TurnId, const TArray<FSeinCommand>& Commands)
{
	ReceivedTurns.Add(TurnId, Commands);
	if (ClientResyncPhase == EClientResyncPhase::CatchingUp
		|| ClientResyncPhase == EClientResyncPhase::Adopting)
	{
		ClientResyncHighestReceivedTurn =
			FMath::Max(ClientResyncHighestReceivedTurn, TurnId);
	}
	OnTurnReceived.Broadcast(TurnId, Commands);
}

void USeinNetSubsystem::ClientHandleTurn(
	const FSeinProtocolContext& Context,
	int32 TurnId,
	const FSeinOpaqueCommandBatch& OpaqueCommands)
{
	if (!IsCurrentProtocolContext(Context, TEXT("ClientHandleTurn"))) return;
	PruneProtocolState(GetCurrentTurn());
	if (!IsCommandTurnWithinProtocolWindow(TurnId, TEXT("ClientHandleTurn")))
	{
		// A live turn far beyond this peer's window means we are on a stale
		// timeline (rejoined a launched match, or fell hopelessly behind).
		// Self-detect and request the checkpoint+tail resync; the phase
		// machine debounces repeats.
		MaybeAutoRequestResync(TurnId);
		return;
	}
	// A world still AWAITING bootstrap while live turns flow is a late
	// joiner: the match launched without us and no launch RPC is coming.
	// Turn fan-out only begins after every launch participant consumed its
	// authorization, so this cannot race a normal match start.
	if (ClientResyncPhase == EClientResyncPhase::None && !IsServer())
	{
		const USeinWorldSubsystem* JoinSim = GetWorld()
			? GetWorld()->GetSubsystem<USeinWorldSubsystem>() : nullptr;
		if (JoinSim
			&& JoinSim->GetMatchBootstrapState()
				== ESeinMatchBootstrapState::Awaiting)
		{
			FString AutoError;
			if (RequestResync(AutoError))
			{
				UE_LOG(LogSeinNet, Log,
					TEXT("[Resync] auto-requested: live turn %d arrived while this world still awaits bootstrap (late join)."),
					TurnId);
			}
		}
	}
	TArray<FSeinCommand> Commands;
	FString WireError;
	if (!FSeinNetCommandWireCodec::DecodeCommands(
		OpaqueCommands,
		GetMaxCommandsPerCanonicalTurn(),
		[this](FGameplayTag Type, int32 Version, FSeinCommandSchemaDescriptor& Out)
		{
			return FindFrozenCommandSchema(Type, Version, Out);
		},
		Commands,
		WireError))
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Client] rejecting malformed opaque assembled turn=%d: %s."),
			TurnId, *WireError);
		return;
	}
	for (const FSeinCommand& Command : Commands)
	{
		FSeinCommandSchemaDescriptor Schema;
		const bool bExternalIssuer =
			Command.IssuerKind == ESeinCommandIssuerKind::Player
			|| Command.IssuerKind == ESeinCommandIssuerKind::MatchAdministrator;
		if (!bExternalIssuer || Command.DerivedResourcePayer.IsValid()
			|| !FindFrozenCommandSchema(
				Command.CommandType, Command.SchemaVersion, Schema))
		{
			UE_LOG(LogSeinNet, Error,
				TEXT("[Client] rejecting non-canonical assembled turn=%d command=%s issuer=%d derivedPayer=%u."),
				TurnId, *Command.CommandType.ToString(),
				static_cast<int32>(Command.IssuerKind),
				Command.DerivedResourcePayer.Value);
			return;
		}
	}

	// Per-turn chatter — Verbose. See ServerCheckTurnComplete's note for why.
	UE_LOG(LogSeinNet, Verbose, TEXT("[Client] Receive turn: TurnId=%d Count=%d  (buffered for turn-boundary drain)"),
		TurnId, Commands.Num());

	// Phase 2b: store the assembled turn keyed by TurnId. The sim's gate
	// (ResolveTurnReady → ConsumeTurn) drains this map at the matching
	// turn boundary, guaranteeing every client applies turn N's commands
	// at the same sim tick (= N * TicksPerTurn). Empty turns still get an
	// entry so the gate sees them as "ready" instead of stalling.
	if (ReceivedTurns.Contains(TurnId))
	{
		UE_LOG(LogSeinNet, Verbose,
			TEXT("[Client] duplicate assembled turn=%d ignored; first delivery is immutable."), TurnId);
		return;
	}
	BufferReceivedTurn(TurnId, Commands);
}

USeinReplayReader* USeinNetSubsystem::GetOrCreateReplayReader()
{
	if (!ReplayReader)
	{
		ReplayReader = NewObject<USeinReplayReader>(this);
	}
	return ReplayReader;
}

void USeinNetSubsystem::FinalizeCompletedTurnDiagnostics(
	int32 TurnId, FSeinPlayerID CompletingSubmitter)
{
	FSeinIncompleteTurnDiagnostic Diagnostic;
	const bool bCompletedAfterIncomplete =
		IncompleteTurnDiagnostics.RemoveAndCopyValue(TurnId, Diagnostic);
	if (bCompletedAfterIncomplete)
	{
		if (Diagnostic.bEscalated)
		{
			UE_LOG(LogSeinNet, Log,
				TEXT("[BUFFER INCOMPLETE persistent] turn=%d RESOLVED — completed after stall. Sim resuming."),
				TurnId);
		}
	}

	++TurnsCompletedCount;
	if (bCompletedAfterIncomplete
		&& Diagnostic.bReachedExecutionGate
		&& CompletingSubmitter.IsValid())
	{
		RecordStragglerIfApplicable(TurnId, CompletingSubmitter);
	}
}

void USeinNetSubsystem::RecordStragglerIfApplicable(int32 TurnId, FSeinPlayerID LastSubmittingSlot)
{
	if (!LastSubmittingSlot.IsValid()) return;
	int32& Count = StragglerCounts.FindOrAdd(LastSubmittingSlot);
	++Count;

	// Recommend bumping InputDelayTurns once a peer crosses 5% straggle rate
	// over a meaningful sample size. Rate-limit to once every 64 turns to
	// avoid log spam. Designers can manually raise InputDelayTurns in
	// settings; full automatic dynamic-delay adjustment is deferred (see the
	// header note next to StragglerCounts).
	const int32 RECOMMEND_INTERVAL_TURNS = 64;
	const int32 SAMPLE_THRESHOLD = 50;
	if (TurnsCompletedCount >= SAMPLE_THRESHOLD &&
		(TurnsCompletedCount % RECOMMEND_INTERVAL_TURNS) == 0)
	{
		const float Rate = static_cast<float>(Count) / static_cast<float>(TurnsCompletedCount);
		if (Rate > 0.05f)
		{
			UE_LOG(LogSeinNet, Warning,
				TEXT("[ADAPTIVE INPUT DELAY] slot=%u completed after the execution gate on %d / %d turns (%.1f%%). Consider raising USeinARTSCoreSettings::InputDelayTurns to absorb this peer's latency. Today this is a manual change; automatic dynamic adjustment is a future polish item."),
				LastSubmittingSlot.Value, Count, TurnsCompletedCount, Rate * 100.0f);
		}
	}
}

// ============================================================================
// Determinism gossip
// ============================================================================

bool USeinNetSubsystem::ResolveLocalWorldStateRoot(
	FGuid& OutRoot,
	FString& OutError) const
{
	OutRoot.Invalidate();
	OutError.Reset();

#if WITH_DEV_AUTOMATION_TESTS
	if (TestWorldStateRootResolverOverride)
	{
		FGuid Candidate;
		FString CandidateError;
		if (!TestWorldStateRootResolverOverride(Candidate, CandidateError)
			|| !Candidate.IsValid())
		{
			OutError = CandidateError.IsEmpty()
				? TEXT("The test world-state-root resolver returned no valid root.")
				: MoveTemp(CandidateError);
			return false;
		}
		OutRoot = Candidate;
		return true;
	}
#endif

	const UWorld* World = GetWorld();
	const USeinWorldSubsystem* WorldSub =
		World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	if (!WorldSub)
	{
		OutError =
			TEXT("The current world has no Sein simulation subsystem.");
		return false;
	}

	FGuid Candidate;
	FString CandidateError;
	const int32 CompletedTick = WorldSub->GetCurrentTick();
	if (!WorldSub->GetSealedRoutineCanonicalStateRoot(
			CompletedTick, Candidate, CandidateError))
	{
		// Pause-control and bootstrap paths may request a root outside the ordinary
		// completed-tick observer. They are rare; seal the same incremental root
		// lazily, never fall back to the full snapshot serializer.
		CandidateError.Reset();
		if (!WorldSub->SealRoutineCanonicalStateRoot(
				CompletedTick,
				/*bForceFullRebuild=*/false,
				Candidate,
				CandidateError))
		{
			OutError = CandidateError.IsEmpty()
				? TEXT("Core returned no valid routine canonical world-state root.")
				: MoveTemp(CandidateError);
			return false;
		}
	}
	if (!Candidate.IsValid())
	{
		OutError = CandidateError.IsEmpty()
			? TEXT("Core returned no valid routine canonical world-state root.")
			: MoveTemp(CandidateError);
		return false;
	}

	OutRoot = Candidate;
	return true;
}

void USeinNetSubsystem::MaybeSubmitWorldStateRootCheck(int32 JustFinishedTurn)
{
	if (!IsDeterminismGossipEnabled()) return;
	if (!IsNetworkingActive()) return;
	if (!LocalParticipantID.IsValid()) return;
	if (DeterminismSessionFailure.IsValid()) return;
	const int32 Interval = GetDeterminismCheckIntervalTurns();
	if (Interval <= 0) return;

	bool bForceSingleReporterObligationForTests = false;
#if WITH_DEV_AUTOMATION_TESTS
	bForceSingleReporterObligationForTests =
		TestDeterminismCheckIntervalOverride.IsSet();
#endif
	if (!HasComparableWorldRootPeerInManifest()
		&& !bForceSingleReporterObligationForTests)
	{
		return;
	}

	// Cadence: every N turns, starting at turn 0 (which is grace anyway, so
	// no real check fires for it; first real check is turn `Interval`).
	if (!IsDueWorldStateRootCheckpoint(JustFinishedTurn)) return;
	if (JustFinishedTurn <= LastWorldStateRootReportedTurn
		|| JustFinishedTurn <= LastWorldStateRootQueuedTurn)
	{
		return;
	}

	// First time this peer reports a root — log at Log level so the user
	// sees gossip is live without needing Verbose. Subsequent reports stay
	// Verbose unless there's a desync.
	if (LastWorldStateRootReportedTurn < 0)
	{
		UE_LOG(LogSeinNet, Log,
			TEXT("[DETERMINISM] gossip active — reporting canonical world-state roots every %d turn(s). Coordinator compares + fires red on-screen alarm if peers diverge."),
			Interval);
	}

	FGuid LocalWorldRoot;
	FString RootError;
	if (!ResolveLocalWorldStateRoot(LocalWorldRoot, RootError))
	{
		LocalDeterminismFailureDiagnostic = RootError.Left(512);
		UE_LOG(LogSeinNet, Error,
			TEXT("MaybeSubmitWorldStateRootCheck: canonical root unavailable at due checkpoint turn %d; failing the lockstep epoch: %s"),
			JustFinishedTurn, *RootError);
		ReportLocalWorldStateRootCaptureFailure(JustFinishedTurn);
		return;
	}

	EnqueueWorldStateRootReport(JustFinishedTurn, LocalWorldRoot);
	FlushPendingWorldStateRootReports();
}

void USeinNetSubsystem::EnqueueWorldStateRootReport(
	int32 Turn,
	const FGuid& WorldRoot)
{
	if (!WorldRoot.IsValid())
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("EnqueueWorldStateRootReport: refusing invalid root for turn=%d."),
			Turn);
		return;
	}
	if (Turn <= LastWorldStateRootReportedTurn
		|| Turn <= LastWorldStateRootQueuedTurn)
	{
		return;
	}
	FSeinPendingWorldStateRootReport& Pending =
		PendingWorldStateRootReports.Emplace_GetRef();
	Pending.Turn = Turn;
	Pending.WorldRoot = WorldRoot;
	LastWorldStateRootQueuedTurn = Turn;
}

void USeinNetSubsystem::FlushPendingWorldStateRootReports()
{
	while (!PendingWorldStateRootReports.IsEmpty())
	{
		const FSeinPendingWorldStateRootReport& Pending =
			PendingWorldStateRootReports[0];
		bool bSent = false;
#if WITH_DEV_AUTOMATION_TESTS
		if (TestWorldStateRootSubmitOverride)
		{
			bSent = TestWorldStateRootSubmitOverride(
				Pending.Turn, Pending.WorldRoot);
		}
		else
#endif
		{
			if (!IsNetworkingActive() || !LocalParticipantID.IsValid()
				|| !ActiveProtocolContext.IsValid() ||
				!IsDeterminismEvidenceTurnWithinProtocolWindow(
					Pending.Turn,
					TEXT("FlushPendingWorldStateRootReports")))
			{
				return;
			}

			if (IsDedicatedAuthority() && IsServer())
			{
				ServerHandleWorldStateRootReportForParticipant(
					LocalParticipantID, Pending.Turn, Pending.WorldRoot);
				bSent = true;
			}
			else if (ASeinNetRelay* Relay = LocalRelay.Get())
			{
				UE_LOG(LogSeinNet, Verbose,
					TEXT("[DETERMINISM] reporting worldRoot=%s for turn=%d participant=%s"),
					*Pending.WorldRoot.ToString(EGuidFormats::Digits),
					Pending.Turn,
					*LocalParticipantID.ToCanonicalString());
				Relay->Server_ReportWorldStateRoot(
					ActiveProtocolContext, Pending.Turn, Pending.WorldRoot);
				bSent = true;
			}
		}

		if (!bSent)
		{
			UE_LOG(LogSeinNet, Warning,
				TEXT("FlushPendingWorldStateRootReports: retaining exact turn=%d root=%s for retry."),
				Pending.Turn,
				*Pending.WorldRoot.ToString(EGuidFormats::Digits));
			return;
		}

		LastWorldStateRootReportedTurn =
			FMath::Max(LastWorldStateRootReportedTurn, Pending.Turn);
		PendingWorldStateRootReports.RemoveAt(
			0, 1, EAllowShrinking::No);
	}
}

void USeinNetSubsystem::ReportLocalWorldStateRootCaptureFailure(
	int32 CheckpointTurn)
{
	if (!LocalParticipantID.IsValid()
		|| !ActiveProtocolContext.IsValid()
		|| !IsDueWorldStateRootCheckpoint(CheckpointTurn))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("[DETERMINISM] refusing malformed local canonical-root capture failure turn=%d participant=%s."),
			CheckpointTurn,
			*LocalParticipantID.ToCanonicalString());
		return;
	}

	FSeinDeterminismSessionFailure Failure;
	Failure.Kind =
		ESeinDeterminismSessionFailureKind::CanonicalRootCaptureFailed;
	Failure.Turn = CheckpointTurn;
	Failure.ParticipantID = LocalParticipantID;
	ReportLocalDeterminismSessionFailure(Failure);
}

void USeinNetSubsystem::SetDeterminismSessionFailureSubmitter(
	FSeinDeterminismSessionFailureSubmitter Submitter)
{
	DeterminismSessionFailureSubmitter = MoveTemp(Submitter);
}

void USeinNetSubsystem::ClearDeterminismSessionFailureSubmitter()
{
	DeterminismSessionFailureSubmitter.Unbind();
}

bool USeinNetSubsystem::RetryPendingDeterminismSessionFailureReport()
{
	FlushPendingDeterminismSessionFailure();
	return !PendingDeterminismSessionFailureReport.IsSet();
}

void USeinNetSubsystem::HandleExecutionTopologyInvalidated(
	const FString& Reason)
{
	if (!LocalParticipantID.IsValid() || !ActiveProtocolContext.IsValid())
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("[DETERMINISM] execution topology invalidated outside an active participant protocol context: %s"),
			*Reason);
		return;
	}

	FSeinDeterminismSessionFailure Failure;
	Failure.Kind =
		ESeinDeterminismSessionFailureKind::ExecutionTopologyInvalidated;
	Failure.Turn = GetCurrentTurn();
	Failure.ParticipantID = LocalParticipantID;
	UE_LOG(LogSeinNet, Error,
		TEXT("[DETERMINISM] local execution topology invalidated at turn=%d participant=%s: %s"),
		Failure.Turn,
		*Failure.ParticipantID.ToCanonicalString(),
		*Reason);
	ReportLocalDeterminismSessionFailure(Failure);
}

void USeinNetSubsystem::ReportLocalDeterminismSessionFailure(
	const FSeinDeterminismSessionFailure& Failure)
{
	FSeinDeterminismSessionFailure LocalFailure = Failure;
	LocalFailure.ParticipantID = LocalParticipantID;
	if (!LocalParticipantID.IsValid()
		|| !ActiveProtocolContext.IsValid()
		|| !LocalFailure.IsValid()
		|| !LocalFailure.IsParticipantReportable())
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("[DETERMINISM] refusing malformed local session-failure report kind=%d turn=%d participant=%s."),
			static_cast<int32>(LocalFailure.Kind),
			LocalFailure.Turn,
			*LocalParticipantID.ToCanonicalString());
		return;
	}

	if (IsLocalProtocolCoordinator()
		&& HandleAuthenticatedDeterminismSessionFailure(
			ActiveProtocolContext, LocalParticipantID, LocalFailure))
	{
		return;
	}

	// A non-coordinator must stop immediately rather than advancing while its
	// terminal report is in flight. The coordinator's exact value replaces
	// this provisional local value when the authoritative notification arrives.
	EnterDeterminismSessionFailure(
		LocalFailure,
		/*bAuthoritative=*/false,
		/*bNotifyPeers=*/false);
	if (!PendingDeterminismSessionFailureReport.IsSet())
	{
		PendingDeterminismSessionFailureReport = LocalFailure;
	}
	FlushPendingDeterminismSessionFailure();
}

void USeinNetSubsystem::FlushPendingDeterminismSessionFailure()
{
	if (!PendingDeterminismSessionFailureReport.IsSet()) return;
	if (bDeterminismSessionFailureAuthoritative)
	{
		PendingDeterminismSessionFailureReport.Reset();
		return;
	}

	const FSeinDeterminismSessionFailure Pending =
		PendingDeterminismSessionFailureReport.GetValue();
	bool bSent = false;
#if WITH_DEV_AUTOMATION_TESTS
	if (TestDeterminismSessionFailureSubmitOverride)
	{
		bSent = TestDeterminismSessionFailureSubmitOverride(Pending);
	}
	else
#endif
	{
		if ((!IsNetworkingActive()
				&& !DeterminismSessionFailureSubmitter.IsBound())
			|| !LocalParticipantID.IsValid()
			|| !ActiveProtocolContext.IsValid())
		{
			return;
		}

		if (IsLocalProtocolCoordinator())
		{
			bSent = HandleAuthenticatedDeterminismSessionFailure(
				ActiveProtocolContext,
				LocalParticipantID,
				Pending);
		}
		else if (DeterminismSessionFailureSubmitter.IsBound())
		{
			bSent = DeterminismSessionFailureSubmitter.Execute(
				ActiveProtocolContext,
				Pending);
		}
		else if (ASeinNetRelay* Relay = LocalRelay.Get())
		{
			Relay->Server_ReportDeterminismSessionFailure(
				ActiveProtocolContext,
				Pending);
			bSent = true;
		}
	}

	if (!bSent)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[DETERMINISM] retaining session-failure report kind=%d turn=%d participant=%s for transport retry."),
			static_cast<int32>(Pending.Kind),
			Pending.Turn,
			*Pending.ParticipantID.ToCanonicalString());
		return;
	}
	PendingDeterminismSessionFailureReport.Reset();
}

void USeinNetSubsystem::EnterDeterminismSessionFailure(
	const FSeinDeterminismSessionFailure& Failure,
	bool bAuthoritative,
	bool bNotifyPeers)
{
	if (!Failure.IsValid()) return;

	bool bStateChanged = false;
	if (!DeterminismSessionFailure.IsValid())
	{
		DeterminismSessionFailure = Failure;
		bDeterminismSessionFailureAuthoritative = bAuthoritative;
		bStateChanged = true;
	}
	else if (!bDeterminismSessionFailureAuthoritative && bAuthoritative)
	{
		DeterminismSessionFailure = Failure;
		bDeterminismSessionFailureAuthoritative = true;
		bStateChanged = true;
	}
	else
	{
		return;
	}
	if (bAuthoritative)
	{
		PendingDeterminismSessionFailureReport.Reset();
		PendingAuthenticatedDeterminismSessionFailures.Reset();
	}

	bStartSessionRequested = false;
	bServerStartRequested = false;
	if (UWorld* World = GetWorld())
	{
		if (USeinWorldSubsystem* WorldSub =
			World->GetSubsystem<USeinWorldSubsystem>())
		{
			WorldSub->StopSimulation();
		}
	}

	const TCHAR* KindText = TEXT("unknown");
	switch (Failure.Kind)
	{
	case ESeinDeterminismSessionFailureKind::CanonicalRootCaptureFailed:
		KindText = TEXT("canonical root capture failed");
		break;
	case ESeinDeterminismSessionFailureKind::CanonicalRootCheckpointExpired:
		KindText = TEXT("canonical root checkpoint expired incomplete");
		break;
	case ESeinDeterminismSessionFailureKind::ExecutionTopologyInvalidated:
		KindText = TEXT("execution topology invalidated");
		break;
	default:
		break;
	}
	UE_LOG(LogSeinNet, Error,
		TEXT("[DETERMINISM SESSION FAILED] turn=%d participant=%s reason=%s authority=%s. Simulation stopped."),
		Failure.Turn,
		*Failure.ParticipantID.ToCanonicalString(),
		KindText,
		bAuthoritative ? TEXT("coordinator") : TEXT("local-provisional"));

	if (bStateChanged)
	{
		OnDeterminismSessionFailure.Broadcast(
			DeterminismSessionFailure);
		OnDeterminismSessionFailureBP.Broadcast(
			DeterminismSessionFailure);
	}

	if (bNotifyPeers && IsLocalProtocolCoordinator()
		&& ActiveProtocolContext.IsValid())
	{
		for (const TWeakObjectPtr<ASeinNetRelay>& WeakRelay : Relays)
		{
			if (ASeinNetRelay* Relay = WeakRelay.Get())
			{
				Relay->Client_NotifyDeterminismSessionFailure(
					ActiveProtocolContext,
					DeterminismSessionFailure);
			}
		}
	}
}

void USeinNetSubsystem::ServerHandleConfigFingerprint(
	ASeinNetRelay* SourceRelay,
	const FSeinProtocolContext& Context,
	int32 Fingerprint,
	FGuid CommandProtocolDigest,
	FGuid MatchSettingsDigest,
	FGuid SimulationContentDigest)
{
	if (!IsServer() || !SourceRelay) return;
	if (!IsCurrentProtocolContext(Context, TEXT("ServerHandleConfigFingerprint"))) return;
	const FSeinNetworkParticipantID* ParticipantPtr = RelayToParticipant.Find(SourceRelay);
	if (!ParticipantPtr || !ParticipantPtr->IsValid())
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[CONFIG] ServerHandleConfigFingerprint: unmapped relay %s — dropping fingerprint."),
			*GetNameSafe(SourceRelay));
		return;
	}
	const FSeinNetworkParticipantID ParticipantID = *ParticipantPtr;
	if (!MatchSettingsDigest.IsValid()
		|| MatchSettingsDigest != ActiveProtocolContext.MatchSettingsDigest)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("[CONFIG] participant=%s match-settings digest MISMATCH — kicking."),
			*ParticipantID.ToCanonicalString());
		AcceptedConfigFingerprints.Remove(ParticipantID);
		FailBootstrapSession(
			TEXT("A frozen participant has incompatible match settings."),
			/*bNotifyPeers=*/true);
		SourceRelay->Client_NotifyKicked(
			TEXT("Match-settings mismatch: received bootstrap data cannot reproduce the host's canonical settings."));
		return;
	}
	FGuid ServerSimulationContentDigest;
	if (!ResolveLocalSimulationContentDigest(
			ServerSimulationContentDigest)
		|| ServerSimulationContentDigest
			!= ActiveProtocolContext.SimulationContentDigest)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("[CONFIG] authority simulation content no longer matches its active context; bootstrap halted."));
		FailBootstrapSession(
			TEXT("The coordinator simulation content changed during bootstrap."),
			/*bNotifyPeers=*/true);
		return;
	}
	if (!SimulationContentDigest.IsValid()
		|| SimulationContentDigest
			!= ServerSimulationContentDigest)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("[CONFIG] participant=%s simulation-content MISMATCH (client %s vs host %s) — kicking."),
			*ParticipantID.ToCanonicalString(),
			*SimulationContentDigest.ToString(EGuidFormats::Digits),
			*ServerSimulationContentDigest.ToString(EGuidFormats::Digits));
		AcceptedConfigFingerprints.Remove(ParticipantID);
		FailBootstrapSession(
			TEXT("A frozen participant has incompatible simulation content."),
			/*bNotifyPeers=*/true);
		SourceRelay->Client_NotifyKicked(
			TEXT("Simulation-content mismatch: generated gameplay content differs from the host."));
		return;
	}
	FGuid ServerCommandProtocolDigest;
	if (!ResolveLocalCommandProtocolDigest(ServerCommandProtocolDigest)
		|| ServerCommandProtocolDigest != ActiveProtocolContext.CommandProtocolDigest)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("[CONFIG] authority command protocol no longer matches its active context; bootstrap halted."));
		FailBootstrapSession(
			TEXT("The coordinator command protocol changed during bootstrap."),
			/*bNotifyPeers=*/true);
		return;
	}
	if (!CommandProtocolDigest.IsValid()
		|| CommandProtocolDigest != ServerCommandProtocolDigest)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("[CONFIG] participant=%s command-protocol MISMATCH (client %s vs host %s) — kicking."),
			*ParticipantID.ToCanonicalString(),
			*CommandProtocolDigest.ToString(EGuidFormats::Digits),
			*ServerCommandProtocolDigest.ToString(EGuidFormats::Digits));
		AcceptedConfigFingerprints.Remove(ParticipantID);
		FailBootstrapSession(
			TEXT("A frozen participant has an incompatible command protocol."),
			/*bNotifyPeers=*/true);
		SourceRelay->Client_NotifyKicked(
			TEXT("Command protocol mismatch: installed command schemas or authority policy differ from the host."));
		return;
	}
	if (!FindParticipantBinding(ParticipantID) || !IsParticipantConnected(ParticipantID))
	{
		UE_LOG(LogSeinNet, Verbose,
			TEXT("[CONFIG] ignoring fingerprint from non-connected/unknown participant=%s."),
			*ParticipantID.ToCanonicalString());
		return;
	}

	// Configuration is compatibility-only. Tick-zero readiness is proved
	// exclusively by the separate materialization receipt consensus.
	if (!IsConfigParityCheckEnabled())
	{
		TryDispatchLockstepSessionStart();
		return;
	}

	const UWorld* World = GetWorld();
	const USeinWorldSubsystem* WorldSub =
		World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	if (!WorldSub)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("[CONFIG] authority world unavailable; bootstrap halted."));
		if (bServerStartRequested)
		{
			FailBootstrapSession(
				TEXT("The coordinator world became unavailable during compatibility checks."),
				/*bNotifyPeers=*/true);
		}
		return;
	}
	const int32 ServerFingerprint = WorldSub->GetConfigFingerprint();

	if (Fingerprint == ServerFingerprint)
	{
		AcceptedConfigFingerprints.Add(ParticipantID, Fingerprint);
		UE_LOG(LogSeinNet, Log,
			TEXT("[CONFIG] participant=%s config parity OK (fingerprint 0x%08x)."),
			*ParticipantID.ToCanonicalString(), static_cast<uint32>(ServerFingerprint));
		TryDispatchLockstepSessionStart();
		return;
	}

	// Mismatch — the joining client's sim-affecting settings differ from the host's; it would desync
	// every tick. Reject it before the match starts, via the same disconnect + travel-to-menu path the
	// lobby kick uses. The disconnect drives the normal leave/logout cleanup (UnregisterRelay clears
	// RelayToSlot), so the slot frees for a correctly-configured re-join.
	UE_LOG(LogSeinNet, Error,
		TEXT("[CONFIG] participant=%s config MISMATCH (client 0x%08x vs host 0x%08x) — kicking."),
		*ParticipantID.ToCanonicalString(), static_cast<uint32>(Fingerprint),
		static_cast<uint32>(ServerFingerprint));
	AcceptedConfigFingerprints.Remove(ParticipantID);
	FailBootstrapSession(
		TEXT("A frozen participant has an incompatible simulation configuration."),
		/*bNotifyPeers=*/true);
	SourceRelay->Client_NotifyKicked(FString::Printf(
		TEXT("Config mismatch: your SeinARTS sim settings differ from the host's (fingerprint 0x%08x vs 0x%08x). Ensure both use the same DefaultGame.ini."),
		static_cast<uint32>(Fingerprint), static_cast<uint32>(ServerFingerprint)));
}

void USeinNetSubsystem::ServerHandleWorldStateRootReport(
	ASeinNetRelay* SourceRelay,
	const FSeinProtocolContext& Context,
	int32 Turn,
	FGuid WorldRoot)
{
	if (!IsServer() || !SourceRelay) return;
	if (!IsCurrentProtocolContext(
		Context, TEXT("ServerHandleWorldStateRootReport")))
	{
		return;
	}
	if (!WorldRoot.IsValid())
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[DETERMINISM] rejecting invalid world-state root from relay %s for turn %d."),
			*GetNameSafe(SourceRelay), Turn);
		return;
	}

	const FSeinNetworkParticipantID* ParticipantPtr = RelayToParticipant.Find(SourceRelay);
	if (!ParticipantPtr || !ParticipantPtr->IsValid())
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[DETERMINISM] ServerHandleWorldStateRootReport: unmapped relay %s — dropping root for turn %d."),
			*GetNameSafe(SourceRelay), Turn);
		return;
	}
	const FSeinNetworkParticipantID ParticipantID = *ParticipantPtr;
	const FSeinParticipantBinding* Binding = FindParticipantBinding(ParticipantID);
	if (!Binding || !Binding->bReportsWorldRoots
		|| !IsParticipantConnected(ParticipantID))
	{
		UE_LOG(LogSeinNet, Verbose,
			TEXT("[DETERMINISM] ignoring world-state root from unauthorized/non-connected participant=%s turn=%d."),
			*ParticipantID.ToCanonicalString(), Turn);
		return;
	}
	ServerHandleWorldStateRootReportForParticipant(
		ParticipantID, Turn, WorldRoot);
}

void USeinNetSubsystem::ServerHandleDeterminismSessionFailure(
	ASeinNetRelay* SourceRelay,
	const FSeinProtocolContext& Context,
	const FSeinDeterminismSessionFailure& Failure)
{
	if (!IsServer() || !SourceRelay) return;
	if (!IsCurrentProtocolContext(
		Context,
		TEXT("ServerHandleDeterminismSessionFailure")))
	{
		return;
	}

	const FSeinNetworkParticipantID* ParticipantPtr =
		RelayToParticipant.Find(SourceRelay);
	if (!ParticipantPtr || !ParticipantPtr->IsValid())
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[DETERMINISM] session failure from unmapped relay %s at turn=%d was rejected."),
			*GetNameSafe(SourceRelay),
			Failure.Turn);
		return;
	}
	if (!FindParticipantBinding(*ParticipantPtr)
		|| !IsParticipantConnected(*ParticipantPtr))
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[DETERMINISM] session failure from unauthorized/non-connected participant=%s at turn=%d was rejected."),
			*ParticipantPtr->ToCanonicalString(),
			Failure.Turn);
		return;
	}
	HandleAuthenticatedDeterminismSessionFailure(
		Context,
		*ParticipantPtr,
		Failure);
}

bool USeinNetSubsystem::HandleAuthenticatedDeterminismSessionFailure(
	const FSeinProtocolContext& Context,
	FSeinNetworkParticipantID AuthenticatedParticipantID,
	const FSeinDeterminismSessionFailure& Failure)
{
	if (!IsLocalProtocolCoordinator()
		|| !IsCurrentProtocolContext(
			Context,
			TEXT("HandleAuthenticatedDeterminismSessionFailure")))
	{
		return false;
	}

	FSeinDeterminismSessionFailure AuthenticatedFailure = Failure;
	AuthenticatedFailure.ParticipantID = AuthenticatedParticipantID;
	const FSeinParticipantBinding* Binding =
		FindParticipantBinding(AuthenticatedParticipantID);
	const bool bAuthorizedKind =
		Binding
		&& ((AuthenticatedFailure.Kind
				== ESeinDeterminismSessionFailureKind::
					CanonicalRootCaptureFailed
				&& Binding->bReportsWorldRoots)
			|| (AuthenticatedFailure.Kind
				== ESeinDeterminismSessionFailureKind::
					ExecutionTopologyInvalidated
				&& Binding->bSimulates));
	if (!AuthenticatedParticipantID.IsValid()
		|| !AuthenticatedFailure.IsValid()
		|| !AuthenticatedFailure.IsParticipantReportable()
		|| !bAuthorizedKind
		|| (AuthenticatedFailure.RequiresCanonicalRootCheckpoint()
			&& !IsDueWorldStateRootCheckpoint(AuthenticatedFailure.Turn))
		|| !IsDeterminismEvidenceTurnWithinProtocolWindow(
			AuthenticatedFailure.Turn,
			TEXT("HandleAuthenticatedDeterminismSessionFailure")))
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[DETERMINISM] malformed or unauthorized session failure kind=%d participant=%s turn=%d current=%d was rejected."),
			static_cast<int32>(AuthenticatedFailure.Kind),
			*AuthenticatedParticipantID.ToCanonicalString(),
			AuthenticatedFailure.Turn,
			GetCurrentTurn());
		return false;
	}

	if (AuthenticatedFailure.Kind
		== ESeinDeterminismSessionFailureKind::CanonicalRootCaptureFailed
		&& (AuthenticatedFailure.Turn
				<= CompletedWorldStateRootRejectionFloor
			|| CompletedWorldStateRootChecks.Contains(
				AuthenticatedFailure.Turn)))
	{
		UE_LOG(LogSeinNet, Verbose,
			TEXT("[DETERMINISM] late capture failure participant=%s turn=%d arrived after checkpoint completion."),
			*AuthenticatedParticipantID.ToCanonicalString(),
			AuthenticatedFailure.Turn);
		return false;
	}
	if (AuthenticatedFailure.Kind
		== ESeinDeterminismSessionFailureKind::CanonicalRootCaptureFailed)
	{
		if (const TMap<FSeinNetworkParticipantID, FGuid>* Existing =
			ServerWorldStateRootReports.Find(AuthenticatedFailure.Turn))
		{
			if (Existing->Contains(AuthenticatedParticipantID))
			{
				UE_LOG(LogSeinNet, Warning,
					TEXT("[DETERMINISM] participant=%s reported both a root and capture failure for turn=%d; first root retained."),
					*AuthenticatedParticipantID.ToCanonicalString(),
					AuthenticatedFailure.Turn);
				return false;
			}
		}
	}

	if (AuthenticatedFailure.Turn > GetCurrentTurn())
	{
		TArray<FSeinDeterminismSessionFailure>& Pending =
			PendingAuthenticatedDeterminismSessionFailures.FindOrAdd(
				AuthenticatedFailure.Turn);
		if (!Pending.Contains(AuthenticatedFailure))
		{
			Pending.Add(AuthenticatedFailure);
		}
		UE_LOG(LogSeinNet, Verbose,
			TEXT("[DETERMINISM] authenticated future session failure kind=%d participant=%s turn=%d buffered until the coordinator reaches that turn."),
			static_cast<int32>(AuthenticatedFailure.Kind),
			*AuthenticatedParticipantID.ToCanonicalString(),
			AuthenticatedFailure.Turn);
		return true;
	}

	EnterDeterminismSessionFailure(
		AuthenticatedFailure,
		/*bAuthoritative=*/true,
		/*bNotifyPeers=*/true);
	return true;
}

void USeinNetSubsystem::ApplyDueAuthenticatedDeterminismSessionFailuresThrough(
	int32 ThroughTurn)
{
	if (!IsLocalProtocolCoordinator()
		|| bDeterminismSessionFailureAuthoritative)
	{
		return;
	}

	TArray<int32> DueTurns;
	PendingAuthenticatedDeterminismSessionFailures.GetKeys(DueTurns);
	DueTurns.RemoveAll(
		[ThroughTurn](int32 Turn) { return Turn > ThroughTurn; });
	DueTurns.Sort();
	for (const int32 DueTurn : DueTurns)
	{
		TArray<FSeinDeterminismSessionFailure> Failures;
		if (!PendingAuthenticatedDeterminismSessionFailures
				.RemoveAndCopyValue(DueTurn, Failures)
			|| Failures.IsEmpty())
		{
			continue;
		}
		Failures.Sort([](
			const FSeinDeterminismSessionFailure& A,
			const FSeinDeterminismSessionFailure& B)
		{
			const uint8 AKind = static_cast<uint8>(A.Kind);
			const uint8 BKind = static_cast<uint8>(B.Kind);
			return AKind != BKind
				? AKind < BKind
				: A.ParticipantID.ToCanonicalString()
					< B.ParticipantID.ToCanonicalString();
		});
		EnterDeterminismSessionFailure(
			Failures[0],
			/*bAuthoritative=*/true,
			/*bNotifyPeers=*/true);
		return;
	}
}

void USeinNetSubsystem::HandleAuthoritativeDeterminismSessionFailure(
	const FSeinProtocolContext& Context,
	const FSeinDeterminismSessionFailure& Failure)
{
	if (!IsCurrentProtocolContext(
			Context,
			TEXT("HandleAuthoritativeDeterminismSessionFailure"))
		|| !Failure.IsValid()
		|| (Failure.RequiresCanonicalRootCheckpoint()
			&& !IsDueWorldStateRootCheckpoint(Failure.Turn))
		|| !FindParticipantBinding(Failure.ParticipantID))
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[DETERMINISM] invalid authoritative session-failure notification was rejected."));
		return;
	}
	EnterDeterminismSessionFailure(
		Failure,
		/*bAuthoritative=*/true,
		/*bNotifyPeers=*/false);
}

void USeinNetSubsystem::ServerHandleWorldStateRootReportForParticipant(
	FSeinNetworkParticipantID ParticipantID,
	int32 Turn,
	const FGuid& WorldRoot)
{
	if (!IsServer() || !ParticipantID.IsValid() || !WorldRoot.IsValid()
		|| DeterminismSessionFailure.IsValid())
	{
		return;
	}

	PruneProtocolState(GetCurrentTurn());
	if (DeterminismSessionFailure.IsValid()) return;
	if (!IsDeterminismEvidenceTurnWithinProtocolWindow(
		Turn, TEXT("ServerHandleWorldStateRootReportForParticipant")))
	{
		return;
	}

	if (Turn <= CompletedWorldStateRootRejectionFloor
		|| CompletedWorldStateRootChecks.Contains(Turn))
	{
		// Late report for an already-compared turn. Drop silently — the
		// alarm (if any) already fanned out to all peers, no need to redo.
		UE_LOG(LogSeinNet, Verbose,
			TEXT("[DETERMINISM] late world-root report from participant=%s for already-compared turn=%d — dropped."),
			*ParticipantID.ToCanonicalString(), Turn);
		return;
	}
	if (const TArray<FSeinDeterminismSessionFailure>* PendingFailures =
		PendingAuthenticatedDeterminismSessionFailures.Find(Turn))
	{
		if (PendingFailures->ContainsByPredicate(
			[ParticipantID](const FSeinDeterminismSessionFailure& Failure)
			{
				return Failure.Kind
						== ESeinDeterminismSessionFailureKind::
							CanonicalRootCaptureFailed
					&& Failure.ParticipantID == ParticipantID;
			}))
		{
			UE_LOG(LogSeinNet, Warning,
				TEXT("[DETERMINISM] participant=%s reported a root after its capture failure was authenticated for turn=%d; first failure retained."),
				*ParticipantID.ToCanonicalString(),
				Turn);
			return;
		}
	}

	const EFirstAcceptResult InsertResult =
		BufferWorldStateRootReportFirstWins(
			Turn, ParticipantID, WorldRoot);
	if (InsertResult == EFirstAcceptResult::IdenticalDuplicate)
	{
		UE_LOG(LogSeinNet, Verbose,
			TEXT("[DETERMINISM] identical duplicate world root participant=%s turn=%d — idempotent no-op."),
			*ParticipantID.ToCanonicalString(), Turn);
		return;
	}
	if (InsertResult == EFirstAcceptResult::ConflictingDuplicate)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("[DETERMINISM] conflicting duplicate world root participant=%s turn=%d old checkpoint retained; new root=%s rejected."),
			*ParticipantID.ToCanonicalString(), Turn,
			*WorldRoot.ToString(EGuidFormats::Digits));
		return;
	}

	const TMap<FSeinNetworkParticipantID, FGuid>& BufferForTurn =
		ServerWorldStateRootReports.FindChecked(Turn);
	TArray<FSeinNetworkParticipantID> ExpectedParticipants;
	GetExpectedWorldRootReporterParticipants(ExpectedParticipants);

	UE_LOG(LogSeinNet, Verbose,
		TEXT("[DETERMINISM] buffered world root participant=%s turn=%d root=%s (have %d/%d participants)."),
		*ParticipantID.ToCanonicalString(), Turn,
		*WorldRoot.ToString(EGuidFormats::Digits),
		BufferForTurn.Num(), ExpectedParticipants.Num());

	if (AreExpectedWorldRootReportsComplete(BufferForTurn))
	{
		ServerCompareWorldStateRootsForTurn(Turn);
	}
}

void USeinNetSubsystem::ServerCompareWorldStateRootsForTurn(int32 Turn)
{
	const TMap<FSeinNetworkParticipantID, FGuid>* Buffer =
		ServerWorldStateRootReports.Find(Turn);
	if (!Buffer || Buffer->IsEmpty()) return;
	TArray<FSeinNetworkParticipantID> ExpectedParticipants;
	GetExpectedWorldRootReporterParticipants(ExpectedParticipants);
	if (ExpectedParticipants.IsEmpty()
		|| !AreExpectedWorldRootReportsComplete(*Buffer))
	{
		return;
	}

	// Compare only live reporter processes. A dropped gameplay slot can still
	// have an earlier report buffered, but it is no longer a peer in this
	// checkpoint and must not satisfy or contaminate the live-peer comparison.
	const FGuid Reference = Buffer->FindChecked(ExpectedParticipants[0]);
	bool bAllAgree = true;
	for (const FSeinNetworkParticipantID ParticipantID : ExpectedParticipants)
	{
		if (Buffer->FindChecked(ParticipantID) != Reference)
		{
			bAllAgree = false;
			break;
		}
	}

	// Build a per-participant summary array for either the agreement log or the
	// fan-out payload — same shape, only the verbosity differs.
	TArray<FSeinParticipantWorldRootEntry> SortedRoots;
	SortedRoots.Reserve(ExpectedParticipants.Num());
	for (const FSeinNetworkParticipantID ParticipantID : ExpectedParticipants)
	{
		SortedRoots.Emplace(ParticipantID, Buffer->FindChecked(ParticipantID));
	}
	SortedRoots.Sort([](
		const FSeinParticipantWorldRootEntry& A,
		const FSeinParticipantWorldRootEntry& B)
	{
		return A.ParticipantID.ToCanonicalString() < B.ParticipantID.ToCanonicalString();
	});

	if (bAllAgree)
	{
		if (Turn > LatestSuccessfulWorldStateRootCheckTurn)
		{
			LatestSuccessfulWorldStateRootCheckTurn = Turn;
			LatestSuccessfulWorldStateRootReporterCount =
				ExpectedParticipants.Num();
		}
		// Most check-turns are silent (Verbose). Promote the first successful
		// comparison and every 5th thereafter so a short validation run proves
		// both reporters reached the coordinator without spamming long matches.
		const int32 Interval = GetDeterminismCheckIntervalTurns();
		const bool bPeriodicConfirm = CompletedWorldStateRootChecks.IsEmpty()
			|| ((Interval > 0) && ((Turn / Interval) % 5 == 0));
		if (bPeriodicConfirm)
		{
			UE_LOG(LogSeinNet, Log,
				TEXT("[DETERMINISM] turn=%d  %d/%d peers agree on world root %s — OK."),
				Turn, ExpectedParticipants.Num(), ExpectedParticipants.Num(),
				*Reference.ToString(EGuidFormats::Digits));
		}
		else
		{
			UE_LOG(LogSeinNet, Verbose,
				TEXT("[DETERMINISM] turn=%d  %d/%d peers agree on world root %s — OK."),
				Turn, ExpectedParticipants.Num(), ExpectedParticipants.Num(),
				*Reference.ToString(EGuidFormats::Digits));
		}
	}
	else
	{
		// Build a diagnostic line listing each participant's root for the log.
		FString Report;
		for (const FSeinParticipantWorldRootEntry& Entry : SortedRoots)
		{
			Report += FString::Printf(
				TEXT("[participant=%s root=%s] "),
				*Entry.ParticipantID.ToCanonicalString(),
				*Entry.WorldRoot.ToString(EGuidFormats::Digits));
		}
		UE_LOG(LogSeinNet, Error,
			TEXT("[DESYNC DETECTED] turn=%d — peer world roots diverge: %s. Fanning Client_NotifyDesync to %d relay(s) so every peer surfaces the on-screen alarm."),
			Turn, *Report, Relays.Num());

		// Fan to every relay (including host's own — RPC-loopback routes
		// to local process). Each peer's ClientHandleDesyncNotification
		// posts the red on-screen debug message + sets bDesyncDetected.
		for (const TWeakObjectPtr<ASeinNetRelay>& Wp : Relays)
		{
			if (ASeinNetRelay* Target = Wp.Get())
			{
				Target->Client_NotifyDesync(
					ActiveProtocolContext, Turn, SortedRoots);
			}
		}
	}

	ServerWorldStateRootReports.Remove(Turn);
	CompletedWorldStateRootChecks.Add(Turn);
}

void USeinNetSubsystem::ClientHandleDesyncNotification(
	const FSeinProtocolContext& Context,
	int32 Turn,
	const TArray<FSeinParticipantWorldRootEntry>& PeerRoots)
{
	if (!IsCurrentProtocolContext(Context, TEXT("ClientHandleDesyncNotification"))) return;
	bDesyncDetected = true;

	// Build a one-line summary listing every peer's root. Sort by participant for
	// readability — server already sorted, but be defensive.
	TArray<FSeinParticipantWorldRootEntry> Sorted = PeerRoots;
	Sorted.Sort([](
		const FSeinParticipantWorldRootEntry& A,
		const FSeinParticipantWorldRootEntry& B)
	{
		return A.ParticipantID.ToCanonicalString() < B.ParticipantID.ToCanonicalString();
	});

	FString PeerSummary;
	for (const FSeinParticipantWorldRootEntry& Entry : Sorted)
	{
		const TCHAR* MarkLocal = (Entry.ParticipantID == LocalParticipantID) ? TEXT("*") : TEXT("");
		PeerSummary += FString::Printf(
			TEXT("participant %s%s = %s  "),
			*Entry.ParticipantID.ToCanonicalString(), MarkLocal,
			*Entry.WorldRoot.ToString(EGuidFormats::Digits));
	}

	UE_LOG(LogSeinNet, Error,
		TEXT("[DESYNC] localParticipant=%s turn=%d peers: %s (* = this peer). Lockstep is broken — sim state has diverged."),
		*LocalParticipantID.ToCanonicalString(), Turn, *PeerSummary);

	// On-screen RED debug message. AddOnScreenDebugMessage with a stable Key
	// per-turn so successive desyncs don't all stack identically; we want
	// each unique (Turn) to surface once. -1 = persistent until cleared.
	if (GEngine)
	{
		const int32 KeyBase = 0x5E7DE57C; // arbitrary salt unique to Sein desync
		const uint64 KeyTurn = static_cast<uint64>(KeyBase) ^ static_cast<uint64>(Turn);
		const FString HeaderMsg = FString::Printf(
			TEXT("[SEINARTS DESYNC] turn=%d localParticipant=%s — sim state diverged across peers."),
			Turn, *LocalParticipantID.ToCanonicalString());
		GEngine->AddOnScreenDebugMessage(static_cast<int32>(KeyTurn & 0x7FFFFFFFull),
			30.0f, FColor::Red, HeaderMsg, /*bNewerOnTop=*/true,
			FVector2D(1.25f, 1.25f));

		// Also surface the per-slot summary as a second line so designers
		// can see who diverged at a glance without checking the log.
		const uint64 KeyTurnSummary = KeyTurn ^ 0x1ull;
		GEngine->AddOnScreenDebugMessage(static_cast<int32>(KeyTurnSummary & 0x7FFFFFFFull),
			30.0f, FColor(255, 100, 100), FString::Printf(TEXT("  Peers: %s"), *PeerSummary),
			/*bNewerOnTop=*/true, FVector2D(1.0f, 1.0f));
	}
}
