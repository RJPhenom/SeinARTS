/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinWorldSubsystem.cpp
 * @brief   Implementation of the core simulation subsystem.
 */

#include "Simulation/SeinWorldSubsystem.h"
#include "Simulation/SeinActorBridgeSubsystem.h"
#include "Actor/SeinActor.h"
#include "AI/SeinAIController.h"
#include "HAL/IConsoleManager.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Components/SeinIdentityComponent.h"
#include "Data/SeinFaction.h"
#include "Data/SeinWorldSnapshot.h"
#include "Settings/PluginSettings.h"
#include "Core/SeinSimContext.h"
#include "Abilities/SeinAbility.h"
#include "Abilities/SeinAbilityValidation.h"
#include "Abilities/SeinLatentActionManager.h"
#include "Abilities/SeinTargeterSpec.h"
#include "Components/SeinExtentsHelpers.h"
#include "Components/SeinAbilityComponent.h"
#include "Components/SeinActiveEffectsComponent.h"
#include "Components/SeinAttachmentSpec.h"
#include "Components/SeinBrokerMembershipData.h"
#include "Components/SeinCommandBrokerData.h"
#include "Components/SeinConstructionComponent.h"
#include "Components/SeinContainmentData.h"
#include "Components/SeinContainmentMemberData.h"
#include "Components/SeinProductionComponent.h"
#include "Components/SeinTransportSpec.h"
#include "Actor/SeinEntityComponent.h"
#include "Brokers/SeinCommandBrokerResolver.h"
#include "Brokers/SeinDefaultCommandBrokerResolver.h"
#include "Attributes/SeinModifier.h"
#include "Attributes/SeinAttributeResolver.h"
#include "Core/SeinSimContext.h"
#include "Effects/SeinEffect.h"
#include "Lib/SeinAbilityBPFL.h"   // for runtime Grant/Revoke during effect fan-out
#include "Lib/SeinResourceBPFL.h"
#include "Lib/SeinCommandBrokerBPFL.h"   // ComputeMultiBrokerAnchors (shared with the preview)
#include "Tags/SeinARTSGameplayTags.h"
#include "Containers/Ticker.h"
#include "StructUtils/InstancedStruct.h"

// Built-in systems
#include "Simulation/Systems/SeinEffectTickSystem.h"
#include "Simulation/Systems/SeinCooldownSystem.h"
#include "Simulation/Systems/SeinAbilityTickSystem.h"
#include "Simulation/Systems/SeinCommandBrokerSystem.h"
#include "Simulation/Systems/SeinProductionSystem.h"
#include "Simulation/Systems/SeinCollisionResolutionSystem.h"
#include "Simulation/Systems/SeinCollisionBroadphaseSystem.h"
#include "Simulation/Systems/SeinStateHashSystem.h"
#include "Simulation/Systems/SeinLifespanSystem.h"

#include "Brokers/SeinBrokerTypes.h"

#include "SeinARTSCoreEntityLog.h"  // LogSeinSim (module-shared)

void USeinWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	EntityPool.Initialize(1024);
	CurrentTick = 0;
	TimeAccumulator = 0.0f;
	bIsRunning = false;

	// Create latent action manager
	LatentActionManager = NewObject<USeinLatentActionManager>(this);

	// Read settings
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	FixedDeltaTimeSeconds = 1.0f / static_cast<float>(Settings->SimulationTickRate);

	// Collision broadphase — cell size 200 cm balances bucket fan-out cost
	// against query precision. Origin = world (0,0,0); levels offset from origin
	// just produce sparse buckets at high indices, no correctness impact.
	CollisionSpatialHash.Initialize(FFixedPoint::FromInt(200), FFixedVector::ZeroVector);

	// A collider spawning/dying changes the static set; flag the broadphase's
	// static tier for rebuild (cheap bool; the broadphase system rebuilds it on
	// the next PreTick if set). These delegates are multicast, so this composes
	// with other spawn/destroy listeners (e.g. extension subsystems).
	OnEntitySpawned.AddLambda([this](FSeinEntityHandle) { CollisionSpatialHash.MarkStaticDirty(); });
	OnEntityDestroyed.AddLambda([this](FSeinEntityHandle) { CollisionSpatialHash.MarkStaticDirty(); });

	// Register built-in systems
	BuiltInSystems.Add(new FSeinEffectTickSystem());
	BuiltInSystems.Add(new FSeinCollisionBroadphaseSystem());
	BuiltInSystems.Add(new FSeinCooldownSystem());
	BuiltInSystems.Add(new FSeinAbilityTickSystem());
	BuiltInSystems.Add(new FSeinProductionSystem());
	BuiltInSystems.Add(new FSeinLifespanSystem());
	BuiltInSystems.Add(new FSeinCommandBrokerSystem());
	BuiltInSystems.Add(new FSeinCollisionResolutionSystem());
	BuiltInSystems.Add(new FSeinStateHashSystem());

	for (ISeinSystem* Sys : BuiltInSystems)
	{
		RegisterSystem(Sys);
	}

	// Auto-register the Neutral player (ID 0). Entities that logically have "no
	// player" (neutral capture points, resource piles, scenario owners, environmental
	// hazards) resolve to this sentinel. See §1 Entities, "Ownership" decision.
	RegisterPlayer(FSeinPlayerID::Neutral(), FSeinFactionID(), /*TeamID=*/0);

	UE_LOG(LogSeinSim, Log, TEXT("SeinWorldSubsystem initialized (tick rate: %d Hz, %d systems)"),
		Settings->SimulationTickRate, Systems.Num());
}

void USeinWorldSubsystem::Deinitialize()
{
	StopSimulation();

	// Clean up component storages
	for (auto& Pair : ComponentStorages)
	{
		delete Pair.Value;
	}
	ComponentStorages.Empty();

	// Clean up built-in systems
	for (ISeinSystem* Sys : BuiltInSystems)
	{
		delete Sys;
	}
	BuiltInSystems.Empty();

	EntityPool.Reset();
	PlayerStates.Empty();
	Systems.Empty();
	PendingCommands.Clear();
	PendingDestroy.Empty();
	EntityTagIndex.Empty();
	NamedEntityRegistry.Empty();

	Super::Deinitialize();

	UE_LOG(LogSeinSim, Log, TEXT("SeinWorldSubsystem deinitialized"));
}

void USeinWorldSubsystem::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(InThis, Collector);

	USeinWorldSubsystem* Self = CastChecked<USeinWorldSubsystem>(InThis);
	for (auto& Pair : Self->ComponentStorages)
	{
		if (ISeinComponentStorage* Storage = Pair.Value)
		{
			Storage->CollectReferences(Collector, Self);
		}
	}
}

// ==================== Simulation Control ====================

void USeinWorldSubsystem::StartSimulation()
{
	if (bIsRunning)
	{
		UE_LOG(LogSeinSim, Warning, TEXT("Simulation already running"));
		return;
	}

	bIsRunning = true;
	TimeAccumulator = 0.0f;

	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &USeinWorldSubsystem::TickSimulation)
	);

	UE_LOG(LogSeinSim, Log, TEXT("Simulation started"));
}

void USeinWorldSubsystem::StopSimulation()
{
	if (!bIsRunning) return;

	bIsRunning = false;
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}

	UE_LOG(LogSeinSim, Log, TEXT("Simulation stopped at tick %d"), CurrentTick);
}

float USeinWorldSubsystem::GetInterpolationAlpha() const
{
	if (FixedDeltaTimeSeconds > 0.0f)
	{
		return FMath::Clamp(TimeAccumulator / FixedDeltaTimeSeconds, 0.0f, 1.0f);
	}
	return 0.0f;
}

bool USeinWorldSubsystem::TickSimulation(float DeltaTime)
{
	if (!bIsRunning) return false;

	// Paused sim: freeze the wall-clock accumulator (no drift-to-resume catch-up
	// spikes) and skip the tick-system loop entirely. Commands accumulate in
	// PendingCommands (Tactical pause per DESIGN §17 / §18) and flush on resume.
	// Visual events already buffered flush via actor bridge's own read loop.
	if (bSimPaused)
	{
		TimeAccumulator = 0.0f;
		return true;
	}

	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	const int32 MaxTicks = Settings->MaxTicksPerFrame;

	// TicksPerTurn: how many sim ticks make up one network turn. Derived from
	// the two settings; integer division so a misconfiguration doesn't yield
	// a fractional gate (better to round down and re-check sooner). Only
	// consulted when the lockstep resolver is bound (USeinNetSubsystem
	// registers it once the local slot is assigned).
	const int32 TicksPerTurn = (Settings->TurnRate > 0)
		? FMath::Max(1, Settings->SimulationTickRate / Settings->TurnRate)
		: 1;

	TimeAccumulator += DeltaTime;

	int32 TicksProcessed = 0;
	while (TimeAccumulator >= FixedDeltaTimeSeconds && TicksProcessed < MaxTicks)
	{
		const int32 NextTick = CurrentTick + 1;

		// Lockstep gate (Phase 2b). At each turn boundary, ask the network
		// layer whether the assembled turn for the upcoming turn is ready.
		// If not, stall — leave the accumulator alone so we retry next frame.
		// Resolver unbound (Standalone, networking disabled, or NetSubsystem
		// hasn't latched yet) = no gating, sim runs free.
		if (TurnReadyResolver.IsBound() && (NextTick % TicksPerTurn == 0))
		{
			const int32 NextTurn = NextTick / TicksPerTurn;
			if (!TurnReadyResolver.Execute(NextTurn))
			{
				// Stall — break out of the catch-up loop without consuming
				// the accumulator. Next frame's pump will retry. The "falling
				// behind" warning at the bottom is suppressed by the early
				// break since TicksProcessed < MaxTicks may still be true.
				break;
			}
			if (TurnConsumeNotifier.IsBound())
			{
				TurnConsumeNotifier.Execute(NextTurn);
			}
		}

		CurrentTick = NextTick;
		TimeAccumulator -= FixedDeltaTimeSeconds;
		TicksProcessed++;

		// Convert to deterministic fixed-point (from compile-time constant, not runtime float)
		FFixedPoint SimDeltaTime = FFixedPoint::One / FFixedPoint::FromInt(Settings->SimulationTickRate);

		TickSystems(SimDeltaTime);

#if !UE_BUILD_SHIPPING
		// Determinism verification: when the Log cvar is on, dump the sim
		// state hash each tick. Run two PIE clients (or two PIE sessions)
		// with this enabled and diff the logs — any divergence pinpoints
		// the tick where lockstep breaks. Gated off in shipping builds so
		// the hash walk doesn't cost production CPU.
		//
		// Two log levels:
		//   = 1: hash only — `StateHash[tick N] = 0xXXXX`. Compact, finds
		//        first divergent tick. Use for long sessions.
		//   = 2: hash + per-entity dump on tick 1, then hash-only on
		//        subsequent ticks. Tick 1 is the initial state — diffing
		//        two log files at tick 1 reveals what's structurally
		//        different about the starting world (entity IDs / owners
		//        / positions). Best for "spawns are wrong sometimes" hunts.
		//   = 3: hash + per-entity dump EVERY tick. Verbose; use only for
		//        narrow ranges or very early divergences.
		{
			static IConsoleVariable* CVarLog = IConsoleManager::Get().FindConsoleVariable(TEXT("Sein.Sim.StateHash.Log"));
			const int32 StateLogLevel = CVarLog ? CVarLog->GetInt() : 0;
			if (StateLogLevel != 0)
			{
				UE_LOG(LogSeinSim, Log, TEXT("StateHash[tick %d] = 0x%08x"),
					CurrentTick, static_cast<uint32>(ComputeStateHash()));

				const bool bDumpEntities = (StateLogLevel >= 3) || (StateLogLevel == 2 && CurrentTick == 1);
				if (bDumpEntities)
				{
					UE_LOG(LogSeinSim, Log, TEXT("StateHash[tick %d] entity dump  (active=%d):"),
						CurrentTick, EntityPool.GetActiveCount());

					// Walk every alive entity and print ID, owner, and the
					// raw fixed-point position. Raw int64 values diff
					// cleanly across log files (no float-to-string drift).
					EntityPool.ForEachEntity([this](FSeinEntityHandle Handle, const FSeinEntity& Entity)
					{
						const FSeinPlayerID Owner = EntityPool.GetOwner(Handle);
						const FFixedVector& L = Entity.Transform.Location;
						UE_LOG(LogSeinSim, Log,
							TEXT("  H(%d:%d)  slot=%u  pos=(%lld, %lld, %lld) [raw 32.32]"),
							Handle.Index, Handle.Generation, Owner.Value,
							L.X.Value, L.Y.Value, L.Z.Value);
					});
				}
			}
		}
#endif

		OnSimTickCompleted.Broadcast(CurrentTick);
	}

	if (TicksProcessed >= MaxTicks && TimeAccumulator > FixedDeltaTimeSeconds)
	{
		// Persistence-escalated log: most clamps are single-frame hitches
		// (PIE multi-window, GC, level streaming) — recovers on the next
		// frame. Only escalate to Warning when the sim has been clamping
		// CONTINUOUSLY for ≥1 second (i.e. genuinely can't keep up). Below
		// the threshold, stay at Verbose so the log isn't drowned.
		const double NowSec = FPlatformTime::Seconds();
		if (LastClampTime <= 0.0 || (NowSec - LastClampTime) > 0.25)
		{
			// Not been clamping recently — start a fresh clamp window.
			ClampWindowStartTime = NowSec;
			bClampWarningEmitted = false;
		}
		LastClampTime = NowSec;
		const double ClampDuration = NowSec - ClampWindowStartTime;
		if (ClampDuration < 1.0)
		{
			UE_LOG(LogSeinSim, Verbose, TEXT("Simulation falling behind (transient clamp). Accumulator clamped."));
		}
		else if (!bClampWarningEmitted || (NowSec - LastClampWarnTime) >= 2.0)
		{
			UE_LOG(LogSeinSim, Warning,
				TEXT("Simulation falling behind for %.1fs (continuous). Sim cannot keep up — drop SimulationTickRate or raise MaxTicksPerFrame in plugin settings."),
				ClampDuration);
			LastClampWarnTime = NowSec;
			bClampWarningEmitted = true;
		}
		TimeAccumulator = FixedDeltaTimeSeconds;
	}

	return true;
}

void USeinWorldSubsystem::TickSystems(FFixedPoint DeltaTime)
{
	SEIN_SIM_SCOPE

	SortSystemsIfNeeded();

	// Phase 1: PreTick — effects, cooldowns, resources
	for (ISeinSystem* System : Systems)
	{
		if (System->GetPhase() == ESeinTickPhase::PreTick)
		{
			System->Tick(DeltaTime, *this);
		}
	}

	// Advance the match state machine (DESIGN §18) — Starting-state pre-match
	// countdown transitions to Playing once the deadline tick is reached.
	TickMatchState();
	// Expire idle votes (DESIGN §18 voting primitive).
	TickVotes();

	// Phase 2: CommandProcessing — tick AI first (DESIGN §16) so emitted
	// commands accumulate in PendingCommands and get drained same-tick.
	TickAIControllers(DeltaTime);
	ProcessCommands();
	for (ISeinSystem* System : Systems)
	{
		if (System->GetPhase() == ESeinTickPhase::CommandProcessing)
		{
			System->Tick(DeltaTime, *this);
		}
	}

	// Phase 3: AbilityExecution — tick active abilities and latent actions
	if (LatentActionManager)
	{
		LatentActionManager->TickAll(DeltaTime, *this);
	}
	for (ISeinSystem* System : Systems)
	{
		if (System->GetPhase() == ESeinTickPhase::AbilityExecution)
		{
			System->Tick(DeltaTime, *this);
		}
	}

	// Phase 4: PostTick — cleanup, state hash
	ProcessDeferredDestroys();
	for (ISeinSystem* System : Systems)
	{
		if (System->GetPhase() == ESeinTickPhase::PostTick)
		{
			System->Tick(DeltaTime, *this);
		}
	}
}

// ==================== Command Processing ====================

void USeinWorldSubsystem::ProcessCommands()
{
	// Diagnostic trace of command-buffer drain — each tick that has pending
	// commands, dump the type list. Verbose so it stays out of default logs;
	// enable with `Log LogSeinSim Verbose` when debugging command-flow issues
	// (auto-activate-Build chain, observer command leak, broker dispatch, etc).
	if (PendingCommands.Num() > 0)
	{
		UE_LOG(LogSeinSim, Verbose, TEXT("ProcessCommands[tick %d]: %d commands pending at entry"),
			CurrentTick, PendingCommands.Num());
		for (const FSeinCommand& C : PendingCommands.GetCommands())
		{
			UE_LOG(LogSeinSim, Verbose,
				TEXT("  - type=%s entity=%s ability=%s target=%s player=%s"),
				*C.CommandType.ToString(), *C.EntityHandle.ToString(),
				*C.AbilityTag.ToString(), *C.TargetEntity.ToString(),
				*C.PlayerID.ToString());
		}
	}

	// Broadcast for debug tooling before processing (commands are still in the buffer)
	if (PendingCommands.Num() > 0)
	{
		OnCommandsProcessing.Broadcast(CurrentTick, PendingCommands.GetCommands());
	}

	// SNAPSHOT-AND-DRAIN: copy the pending commands into a local working set,
	// then clear the buffer BEFORE we iterate. Critical because ability
	// OnActivate BPs (and effects, and broker dispatches) can enqueue NEW
	// commands during processing — e.g. SA_PlaceBarracks's OnActivate calls
	// SeinIssueAbilityCommand to chain into Build. Without the snapshot, those
	// new enqueues either iterator-invalidate the live PendingCommands array
	// or get wiped by the final Clear(), depending on whether TArray realloc
	// fires. With the snapshot, mid-processing enqueues land in the now-empty
	// PendingCommands and get processed cleanly on the next sim tick.
	const TArray<FSeinCommand> CommandsThisTick = PendingCommands.GetCommands();
	PendingCommands.Clear();

	for (const FSeinCommand& Cmd : CommandsThisTick)
	{
		// Diagnostic trace — per-command handling. Verbose so it stays out of
		// default logs; pair with the entry-summary above when debugging.
		UE_LOG(LogSeinSim, Verbose,
			TEXT("ProcessCommands: handling type=%s entity=%s ability=%s"),
			*Cmd.CommandType.ToString(), *Cmd.EntityHandle.ToString(), *Cmd.AbilityTag.ToString());

		// Observer commands live under SeinARTS.Command.Type.Observer.* — logged
		// for replay but never processed by the sim.
		if (Cmd.IsObserverCommand())
		{
			continue;
		}

		// Convenience: fire a CommandRejected visual event with the original
		// command's type tag + a reason tag under SeinARTS.Command.Reject.* so UI
		// can distinguish "not enough resources" from "can't reach there" etc.
		auto RejectCommand = [this, &Cmd](FGameplayTag Reason = FGameplayTag())
		{
			EnqueueVisualEvent(FSeinVisualEvent::MakeCommandRejectedEvent(
				Cmd.PlayerID, Cmd.EntityHandle, Cmd.CommandType, Reason));
		};

		// Match flow commands (DESIGN §18). Handled before the spectator / pause
		// / starting-state filters so Resume / End / Restart can always unstick
		// the sim. No entity handle required — these target the subsystem itself.
		if (Cmd.CommandType == SeinARTSTags::Command_Type_StartMatch)
		{
			FSeinMatchSettings Settings;
			if (Cmd.Payload.IsValid() && Cmd.Payload.GetScriptStruct() == FSeinMatchSettings::StaticStruct())
			{
				Settings = Cmd.Payload.Get<FSeinMatchSettings>();
			}
			StartMatch(Settings);
			continue;
		}
		if (Cmd.CommandType == SeinARTSTags::Command_Type_EndMatch)
		{
			// Winner = PlayerID on the command; reason = AbilityTag slot.
			EndMatch(Cmd.PlayerID, Cmd.AbilityTag);
			continue;
		}
		if (Cmd.CommandType == SeinARTSTags::Command_Type_PauseMatchRequest)
		{
			// Tactical-style pause by default (commands queue + drain). Designer
			// wanting Hard-pause behavior can either: (1) call SetSimPaused
			// directly with bRejectCommandsWhilePaused=true from BP, or (2)
			// reject input at PC layer during pause so commands never reach
			// the wire.
			SetSimPaused(true, /*bRejectCommandsWhilePaused=*/false);
			continue;
		}
		if (Cmd.CommandType == SeinARTSTags::Command_Type_ResumeMatchRequest)
		{
			SetSimPaused(false);
			continue;
		}
		if (Cmd.CommandType == SeinARTSTags::Command_Type_ConcedeMatch)
		{
			// V1: concede immediately ends the match with no-winner. Designers
			// who want per-team victory / last-player-standing wire their own
			// scenario + call EndMatch with the right winner.
			EndMatch(FSeinPlayerID::Neutral(), SeinARTSTags::Command_Type_ConcedeMatch);
			continue;
		}
		if (Cmd.CommandType == SeinARTSTags::Command_Type_RestartMatch)
		{
			MatchState = ESeinMatchState::Lobby;
			EnqueueVisualEvent(FSeinVisualEvent::MakeMatchFlowEvent(ESeinVisualEventType::MatchEnded));
			continue;
		}

		// Vote commands (DESIGN §18 voting primitive).
		if (Cmd.CommandType == SeinARTSTags::Command_Type_StartVote)
		{
			FSeinStartVoteCommandPayload Pay;
			if (Cmd.Payload.IsValid() && Cmd.Payload.GetScriptStruct() == FSeinStartVoteCommandPayload::StaticStruct())
			{
				Pay = Cmd.Payload.Get<FSeinStartVoteCommandPayload>();
			}
			StartVote(Cmd.AbilityTag, Pay.Resolution, Pay.RequiredThreshold, Pay.ExpiresInTicks, Cmd.PlayerID);
			continue;
		}
		if (Cmd.CommandType == SeinARTSTags::Command_Type_CastVote)
		{
			CastVote(Cmd.AbilityTag, Cmd.PlayerID, Cmd.QueueIndex);
			continue;
		}

		// Global filters on sim-mutating commands.
		auto EmitFilterReject = [this, &Cmd](FGameplayTag Reason)
		{
			EnqueueVisualEvent(FSeinVisualEvent::MakeCommandRejectedEvent(
				Cmd.PlayerID, Cmd.EntityHandle, Cmd.CommandType, Reason));
		};
		if (const FSeinPlayerState* PS = GetPlayerState(Cmd.PlayerID))
		{
			if (PS->bIsSpectator)
			{
				EmitFilterReject(SeinARTSTags::Command_Reject_SpectatorForbidden);
				continue;
			}
		}
		if (bSimPausedHard)
		{
			EmitFilterReject(SeinARTSTags::Command_Reject_SimPaused);
			continue;
		}
		if (MatchState == ESeinMatchState::Starting)
		{
			EmitFilterReject(SeinARTSTags::Command_Reject_MatchStateInvalid);
			continue;
		}

		// Ping commands don't require a valid entity — emit a visual event and continue.
		if (Cmd.CommandType == SeinARTSTags::Command_Type_Ping)
		{
			EnqueueVisualEvent(FSeinVisualEvent::MakePingEvent(
				Cmd.PlayerID, Cmd.TargetLocation, Cmd.TargetEntity));
			continue;
		}

		// BrokerOrder targets a member list (Cmd.EntityList), not Cmd.EntityHandle.
		// Handle it before the single-entity validity guard below.
		if (Cmd.CommandType == SeinARTSTags::Command_Type_BrokerOrder)
		{
			if (Cmd.EntityList.Num() == 0)
			{
				RejectCommand(SeinARTSTags::Command_Reject_InvalidTarget);
				continue;
			}

			// Filter members by ownership (DESIGN §5 "wholly-single-player" invariant).
			// Strict single-owner: only members owned by `Cmd.PlayerID` survive.
			// Allied humans sharing command authority over the same units is NOT
			// supported by the framework; designer-authored multiplayer that
			// needs this would add a filter-override delegate (deferred — no
			// current consumer).
			TArray<FSeinEntityHandle> Filtered;
			Filtered.Reserve(Cmd.EntityList.Num());
			for (const FSeinEntityHandle& M : Cmd.EntityList)
			{
				if (!EntityPool.IsValid(M)) continue;
				const FSeinPlayerID MemberOwner = GetEntityOwner(M);
				if (MemberOwner == Cmd.PlayerID) { Filtered.Add(M); }
				// else: foreign member, drop silently (caller-side UX should have filtered).
			}
			if (Filtered.Num() == 0)
			{
				RejectCommand(SeinARTSTags::Command_Reject_InvalidTarget);
				continue;
			}

			// Extract the typed BrokerOrder payload — smart-resolution context +
			// drag-order endpoint. Missing payload is a malformed command.
			const FSeinBrokerOrderPayload* Payload = Cmd.Payload.GetPtr<FSeinBrokerOrderPayload>();
			if (!Payload)
			{
				RejectCommand(SeinARTSTags::Command_Reject_InvalidTarget);
				continue;
			}

			// Resolve the predetermined ability ONCE — both cost + footprint
			// gates need the same lookup (first capable member's instance of
			// the named ability). Smart-resolved orders (no PredeterminedAbilityTag)
			// leave this null and skip both gates; their per-member cost across
			// heterogeneous selections is not well-defined for "one click" semantics.
			const USeinAbility* PredeterminedAbility = nullptr;
			if (Payload->PredeterminedAbilityTag.IsValid())
			{
				for (const FSeinEntityHandle& M : Filtered)
				{
					const FSeinAbilityComponent* MemberAC = GetComponent<FSeinAbilityComponent>(M);
					if (!MemberAC) continue;
					if (USeinAbility* Found = MemberAC->FindAbilityByTag(*this, Payload->PredeterminedAbilityTag))
					{
						PredeterminedAbility = Found;
						break;
					}
				}
			}

			// Cost gate REMOVED (refactored 2026-05-07 alongside broker dispatch
			// going through ProcessCommands' full gate). Previously the broker-
			// order branch deducted cost up front for targeter-originated orders
			// and the per-member dispatch in SeinCommandBrokerSystem skipped
			// cost. Now per-member dispatch enqueues ActivateAbility commands
			// that go through the full activation gate (including cost deduct)
			// — keeping the cost gate here would double-charge.
			//
			// Affordability pre-check at click time (so the player can't
			// over-issue) belongs at the UI layer (HUD button greying), not
			// here. The activation gate rejects with Unaffordable if the
			// player runs out by the time the per-member command processes.

			// Footprint placement gate — only meaningful for targeter-originated
			// orders (have PredeterminedAbilityTag + TargeterPoints). Reuses
			// the PredeterminedAbility resolved above. Skip silently if any
			// precondition fails: no predetermined ability, no points, no
			// capable member, no spec, no extents on the building. This keeps
			// the gate opt-in and additive — abilities that don't set
			// bRequiresFreeFootprint are unaffected.
			if (PredeterminedAbility && Payload->TargeterPoints.Num() > 0
				&& FootprintPlacementResolver.IsBound()
				&& PredeterminedAbility->bRequiresFreeFootprint)
			{
				// Pull the spec to get BuildingClass, then read extents from CDO.
				// Only USeinPointFacingTargeterSpec carries a BuildingClass; other
				// specs silently bypass.
				const USeinPointFacingTargeterSpec* PFSpec =
					Cast<USeinPointFacingTargeterSpec>(PredeterminedAbility->TargeterSpec);
				const FSeinExtentsShape* Shape = nullptr;
				if (PFSpec && !PFSpec->BuildingClass.IsNull())
				{
					UClass* BuildingClass = PFSpec->BuildingClass.LoadSynchronous();
					Shape = SeinExtentsHelpers::GetPrimaryExtentsShape(BuildingClass);
				}

				if (Shape)
				{
					const FSeinTargeterPoint& First = Payload->TargeterPoints[0];

					// Yaw degrees from RotationStep × RotationStepDegrees on the spec.
					const FFixedPoint YawDeg = FFixedPoint::FromInt(
						static_cast<int32>(First.RotationStep) * PFSpec->RotationStepDegrees);

					// AgentLayerMask: blocking-perspective bit. We don't have an
					// agent here (placing a building, not pathing through one).
					// Use 0xFF "block on any layer" so any blocker rejects placement.
					const uint8 AgentLayerMask = 0xFF;

					if (!FootprintPlacementResolver.Execute(First.Location, YawDeg, *Shape, AgentLayerMask))
					{
						UE_LOG(LogSeinSim, Warning,
							TEXT("BrokerOrder[%s]: footprint blocked at (%.1f, %.1f, %.1f) yaw=%.1f"),
							*Payload->PredeterminedAbilityTag.ToString(),
							First.Location.X.ToFloat(), First.Location.Y.ToFloat(), First.Location.Z.ToFloat(),
							YawDeg.ToFloat());
						RejectCommand(SeinARTSTags::Command_Reject_FootprintBlocked);
						continue;
					}
				}
				// Else: ability requires footprint check but we couldn't resolve
				// a shape — log Verbose and let the order through. Designer
				// either forgot to set BuildingClass or the BP has no extents
				// component; failing closed here would block legitimate-looking
				// orders during authoring iteration.
				else
				{
					UE_LOG(LogSeinSim, Verbose,
						TEXT("BrokerOrder[%s]: bRequiresFreeFootprint set but no shape resolved (spec or BuildingClass missing); skipping gate."),
						*Payload->PredeterminedAbilityTag.ToString());
				}
			}

			FSeinBrokerQueuedOrder Order;
			Order.Context = Payload->CommandContext;
			Order.TargetEntity = Cmd.TargetEntity;
			Order.TargetLocation = Cmd.TargetLocation;
			Order.FormationEnd = Payload->FormationEnd;
			Order.GuidePoints = Payload->GuidePoints;
			Order.FormationTag = Payload->FormationTag;
			Order.TargeterPoints = Payload->TargeterPoints;
			Order.PredeterminedAbilityTag = Payload->PredeterminedAbilityTag;

			// Snap pure location-targets (no entity click) to the nearest
			// nav-passable cell. Without this, a click that lands on a non-
			// walkable surface (wall side, building roof, water mesh — anything
			// the cursor trace stops on whose footprint is bake-blocked) routes
			// downstream as TargetLocation-on-a-blocked-cell. Per-member FindPath
			// then either rejects the command (bRequiresPathableTarget) or
			// returns a degenerate path, producing the "click on wall, nobody
			// moves" failure mode. Snap to nearest pathable matches the standard
			// RTS contract: a move order always commits to *somewhere* reachable.
			//
			// Entity-targeted orders skip the snap — the resolver dispatches
			// against the entity (Attack X, Repair Y), TargetLocation is only
			// a fallback for resolvers that need a click anchor, and snapping
			// it away from the clicked entity would be actively wrong.
			//
			// Sim-side rather than PC-side so AI-issued orders get the same
			// safety net, and so the snap is deterministic (same nav data on
			// every client). Bypass on no-nav (tests / nav-less games).
			if (!Cmd.TargetEntity.IsValid() && NavProjectResolver.IsBound())
			{
				FFixedVector Projected;
				if (NavProjectResolver.Execute(Order.TargetLocation, Projected))
				{
					Order.TargetLocation = Projected;
				}
				// Formation-drag endpoint gets the same treatment so the
				// formation line's far end doesn't land on a wall.
				if (!Order.FormationEnd.IsNearlyZero())
				{
					FFixedVector ProjectedEnd;
					if (NavProjectResolver.Execute(Order.FormationEnd, ProjectedEnd))
					{
						Order.FormationEnd = ProjectedEnd;
					}
				}
				// Gesture guide path: project each point so the formation lays out on
				// reachable cells (same contract as TargetLocation / FormationEnd).
				for (FFixedVector& GuidePoint : Order.GuidePoints)
				{
					FFixedVector ProjectedGuide;
					if (NavProjectResolver.Execute(GuidePoint, ProjectedGuide))
					{
						GuidePoint = ProjectedGuide;
					}
				}
			}

			// Persistent-broker partitioning: any selected entity that ITSELF carries
			// FSeinCommandBrokerData is a persistent broker (squad / scenario-owned).
			// Append the order directly to its OrderQueue rather than wrapping it
			// in an ephemeral broker — persistent brokers are sub-brokers from the
			// player POV (one entity), and wrapping would create a meaningless
			// top-level broker whose only "member" is itself a broker. Entities
			// without FSeinCommandBrokerData flow through the existing ephemeral path.
			TArray<FSeinEntityHandle> PersistentBrokerEntities;
			TArray<FSeinEntityHandle> EphemeralEntities;
			PersistentBrokerEntities.Reserve(Filtered.Num());
			EphemeralEntities.Reserve(Filtered.Num());
			for (const FSeinEntityHandle& M : Filtered)
			{
				if (HasComponent<FSeinCommandBrokerData>(M)) { PersistentBrokerEntities.Add(M); }
				else                                          { EphemeralEntities.Add(M); }
			}

			// Persistent broker path — route the order to each broker's queue.
			// Each persistent broker gets its OWN copy of the Order (so per-broker
			// mutations to TargetMembers don't bleed across brokers). Order applies
			// to all of that broker's members (TargetMembers left empty = all).
			//
			// Replace vs. append by `Cmd.bQueueCommand`:
			//  - bQueueCommand == false (default right-click): clear pending orders
			//    AND explicitly cancel each member's active ability so in-flight
			//    work stops unconditionally (doesn't rely on tag-pair self-cancel
			//    pattern at the ability level).
			//  - bQueueCommand == true (shift-click): append, executing the new
			//    order after the current one finishes.
			//
			// Multi-broker lateral spacing — when more than one persistent broker
			// is selected, each gets its own laterally-offset anchor so they march
			// side-by-side instead of stacking on the click point. Spacing is
			// computed from per-broker FormationWidth along the move direction's
			// right axis:
			//   gap_between_edges ≈ (avg broker width) / 2
			//   anchor[i] = click + RightAxis * ((cursor + width[i]/2) - half_total)
			// Single persistent broker → no offset, anchor = click point as before.
			if (PersistentBrokerEntities.Num() > 0)
			{
				// Per-broker laterally-offset anchors so multiple squads march
				// side-by-side instead of stacking on the click point. Extracted to a
				// shared helper (USeinCommandBrokerBPFL::ComputeMultiBrokerAnchors) so
				// the destination preview computes byte-identical anchors so preview ==
				// commit for multi-squad moves. Single broker = click point.
				const TArray<FFixedVector> BrokerAnchors =
					USeinCommandBrokerBPFL::ComputeMultiBrokerAnchors(
						*this, PersistentBrokerEntities, Order.TargetLocation, Order.GuidePoints);

				for (int32 i = 0; i < PersistentBrokerEntities.Num(); ++i)
				{
					const FSeinEntityHandle BrokerHandle = PersistentBrokerEntities[i];
					FSeinCommandBrokerData* PersistentBroker = GetComponent<FSeinCommandBrokerData>(BrokerHandle);
					if (!PersistentBroker) { continue; }

					const FFixedVector BrokerAnchor = BrokerAnchors.IsValidIndex(i)
						? BrokerAnchors[i] : Order.TargetLocation;

					if (!Cmd.bQueueCommand)
					{
						for (const FSeinEntityHandle& Member : PersistentBroker->Members)
						{
							FSeinAbilityComponent* MemberAC = GetComponent<FSeinAbilityComponent>(Member);
							if (!MemberAC) continue;
							if (USeinAbility* Active = MemberAC->GetActiveAbility(*this))
							{
								if (Active->bIsActive)
								{
									Active->CancelAbility();
								}
								MemberAC->ActiveAbilityID = INDEX_NONE;
							}
						}
						// Reset clears every queued order (per-order bIsExecuting
						// flags travel with the orders themselves under the
						// Option C parallelism model).
						PersistentBroker->OrderQueue.Reset();
						PersistentBroker->CurrentOrderContext = FGameplayTagContainer();
					}

					FSeinBrokerQueuedOrder MyOrder = Order;
					MyOrder.TargetLocation = BrokerAnchor;
					PersistentBroker->OrderQueue.Add(MyOrder);
				}
			}

			// Ephemeral-units path — original ephemeral-broker logic, applied only
			// to entities without persistent brokers. If the selection was all
			// persistent brokers, this is empty and the block no-ops.
			if (EphemeralEntities.Num() > 0)
			{
				FSeinEntityHandle ExistingBroker;
				if (Cmd.bQueueCommand)
				{
					ExistingBroker = FindSharedBroker(EphemeralEntities);
				}
				if (ExistingBroker.IsValid())
				{
					if (FSeinCommandBrokerData* Broker = GetComponent<FSeinCommandBrokerData>(ExistingBroker))
					{
						// Strict subset? Order is TargetMembers-scoped. Full match? All-members.
						if (EphemeralEntities.Num() < Broker->Members.Num())
						{
							Order.TargetMembers = EphemeralEntities;
						}
						Broker->OrderQueue.Add(Order);
					}
				}
				else
				{
					CreateBrokerForMembers(EphemeralEntities, Cmd.PlayerID, Order);
				}
			}
			continue;
		}

		if (!EntityPool.IsValid(Cmd.EntityHandle))
		{
			UE_LOG(LogSeinSim, Verbose,
				TEXT("ProcessCommands[%s]: entity %s is not valid in pool — silent rejection ⇒ logged"),
				*Cmd.CommandType.ToString(), *Cmd.EntityHandle.ToString());
			RejectCommand(SeinARTSTags::Command_Reject_MissingComponent);
			continue;
		}

		if (Cmd.CommandType == SeinARTSTags::Command_Type_ActivateAbility)
		{
			FSeinAbilityComponent* AbilityComp = GetComponent<FSeinAbilityComponent>(Cmd.EntityHandle);
			if (!AbilityComp)
			{
				UE_LOG(LogSeinSim, Verbose, TEXT("ActivateAbility[%s]: entity %s has no FSeinAbilityComponent"),
					*Cmd.AbilityTag.ToString(), *Cmd.EntityHandle.ToString());
				RejectCommand(SeinARTSTags::Command_Reject_MissingComponent);
				continue;
			}

			USeinAbility* Ability = AbilityComp->FindAbilityByTag(*this, Cmd.AbilityTag);
			if (!Ability)
			{
				UE_LOG(LogSeinSim, Verbose, TEXT("ActivateAbility[%s]: entity %s has no ability with that tag (%d instances from %d granted classes)"),
					*Cmd.AbilityTag.ToString(), *Cmd.EntityHandle.ToString(),
					AbilityComp->AbilityInstanceIDs.Num(), AbilityComp->GrantedAbilities.Num());
				RejectCommand(SeinARTSTags::Command_Reject_InvalidTarget);
				continue;
			}
			// Full activation ordering per DESIGN §7:
			//   1. Cooldown check
			//   2a. BlockedTags vs entity tags
			//   2b. RequiredEntityTags vs entity tags
			//   2c. RequiredPlayerTags vs player tags
			//   3. Declarative target validation (range / tags / LOS)
			//   4. CanActivate BP escape hatch
			//   5. Affordability check
			//   6. Deduct cost
			//   7. Cancel-others via CancelAbilitiesWithTag
			//   8. Record deducted cost snapshot + USeinAbility::ActivateAbility
			//      (which handles cooldown start + GrantedTags grant + OnActivate)

			// 1. Cooldown
			if (Ability->IsOnCooldown())
			{
				UE_LOG(LogSeinSim, Verbose, TEXT("ActivateAbility[%s]: on cooldown"), *Cmd.AbilityTag.ToString());
				RejectCommand(SeinARTSTags::Command_Reject_OnCooldown);
				continue;
			}

			// 2a. BlockedTags — entity must NOT have any of these.
			if (!Ability->BlockedTags.IsEmpty())
			{
				if (HasAnyTag(Cmd.EntityHandle, Ability->BlockedTags))
				{
					UE_LOG(LogSeinSim, Verbose, TEXT("ActivateAbility[%s]: blocked by entity tags"),
						*Cmd.AbilityTag.ToString());
					RejectCommand(SeinARTSTags::Command_Reject_BlockedByTag);
					continue;
				}
			}

			// 2b. RequiredEntityTags — entity must have ALL of these.
			// Use for entity-state preconditions: a Heal ability that requires
			// the caster to be `SeinARTS.State.Damaged`, etc. Reject silently
			// if the entity has no matching tags.
			if (!Ability->RequiredEntityTags.IsEmpty())
			{
				if (!HasAllTags(Cmd.EntityHandle, Ability->RequiredEntityTags))
				{
					UE_LOG(LogSeinSim, Verbose,
						TEXT("ActivateAbility[%s]: missing required entity tags"),
						*Cmd.AbilityTag.ToString());
					RejectCommand(SeinARTSTags::Command_Reject_BlockedByTag);
					continue;
				}
			}

			// 2c. RequiredPlayerTags — owning player must have ALL listed tags.
			// This is where tech prereqs land for production abilities (e.g.
			// `SeinARTS.Tech.VehicleAccess` on `SA_TrainTank`). Player tags are
			// refcounted via USeinWorldSubsystem::GrantPlayerTag (DESIGN §10).
			if (!Ability->RequiredPlayerTags.IsEmpty())
			{
				const FSeinPlayerState* PS = GetPlayerState(Cmd.PlayerID);
				if (!PS || !PS->HasAllPlayerTags(Ability->RequiredPlayerTags))
				{
					UE_LOG(LogSeinSim, Verbose,
						TEXT("ActivateAbility[%s]: blocked by missing player tags"),
						*Cmd.AbilityTag.ToString());
					RejectCommand(SeinARTSTags::Command_Reject_BlockedByTag);
					continue;
				}
			}

			// 3. Declarative target validation (range / tags / LOS)
			const ESeinAbilityTargetValidationResult ValidationResult = FSeinAbilityValidation::ValidateTarget(
				*Ability, Cmd.EntityHandle, Cmd.TargetEntity, Cmd.TargetLocation, *this);
			if (ValidationResult != ESeinAbilityTargetValidationResult::Valid)
			{
				// OutOfRange + AutoMoveThen: prepend an internal Move order on a
				// single-member broker, then queue the original ability behind it.
				// The Move targets the target's current position (or Cmd.TargetLocation
				// if no target entity). Cost is deducted upfront — the player's click
				// commits at AutoMoveThen-acceptance time (classic RTS UX). Broker
				// dispatch skips cost re-deduction per SeinCommandBrokerSystem.
				if (ValidationResult == ESeinAbilityTargetValidationResult::OutOfRange &&
					Ability->OutOfRangeBehavior == ESeinOutOfRangeBehavior::AutoMoveThen)
				{
					// Member must have a Move ability to fulfill the prefix. If not,
					// there's nothing to auto-move with — reject as OutOfRange.
					// Move-ability lookup is via the bIsMoveAbility flag designer-set
					// on the move ability (no hardcoded tag).
					const USeinAbility* MoveAbility = AbilityComp->FindMoveAbility(*this);
					if (!MoveAbility || !MoveAbility->AbilityTag.IsValid())
					{
						UE_LOG(LogSeinSim, Verbose,
							TEXT("ActivateAbility[%s]: AutoMoveThen requested but entity has no ability flagged as Move (bIsMoveAbility) with a valid tag; rejecting"),
							*Cmd.AbilityTag.ToString());
						RejectCommand(SeinARTSTags::Command_Reject_OutOfRange);
						continue;
					}
					const FGameplayTag MoveAbilityTag = MoveAbility->AbilityTag;

					// Affordability check stays — AutoMoveThen is an "accept this
					// command" path, not a free pass on cost gates. Catalog-aware
					// split mirrors the direct-activation path: only AtEnqueue
					// deducts here; the queue (if this is a production ability
					// dispatched via AutoMoveThen → broker followup) handles
					// AtCompletion at spawn time.
					FSeinResourceCost AutoMoveAtEnqueue;
					FSeinResourceCost AutoMoveAtCompletionUnused;
					USeinResourceBPFL::SeinSplitCostByCatalog(this, Ability->ResourceCost, AutoMoveAtEnqueue, AutoMoveAtCompletionUnused);
					if (!USeinResourceBPFL::SeinCanAfford(this, Cmd.PlayerID, AutoMoveAtEnqueue))
					{
						UE_LOG(LogSeinSim, Verbose,
							TEXT("ActivateAbility[%s]: AutoMoveThen rejected — unaffordable"),
							*Cmd.AbilityTag.ToString());
						RejectCommand(SeinARTSTags::Command_Reject_Unaffordable);
						continue;
					}
					USeinResourceBPFL::SeinDeduct(this, Cmd.PlayerID, AutoMoveAtEnqueue);

					// Resolve the Move destination. If the command targets an entity,
					// stand at the EDGE of its footprint (CoH / AoE-style — units
					// build / repair / attack on the footprint perimeter, not the
					// center). Falls back to the entity center when the target has
					// no extents data, and to the raw TargetLocation when there's
					// no target entity at all.
					FFixedVector MoveDest = Cmd.TargetLocation;
					if (Cmd.TargetEntity.IsValid())
					{
						if (const FSeinEntity* Tgt = GetEntity(Cmd.TargetEntity))
						{
							const FFixedVector TargetCenter = Tgt->Transform.GetLocation();
							const FSeinEntity* MoverEntity = GetEntity(Cmd.EntityHandle);
							const FFixedVector ApproachFrom = MoverEntity ? MoverEntity->Transform.GetLocation() : TargetCenter;

							// Use the target's runtime extents if present. First-shape
							// only — compound bodies (turret + chassis) take the chassis
							// shape's bounding radius, which is usually the larger one
							// anyway. Falls back to TargetCenter if no extents.
							const FSeinExtentsComponent* TargetExtents = GetComponent<FSeinExtentsComponent>(Cmd.TargetEntity);
							const FSeinExtentsShape* TargetShape = (TargetExtents && TargetExtents->Shapes.Num() > 0)
								? &TargetExtents->Shapes[0]
								: nullptr;

							MoveDest = SeinExtentsHelpers::ComputeStandoffPoint(
								TargetShape, Tgt->Transform, ApproachFrom);
						}
					}

					UE_LOG(LogSeinSim, Verbose,
						TEXT("ActivateAbility[%s]: AutoMoveThen — out of range, queueing Move + Build on member %s targeting (%.1f, %.1f, %.1f)"),
						*Cmd.AbilityTag.ToString(), *Cmd.EntityHandle.ToString(),
						MoveDest.X.ToFloat(), MoveDest.Y.ToFloat(), MoveDest.Z.ToFloat());

					const TArray<FSeinEntityHandle> SingleMember = { Cmd.EntityHandle };

					// Move-prefix + follow-up targeted at just this member. Both
					// orders carry the one-broker-per-member invariant and the
					// subset-targeting machinery so non-target members (if this
					// folds into an existing multi-member broker) stay untouched.
					//
					// CRITICAL: set `PredeterminedAbilityTag` (NOT just `Context`)
					// so the default resolver's first-capable-member fast-path
					// dispatches the ability directly. Context-only entries
					// require the member's DefaultCommands table to map the
					// context tag to an ability — designers don't author such
					// mappings for framework-internal AutoMoveThen, so without
					// the predetermined tag the resolver silently no-ops and the
					// member stays idle.
					FSeinBrokerQueuedOrder MovePrefix;
					MovePrefix.Context.AddTag(MoveAbilityTag);
					MovePrefix.PredeterminedAbilityTag = MoveAbilityTag;
					MovePrefix.TargetLocation = MoveDest;
					MovePrefix.TargetMembers = SingleMember;
					MovePrefix.bIsInternalPrefix = true;

					FSeinBrokerQueuedOrder Followup;
					Followup.Context.AddTag(Cmd.AbilityTag);
					Followup.PredeterminedAbilityTag = Cmd.AbilityTag;
					Followup.TargetEntity = Cmd.TargetEntity;
					Followup.TargetLocation = Cmd.TargetLocation;
					Followup.TargetMembers = SingleMember;
					Followup.bIsInternalPrefix = true;

					// Prefer the member's existing broker if it has one — inject the
					// [Move, Follow-up] pair right after the currently-executing
					// order. One-broker-per-member preserved, shift-queue on the
					// existing broker preserved, non-target members unaffected.
					FSeinEntityHandle ExistingBroker;
					if (const FSeinBrokerMembershipData* Memb = GetComponent<FSeinBrokerMembershipData>(Cmd.EntityHandle))
					{
						ExistingBroker = Memb->CurrentBrokerHandle;
					}
					if (ExistingBroker.IsValid() && EntityPool.IsValid(ExistingBroker))
					{
						if (FSeinCommandBrokerData* Broker = GetComponent<FSeinCommandBrokerData>(ExistingBroker))
						{
							// Insert position: right after the LAST currently-executing
							// order. Under per-order parallelism multiple orders may be
							// executing concurrently; we want the AutoMoveThen pair to
							// be the next-priority candidate AFTER everything that's
							// currently in flight, but BEFORE any other queued
							// non-executing orders. Counts forward through the queue
							// so the result is `last_executing_index + 1`. With no
							// executing orders, InsertAt stays at 0 (front).
							int32 InsertAt = 0;
							for (int32 q = 0; q < Broker->OrderQueue.Num(); ++q)
							{
								if (Broker->OrderQueue[q].bIsExecuting)
								{
									InsertAt = q + 1;
								}
							}
							// Insert Followup first, then MovePrefix at the same index
							// — MovePrefix pushes Followup back one slot. Net result:
							// [..., executing..., MovePrefix, Followup, queued...].
							// Move runs first, Followup runs when Move completes (the
							// member lock keeps them serialized for this same member).
							Broker->OrderQueue.Insert(Followup, FMath::Min(InsertAt, Broker->OrderQueue.Num()));
							Broker->OrderQueue.Insert(MovePrefix, FMath::Min(InsertAt, Broker->OrderQueue.Num()));
							UE_LOG(LogSeinSim, Verbose,
								TEXT("AutoMoveThen[%s]: injected Move+Followup into existing broker %s at index %d (queue depth now=%d)"),
								*Cmd.AbilityTag.ToString(), *ExistingBroker.ToString(),
								InsertAt, Broker->OrderQueue.Num());
							continue;
						}
					}

					// No existing broker — spawn one for this single member with the
					// Move+Follow-up queued. CreateBrokerForMembers takes a first
					// order and pre-queues it; append follow-up after.
					FSeinEntityHandle BrokerHandle = CreateBrokerForMembers(SingleMember, Cmd.PlayerID, MovePrefix);
					if (BrokerHandle.IsValid())
					{
						if (FSeinCommandBrokerData* Broker = GetComponent<FSeinCommandBrokerData>(BrokerHandle))
						{
							Broker->OrderQueue.Add(Followup);
							UE_LOG(LogSeinSim, Verbose,
								TEXT("AutoMoveThen[%s]: created new broker %s with Move+Followup (queue depth=%d)"),
								*Cmd.AbilityTag.ToString(), *BrokerHandle.ToString(), Broker->OrderQueue.Num());
						}
					}
					else
					{
						UE_LOG(LogSeinSim, Verbose,
							TEXT("AutoMoveThen[%s]: CreateBrokerForMembers returned invalid handle — silent failure"),
							*Cmd.AbilityTag.ToString());
					}
					continue;
				}
				UE_LOG(LogSeinSim, Verbose, TEXT("ActivateAbility[%s]: target validation failed (%d)"),
					*Cmd.AbilityTag.ToString(), static_cast<int32>(ValidationResult));
				FGameplayTag ReasonTag;
				switch (ValidationResult)
				{
				case ESeinAbilityTargetValidationResult::OutOfRange:     ReasonTag = SeinARTSTags::Command_Reject_OutOfRange; break;
				case ESeinAbilityTargetValidationResult::NoLineOfSight:  ReasonTag = SeinARTSTags::Command_Reject_NoLineOfSight; break;
				default:                                                  ReasonTag = SeinARTSTags::Command_Reject_InvalidTarget; break;
				}
				RejectCommand(ReasonTag);
				continue;
			}

			// 3b. Pathable-target gate — if enabled on this ability, consult the
			// nav-registered resolver for reachability. From/To stay FFixedVector
			// end-to-end; no lossy float round-trip on the sim path.
			if (Ability->bRequiresPathableTarget && PathableTargetResolver.IsBound())
			{
				const FSeinEntity* ActingEntity = GetEntity(Cmd.EntityHandle);
				if (ActingEntity)
				{
					const FFixedVector FromWorld = ActingEntity->Transform.GetLocation();
					const FFixedVector ToWorld = Cmd.TargetLocation;

					const FGameplayTagContainer& AgentTags = GetEntityTags(Cmd.EntityHandle);

					if (!PathableTargetResolver.Execute(FromWorld, ToWorld, AgentTags))
					{
						UE_LOG(LogSeinSim, Verbose, TEXT("ActivateAbility[%s]: target not reachable"),
							*Cmd.AbilityTag.ToString());
						RejectCommand(SeinARTSTags::Command_Reject_PathUnreachable);
						continue;
					}
				}
			}

			// 4. CanActivate escape hatch (after declarative validation)
			if (!Ability->CanActivate())
			{
				UE_LOG(LogSeinSim, Verbose, TEXT("ActivateAbility[%s]: CanActivate returned false"),
					*Cmd.AbilityTag.ToString());
				RejectCommand(SeinARTSTags::Command_Reject_CanActivateFailed);
				continue;
			}

			// 5. Catalog-aware cost split — universal across production and
			// non-production abilities. The catalog tags each resource as
			// AtEnqueue (deduct on click — manpower, munitions, fuel) or
			// AtCompletion (deferred, deduct when the produced unit actually
			// spawns — pop, supply). For non-production abilities, only
			// AtEnqueue resources should appear in the cost map; the
			// PendingCompletionCost bucket is then empty and the deduct path
			// behaves identically to the pre-refactor flat-deduct.
			//
			// For production abilities the AtCompletion bucket is held in
			// `PendingCompletionCost` for `SeinEnqueueProduction` (called from
			// the ability's BP graph) to seed the queue entry's
			// `AtCompletionCost`; the production system deducts it at spawn.
			FSeinResourceCost AtEnqueueCost;
			FSeinResourceCost PendingCompletionCost;
			USeinResourceBPFL::SeinSplitCostByCatalog(this, Ability->ResourceCost, AtEnqueueCost, PendingCompletionCost);

			// 6. Affordability check on the AtEnqueue portion — the only part
			// the activation gate commits. AtCompletion affordability is
			// re-checked by the production system at spawn time (DESIGN §9
			// stall-at-completion) and cannot fail here.
			if (!USeinResourceBPFL::SeinCanAfford(this, Cmd.PlayerID, AtEnqueueCost))
			{
				UE_LOG(LogSeinSim, Verbose, TEXT("ActivateAbility[%s]: unaffordable"),
					*Cmd.AbilityTag.ToString());
				RejectCommand(SeinARTSTags::Command_Reject_Unaffordable);
				continue;
			}

			// 7. Deduct AtEnqueue. Non-production abilities deduct everything
			// here (AtEnqueue == full cost). Production abilities deduct only
			// the AtEnqueue portion; AtCompletion is handed to the queue entry
			// via `PendingCompletionCost` snapshot below.
			USeinResourceBPFL::SeinDeduct(this, Cmd.PlayerID, AtEnqueueCost);

			// 7a. Cancel OTHER abilities whose GrantedTags intersect this ability's
			// CancelAbilitiesWithTag. Explicitly skip self — matching self here
			// would cancel-then-reactivate on every duplicate command (e.g. a
			// broker dispatching the same move twice in one command frame), and
			// nothing actually moves. Self-cancelling-reissue is handled below.
			if (!Ability->CancelAbilitiesWithTag.IsEmpty())
			{
				for (int32 OtherID : AbilityComp->AbilityInstanceIDs)
				{
					USeinAbility* Other = GetAbilityInstance(OtherID);
					if (Other && Other != Ability && Other->bIsActive &&
						Other->GrantedTags.HasAny(Ability->CancelAbilitiesWithTag))
					{
						Other->CancelAbility();
					}
				}
			}

			// 7b. Self-cancelling reissue: if this ability is already running and
			// lists any of its own GrantedTags in CancelAbilitiesWithTag, the
			// designer is asking "re-issuing me should kill the previous run
			// before the new one starts" — so cancel the prior activation
			// before ActivateAbility spins up a fresh one.
			if (Ability->bIsActive &&
				Ability->GrantedTags.HasAny(Ability->CancelAbilitiesWithTag))
			{
				Ability->CancelAbility();
			}

			UE_LOG(LogSeinSim, Verbose,
				TEXT("ActivateAbility[%s]: gates passed, calling Ability->ActivateAbility on entity %s targeting %s"),
				*Cmd.AbilityTag.ToString(), *Cmd.EntityHandle.ToString(), *Cmd.TargetEntity.ToString());

			// 8. Stamp the deducted + pending-completion snapshots and commit
			// activation. Production abilities consult PendingCompletionCost
			// in their OnActivate BP graph (via SeinEnqueueProduction); refund-
			// on-cancel reads DeductedCost (the AtEnqueue portion only — the
			// AtCompletion portion was never deducted from the player's balance).
			//
			// Targeter-originated commands carry captured points; route through
			// the points-aware overload so the ability's runtime TargeterPoints
			// array gets populated. Empty array degrades to the basic activation
			// path. Broker per-member dispatches carry these forward via
			// SeinCommandBrokerDispatch::ActivateMemberAbility.
			Ability->RecordDeductedCost(AtEnqueueCost);
			Ability->RecordPendingCompletionCost(PendingCompletionCost);
			if (Cmd.TargeterPoints.Num() > 0)
			{
				Ability->ActivateAbilityWithTargeterPoints(Cmd.TargetEntity, Cmd.TargetLocation, Cmd.TargeterPoints);
			}
			else
			{
				Ability->ActivateAbility(Cmd.TargetEntity, Cmd.TargetLocation);
			}
			if (!Ability->bIsPassive)
			{
				// Find the ID of `Ability` in this entity's instances. Linear
				// scan; ability count per entity is small.
				int32 ActiveID = INDEX_NONE;
				for (int32 ID : AbilityComp->AbilityInstanceIDs)
				{
					if (GetAbilityInstance(ID) == Ability) { ActiveID = ID; break; }
				}
				AbilityComp->ActiveAbilityID = ActiveID;
			}
		}
		else if (Cmd.CommandType == SeinARTSTags::Command_Type_CancelAbility)
		{
			FSeinAbilityComponent* AbilityComp = GetComponent<FSeinAbilityComponent>(Cmd.EntityHandle);
			USeinAbility* Active = AbilityComp ? AbilityComp->GetActiveAbility(*this) : nullptr;
			if (Active && Active->bIsActive)
			{
				Active->CancelAbility();
				AbilityComp->ActiveAbilityID = INDEX_NONE;
			}
			else
			{
				RejectCommand(SeinARTSTags::Command_Reject_InvalidTarget);
			}
		}
		// Command_Type_QueueProduction removed (refactored 2026-05-05): production
		// now flows through Command_Type_ActivateAbility on production-marked
		// abilities (USeinAbility::ProducibleClass / ResearchEffectClass set).
		// The ability's OnActivate BP graph calls USeinProductionBPFL::
		// SeinEnqueueProduction to append to the producer's queue. Cost
		// deduction (catalog-aware AtEnqueue split) happens at the activation
		// gate above. Tech prereqs go on USeinAbility::RequiredPlayerTags.
		else if (Cmd.CommandType == SeinARTSTags::Command_Type_CancelProduction)
		{
			FSeinProductionComponent* ProdComp = GetComponent<FSeinProductionComponent>(Cmd.EntityHandle);
			if (!ProdComp) { RejectCommand(SeinARTSTags::Command_Reject_MissingComponent); continue; }

			const int32 CancelIdx = Cmd.QueueIndex;
			if (CancelIdx < 0 || CancelIdx >= ProdComp->Queue.Num()) { RejectCommand(SeinARTSTags::Command_Reject_InvalidTarget); continue; }

			// Refund AtEnqueueCost only (AtCompletion was never deducted). Policy
			// chooses between progress-proportional (default) and flat-custom.
			if (FSeinPlayerState* PS = GetPlayerStateMutable(Cmd.PlayerID))
			{
				const FSeinProductionQueueEntry& CancelledEntry = ProdComp->Queue[CancelIdx];

				FFixedPoint RefundFraction;
				if (CancelledEntry.RefundPolicy.bUseCustomRefund)
				{
					RefundFraction = CancelledEntry.RefundPolicy.CustomRefundPercentage;
				}
				else
				{
					// Progress-proportional: refund = (1 - progress) * cost.
					// Only the front entry has non-zero progress.
					FFixedPoint ProgressFraction = FFixedPoint::Zero;
					if (CancelIdx == 0 && CancelledEntry.TotalBuildTime > FFixedPoint::Zero)
					{
						ProgressFraction = ProdComp->CurrentBuildProgress / CancelledEntry.TotalBuildTime;
						if (ProgressFraction > FFixedPoint::One) ProgressFraction = FFixedPoint::One;
					}
					RefundFraction = FFixedPoint::One - ProgressFraction;
				}

				if (RefundFraction > FFixedPoint::Zero)
				{
					FSeinResourceCost Refund;
					Refund.Amounts.Reserve(CancelledEntry.AtEnqueueCost.Amounts.Num());
					for (const auto& Pair : CancelledEntry.AtEnqueueCost.Amounts)
					{
						Refund.Amounts.Add(Pair.Key, Pair.Value * RefundFraction);
					}
					USeinResourceBPFL::SeinRefund(this, Cmd.PlayerID, Refund);
				}
			}

			ProdComp->Queue.RemoveAt(CancelIdx);
			if (CancelIdx == 0)
			{
				ProdComp->CurrentBuildProgress = FFixedPoint::Zero;
				ProdComp->bStalledAtCompletion = false;
			}
		}
		// Command_Type_SetRallyPoint removed (refactored 2026-05-05): rally
		// authoring now flows through SA_SetRallyPoint abilities calling
		// USeinProductionBPFL::SeinSetRallyPoint (transform), SeinSetRallyEntity
		// (chase), or SeinClearRallyPoint. Programmatic non-ability callers
		// can call the BPFLs directly.
		else
		{
			UE_LOG(LogSeinSim, Warning, TEXT("ProcessCommands: unknown command type %s"), *Cmd.CommandType.ToString());
			RejectCommand(SeinARTSTags::Command_Reject_InvalidTarget);
		}
	}

	// PendingCommands was cleared up-front (see snapshot-and-drain comment at
	// top of function). Any commands enqueued by abilities/effects DURING the
	// iteration above are sitting in PendingCommands now, queued for next tick.
}

void USeinWorldSubsystem::EnqueueCommand(const FSeinCommand& Command)
{
	PendingCommands.AddCommand(Command);
}

// ==================== Entity Management ====================

FSeinEntityHandle USeinWorldSubsystem::SpawnEntity(
	TSubclassOf<ASeinActor> ActorClass,
	const FFixedTransform& SpawnTransform,
	FSeinPlayerID OwnerPlayerID)
{
	if (!ActorClass)
	{
		UE_LOG(LogSeinSim, Error, TEXT("Cannot spawn entity: ActorClass is null"));
		return FSeinEntityHandle::Invalid();
	}

	// CDO required for the SCS-aware walk below. Legacy ArchetypeDefinition has
	// been excised — identity/producible/extents/etc. live as
	// FSein*Component entries in USeinEntityComponent::ComponentData.
	const ASeinActor* CDO = GetDefault<ASeinActor>(ActorClass);
	if (!CDO)
	{
		UE_LOG(LogSeinSim, Error, TEXT("Cannot spawn entity: Blueprint %s has no CDO"), *ActorClass->GetName());
		return FSeinEntityHandle::Invalid();
	}

	// Degenerate-scale guard. A zero scale component is never a legitimate
	// spawn input, but it fails SILENTLY: the entity is fully functional in
	// the sim (movement/collision/extents never read scale) while the bridge
	// drives the actor's render transform from it — an invisible "ghost"
	// unit. Corrupted authored FFixedTransform data (the fix-1 serializer
	// window) shipped exactly this via squad slot offsets. Normalize to
	// Identity and say so loudly. Deterministic: pure function of the input.
	FFixedTransform SafeTransform = SpawnTransform;
	if (SafeTransform.Scale.X == FFixedPoint::Zero
		|| SafeTransform.Scale.Y == FFixedPoint::Zero
		|| SafeTransform.Scale.Z == FFixedPoint::Zero)
	{
		UE_LOG(LogSeinSim, Warning,
			TEXT("SpawnEntity(%s): spawn transform has a zero scale component (%s) — normalized to Identity. "
				 "Check the authored transform data feeding this spawn."),
			*ActorClass->GetName(), *SafeTransform.Scale.ToString());
		SafeTransform.Scale = FFixedVector::Identity;
	}

	FSeinEntityHandle Handle = EntityPool.Acquire(
		SafeTransform,
		OwnerPlayerID
	);

	if (!Handle.IsValid())
	{
		UE_LOG(LogSeinSim, Error, TEXT("Failed to acquire entity from pool"));
		return FSeinEntityHandle::Invalid();
	}

	// Store actor class for bridge spawning
	EntityActorClassMap.Add(Handle, ActorClass);

	// Walk the Blueprint CDO's USeinEntityComponent subobjects and inject
	// every authored ComponentData entry into deterministic sim storage. This
	// is the sole sim-authoring path post-Phase-5 — typed-wrapper ACs are
	// gone; designers compose entities by adding entries to the entity
	// component's ComponentData array.
	//
	// NB: AActor::GetComponents() on a CDO only sees native CreateDefaultSubobject
	// components — BP-editor-added components live on the SCS. The helper below
	// walks native components + SCS nodes up the BP hierarchy in a stable order.
	// Walk the BP CDO's USeinEntityComponent ONCE here; the resolved bridge
	// (BridgeCDO) is reused for BaseTags seeding below instead of walking twice.
	const USeinEntityComponent* BridgeCDO = nullptr;
	if (CDO)
	{
		TArray<const USeinEntityComponent*> EntityComps;
		AActor::GetActorClassDefaultComponents<USeinEntityComponent>(ActorClass, EntityComps);
		if (EntityComps.Num() > 1)
		{
			UE_LOG(LogSeinSim, Warning,
				TEXT("SpawnEntity: %s carries %d USeinEntityComponents — only the first will be used."),
				*ActorClass->GetName(), EntityComps.Num());
		}
		if (EntityComps.Num() > 0 && EntityComps[0])
		{
			BridgeCDO = EntityComps[0];
			BridgeCDO->InjectAuthoredComponents(*this, Handle);
		}
	}

	// Instantiate ability UObjects if the entity was granted any
	InitializeEntityAbilities(Handle);

	// Initialize the entity's tag state. Seed BaseTags from the entity bridge's
	// authored BaseTags UPROPERTY, then merge in the identity tag (from
	// FSeinIdentityComponent) and the
	// UnderConstruction tag if the entity carries a construction component.
	// Finally seed refcounts + the global EntityTagIndex from the full set.
	//
	// The matching ungrant for UnderConstruction lives in SeinFinishConstruction
	// (drops the refcount we add here via the BaseTags seed). Designers can
	// also list UnderConstruction explicitly in BaseTags — harmless, just gives
	// a +1 refcount that the system holds onto.
	const bool bHasConstructionComponent = GetComponent<FSeinConstructionComponent>(Handle) != nullptr;
	{
		FSeinEntityTagState& TagState = EntityTagStates.FindOrAdd(Handle);

		// Seed from the entity bridge's authored BaseTags, reusing the single CDO
		// walk above (BridgeCDO). Falls back to a fresh walk only if injection
		// didn't run (CDO was null); preserves the original unconditional seed.
		const USeinEntityComponent* TagBridge = BridgeCDO;
		if (!TagBridge)
		{
			TArray<const USeinEntityComponent*> EntityComps;
			AActor::GetActorClassDefaultComponents<USeinEntityComponent>(ActorClass, EntityComps);
			if (EntityComps.Num() > 0) TagBridge = EntityComps[0];
		}
		if (TagBridge)
		{
			TagState.BaseTags.AppendTags(TagBridge->BaseTags);
		}

		// Resolve the identity tag from the entity's FSeinIdentityComponent
		// (injected from the bridge's ComponentData array above).
		if (const FSeinIdentityComponent* Identity = GetComponent<FSeinIdentityComponent>(Handle))
		{
			if (Identity->IdentityTag.IsValid())
			{
				TagState.BaseTags.AddTag(Identity->IdentityTag);
			}
		}
		if (bHasConstructionComponent)
		{
			TagState.BaseTags.AddTag(SeinARTSTags::State_UnderConstruction);
		}
		SeedEntityTagsFromBase(Handle);

		// AFTER tag seeding — replay any active player-scope effects that
		// grant abilities to entities matching this entity's tag state.
		// Covers the "unit spawned after tech research completed" case so
		// the new unit picks up unlocked abilities at spawn instead of
		// being permanently stuck without them.
		ReplayEffectAbilityGrants(Handle);
	}

	// Fire spawn visual event. The actor bridge processes EntitySpawned first
	// (creates the bridged actor), THEN downstream events for the same entity
	// land on its now-live ACs. Order matters — we enqueue spawn before the
	// optional construction-state event so the construction AC exists by the
	// time the construction event reaches it.
	EnqueueVisualEvent(FSeinVisualEvent::MakeSpawnEvent(Handle, SafeTransform.GetLocation()));

	// Construction-state notification — drives the placement-visual swap on the
	// bridged actor's USeinConstructionComponent. Only fired when the entity
	// actually carries a construction component (which is also what drove the
	// auto-grant above). Symmetric with the un-grant + event in SeinFinishConstruction.
	if (bHasConstructionComponent)
	{
		EnqueueVisualEvent(FSeinVisualEvent::MakeConstructionStateChangedEvent(Handle, /*bUnderConstruction=*/true));
	}

	UE_LOG(LogSeinSim, Verbose, TEXT("Spawned entity %s from %s (owner: %s)"),
		*Handle.ToString(), *ActorClass->GetName(), *OwnerPlayerID.ToString());

	// Fire OnEntitySpawned AFTER components are injected + visual event
	// enqueued. Optional system subsystems (USeinCoverSubsystem etc.)
	// subscribe to discover entities with their relevant components and
	// self-register them in their per-system registries.
	OnEntitySpawned.Broadcast(Handle);

	return Handle;
}

FSeinEntityHandle USeinWorldSubsystem::SpawnEntityFromPlacedActor(
	ASeinActor* PlacedActor,
	FSeinPlayerID OwnerPlayerID)
{
	if (!PlacedActor)
	{
		UE_LOG(LogSeinSim, Error, TEXT("SpawnEntityFromPlacedActor: null actor"));
		return FSeinEntityHandle::Invalid();
	}
	// Legacy ArchetypeDefinition has been excised — identity / producible / extents
	// data lives on the entity bridge's ComponentData array.

	// Sim transform = editor-baked snapshot. Both LOCATION and ROTATION
	// are baked in the editor (`ASeinActor::PostEditMove`); the int64 bits
	// were serialized to the .umap. We just read them here — no FromFloat /
	// FromQuat at runtime, so cross-arch clients (PC + ARM Mac + mobile +
	// console) land on identical sim transforms.
	//
	// Migration path: actors placed before the bakes existed have
	// `bSimLocationBaked == false` and/or `bSimRotationBaked == false`. We
	// log a warning and fall back to runtime conversion — single-platform
	// tests still work, but cross-arch lockstep needs the designer to re-
	// save the level (or run the "Bake Determinism Snapshots" menu when it
	// lands). Rotation bake landed AFTER location bake — pre-rotation
	// projects will hit the rotation warning until they re-save.
	FFixedVector SimLocation;
	if (PlacedActor->bSimLocationBaked)
	{
		SimLocation = PlacedActor->PlacedSimLocation;
	}
	else
	{
		UE_LOG(LogSeinSim, Warning,
			TEXT("SpawnEntityFromPlacedActor: %s has no baked PlacedSimLocation — "
				 "falling back to runtime FromFloat. Re-save the level (or nudge "
				 "the actor in editor) to bake the snapshot. NOT cross-arch deterministic."),
			*PlacedActor->GetName());
		SimLocation = FFixedVector::FromVector(PlacedActor->GetActorLocation());
	}

	FFixedQuaternion SimRotation;
	if (PlacedActor->bSimRotationBaked)
	{
		SimRotation = PlacedActor->PlacedSimRotation;
	}
	else
	{
		UE_LOG(LogSeinSim, Warning,
			TEXT("SpawnEntityFromPlacedActor: %s has no baked PlacedSimRotation — "
				 "falling back to runtime FromQuat. Re-save the level (or nudge "
				 "the actor in editor) to bake the snapshot. NOT cross-arch deterministic."),
			*PlacedActor->GetName());
		SimRotation = FFixedQuaternion::FromQuat(PlacedActor->GetActorQuat());
	}

	const FFixedTransform SimTransform(SimLocation, SimRotation);

	FSeinEntityHandle Handle = EntityPool.Acquire(SimTransform, OwnerPlayerID);
	if (!Handle.IsValid())
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("SpawnEntityFromPlacedActor: pool.Acquire failed for %s"),
			*PlacedActor->GetName());
		return FSeinEntityHandle::Invalid();
	}

	EntityActorClassMap.Add(Handle, PlacedActor->GetClass());

	// Inject the LIVE actor's entity component ComponentData — captures
	// per-instance edits beyond CDO defaults. Designers can drop a placed
	// actor and tune fields on the level instance; this path picks them up
	// correctly.
	if (USeinEntityComponent* EntityComp = PlacedActor->FindComponentByClass<USeinEntityComponent>())
	{
		EntityComp->InjectAuthoredComponents(*this, Handle);
	}

	InitializeEntityAbilities(Handle);

	const bool bHasConstructionComponent = GetComponent<FSeinConstructionComponent>(Handle) != nullptr;
	{
		// Initialize tag state — mirror of SpawnEntity's path. Seeds BaseTags
		// from the LIVE placed actor's entity bridge (per-instance edits to
		// BaseTags on the level instance are honored, just like other per-
		// instance authoring on placed actors).
		FSeinEntityTagState& TagState = EntityTagStates.FindOrAdd(Handle);

		if (const USeinEntityComponent* LiveBridge = PlacedActor->FindComponentByClass<USeinEntityComponent>())
		{
			TagState.BaseTags.AppendTags(LiveBridge->BaseTags);
		}

		// Identity-data first — matches SpawnEntity. Legacy archetype-def fallback is gone.
		if (const FSeinIdentityComponent* Identity = GetComponent<FSeinIdentityComponent>(Handle))
		{
			if (Identity->IdentityTag.IsValid())
			{
				TagState.BaseTags.AddTag(Identity->IdentityTag);
			}
		}
		if (bHasConstructionComponent)
		{
			TagState.BaseTags.AddTag(SeinARTSTags::State_UnderConstruction);
		}
		SeedEntityTagsFromBase(Handle);

		// AFTER tag seeding — replay any active player-scope effects that
		// grant abilities to entities matching this entity's tag state.
		// Covers the "unit spawned after tech research completed" case so
		// the new unit picks up unlocked abilities at spawn instead of
		// being permanently stuck without them.
		ReplayEffectAbilityGrants(Handle);
	}

	// Deliberately NO EntitySpawned visual event — placed actors already exist
	// in the world; firing EntitySpawned would make the actor bridge spawn a
	// second render actor in addition to the one the designer placed.
	//
	// ConstructionStateChanged IS safe to emit — it's a state notification
	// dispatched to the existing actor's construction AC (mesh swap), no
	// extra actor spawn. Designers placing under-construction stubs in the
	// editor get correct preview visuals at PIE start.
	if (bHasConstructionComponent)
	{
		EnqueueVisualEvent(FSeinVisualEvent::MakeConstructionStateChangedEvent(Handle, /*bUnderConstruction=*/true));
	}

	// Verbose: large maps register dozens of placed actors at travel time;
	// per-actor lines drown the log. Re-enable with `Log LogSeinSim Verbose`
	// when diagnosing slot-binding / handle-allocation bugs.
	UE_LOG(LogSeinSim, Verbose,
		TEXT("Auto-registered placed actor %s as entity %s (owner: %s)"),
		*PlacedActor->GetName(), *Handle.ToString(), *OwnerPlayerID.ToString());

	// Fire OnEntitySpawned — same notification placed actors get as freshly-
	// spawned ones. Cover providers placed in the level via BP get their
	// USeinCoverSubsystem registration through this path.
	OnEntitySpawned.Broadcast(Handle);

	return Handle;
}

FSeinEntityHandle USeinWorldSubsystem::SpawnAbstractEntity(
	const FFixedTransform& SpawnTransform,
	FSeinPlayerID OwnerPlayerID)
{
	// Acquire a handle from the pool; no ActorClass = no render spawn, no
	// CDO walk, no ability initialization. The caller is on the hook to
	// add whatever components the abstract entity needs via AddComponent<T>.
	FSeinEntityHandle Handle = EntityPool.Acquire(SpawnTransform, OwnerPlayerID);
	if (!Handle.IsValid())
	{
		UE_LOG(LogSeinSim, Error, TEXT("SpawnAbstractEntity: pool.Acquire failed"));
		return FSeinEntityHandle::Invalid();
	}
	// Intentionally no EntityActorClassMap entry — actor bridge no-ops on missing map entry.
	UE_LOG(LogSeinSim, Verbose, TEXT("Spawned abstract entity %s (owner: %s)"),
		*Handle.ToString(), *OwnerPlayerID.ToString());
	return Handle;
}

void USeinWorldSubsystem::DestroyEntity(FSeinEntityHandle Handle)
{
	if (!Handle.IsValid() || !EntityPool.IsValid(Handle))
	{
		return;
	}

	// Mark for deferred destruction
	FSeinEntity* Entity = EntityPool.Get(Handle);
	if (Entity)
	{
		Entity->SetAlive(false);
	}
	PendingDestroy.AddUnique(Handle);
}

void USeinWorldSubsystem::ProcessDeferredDestroys()
{
	for (const FSeinEntityHandle& Handle : PendingDestroy)
	{
		if (!EntityPool.IsValid(Handle)) continue;

		// Cancel any active abilities/latent actions
		if (LatentActionManager)
		{
			LatentActionManager->CancelActionsForEntity(Handle);
		}

		// Strip any effects this entity was the source of, where the effect class
		// declares bRemoveOnSourceDeath (DESIGN §8 Q4c). Runs BEFORE component
		// storages + pool release so downstream consumers can still read the
		// effect's state while the removal hooks fire.
		RemoveEffectsFromDeadSource(Handle);

		// Containment death propagation (DESIGN §14) runs before storages clear so
		// PropagateContainerDeath can still read the container's Occupants list +
		// OnEject/OnContainerDeath effect classes off FSeinContainmentData.
		if (GetComponent<FSeinContainmentData>(Handle))
		{
			PropagateContainerDeath(Handle);
		}

		// Member-side: if the dying entity is contained, evict it from its
		// container's Occupants + CurrentLoad / VisualSlotAssignments / attachment
		// slot. Mirrors the CommandBroker eviction below.
		if (const FSeinContainmentMemberData* MemComp = GetComponent<FSeinContainmentMemberData>(Handle))
		{
			if (EntityPool.IsValid(MemComp->CurrentContainer))
			{
				if (FSeinContainmentData* Container = GetComponent<FSeinContainmentData>(MemComp->CurrentContainer))
				{
					Container->Occupants.Remove(Handle);
					Container->CurrentLoad = FMath::Max(0, Container->CurrentLoad - MemComp->Size);
					if (Container->bTracksVisualSlots)
					{
						const int32 Idx = MemComp->VisualSlotIndex;
						if (Container->VisualSlotAssignments.IsValidIndex(Idx))
						{
							Container->VisualSlotAssignments[Idx] = FSeinEntityHandle();
						}
					}
					// Attachment slot (if any) — clear assignment + fire visual event.
					if (MemComp->CurrentSlot.IsValid())
					{
						if (FSeinAttachmentSpec* Spec = GetComponent<FSeinAttachmentSpec>(MemComp->CurrentContainer))
						{
							Spec->Assignments.Remove(MemComp->CurrentSlot);
						}
						EnqueueVisualEvent(FSeinVisualEvent::MakeAttachmentSlotEmptiedEvent(
							MemComp->CurrentContainer, Handle, MemComp->CurrentSlot));
					}
					// Death of a contained entity doesn't spawn an exit-location event
					// — container dying with eject=false funnels through
					// PropagateContainerDeath above; death of just one occupant inside
					// a still-living container is a quieter cleanup (no world teleport).
				}
			}
		}

		// Evict from the dying entity's current broker (DESIGN §5). If this leaves
		// the broker with no members and no queued orders, cull it via DestroyEntity
		// — it'll be processed on the next tick's PostTick.
		if (const FSeinBrokerMembershipData* Memb = GetComponent<FSeinBrokerMembershipData>(Handle))
		{
			if (EntityPool.IsValid(Memb->CurrentBrokerHandle))
			{
				if (FSeinCommandBrokerData* Broker = GetComponent<FSeinCommandBrokerData>(Memb->CurrentBrokerHandle))
				{
					Broker->Members.Remove(Handle);
					Broker->bCapabilityMapDirty = true;
					// Per-order parallelism: queue-empty implies nothing executing.
					if (Broker->bSelfCullOnEmpty && Broker->Members.Num() == 0 && Broker->OrderQueue.Num() == 0)
					{
						DestroyEntity(Memb->CurrentBrokerHandle);
					}
				}
			}
		}

		// Clear the entity from the global tag index and the named registry
		// before component storages are freed (UnindexEntityTags reads EntityTagStates).
		UnindexEntityTags(Handle);
		UnregisterHandleFromNames(Handle);

		// Phase 4 architecture: release this entity's ability + resolver pool
		// slots BEFORE component storage clears. The pool slots own the
		// UObject lifetime via UPROPERTY; freeing the slot lets the GC reap
		// the ability/resolver instance the next pass.
		if (const FSeinAbilityComponent* AbilityComp = GetComponent<FSeinAbilityComponent>(Handle))
		{
			for (int32 ID : AbilityComp->AbilityInstanceIDs)
			{
				UnregisterAbilityInstance(ID);
			}
		}
		if (const FSeinCommandBrokerData* BrokerComp = GetComponent<FSeinCommandBrokerData>(Handle))
		{
			UnregisterCommandBrokerResolver(BrokerComp->ResolverID);
		}

		// Fire OnEntityDestroyed BEFORE wiping components — subscribers
		// (USeinCoverSubsystem, etc.) need to read storage to decide on
		// per-system unregistration.
		OnEntityDestroyed.Broadcast(Handle);

		// Remove all components
		for (auto& Pair : ComponentStorages)
		{
			Pair.Value->RemoveAllForEntity(Handle);
		}

		EnqueueVisualEvent(FSeinVisualEvent::MakeDestroyEvent(Handle));
		EntityActorClassMap.Remove(Handle);
		EntityPool.Release(Handle);

		UE_LOG(LogSeinSim, Log, TEXT("Destroyed entity %s"), *Handle.ToString());
	}

	PendingDestroy.Empty();
}

FSeinEntity* USeinWorldSubsystem::GetEntity(FSeinEntityHandle Handle)
{
	return EntityPool.Get(Handle);
}

const FSeinEntity* USeinWorldSubsystem::GetEntity(FSeinEntityHandle Handle) const
{
	return EntityPool.Get(Handle);
}

bool USeinWorldSubsystem::IsEntityAlive(FSeinEntityHandle Handle) const
{
	const FSeinEntity* Entity = EntityPool.Get(Handle);
	return Entity && Entity->IsAlive();
}

FSeinPlayerID USeinWorldSubsystem::GetEntityOwner(FSeinEntityHandle Handle) const
{
	return EntityPool.GetOwner(Handle);
}

void USeinWorldSubsystem::SetEntityOwner(FSeinEntityHandle Handle, FSeinPlayerID NewOwner)
{
	// Mutates sim state — must run inside the sim tick (e.g. a capture-point
	// passive ability/effect). Asserted in non-shipping builds; compiles out in
	// shipping. See Core/SeinSimContext.h.
	SEIN_CHECK_SIM();
	EntityPool.SetOwner(Handle, NewOwner);
}

TSubclassOf<ASeinActor> USeinWorldSubsystem::GetEntityActorClass(FSeinEntityHandle Handle) const
{
	const TSubclassOf<ASeinActor>* Found = EntityActorClassMap.Find(Handle);
	return Found ? *Found : nullptr;
}

FSeinPlayerState* USeinWorldSubsystem::GetPlayerStateMutable(FSeinPlayerID PlayerID)
{
	return PlayerStates.Find(PlayerID);
}

// ==================== Player & Faction ====================

void USeinWorldSubsystem::RegisterPlayer(FSeinPlayerID PlayerID, FSeinFactionID FactionID, uint8 TeamID)
{
	if (PlayerStates.Contains(PlayerID))
	{
		UE_LOG(LogSeinSim, Warning, TEXT("Player %s already registered"), *PlayerID.ToString());
		return;
	}

	FSeinPlayerState NewState(PlayerID, FactionID, TeamID);

	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	const TArray<FSeinResourceDefinition>& Catalog = Settings->ResourceCatalog;

	// Layer 1: catalog defaults populate every resource the project knows
	// about. This is the "actual default" — if nothing overrides, the player
	// starts with the catalog's DefaultStartingValue / DefaultCap. Designers
	// who set DefaultStartingValue=500 on Money expect every player to start
	// with 500 Money, full stop. (Faction kits + GameMode StartingResources
	// layer on top of this and override per-player.)
	for (const FSeinResourceDefinition& Def : Catalog)
	{
		if (!Def.ResourceTag.IsValid()) { continue; }
		NewState.Resources.Add(Def.ResourceTag, Def.DefaultStartingValue);
		if (Def.DefaultCap > FFixedPoint::Zero)
		{
			NewState.ResourceCaps.Add(Def.ResourceTag, Def.DefaultCap);
		}
	}

	// Layer 2: faction's ResourceKit overrides the catalog defaults for the
	// resources it cares about. Used for asymmetric factions (e.g. one starts
	// with bonus fuel, another with bonus manpower) without rewriting the
	// catalog per faction.
	if (TObjectPtr<USeinFaction>* FactionPtr = Factions.Find(FactionID))
	{
		for (const FSeinFactionResourceEntry& KitEntry : (*FactionPtr)->ResourceKit)
		{
			if (!KitEntry.ResourceTag.IsValid()) { continue; }

			const FSeinResourceDefinition* CatalogEntry = Catalog.FindByPredicate(
				[&](const FSeinResourceDefinition& D) { return D.ResourceTag == KitEntry.ResourceTag; });

			const FFixedPoint StartingValue = KitEntry.bOverrideStartingValue
				? KitEntry.StartingValueOverride
				: (CatalogEntry ? CatalogEntry->DefaultStartingValue : FFixedPoint::Zero);

			const FFixedPoint Cap = KitEntry.bOverrideCap
				? KitEntry.CapOverride
				: (CatalogEntry ? CatalogEntry->DefaultCap : FFixedPoint::Zero);

			NewState.Resources.Add(KitEntry.ResourceTag, StartingValue);
			if (Cap > FFixedPoint::Zero)
			{
				NewState.ResourceCaps.Add(KitEntry.ResourceTag, Cap);
			}
		}
	}

	// Layer 3: GameMode's StartingResources is applied AFTER RegisterPlayer
	// returns (in ASeinGameMode::RegisterPlayerWithSim). Final override —
	// match-host or scenario-specific tweaks land last.

	PlayerStates.Add(PlayerID, MoveTemp(NewState));

	UE_LOG(LogSeinSim, Log, TEXT("Registered player %s (faction: %s, team: %d)"),
		*PlayerID.ToString(), *FactionID.ToString(), TeamID);
}

FSeinPlayerState* USeinWorldSubsystem::GetPlayerState(FSeinPlayerID PlayerID)
{
	return PlayerStates.Find(PlayerID);
}

const FSeinPlayerState* USeinWorldSubsystem::GetPlayerState(FSeinPlayerID PlayerID) const
{
	return PlayerStates.Find(PlayerID);
}

bool USeinWorldSubsystem::GetPlayerStateCopy(FSeinPlayerID PlayerID, FSeinPlayerState& OutState) const
{
	const FSeinPlayerState* Found = PlayerStates.Find(PlayerID);
	if (Found)
	{
		OutState = *Found;
		return true;
	}
	return false;
}

void USeinWorldSubsystem::RegisterFaction(USeinFaction* Faction)
{
	if (!Faction) return;
	Factions.Add(Faction->FactionID, Faction);
	UE_LOG(LogSeinSim, Log, TEXT("Registered faction: %s (FactionID=%u)"),
		*Faction->FactionName.ToString(), Faction->FactionID.Value);
}

void USeinWorldSubsystem::SeedSimRandom(int64 Seed)
{
	SimRandom.SetSeed(static_cast<uint64>(Seed));
	UE_LOG(LogSeinSim, Log, TEXT("SeedSimRandom: PRNG seeded with %lld."), Seed);
}

// ============================================================================
// Ability + Resolver pools (Phase 4 architecture cleanup)
// ============================================================================
//
// Generic pool primitive shared between abilities and resolvers. Free-list-
// recycled, deterministic by allocation order, GC-rooted via UPROPERTY-tagged
// pool arrays on the subsystem.

namespace
{
	template <typename T>
	int32 PoolRegister(TArray<TObjectPtr<T>>& Pool, TArray<int32>& FreeList, T* Obj)
	{
		if (!Obj) return INDEX_NONE;
		if (FreeList.Num() > 0)
		{
			const int32 ID = FreeList.Pop(EAllowShrinking::No);
			Pool[ID] = Obj;
			return ID;
		}
		return Pool.Add(Obj);
	}

	template <typename T>
	void PoolUnregister(TArray<TObjectPtr<T>>& Pool, TArray<int32>& FreeList, int32 ID)
	{
		if (!Pool.IsValidIndex(ID)) return;
		if (Pool[ID] == nullptr) return; // already released
		Pool[ID] = nullptr;
		FreeList.Add(ID);
	}

	template <typename T>
	T* PoolGet(const TArray<TObjectPtr<T>>& Pool, int32 ID)
	{
		return Pool.IsValidIndex(ID) ? Pool[ID].Get() : nullptr;
	}
}

int32 USeinWorldSubsystem::RegisterAbilityInstance(USeinAbility* Ability)
{
	return PoolRegister(AbilityPool, AbilityPoolFreeList, Ability);
}

void USeinWorldSubsystem::UnregisterAbilityInstance(int32 AbilityID)
{
	PoolUnregister(AbilityPool, AbilityPoolFreeList, AbilityID);
}

USeinAbility* USeinWorldSubsystem::GetAbilityInstance(int32 AbilityID) const
{
	return PoolGet(AbilityPool, AbilityID);
}

int32 USeinWorldSubsystem::RegisterCommandBrokerResolver(USeinCommandBrokerResolver* Resolver)
{
	return PoolRegister(CommandBrokerResolverPool, CommandBrokerResolverPoolFreeList, Resolver);
}

void USeinWorldSubsystem::UnregisterCommandBrokerResolver(int32 ResolverID)
{
	PoolUnregister(CommandBrokerResolverPool, CommandBrokerResolverPoolFreeList, ResolverID);
}

USeinCommandBrokerResolver* USeinWorldSubsystem::GetCommandBrokerResolver(int32 ResolverID) const
{
	return PoolGet(CommandBrokerResolverPool, ResolverID);
}

// ============================================================================
// World Snapshot — Capture + Restore (Phase 4 architecture)
// ============================================================================

void USeinWorldSubsystem::CaptureSnapshot(FSeinWorldSnapshot& OutSnapshot)
{
	OutSnapshot.SnapshotVersion = 1;
	OutSnapshot.FrameworkVersion = TEXT("0.1.0");
	OutSnapshot.GameVersion = TEXT("unset");
	OutSnapshot.MapIdentifier = GetWorld() ? FName(*GetWorld()->GetMapName()) : NAME_None;
	OutSnapshot.CapturedAt = FDateTime::UtcNow();

	OutSnapshot.CurrentTick = CurrentTick;
	OutSnapshot.SessionSeed = 0;
	OutSnapshot.PRNGState0 = static_cast<int64>(SimRandom.State0);
	OutSnapshot.PRNGState1 = static_cast<int64>(SimRandom.State1);

	OutSnapshot.MatchSettings = CurrentMatchSettings;
	OutSnapshot.MatchState = static_cast<uint8>(MatchState);
	OutSnapshot.MatchStartTick = MatchStartTick;
	OutSnapshot.StartingStateDeadlineTick = StartingStateDeadlineTick;

	OutSnapshot.PlayerStates = PlayerStates;

	OutSnapshot.Entities.Reset();
	EntityPool.ForEachEntity([&OutSnapshot, this](FSeinEntityHandle Handle, const FSeinEntity& Entity)
	{
		FSeinSnapshotEntityRecord Rec;
		Rec.SlotIndex = Handle.Index;
		Rec.Generation = Handle.Generation;
		Rec.Transform = Entity.Transform;
		Rec.Owner = EntityPool.GetOwner(Handle);
		Rec.bAlive = Entity.IsAlive();
		if (const TSubclassOf<ASeinActor>* SpawnedClass = EntityActorClassMap.Find(Handle))
		{
			if (UClass* CRef = SpawnedClass->Get())
			{
				Rec.ActorClassPath = CRef->GetPathName();
			}
		}
		OutSnapshot.Entities.Add(Rec);
	});

	OutSnapshot.ComponentStorageBlobs.Reset();
	for (auto& Pair : ComponentStorages)
	{
		UScriptStruct* StructType = Pair.Key;
		ISeinComponentStorage* Storage = Pair.Value;
		if (!StructType || !Storage) continue;

		// Wrap the FMemoryWriter in FObjectAndNameAsStringProxyArchive — the
		// base FMemoryArchive asserts (check(0)) on any UObject* serialization.
		// Component data may carry TSubclassOf<...> / FInstancedStruct / soft
		// refs that hit that path through UScriptStruct::SerializeBin. The
		// proxy archive stringifies UObject refs as paths (matches the replay
		// writer pattern, see SeinReplayWriter.cpp).
		FSeinSnapshotComponentStorageBlob Blob;
		FMemoryWriter MemWriter(Blob.Bytes, /*bIsPersistent*/ true);
		FObjectAndNameAsStringProxyArchive Writer(MemWriter, /*bInLoadIfFindFails*/ false);
		Blob.EntryCount = Storage->SerializeFromArchive(Writer);
		OutSnapshot.ComponentStorageBlobs.Add(StructType->GetPathName(), MoveTemp(Blob));
	}

	// ---- Ability pool ----
	// Capture per-slot UObject state via reflection. Cooldowns + bIsActive
	// + every UPROPERTY-tagged field on USeinAbility subclasses round-trips
	// through UObject::Serialize wrapped in the proxy archive. Free slots
	// are written too so the free-list reconstructs exactly on restore.
	OutSnapshot.AbilityPoolRecords.Reset(AbilityPool.Num());
	for (int32 ID = 0; ID < AbilityPool.Num(); ++ID)
	{
		FSeinSnapshotPoolInstanceRecord Rec;
		Rec.PoolID = ID;
		USeinAbility* A = AbilityPool[ID].Get();
		if (A)
		{
			Rec.bAlive = true;
			Rec.ClassPath = A->GetClass()->GetPathName();
			FMemoryWriter MemW(Rec.StateBytes, /*bIsPersistent*/ true);
			FObjectAndNameAsStringProxyArchive Writer(MemW, /*bInLoadIfFindFails*/ false);
			// CRITICAL: use SerializeTaggedProperties (UPROPERTY-only) instead
			// of UObject::Serialize. Full Serialize writes the object's
			// identity (Outer / Name / Class refs); deserializing those into
			// a fresh NewObject corrupts the outer chain → GetWorld() returns
			// null → BPs see "No world was found" → movement / nav / anything
			// world-context-aware silently fails. SerializeTaggedProperties
			// only round-trips UPROPERTY-tagged fields, leaving identity
			// intact, which is exactly what we want for state-only snapshot.
			UClass* Cls = A->GetClass();
			Cls->SerializeTaggedProperties(Writer, reinterpret_cast<uint8*>(A), Cls, nullptr);
		}
		OutSnapshot.AbilityPoolRecords.Add(MoveTemp(Rec));
	}

	// ---- Resolver pool ---- (same shape, same SerializeTaggedProperties rationale)
	OutSnapshot.ResolverPoolRecords.Reset(CommandBrokerResolverPool.Num());
	for (int32 ID = 0; ID < CommandBrokerResolverPool.Num(); ++ID)
	{
		FSeinSnapshotPoolInstanceRecord Rec;
		Rec.PoolID = ID;
		USeinCommandBrokerResolver* R = CommandBrokerResolverPool[ID].Get();
		if (R)
		{
			Rec.bAlive = true;
			Rec.ClassPath = R->GetClass()->GetPathName();
			FMemoryWriter MemW(Rec.StateBytes, /*bIsPersistent*/ true);
			FObjectAndNameAsStringProxyArchive Writer(MemW, /*bInLoadIfFindFails*/ false);
			UClass* Cls = R->GetClass();
			Cls->SerializeTaggedProperties(Writer, reinterpret_cast<uint8*>(R), Cls, nullptr);
		}
		OutSnapshot.ResolverPoolRecords.Add(MoveTemp(Rec));
	}

	UE_LOG(LogSeinSim, Log,
		TEXT("CaptureSnapshot: tick=%d  entities=%d  componentStorages=%d  playerStates=%d  abilityPool=%d  resolverPool=%d"),
		OutSnapshot.CurrentTick, OutSnapshot.Entities.Num(),
		OutSnapshot.ComponentStorageBlobs.Num(), OutSnapshot.PlayerStates.Num(),
		OutSnapshot.AbilityPoolRecords.Num(), OutSnapshot.ResolverPoolRecords.Num());

	// Let upstream modules (Framework: camera, UI; designer extensions) stamp
	// their own snapshot slots. See header for cycle-avoidance rationale.
	OnCaptureSnapshotPostSim.Broadcast(OutSnapshot);
}

bool USeinWorldSubsystem::RestoreSnapshot(const FSeinWorldSnapshot& InSnapshot)
{
	if (InSnapshot.SnapshotVersion != 1)
	{
		UE_LOG(LogSeinSim, Error, TEXT("RestoreSnapshot: unsupported version %d (expected 1)."), InSnapshot.SnapshotVersion);
		return false;
	}

	if (bIsRunning) StopSimulation();

	CurrentTick = InSnapshot.CurrentTick;
	SimRandom.State0 = static_cast<uint64>(InSnapshot.PRNGState0);
	SimRandom.State1 = static_cast<uint64>(InSnapshot.PRNGState1);

	CurrentMatchSettings = InSnapshot.MatchSettings;
	MatchState = static_cast<ESeinMatchState>(InSnapshot.MatchState);
	MatchStartTick = InSnapshot.MatchStartTick;
	StartingStateDeadlineTick = InSnapshot.StartingStateDeadlineTick;

	PlayerStates = InSnapshot.PlayerStates;

	// Wipe the actor-class map so reconcile spawns the right classes from
	// the snapshot's ActorClassPath records (rebuilt below).
	EntityActorClassMap.Reset();

	{
		int32 MaxSlot = 0;
		TArray<int32> Indices;
		TArray<int32> Gens;
		TArray<FFixedTransform> Transforms;
		TArray<FSeinPlayerID> Owners;
		TArray<bool> Alives;
		Indices.Reserve(InSnapshot.Entities.Num());
		Gens.Reserve(InSnapshot.Entities.Num());
		Transforms.Reserve(InSnapshot.Entities.Num());
		Owners.Reserve(InSnapshot.Entities.Num());
		Alives.Reserve(InSnapshot.Entities.Num());
		for (const FSeinSnapshotEntityRecord& Rec : InSnapshot.Entities)
		{
			Indices.Add(Rec.SlotIndex);
			Gens.Add(Rec.Generation);
			Transforms.Add(Rec.Transform);
			Owners.Add(Rec.Owner);
			Alives.Add(Rec.bAlive);
			MaxSlot = FMath::Max(MaxSlot, Rec.SlotIndex);

			// Rebuild EntityActorClassMap so the bridge's reconcile pass
			// (ReconcileBridgeAfterRestore, called below) can spawn the
			// correct class for entities the world has no actor for.
			if (!Rec.ActorClassPath.IsEmpty() && Rec.bAlive)
			{
				if (UClass* CRef = LoadClass<ASeinActor>(nullptr, *Rec.ActorClassPath))
				{
					const FSeinEntityHandle H(Rec.SlotIndex, Rec.Generation);
					EntityActorClassMap.Add(H, TSubclassOf<ASeinActor>(CRef));
				}
			}
		}
		EntityPool.RebuildFromSnapshot(MaxSlot, Indices, Gens, Transforms, Owners, Alives);
	}

	for (const auto& Pair : InSnapshot.ComponentStorageBlobs)
	{
		const FString& StructPath = Pair.Key;
		const FSeinSnapshotComponentStorageBlob& Blob = Pair.Value;

		UScriptStruct* StructType = FindObject<UScriptStruct>(nullptr, *StructPath);
		if (!StructType)
		{
			UE_LOG(LogSeinSim, Warning, TEXT("RestoreSnapshot: failed to resolve struct %s — skipping %d entries."),
				*StructPath, Blob.EntryCount);
			continue;
		}

		ISeinComponentStorage* Storage = GetOrCreateStorageForType(StructType);
		if (!Storage)
		{
			UE_LOG(LogSeinSim, Warning, TEXT("RestoreSnapshot: no storage for %s — skipping."), *StructPath);
			continue;
		}
		Storage->Grow(EntityPool.GetCapacity());

		// Mirror Capture's proxy-archive wrap. bInLoadIfFindFails=true so
		// referenced UObjects (typically class refs) resolve cleanly on load.
		FMemoryReader MemReader(Blob.Bytes, /*bIsPersistent*/ true);
		FObjectAndNameAsStringProxyArchive Reader(MemReader, /*bInLoadIfFindFails*/ true);
		const int32 Read = Storage->SerializeFromArchive(Reader);
		if (Read != Blob.EntryCount)
		{
			UE_LOG(LogSeinSim, Warning,
				TEXT("RestoreSnapshot: storage %s entry-count mismatch (read %d, blob said %d)."),
				*StructPath, Read, Blob.EntryCount);
		}
	}

	// ---- Ability + resolver pool reconstruction ----
	//
	// Defensive: cancel every running latent action FIRST. The latent action
	// manager holds raw refs into ability instances we're about to replace —
	// if we leave those running, they'd act on stale pointers + drive abilities
	// whose state we just clobbered. Latent-action serialization is a separate
	// concern; for now any in-flight latent execution is dropped on restore.
	if (LatentActionManager)
	{
		LatentActionManager->CancelAllActions();
	}

	// Wipe the existing pools. UPROPERTY arrays let GC reap the old instances
	// once we drop them. Then rebuild slot-by-slot: NewObject by class, walk
	// UObject::Serialize through the proxy archive to replay every UPROPERTY
	// (including CooldownRemaining + bIsActive on USeinAbility), place at the
	// original PoolID. Free slots stay null + go on the free list.
	auto RestorePool = [this](auto& Pool, auto& FreeList, const TArray<FSeinSnapshotPoolInstanceRecord>& Records, const TCHAR* PoolName)
	{
		using TPtr = typename TRemoveReference<decltype(Pool)>::Type::ElementType;
		using TObj = typename TPtr::ElementType;

		Pool.Reset();
		FreeList.Reset();
		Pool.SetNum(Records.Num());

		int32 Restored = 0;
		int32 FreeSlots = 0;
		for (const FSeinSnapshotPoolInstanceRecord& Rec : Records)
		{
			if (!Rec.bAlive)
			{
				Pool[Rec.PoolID] = nullptr;
				FreeList.Add(Rec.PoolID);
				++FreeSlots;
				continue;
			}
			UClass* Cls = LoadClass<TObj>(nullptr, *Rec.ClassPath);
			if (!Cls)
			{
				UE_LOG(LogSeinSim, Warning,
					TEXT("RestoreSnapshot: failed to load %s class %s — slot %d will be empty."),
					PoolName, *Rec.ClassPath, Rec.PoolID);
				Pool[Rec.PoolID] = nullptr;
				FreeList.Add(Rec.PoolID);
				++FreeSlots;
				continue;
			}
			TObj* Obj = NewObject<TObj>(this, Cls);
			TArray<uint8> Mutable = Rec.StateBytes;
			FMemoryReader MemR(Mutable, /*bIsPersistent*/ true);
			FObjectAndNameAsStringProxyArchive Reader(MemR, /*bInLoadIfFindFails*/ true);
			// Mirror Capture's SerializeTaggedProperties — UPROPERTY-only,
			// preserves identity. See Capture for the full rationale.
			Cls->SerializeTaggedProperties(Reader, reinterpret_cast<uint8*>(Obj), Cls, nullptr);
			Pool[Rec.PoolID] = Obj;
			++Restored;
		}

		UE_LOG(LogSeinSim, Log,
			TEXT("RestoreSnapshot: %s pool restored — %d instances, %d free slots."),
			PoolName, Restored, FreeSlots);
	};

	RestorePool(AbilityPool, AbilityPoolFreeList, InSnapshot.AbilityPoolRecords, TEXT("ability"));
	RestorePool(CommandBrokerResolverPool, CommandBrokerResolverPoolFreeList, InSnapshot.ResolverPoolRecords, TEXT("resolver"));

	// Re-bind the cached WorldSubsystem ref on each restored ability. The field
	// is `UPROPERTY(Transient)` (correctly — it shouldn't be in the snapshot
	// blob), but SerializeTaggedProperties skips Transient fields, so after
	// restore the ref is null. Without this, USeinAbility::GetWorld() (which
	// routes through the cached subsystem) returns null → BP nodes that need
	// a world context fail with "No world was found for object". Rebind to
	// `this` so the ability's world chain works again.
	for (TObjectPtr<USeinAbility>& Slot : AbilityPool)
	{
		if (USeinAbility* A = Slot.Get())
		{
			A->WorldSubsystem = this;
		}
	}

	// Post-restore: any ability whose bIsActive=true was captured mid-execution.
	// Cooldowns are preserved (CooldownRemaining round-tripped via the tagged-
	// property serialize), but the latent action that was driving the BP graph
	// is gone (we CancelAllActions'd them above to avoid stale-pointer crashes
	// — latent-action serialization is a follow-up phase). Force these
	// abilities back to idle so the entity's next command activates them
	// cleanly. ActiveAbilityID on the component remains valid; ProcessCommands
	// just sees bIsActive=false and runs the normal activation path.
	int32 NumWedgedReset = 0;
	for (TObjectPtr<USeinAbility>& Slot : AbilityPool)
	{
		USeinAbility* A = Slot.Get();
		if (A && A->bIsActive)
		{
			A->bIsActive = false;
			++NumWedgedReset;
		}
	}
	// Component-side ActiveAbilityID also needs clearing — otherwise the
	// AbilityTickSystem's GetActiveAbility() lookup still returns a non-null
	// ability (just one with bIsActive=false), which is benign but visually
	// suggests "still active" to debug overlays. Walk every entity with an
	// ability component and reset.
	if (FSeinGenericComponentStorage* AbilityStorage = static_cast<FSeinGenericComponentStorage*>(
		ComponentStorages.FindRef(FSeinAbilityComponent::StaticStruct())))
	{
		EntityPool.ForEachEntity([this, AbilityStorage](FSeinEntityHandle Handle, FSeinEntity& /*E*/)
		{
			if (FSeinAbilityComponent* Comp = static_cast<FSeinAbilityComponent*>(AbilityStorage->GetComponentRaw(Handle)))
			{
				Comp->ActiveAbilityID = INDEX_NONE;
			}
		});
	}
	if (NumWedgedReset > 0)
	{
		UE_LOG(LogSeinSim, Log,
			TEXT("RestoreSnapshot: reset %d ability(ies) that were active at capture (latent action serialization is a follow-up — cooldowns preserved, mid-execution state cleared)."),
			NumWedgedReset);
	}

	UE_LOG(LogSeinSim, Log,
		TEXT("RestoreSnapshot: tick=%d  entities=%d  componentStorages=%d  playerStates=%d  abilityPool=%d  resolverPool=%d"),
		CurrentTick, InSnapshot.Entities.Num(),
		InSnapshot.ComponentStorageBlobs.Num(), PlayerStates.Num(),
		AbilityPool.Num(), CommandBrokerResolverPool.Num());

	// Reconcile the actor bridge against the new sim state — cull orphaned
	// actors (entities that no longer exist) + spawn missing actors (entities
	// the snapshot has but the world doesn't). Bridge knows which class to
	// spawn from the EntityActorClassMap entries we rehydrated above.
	if (UWorld* W = GetWorld())
	{
		if (USeinActorBridgeSubsystem* Bridge = W->GetSubsystem<USeinActorBridgeSubsystem>())
		{
			Bridge->ReconcileBridgeAfterRestore();
		}
	}

	// Auto-restart the sim. Without this the actor bridge never sees the
	// new transforms — HandleSimTick (sim → render transform sync) is only
	// fired by the ticker, which StopSimulation (above) tore down. Once
	// the sim resumes, the very next tick fires the sync and every render
	// actor snaps to its restored sim transform.
	StartSimulation();

	// Let upstream modules consume their own slots (camera, UI). Fired
	// after the sim is fully live + bridge reconciled, so the restore
	// handlers can read a coherent world.
	OnRestoreSnapshotPostSim.Broadcast(InSnapshot);

	return true;
}

void USeinWorldSubsystem::RegisterFactionsFromSettings()
{
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	if (!Settings) return;

	int32 NumLoaded = 0;
	int32 NumSkipped = 0;
	for (const TSoftObjectPtr<USeinFaction>& SoftRef : Settings->RegisteredFactions)
	{
		if (SoftRef.IsNull())
		{
			++NumSkipped;
			continue;
		}
		// LoadSynchronous because we need the asset before any RegisterPlayer
		// looks it up. Settings-driven enumeration runs once per world init,
		// not per-tick, so the sync load is fine.
		USeinFaction* Faction = SoftRef.LoadSynchronous();
		if (!Faction)
		{
			UE_LOG(LogSeinSim, Warning,
				TEXT("RegisterFactionsFromSettings: failed to load %s — skipping. Asset moved or deleted?"),
				*SoftRef.ToString());
			++NumSkipped;
			continue;
		}
		if (!Faction->FactionID.IsValid())
		{
			UE_LOG(LogSeinSim, Warning,
				TEXT("RegisterFactionsFromSettings: %s has invalid FactionID (=0). Set a non-zero FactionID on the data asset."),
				*Faction->FactionName.ToString());
			++NumSkipped;
			continue;
		}
		RegisterFaction(Faction);
		++NumLoaded;
	}

	UE_LOG(LogSeinSim, Log,
		TEXT("RegisterFactionsFromSettings: registered %d faction(s) from settings (%d skipped)."),
		NumLoaded, NumSkipped);
}

// ==================== Tags ====================
//
// Per-entity tag state lives in EntityTagStates (keyed by handle) — see
// FSeinEntityTagState. Every alive entity has an entry (auto-created at
// spawn from the bridge's BaseTags UPROPERTY). Tag mutations refcount via
// the state's GrantTagInternal/UngrantTagInternal; this method keeps the
// global EntityTagIndex in sync on 0↔1 edges.

// --- FSeinEntityTagState helpers (formerly on FSeinTagData) ---

void FSeinEntityTagState::RebuildCombinedTags()
{
	CombinedTags.Reset();
	for (const TPair<FGameplayTag, int32>& Pair : TagRefCounts)
	{
		if (Pair.Value > 0)
		{
			CombinedTags.AddTag(Pair.Key);
		}
	}
}

bool FSeinEntityTagState::GrantTagInternal(const FGameplayTag& Tag)
{
	if (!Tag.IsValid()) return false;
	int32& RefCount = TagRefCounts.FindOrAdd(Tag, 0);
	++RefCount;
	if (RefCount == 1)
	{
		CombinedTags.AddTag(Tag);
		return true;
	}
	return false;
}

bool FSeinEntityTagState::UngrantTagInternal(const FGameplayTag& Tag)
{
	if (!Tag.IsValid()) return false;
	int32* RefCount = TagRefCounts.Find(Tag);
	if (!RefCount || *RefCount <= 0) return false;

	--(*RefCount);
	if (*RefCount == 0)
	{
		TagRefCounts.Remove(Tag);
		CombinedTags.RemoveTag(Tag);
		return true;
	}
	return false;
}

bool USeinWorldSubsystem::HasTag(FSeinEntityHandle Handle, FGameplayTag Tag) const
{
	const FSeinEntityTagState* TagState = EntityTagStates.Find(Handle);
	return TagState ? TagState->HasTag(Tag) : false;
}

bool USeinWorldSubsystem::HasAnyTag(FSeinEntityHandle Handle, const FGameplayTagContainer& Tags) const
{
	if (Tags.IsEmpty()) return false;
	const FSeinEntityTagState* TagState = EntityTagStates.Find(Handle);
	return TagState ? TagState->HasAnyTag(Tags) : false;
}

bool USeinWorldSubsystem::HasAllTags(FSeinEntityHandle Handle, const FGameplayTagContainer& Tags) const
{
	if (Tags.IsEmpty()) return true; // vacuously true — no tags required
	const FSeinEntityTagState* TagState = EntityTagStates.Find(Handle);
	return TagState ? TagState->HasAllTags(Tags) : false;
}

const FGameplayTagContainer& USeinWorldSubsystem::GetEntityTags(FSeinEntityHandle Handle) const
{
	static const FGameplayTagContainer Empty;
	const FSeinEntityTagState* TagState = EntityTagStates.Find(Handle);
	return TagState ? TagState->CombinedTags : Empty;
}

const FGameplayTagContainer& USeinWorldSubsystem::GetEntityBaseTags(FSeinEntityHandle Handle) const
{
	static const FGameplayTagContainer Empty;
	const FSeinEntityTagState* TagState = EntityTagStates.Find(Handle);
	return TagState ? TagState->BaseTags : Empty;
}

void USeinWorldSubsystem::GrantTag(FSeinEntityHandle Handle, FGameplayTag Tag)
{
	if (!Tag.IsValid()) return;
	// FindOrAdd — auto-create the entity's tag state if it doesn't exist yet
	// (e.g., transient grants from abilities/effects on entities that didn't
	// author any BaseTags). Refcount handles the rest.
	FSeinEntityTagState& TagState = EntityTagStates.FindOrAdd(Handle);

	if (TagState.GrantTagInternal(Tag))
	{
		EntityTagIndex.FindOrAdd(Tag).Add(Handle);
	}
}

void USeinWorldSubsystem::UngrantTag(FSeinEntityHandle Handle, FGameplayTag Tag)
{
	if (!Tag.IsValid()) return;
	FSeinEntityTagState* TagState = EntityTagStates.Find(Handle);
	if (!TagState) return;

	if (TagState->UngrantTagInternal(Tag))
	{
		if (TArray<FSeinEntityHandle>* Bucket = EntityTagIndex.Find(Tag))
		{
			Bucket->RemoveSingle(Handle);
			if (Bucket->Num() == 0)
			{
				EntityTagIndex.Remove(Tag);
			}
		}
	}
}

// --- Player tags (refcounted, mirrors entity tag plumbing above) ---

void USeinWorldSubsystem::GrantPlayerTag(FSeinPlayerID PlayerID, FGameplayTag Tag)
{
	if (!Tag.IsValid()) return;
	FSeinPlayerState* State = GetPlayerStateMutable(PlayerID);
	if (!State) return;

	int32& Count = State->PlayerTagRefCounts.FindOrAdd(Tag);
	const int32 Old = Count++;
	if (Old == 0)
	{
		State->PlayerTags.AddTag(Tag);
	}
}

void USeinWorldSubsystem::UngrantPlayerTag(FSeinPlayerID PlayerID, FGameplayTag Tag)
{
	if (!Tag.IsValid()) return;
	FSeinPlayerState* State = GetPlayerStateMutable(PlayerID);
	if (!State) return;

	int32* Count = State->PlayerTagRefCounts.Find(Tag);
	if (!Count || *Count <= 0) return;

	--(*Count);
	if (*Count == 0)
	{
		State->PlayerTagRefCounts.Remove(Tag);
		State->PlayerTags.RemoveTag(Tag);
	}
}

bool USeinWorldSubsystem::AddBaseTag(FSeinEntityHandle Handle, FGameplayTag Tag)
{
	if (!Tag.IsValid()) return false;
	FSeinEntityTagState& TagState = EntityTagStates.FindOrAdd(Handle);
	if (TagState.BaseTags.HasTagExact(Tag)) return false;

	TagState.BaseTags.AddTag(Tag);
	GrantTag(Handle, Tag);
	return true;
}

bool USeinWorldSubsystem::RemoveBaseTag(FSeinEntityHandle Handle, FGameplayTag Tag)
{
	if (!Tag.IsValid()) return false;
	FSeinEntityTagState* TagState = EntityTagStates.Find(Handle);
	if (!TagState) return false;
	if (!TagState->BaseTags.HasTagExact(Tag)) return false;

	TagState->BaseTags.RemoveTag(Tag);
	UngrantTag(Handle, Tag);
	return true;
}

void USeinWorldSubsystem::ReplaceBaseTags(FSeinEntityHandle Handle, const FGameplayTagContainer& NewBaseTags)
{
	FSeinEntityTagState& TagState = EntityTagStates.FindOrAdd(Handle);

	// Diff old vs new. Touch refcounts only for tags that actually changed
	// membership in BaseTags — tags that persist keep their existing refcount.
	FGameplayTagContainer ToUngrant;
	for (const FGameplayTag& Existing : TagState.BaseTags)
	{
		if (!NewBaseTags.HasTagExact(Existing))
		{
			ToUngrant.AddTag(Existing);
		}
	}
	FGameplayTagContainer ToGrant;
	for (const FGameplayTag& Incoming : NewBaseTags)
	{
		if (!TagState.BaseTags.HasTagExact(Incoming))
		{
			ToGrant.AddTag(Incoming);
		}
	}

	TagState.BaseTags = NewBaseTags;
	for (const FGameplayTag& Tag : ToUngrant) UngrantTag(Handle, Tag);
	for (const FGameplayTag& Tag : ToGrant)   GrantTag(Handle, Tag);
}

TArray<FSeinEntityHandle> USeinWorldSubsystem::GetEntitiesWithTag(FGameplayTag Tag) const
{
	if (const TArray<FSeinEntityHandle>* Bucket = EntityTagIndex.Find(Tag))
	{
		return *Bucket;
	}
	return {};
}

const TArray<FSeinEntityHandle>* USeinWorldSubsystem::FindEntitiesWithTag(FGameplayTag Tag) const
{
	return EntityTagIndex.Find(Tag);
}

// ==================== Named Entity Registry ====================

void USeinWorldSubsystem::RegisterNamedEntity(FName Name, FSeinEntityHandle Handle)
{
	if (Name.IsNone()) return;
	if (!EntityPool.IsValid(Handle)) return;
	NamedEntityRegistry.Add(Name, Handle);
}

FSeinEntityHandle USeinWorldSubsystem::LookupNamedEntity(FName Name) const
{
	if (Name.IsNone()) return FSeinEntityHandle::Invalid();
	if (const FSeinEntityHandle* Found = NamedEntityRegistry.Find(Name))
	{
		return *Found;
	}
	return FSeinEntityHandle::Invalid();
}

void USeinWorldSubsystem::UnregisterNamedEntity(FName Name)
{
	NamedEntityRegistry.Remove(Name);
}

// ==================== Attribute Resolution ====================

FFixedPoint USeinWorldSubsystem::ResolveAttribute(FSeinEntityHandle Handle, UScriptStruct* ComponentType, FName FieldName)
{
	ISeinComponentStorage* Storage = GetComponentStorageRaw(ComponentType);
	if (!Storage) return FFixedPoint::Zero;

	void* CompData = Storage->GetComponentRaw(Handle);
	if (!CompData) return FFixedPoint::Zero;

	const FFixedPoint BaseValue = FSeinAttributeResolver::ReadFixedPointField(CompData, ComponentType, FieldName);

	TArray<FSeinModifier> AllModifiers;

	// Instance-scope: walk the entity's active effects; CDO supplies the modifier list.
	if (const FSeinActiveEffectsComponent* EffectsComp = GetComponent<FSeinActiveEffectsComponent>(Handle))
	{
		for (const FSeinActiveEffect& Effect : EffectsComp->ActiveEffects)
		{
			const USeinEffect* Def = Effect.EffectClass ? GetDefault<USeinEffect>(Effect.EffectClass) : nullptr;
			if (!Def) continue;
			for (const FSeinModifier& Mod : Def->Modifiers)
			{
				if (Mod.TargetComponentType != ComponentType) continue;
				if (Mod.TargetFieldName != FieldName) continue;
				// Instance-scope modifiers (the effect's scope drives semantics; modifiers
				// on an Instance-scope effect are Instance by construction — the per-modifier
				// Scope field remains for legacy compatibility).
				for (int32 Stack = 0; Stack < Effect.CurrentStacks; ++Stack)
				{
					FSeinModifier& Added = AllModifiers.Add_GetRef(Mod);
					Added.SourceEntity = Effect.Source;
					Added.SourceEffectID = Effect.EffectInstanceID;
				}
			}
		}
	}

	const FSeinPlayerID OwnerID = GetEntityOwner(Handle);
	if (const FSeinPlayerState* PlayerState = GetPlayerState(OwnerID))
	{
		const FGameplayTagContainer& EntityTags = GetEntityTags(Handle);

		// Class-scope: iterate the player's class effects; CDO modifiers are filtered
		// by TargetClassTag against the entity's tags.
		for (const FSeinActiveEffect& Effect : PlayerState->ClassEffects)
		{
			const USeinEffect* Def = Effect.EffectClass ? GetDefault<USeinEffect>(Effect.EffectClass) : nullptr;
			if (!Def) continue;
			for (const FSeinModifier& Mod : Def->Modifiers)
			{
				if (Mod.TargetComponentType != ComponentType) continue;
				if (Mod.TargetFieldName != FieldName) continue;
				const FGameplayTag ArchTag = Mod.TargetClassTag.IsValid()
					? Mod.TargetClassTag
					: Def->DefaultTargetClassTag;
				if (ArchTag.IsValid() && !EntityTags.HasTag(ArchTag))
				{
					continue;
				}
				for (int32 Stack = 0; Stack < Effect.CurrentStacks; ++Stack)
				{
					FSeinModifier& Added = AllModifiers.Add_GetRef(Mod);
					Added.SourceEntity = Effect.Source;
					Added.SourceEffectID = Effect.EffectInstanceID;
				}
			}
		}

		// Legacy `ArchetypeModifiers` flat list retired in Session 2.4 — tech-granted
		// class-scope modifiers now flow through `ClassEffects` above via the
		// unified effect pipeline.
	}

	if (AllModifiers.Num() == 0)
	{
		return BaseValue;
	}

	return FSeinAttributeResolver::ResolveModifiers(BaseValue, AllModifiers);
}

FFixedPoint USeinWorldSubsystem::ResolvePlayerAttribute(FSeinPlayerID PlayerID, UScriptStruct* StructType, FName FieldName) const
{
	const FSeinPlayerState* State = GetPlayerState(PlayerID);
	if (!State || !StructType)
	{
		return FFixedPoint::Zero;
	}

	// Base value — PlayerState itself is the only player-scope struct we reflect today;
	// future sub-structs (income rates, caps) can be targeted the same way.
	const void* BaseStruct = StructType == FSeinPlayerState::StaticStruct() ? static_cast<const void*>(State) : nullptr;
	FFixedPoint BaseValue = FFixedPoint::Zero;
	if (BaseStruct)
	{
		BaseValue = FSeinAttributeResolver::ReadFixedPointField(const_cast<void*>(BaseStruct), StructType, FieldName);
	}

	TArray<FSeinModifier> AllModifiers;
	for (const FSeinActiveEffect& Effect : State->PlayerEffects)
	{
		const USeinEffect* Def = Effect.EffectClass ? GetDefault<USeinEffect>(Effect.EffectClass) : nullptr;
		if (!Def) continue;
		for (const FSeinModifier& Mod : Def->Modifiers)
		{
			if (Mod.TargetComponentType != StructType) continue;
			if (Mod.TargetFieldName != FieldName) continue;
			for (int32 Stack = 0; Stack < Effect.CurrentStacks; ++Stack)
			{
				FSeinModifier& Added = AllModifiers.Add_GetRef(Mod);
				Added.SourceEntity = Effect.Source;
				Added.SourceEffectID = Effect.EffectInstanceID;
			}
		}
	}

	if (AllModifiers.Num() == 0)
	{
		return BaseValue;
	}
	return FSeinAttributeResolver::ResolveModifiers(BaseValue, AllModifiers);
}

// ==================== Effects ====================

uint32 USeinWorldSubsystem::ApplyEffect(FSeinEntityHandle Target, TSubclassOf<USeinEffect> EffectClass, FSeinEntityHandle Source)
{
	if (!EffectClass || !EntityPool.IsValid(Target))
	{
		return 0;
	}

	// If we're inside a sim tick, defer to the PreTick drain. Outside a sim tick
	// (render-side authored apply, test harness), commit synchronously. In shipping
	// SEIN_IS_SIM_CONTEXT() is always true (sim-only macro stripped) — correct
	// default since shipping applies run during the sim tick.
	if (SEIN_IS_SIM_CONTEXT())
	{
		PendingEffectApplies.Add({ Target, EffectClass, Source });
		return 0;
	}
	return ApplyEffectInternal(Target, EffectClass, Source);
}

void USeinWorldSubsystem::ProcessPendingEffectApplies()
{
	if (PendingEffectApplies.Num() == 0)
	{
		return;
	}
	// Swap-out the current queue so any applies-from-hooks land in a fresh queue
	// for the NEXT PreTick (per DESIGN §8 Q9c apply-batching).
	TArray<FSeinPendingEffectApply> Draining;
	Draining.Reserve(PendingEffectApplies.Num());
	Swap(Draining, PendingEffectApplies);

	for (const FSeinPendingEffectApply& P : Draining)
	{
		ApplyEffectInternal(P.Target, P.EffectClass, P.Source);
	}
}

uint32 USeinWorldSubsystem::ApplyEffectInternal(FSeinEntityHandle Target, TSubclassOf<USeinEffect> EffectClass, FSeinEntityHandle Source)
{
	if (!EffectClass || !EntityPool.IsValid(Target))
	{
		return 0;
	}
	const USeinEffect* CDO = GetDefault<USeinEffect>(EffectClass);
	if (!CDO)
	{
		return 0;
	}

	// --- Strip any existing effects matching RemoveEffectsWithTag ---
	for (const FGameplayTag& Tag : CDO->RemoveEffectsWithTag)
	{
		RemoveInstanceEffectsWithTag(Target, Tag);
	}

	// --- Prepare scope-specific storage pointers ---
	TArray<FSeinActiveEffect>* Storage = nullptr;
	uint32* IdCounter = nullptr;
	FSeinPlayerState* OwnerState = nullptr;
	FSeinActiveEffectsComponent* InstanceComp = nullptr;

	switch (CDO->Scope)
	{
		case ESeinModifierScope::Instance:
		{
			InstanceComp = GetComponent<FSeinActiveEffectsComponent>(Target);
			if (!InstanceComp) { return 0; }
			Storage = &InstanceComp->ActiveEffects;
			IdCounter = &InstanceComp->NextEffectInstanceID;
			break;
		}
		case ESeinModifierScope::Class:
		case ESeinModifierScope::Player:
		{
			const FSeinPlayerID OwnerID = GetEntityOwner(Target);
			OwnerState = GetPlayerStateMutable(OwnerID);
			if (!OwnerState) { return 0; }
			Storage = CDO->Scope == ESeinModifierScope::Class
				? &OwnerState->ClassEffects
				: &OwnerState->PlayerEffects;
			IdCounter = &OwnerState->NextEffectInstanceID;
			break;
		}
	}

	if (!Storage || !IdCounter) { return 0; }

	// --- Stacking ---
	FSeinActiveEffect* Existing = nullptr;
	if (CDO->StackingRule != ESeinEffectStackingRule::Independent)
	{
		for (FSeinActiveEffect& E : *Storage)
		{
			if (E.EffectClass == EffectClass) { Existing = &E; break; }
		}
	}

	uint32 AssignedID = 0;
	bool bIsNewInstance = false;

	if (Existing && CDO->StackingRule == ESeinEffectStackingRule::Stack)
	{
		// Stack re-apply: bump CurrentStacks and refresh duration; no new instance,
		// no OnApply hook, no additional GrantedTags refcount.
		Existing->CurrentStacks = FMath::Min(Existing->CurrentStacks + 1, CDO->MaxStacks);
		if (CDO->DurationMode == ESeinEffectDurationMode::Timed)
		{
			Existing->RemainingDuration = CDO->Duration;
		}
		AssignedID = Existing->EffectInstanceID;
	}
	else if (Existing && CDO->StackingRule == ESeinEffectStackingRule::Refresh)
	{
		// Refresh re-apply: stacks stay at 1, duration refreshes. No OnApply.
		Existing->CurrentStacks = 1;
		if (CDO->DurationMode == ESeinEffectDurationMode::Timed)
		{
			Existing->RemainingDuration = CDO->Duration;
		}
		AssignedID = Existing->EffectInstanceID;
	}
	else
	{
		// Independent, or no existing instance: reject if storage already at MaxStacks for this class.
		if (CDO->StackingRule == ESeinEffectStackingRule::Independent)
		{
			int32 Count = 0;
			for (const FSeinActiveEffect& E : *Storage)
			{
				if (E.EffectClass == EffectClass) ++Count;
			}
			if (Count >= CDO->MaxStacks)
			{
				return 0;
			}
		}

		// --- Dev-mode apply-count warning ---
#if !UE_BUILD_SHIPPING
		{
			const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
			const int32 Threshold = Settings ? Settings->EffectCountWarningThreshold : 256;
			const int32 BeforeCount = Storage->Num();
			if (Threshold > 0 && BeforeCount < Threshold && BeforeCount + 1 >= Threshold)
			{
				UE_LOG(LogSeinSim, Warning,
					TEXT("Effect apply count crossing threshold (%d) for target %s — possible runaway effect loop"),
					Threshold, *Target.ToString());
			}
		}
#endif

		FSeinActiveEffect NewEffect;
		NewEffect.EffectClass = EffectClass;
		NewEffect.Source = Source;
		NewEffect.Target = Target;
		NewEffect.CurrentStacks = 1;
		NewEffect.TimeSinceLastPeriodic = FFixedPoint::Zero;
		// RemainingDuration only meaningful for Timed mode. Instant/Persistent
		// effects don't tick down; leaving at the CDO default keeps state hash
		// stable and the tick system early-exits via DurationMode anyway.
		NewEffect.RemainingDuration = (CDO->DurationMode == ESeinEffectDurationMode::Timed)
			? CDO->Duration
			: FFixedPoint::Zero;
		NewEffect.EffectInstanceID = (*IdCounter)++;
		Storage->Add(NewEffect);
		AssignedID = NewEffect.EffectInstanceID;
		bIsNewInstance = true;

		// --- Grant tags (refcount) ---
		// Per DESIGN §10 tech unification:
		//   Instance scope → GrantedTags go to the target entity.
		//   Class / Player scope → EffectTag + GrantedTags go to the target owner's
		//     player-state tag set (refcounted via GrantPlayerTag).
		if (CDO->Scope == ESeinModifierScope::Instance)
		{
			for (const FGameplayTag& Tag : CDO->GrantedTags)
			{
				GrantTag(Target, Tag);
			}
		}
		else
		{
			const FSeinPlayerID Owner = GetEntityOwner(Target);
			if (CDO->EffectTag.IsValid())
			{
				GrantPlayerTag(Owner, CDO->EffectTag);
			}
			for (const FGameplayTag& Tag : CDO->GrantedTags)
			{
				GrantPlayerTag(Owner, Tag);
			}
		}

		// --- Grant abilities (Option C: effect-driven runtime ability grants) ---
		// Instance scope → grant to Target. Class/Player scope → fan out to
		// every entity the affected player owns whose tag state contains
		// `AbilityTargetClassTag`. Empty AbilityTargetClassTag = no fan-out
		// (designer guard against unintended "grant to everyone" footgun).
		// Spawn-time replay handles entities that come online AFTER apply
		// (see InitializeEntityAbilities's effect-replay block).
		if (CDO->GrantedAbilities.Num() > 0)
		{
			if (CDO->Scope == ESeinModifierScope::Instance)
			{
				for (const TSubclassOf<USeinAbility>& AbilityClass : CDO->GrantedAbilities)
				{
					if (AbilityClass)
					{
						USeinAbilityBPFL::SeinGrantAbility(this, Target, AbilityClass);
					}
				}
			}
			else if (CDO->AbilityTargetClassTag.IsValid())
			{
				const FSeinPlayerID Owner = GetEntityOwner(Target);
				const FGameplayTag ClassFilter = CDO->AbilityTargetClassTag;
				// Walk every live entity; gate on owner match + class-tag match
				// before granting. EntityPool iteration is the only enumeration
				// surface today (no per-player index); cost is O(N entities ×
				// N granted abilities) per effect apply, fine for tech-tier
				// frequency (apply runs at most a handful of times per match).
				EntityPool.ForEachEntity([&](FSeinEntityHandle Other, const FSeinEntity& /*OtherEntity*/)
				{
					if (GetEntityOwner(Other) != Owner) return;
					if (!HasTag(Other, ClassFilter)) return;
					for (const TSubclassOf<USeinAbility>& AbilityClass : CDO->GrantedAbilities)
					{
						if (AbilityClass)
						{
							USeinAbilityBPFL::SeinGrantAbility(this, Other, AbilityClass);
						}
					}
				});
			}
		}
	}

	// OnApply fires only on new instances — Stack / Refresh re-applies are
	// "same effect, refreshed" and do not re-trigger the apply hook.
	if (bIsNewInstance)
	{
		USeinEffect* MutableCDO = Cast<USeinEffect>(EffectClass->GetDefaultObject());
		if (MutableCDO)
		{
			MutableCDO->OnApply(Target, Source);
		}
		EnqueueVisualEvent(FSeinVisualEvent::MakeEffectEvent(Target, CDO->EffectTag, /*bApplied=*/true));
	}

	// Instant effect (DurationMode == Instant): remove immediately. OnApply
	// already fired; OnExpire is gated inside RemoveInstanceEffect to not
	// fire for Instant-mode effects (no real expiration occurred).
	if (bIsNewInstance && CDO->DurationMode == ESeinEffectDurationMode::Instant)
	{
		RemoveInstanceEffect(Target, AssignedID, /*bByExpiration=*/true);
	}

	return AssignedID;
}

bool USeinWorldSubsystem::RemoveInstanceEffect(FSeinEntityHandle Target, uint32 EffectInstanceID, bool bByExpiration)
{
	// Locate across all three scope storages (Instance on entity; Class / Player on owner).
	// An instance ID is only unique within its containing storage, so we check each.
	auto TryRemoveFromArray = [&](TArray<FSeinActiveEffect>& Storage, FSeinPlayerID PlayerForTags) -> bool
	{
		for (int32 i = 0; i < Storage.Num(); ++i)
		{
			if (Storage[i].EffectInstanceID == EffectInstanceID)
			{
				const FSeinActiveEffect Effect = Storage[i];
				Storage.RemoveAtSwap(i, EAllowShrinking::No);

				const USeinEffect* Def = Effect.EffectClass ? GetDefault<USeinEffect>(Effect.EffectClass) : nullptr;
				// Symmetrical ungrant — see ApplyEffectInternal grant branch.
				if (Def)
				{
					if (Def->Scope == ESeinModifierScope::Instance)
					{
						for (const FGameplayTag& Tag : Def->GrantedTags)
						{
							UngrantTag(Target, Tag);
						}
					}
					else
					{
						if (Def->EffectTag.IsValid())
						{
							UngrantPlayerTag(PlayerForTags, Def->EffectTag);
						}
						for (const FGameplayTag& Tag : Def->GrantedTags)
						{
							UngrantPlayerTag(PlayerForTags, Tag);
						}
					}

					// Revoke ability grants — mirror of ApplyEffectInternal's
					// grant fan-out. By-class revoke (not by-tag) because we
					// know exactly which classes this effect granted; by-tag
					// would accidentally revoke abilities granted by OTHER
					// sources that happen to share an AbilityTag.
					if (Def->GrantedAbilities.Num() > 0)
					{
						if (Def->Scope == ESeinModifierScope::Instance)
						{
							for (const TSubclassOf<USeinAbility>& AbilityClass : Def->GrantedAbilities)
							{
								if (AbilityClass)
								{
									USeinAbilityBPFL::SeinRevokeAbilityByClass(this, Target, AbilityClass);
								}
							}
						}
						else if (Def->AbilityTargetClassTag.IsValid())
						{
							const FGameplayTag ClassFilter = Def->AbilityTargetClassTag;
							EntityPool.ForEachEntity([&](FSeinEntityHandle Other, const FSeinEntity& /*OtherEntity*/)
							{
								if (GetEntityOwner(Other) != PlayerForTags) return;
								if (!HasTag(Other, ClassFilter)) return;
								for (const TSubclassOf<USeinAbility>& AbilityClass : Def->GrantedAbilities)
								{
									if (AbilityClass)
									{
										USeinAbilityBPFL::SeinRevokeAbilityByClass(this, Other, AbilityClass);
									}
								}
							});
						}
					}
				}

				if (Def)
				{
					USeinEffect* MutableCDO = Cast<USeinEffect>(Effect.EffectClass->GetDefaultObject());
					if (MutableCDO)
					{
						// OnExpire fires only for Timed effects that actually reached
						// the end of their duration. Instant and Persistent modes
						// never fire OnExpire (no real expiration occurred).
						const bool bHadRealDuration = (Def->DurationMode == ESeinEffectDurationMode::Timed);
						if (bByExpiration && bHadRealDuration)
						{
							MutableCDO->OnExpire(Target);
						}
						MutableCDO->OnRemoved(Target, bByExpiration);
					}
					EnqueueVisualEvent(FSeinVisualEvent::MakeEffectEvent(Target, Def->EffectTag, /*bApplied=*/false));
				}
				return true;
			}
		}
		return false;
	};

	const FSeinPlayerID OwnerID = GetEntityOwner(Target);
	if (FSeinActiveEffectsComponent* InstanceComp = GetComponent<FSeinActiveEffectsComponent>(Target))
	{
		if (TryRemoveFromArray(InstanceComp->ActiveEffects, OwnerID))
		{
			return true;
		}
	}

	if (FSeinPlayerState* OwnerState = GetPlayerStateMutable(OwnerID))
	{
		if (TryRemoveFromArray(OwnerState->ClassEffects, OwnerID))
		{
			return true;
		}
		if (TryRemoveFromArray(OwnerState->PlayerEffects, OwnerID))
		{
			return true;
		}
	}
	return false;
}

void USeinWorldSubsystem::RemoveInstanceEffectsWithTag(FSeinEntityHandle Target, FGameplayTag Tag)
{
	if (!Tag.IsValid()) return;

	auto RemoveMatching = [&](TArray<FSeinActiveEffect>& Storage, FSeinPlayerID PlayerForTags)
	{
		for (int32 i = Storage.Num() - 1; i >= 0; --i)
		{
			const USeinEffect* Def = Storage[i].EffectClass ? GetDefault<USeinEffect>(Storage[i].EffectClass) : nullptr;
			if (!Def || !Def->EffectTag.MatchesTag(Tag)) continue;

			const FSeinActiveEffect Effect = Storage[i];
			Storage.RemoveAtSwap(i, EAllowShrinking::No);

			if (Def->Scope == ESeinModifierScope::Instance)
			{
				for (const FGameplayTag& GT : Def->GrantedTags)
				{
					UngrantTag(Target, GT);
				}
			}
			else
			{
				if (Def->EffectTag.IsValid())
				{
					UngrantPlayerTag(PlayerForTags, Def->EffectTag);
				}
				for (const FGameplayTag& GT : Def->GrantedTags)
				{
					UngrantPlayerTag(PlayerForTags, GT);
				}
			}

			USeinEffect* MutableCDO = Cast<USeinEffect>(Effect.EffectClass->GetDefaultObject());
			if (MutableCDO)
			{
				MutableCDO->OnRemoved(Target, /*bByExpiration=*/false);
			}
			EnqueueVisualEvent(FSeinVisualEvent::MakeEffectEvent(Target, Def->EffectTag, /*bApplied=*/false));
		}
	};

	const FSeinPlayerID OwnerID = GetEntityOwner(Target);
	if (FSeinActiveEffectsComponent* InstanceComp = GetComponent<FSeinActiveEffectsComponent>(Target))
	{
		RemoveMatching(InstanceComp->ActiveEffects, OwnerID);
	}
	if (FSeinPlayerState* OwnerState = GetPlayerStateMutable(OwnerID))
	{
		RemoveMatching(OwnerState->ClassEffects, OwnerID);
		RemoveMatching(OwnerState->PlayerEffects, OwnerID);
	}
}

void USeinWorldSubsystem::RemoveEffectsFromDeadSource(FSeinEntityHandle DeadHandle)
{
	if (!DeadHandle.IsValid()) return;

	auto WantsSourceDeathRemoval = [](const TSubclassOf<USeinEffect>& Class) -> bool
	{
		const USeinEffect* Def = Class ? GetDefault<USeinEffect>(Class) : nullptr;
		return Def && Def->bRemoveOnSourceDeath;
	};

	// Collect first, remove after — RemoveInstanceEffect mutates the same storages
	// we're iterating, so buffering the hits keeps iteration stable.
	TArray<TPair<FSeinEntityHandle, uint32>> ToRemove;

	// Instance scope: every entity's FSeinActiveEffectsComponent.
	EntityPool.ForEachEntity([&](FSeinEntityHandle Handle, FSeinEntity& /*Entity*/)
	{
		const FSeinActiveEffectsComponent* EffectsComp = GetComponent<FSeinActiveEffectsComponent>(Handle);
		if (!EffectsComp) return;
		for (const FSeinActiveEffect& E : EffectsComp->ActiveEffects)
		{
			if (E.Source == DeadHandle && WantsSourceDeathRemoval(E.EffectClass))
			{
				ToRemove.Add({ Handle, E.EffectInstanceID });
			}
		}
	});

	// Class / Player scope: every player state.
	ForEachPlayerStateMutable([&](FSeinPlayerID /*PID*/, FSeinPlayerState& State)
	{
		for (const FSeinActiveEffect& E : State.ClassEffects)
		{
			if (E.Source == DeadHandle && WantsSourceDeathRemoval(E.EffectClass))
			{
				ToRemove.Add({ E.Target, E.EffectInstanceID });
			}
		}
		for (const FSeinActiveEffect& E : State.PlayerEffects)
		{
			if (E.Source == DeadHandle && WantsSourceDeathRemoval(E.EffectClass))
			{
				ToRemove.Add({ E.Target, E.EffectInstanceID });
			}
		}
	});

	// bByExpiration=false — this is cancellation by source death, not natural expiry.
	for (const TPair<FSeinEntityHandle, uint32>& R : ToRemove)
	{
		RemoveInstanceEffect(R.Key, R.Value, /*bByExpiration=*/false);
	}
}

// ==================== Component Storage Helpers ====================

ISeinComponentStorage* USeinWorldSubsystem::GetComponentStorageRaw(UScriptStruct* StructType)
{
	ISeinComponentStorage** Found = ComponentStorages.Find(StructType);
	return Found ? *Found : nullptr;
}

const ISeinComponentStorage* USeinWorldSubsystem::GetComponentStorageRaw(UScriptStruct* StructType) const
{
	ISeinComponentStorage* const* Found = ComponentStorages.Find(StructType);
	return Found ? *Found : nullptr;
}

ISeinComponentStorage* USeinWorldSubsystem::GetOrCreateStorageForType(UScriptStruct* StructType)
{
	if (ISeinComponentStorage** Found = ComponentStorages.Find(StructType))
	{
		return *Found;
	}

	FSeinGenericComponentStorage* Storage = new FSeinGenericComponentStorage(StructType, EntityPool.GetCapacity());
	ComponentStorages.Add(StructType, Storage);

	UE_LOG(LogSeinSim, Verbose, TEXT("Created component storage for %s"), *StructType->GetName());

#if !UE_BUILD_SHIPPING
	// Determinism guard (dev only, once per type): warn if this component carries
	// state the desync hash silently drops (TMap/TSet, structs without
	// WithGetTypeHash, arrays of such). Such drift would NOT be caught by the
	// state-hash gossip. See FSeinGenericComponentStorage::ComputeHash.
	{
		TArray<FString> Unhashed;
		FSeinGenericComponentStorage::CollectUnhashedStateFields(StructType, Unhashed);
		if (Unhashed.Num() > 0)
		{
			UE_LOG(LogSeinSim, Warning,
				TEXT("Component '%s' has field(s) excluded from the determinism state hash: %s. ")
				TEXT("Per-instance state in these fields will NOT be caught by desync detection — prefer ")
				TEXT("hashable types (scalars, FName, FGameplayTag, FFixed*) or extend ComputeHash."),
				*StructType->GetName(), *FString::Join(Unhashed, TEXT(", ")));
		}
	}
#endif

	return Storage;
}

// ==================== System Registration ====================

void USeinWorldSubsystem::RegisterSystem(ISeinSystem* System)
{
	if (System && !Systems.Contains(System))
	{
		Systems.Add(System);
		bSystemsSorted = false;
		UE_LOG(LogSeinSim, Log, TEXT("Registered system: %s (phase: %d, priority: %d)"),
			*System->GetSystemName().ToString(),
			static_cast<int32>(System->GetPhase()),
			System->GetPriority());
	}
}

void USeinWorldSubsystem::UnregisterSystem(ISeinSystem* System)
{
	Systems.Remove(System);
}

void USeinWorldSubsystem::SortSystemsIfNeeded()
{
	if (!bSystemsSorted)
	{
		Algo::Sort(Systems, [](const ISeinSystem* A, const ISeinSystem* B)
		{
			if (A->GetPhase() != B->GetPhase())
			{
				return static_cast<uint8>(A->GetPhase()) < static_cast<uint8>(B->GetPhase());
			}
			if (A->GetPriority() != B->GetPriority())
			{
				return A->GetPriority() < B->GetPriority();
			}
			// Total-order tiebreak: equal (phase, priority) systems must sort
			// identically on every client. Algo::Sort is unstable, so a priority
			// collision between cross-module systems (Nav/Squad/Cover/Movement)
			// would otherwise have unspecified — potentially divergent — tick
			// order. LexicalLess is a deterministic by-string compare (FName
			// index order is NOT stable across runs/builds).
			return A->GetSystemName().LexicalLess(B->GetSystemName());
		});
		bSystemsSorted = true;
	}
}

// ==================== Visual Events ====================

void USeinWorldSubsystem::EnqueueVisualEvent(const FSeinVisualEvent& Event)
{
	VisualEventQueue.Enqueue(Event);
}

TArray<FSeinVisualEvent> USeinWorldSubsystem::FlushVisualEvents()
{
	return VisualEventQueue.Flush();
}

// ==================== State Hashing ====================

namespace
{
	// Tag comparator — iteration order that's stable across processes.
	FORCEINLINE bool TagNameLess(const FGameplayTag& A, const FGameplayTag& B)
	{
		return A.GetTagName().Compare(B.GetTagName()) < 0;
	}

	// Walk a TMap<FGameplayTag, T> in sorted-by-name order + hash each
	// (tag, value) pair. Handles the cross-process-stable iteration the
	// audit flagged for pointer-keyed maps + any tag-keyed state that
	// contributes to the sim hash.
	template<typename ValueType, typename HashValueFn>
	FORCEINLINE void HashTagMap(uint32& Hash, const TMap<FGameplayTag, ValueType>& Map, HashValueFn HashVal)
	{
		TArray<FGameplayTag> Keys;
		Map.GetKeys(Keys);
		Keys.Sort(TagNameLess);
		for (const FGameplayTag& Key : Keys)
		{
			Hash = HashCombine(Hash, GetTypeHash(Key));
			Hash = HashCombine(Hash, HashVal(Map[Key]));
		}
	}

	// Hash a FSeinPlayerState field-by-field. The free-function
	// GetTypeHash(FSeinPlayerState) only covers PlayerID (it's a TMap-
	// key hash) — we need the full state for desync detection.
	uint32 HashPlayerStateFields(const FSeinPlayerState& State)
	{
		uint32 Hash = GetTypeHash(State.PlayerID);
		Hash = HashCombine(Hash, GetTypeHash(State.FactionID));
		Hash = HashCombine(Hash, GetTypeHash(State.TeamID));
		Hash = HashCombine(Hash, GetTypeHash(State.bEliminated));
		Hash = HashCombine(Hash, GetTypeHash(State.bReady));
		Hash = HashCombine(Hash, GetTypeHash(State.bIsSpectator));
		Hash = HashCombine(Hash, GetTypeHash(State.bIsAI));
		Hash = HashCombine(Hash, GetTypeHash(State.NextEffectInstanceID));

		HashTagMap(Hash, State.Resources,          [](const FFixedPoint& V) { return GetTypeHash(V); });
		HashTagMap(Hash, State.ResourceCaps,       [](const FFixedPoint& V) { return GetTypeHash(V); });
		HashTagMap(Hash, State.PlayerTagRefCounts, [](int32 V)              { return GetTypeHash(V); });

		// PlayerTags is the cached presence set mirroring PlayerTagRefCounts.
		// Hashing both catches drift between the refcount map and the cache
		// (a known silent-desync category in tag-based systems). Sort the
		// container's tags by name first — FGameplayTagContainer's natural
		// iteration order isn't guaranteed across processes.
		{
			TArray<FGameplayTag> Tags;
			State.PlayerTags.GetGameplayTagArray(Tags);
			Tags.Sort(TagNameLess);
			Hash = HashCombine(Hash, GetTypeHash(Tags.Num()));
			for (const FGameplayTag& T : Tags)
			{
				Hash = HashCombine(Hash, GetTypeHash(T));
			}
		}

		// ClassEffects + PlayerEffects are TArrays — order is already
		// deterministic by insertion (apply order is sim-tick driven).
		Hash = HashCombine(Hash, GetTypeHash(State.ClassEffects.Num()));
		for (const FSeinActiveEffect& E : State.ClassEffects)
		{
			Hash = HashCombine(Hash, GetTypeHash(E));
		}
		Hash = HashCombine(Hash, GetTypeHash(State.PlayerEffects.Num()));
		for (const FSeinActiveEffect& E : State.PlayerEffects)
		{
			Hash = HashCombine(Hash, GetTypeHash(E));
		}
		return Hash;
	}
}

int32 USeinWorldSubsystem::ComputeStateHash() const
{
	uint32 Hash = GetTypeHash(CurrentTick);

	// Entities — pool iterates in slot-index order, already deterministic.
	EntityPool.ForEachEntity([&Hash](FSeinEntityHandle Handle, const FSeinEntity& Entity)
	{
		Hash = HashCombine(Hash, GetTypeHash(Entity));
	});

	// Component storages — TMap is keyed by UScriptStruct* (pointer).
	// Pointer hash is stable within a process but not guaranteed across
	// processes, so sort by struct name before hashing. Keeps cross-process
	// comparison reliable for desync detection.
	{
		TArray<UScriptStruct*> Structs;
		Structs.Reserve(ComponentStorages.Num());
		for (const auto& Pair : ComponentStorages) { Structs.Add(Pair.Key); }
		Structs.Sort([](const UScriptStruct& A, const UScriptStruct& B)
		{
			return A.GetFName().Compare(B.GetFName()) < 0;
		});
		for (UScriptStruct* Struct : Structs)
		{
			Hash = HashCombine(Hash, GetTypeHash(Struct->GetFName()));
			Hash = HashCombine(Hash, ComponentStorages[Struct]->ComputeHash());
		}
	}

	// Player states — sort by PlayerID (uint8, ordering trivially stable).
	{
		TArray<FSeinPlayerID> Keys;
		PlayerStates.GetKeys(Keys);
		Keys.Sort();
		for (const FSeinPlayerID& PID : Keys)
		{
			Hash = HashCombine(Hash, HashPlayerStateFields(PlayerStates[PID]));
		}
	}

	// Entity tag index — sorted by tag name. Redundant with per-entity
	// FSeinEntityTagState (in EntityTagStates), but hashing it catches
	// index/refcount drift the per-entity path would miss.
	{
		TArray<FGameplayTag> Keys;
		EntityTagIndex.GetKeys(Keys);
		Keys.Sort(TagNameLess);
		for (const FGameplayTag& Tag : Keys)
		{
			Hash = HashCombine(Hash, GetTypeHash(Tag));
			const TArray<FSeinEntityHandle>& Bucket = EntityTagIndex[Tag];
			Hash = HashCombine(Hash, GetTypeHash(Bucket.Num()));
			for (const FSeinEntityHandle& H : Bucket)
			{
				Hash = HashCombine(Hash, GetTypeHash(H));
			}
		}
	}

	// Named entity registry — sort by name.
	{
		TArray<FName> Keys;
		NamedEntityRegistry.GetKeys(Keys);
		Keys.Sort([](const FName& A, const FName& B) { return A.Compare(B) < 0; });
		for (const FName& Name : Keys)
		{
			Hash = HashCombine(Hash, GetTypeHash(Name));
			Hash = HashCombine(Hash, GetTypeHash(NamedEntityRegistry[Name]));
		}
	}

	// Active votes — sort by vote type tag. Inner Votes map sorted by player ID.
	{
		TArray<FGameplayTag> Keys;
		ActiveVotes.GetKeys(Keys);
		Keys.Sort(TagNameLess);
		for (const FGameplayTag& VTag : Keys)
		{
			const FSeinVoteState& V = ActiveVotes[VTag];
			Hash = HashCombine(Hash, GetTypeHash(VTag));
			Hash = HashCombine(Hash, GetTypeHash(V.RequiredThreshold));
			Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(V.Resolution)));
			Hash = HashCombine(Hash, GetTypeHash(V.InitiatedAtTick));
			Hash = HashCombine(Hash, GetTypeHash(V.ExpiresAtTick));
			Hash = HashCombine(Hash, GetTypeHash(V.Initiator));
			TArray<FSeinPlayerID> Voters;
			V.Votes.GetKeys(Voters);
			Voters.Sort();
			for (const FSeinPlayerID& Voter : Voters)
			{
				Hash = HashCombine(Hash, GetTypeHash(Voter));
				Hash = HashCombine(Hash, GetTypeHash(V.Votes[Voter]));
			}
		}
	}

	// Pending destroys — order matters if destroys trigger same-tick effects.
	Hash = HashCombine(Hash, GetTypeHash(PendingDestroy.Num()));
	for (const FSeinEntityHandle& H : PendingDestroy)
	{
		Hash = HashCombine(Hash, GetTypeHash(H));
	}

	// Sim PRNG cursor — determinism of any roll-ordered systems depends on
	// it advancing identically on all clients. Hash both state halves.
	Hash = HashCombine(Hash, GetTypeHash(SimRandom.State0));
	Hash = HashCombine(Hash, GetTypeHash(SimRandom.State1));

	// Match + pause flags.
	Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(MatchState)));
	Hash = HashCombine(Hash, GetTypeHash(bSimPaused));
	Hash = HashCombine(Hash, GetTypeHash(bSimPausedHard));

	return static_cast<int32>(Hash);
}

// ==================== Ability Initialization ====================

void USeinWorldSubsystem::InitializeEntityAbilities(FSeinEntityHandle Handle)
{
	FSeinAbilityComponent* AbilityComp = GetComponent<FSeinAbilityComponent>(Handle);
	if (!AbilityComp)
	{
		// Not every entity has abilities (projectiles, static props, resource piles);
		// this is expected, not an error.
		return;
	}

	// Snapshot the authored list — `SeinGrantAbility` mutates
	// `GrantedAbilities` (via `AddUnique`), and iterating the live list
	// while it's being modified would risk reading the just-added element
	// twice. Snapshot is cheap (typically <20 classes).
	const TArray<TSubclassOf<USeinAbility>> AuthoredClasses = AbilityComp->GrantedAbilities;

	// Route through the BPFL so refcount semantics seed correctly: each
	// natively-authored class lands at refcount=1, which subsequent
	// effect-driven grants bump to 2/3/etc., and effect-revokes can
	// decrement back toward 1 without ever destroying the native grant.
	// Also dedupes against authoring mistakes (same class listed twice in
	// the editor) — second grant is idempotent + becomes refcount=2 which
	// is benign at spawn.
	for (const TSubclassOf<USeinAbility>& AbilityClass : AuthoredClasses)
	{
		if (!AbilityClass)
		{
			UE_LOG(LogSeinSim, Warning, TEXT("Entity %s: null ability class in GrantedAbilities"),
				*Handle.ToString());
			continue;
		}
		USeinAbilityBPFL::SeinGrantAbility(this, Handle, AbilityClass);
	}
}

void USeinWorldSubsystem::ReplayEffectAbilityGrants(FSeinEntityHandle Handle)
{
	// A unit spawned AFTER the player completed a tech effect that grants
	// abilities should pick up those grants on spawn — otherwise the
	// effect-driven grant pattern only works for entities that existed at
	// research-complete time. Walk the owner's active Class/Player effect
	// storage; for each whose GrantedAbilities are non-empty and whose
	// AbilityTargetClassTag matches a tag on this brand-new entity, run
	// SeinGrantAbility.
	//
	// MUST be called AFTER `SeedEntityTagsFromBase` (or equivalent) — the
	// AbilityTargetClassTag check reads the entity's tag state, which is
	// only meaningful after BaseTags / identity tag have been seeded.
	// InitializeEntityAbilities runs before tag seeding (so passives can
	// fire OnActivate without depending on BaseTags being present), so we
	// keep this as a SEPARATE post-seed step.
	if (!Handle.IsValid()) return;

	// AbilityComponent is the gate — entities without one can't hold
	// abilities, so there's nothing to grant.
	if (!GetComponent<FSeinAbilityComponent>(Handle)) return;

	const FSeinPlayerID Owner = GetEntityOwner(Handle);
	const FSeinPlayerState* OwnerState = GetPlayerState(Owner);
	if (!OwnerState) return;

	auto ReplayFromEffectStorage = [&](const TArray<FSeinActiveEffect>& Storage)
	{
		for (const FSeinActiveEffect& Active : Storage)
		{
			const USeinEffect* CDO = Active.EffectClass ? GetDefault<USeinEffect>(Active.EffectClass) : nullptr;
			if (!CDO) continue;
			if (CDO->GrantedAbilities.Num() == 0) continue;
			if (!CDO->AbilityTargetClassTag.IsValid()) continue;
			if (!HasTag(Handle, CDO->AbilityTargetClassTag)) continue;
			for (const TSubclassOf<USeinAbility>& AbilityClass : CDO->GrantedAbilities)
			{
				if (AbilityClass)
				{
					USeinAbilityBPFL::SeinGrantAbility(this, Handle, AbilityClass);
				}
			}
		}
	};
	ReplayFromEffectStorage(OwnerState->ClassEffects);
	ReplayFromEffectStorage(OwnerState->PlayerEffects);
}

// ==================== Tag seeding / unindexing ====================

void USeinWorldSubsystem::SeedEntityTagsFromBase(FSeinEntityHandle Handle)
{
	// Ensure the tag-state entry exists first (creates an empty record if
	// the entity is brand-new and hasn't had any tags touched yet).
	FSeinEntityTagState& TagState = EntityTagStates.FindOrAdd(Handle);

	// Snapshot first — GrantTag doesn't touch BaseTags, but a stable
	// iteration source is cheap and makes the intent obvious.
	TArray<FGameplayTag> SeedTags;
	TagState.BaseTags.GetGameplayTagArray(SeedTags);
	for (const FGameplayTag& Tag : SeedTags)
	{
		GrantTag(Handle, Tag);
	}
}

void USeinWorldSubsystem::UnindexEntityTags(FSeinEntityHandle Handle)
{
	const FSeinEntityTagState* TagState = EntityTagStates.Find(Handle);
	if (!TagState) return;

	for (const TPair<FGameplayTag, int32>& Pair : TagState->TagRefCounts)
	{
		if (Pair.Value <= 0) continue;
		if (TArray<FSeinEntityHandle>* Bucket = EntityTagIndex.Find(Pair.Key))
		{
			Bucket->RemoveSingle(Handle);
			if (Bucket->Num() == 0)
			{
				EntityTagIndex.Remove(Pair.Key);
			}
		}
	}

	// Free the tag-state entry — entity is being destroyed.
	EntityTagStates.Remove(Handle);
}

void USeinWorldSubsystem::UnregisterHandleFromNames(FSeinEntityHandle Handle)
{
	for (auto It = NamedEntityRegistry.CreateIterator(); It; ++It)
	{
		if (It.Value() == Handle)
		{
			It.RemoveCurrent();
		}
	}
}

// ==================== CommandBroker helpers (DESIGN §5) ====================

FSeinEntityHandle USeinWorldSubsystem::FindSharedBroker(const TArray<FSeinEntityHandle>& Members) const
{
	if (Members.Num() == 0) return FSeinEntityHandle::Invalid();

	FSeinEntityHandle Shared;
	for (const FSeinEntityHandle& M : Members)
	{
		const FSeinBrokerMembershipData* Memb = GetComponent<FSeinBrokerMembershipData>(M);
		if (!Memb || !Memb->CurrentBrokerHandle.IsValid())
		{
			return FSeinEntityHandle::Invalid();
		}
		if (!Shared.IsValid())
		{
			Shared = Memb->CurrentBrokerHandle;
		}
		else if (Shared != Memb->CurrentBrokerHandle)
		{
			return FSeinEntityHandle::Invalid();
		}
	}
	return Shared;
}

FSeinEntityHandle USeinWorldSubsystem::CreateBrokerForMembers(
	const TArray<FSeinEntityHandle>& FilteredMembers,
	FSeinPlayerID OwnerPlayerID,
	const FSeinBrokerQueuedOrder& FirstOrder)
{
	if (FilteredMembers.Num() == 0) return FSeinEntityHandle::Invalid();

	// 1. Evict each member from its prior broker (one-broker-per-member invariant).
	//    When a player issues a new (non-shift) order, the old broker's in-flight
	//    work for this member needs to terminate cleanly — without this the
	//    member's old active ability (e.g. Move toward the previous destination)
	//    keeps running alongside whatever the new broker dispatches, producing
	//    the "two competing orders" symptom the user reported.
	for (const FSeinEntityHandle& M : FilteredMembers)
	{
		FSeinBrokerMembershipData* Memb = GetComponent<FSeinBrokerMembershipData>(M);
		if (!Memb || !Memb->CurrentBrokerHandle.IsValid()) continue;
		if (!EntityPool.IsValid(Memb->CurrentBrokerHandle)) continue;
		FSeinCommandBrokerData* OldBroker = GetComponent<FSeinCommandBrokerData>(Memb->CurrentBrokerHandle);
		if (!OldBroker) continue;

		// Cancel the member's active primary ability before evicting. The active
		// ability tracked on FSeinAbilityComponent was dispatched by this broker —
		// once we evict, the broker no longer owns it. Cancellation runs the
		// ability's OnEnd cleanup (latent-action teardown, refunds, tag ungrant).
		// Safe no-op if the member has no active ability.
		if (FSeinAbilityComponent* AC = GetComponent<FSeinAbilityComponent>(M))
		{
			if (USeinAbility* Active = AC->GetActiveAbility(*this))
			{
				if (Active->bIsActive)
				{
					Active->CancelAbility();
				}
			}
			AC->ActiveAbilityID = INDEX_NONE;
		}

		OldBroker->Members.Remove(M);
		OldBroker->bCapabilityMapDirty = true;

		// Cull the old broker if it now has no members. A member-less broker
		// can't dispatch its remaining queue anyway — keeping it alive just
		// leaks abstract entities. Relaxed from the previous condition (which
		// also required queue=0 + !bIsExecuting) since neither matters once
		// Members.Num() is zero.
		if (OldBroker->bSelfCullOnEmpty && OldBroker->Members.Num() == 0)
		{
			DestroyEntity(Memb->CurrentBrokerHandle);
		}
	}

	// 2. Compute initial centroid.
	FFixedVector InitialCentroid;
	int32 CentroidCount = 0;
	for (const FSeinEntityHandle& M : FilteredMembers)
	{
		if (const FSeinEntity* E = GetEntity(M))
		{
			InitialCentroid += E->Transform.GetLocation();
			++CentroidCount;
		}
	}
	if (CentroidCount > 0)
	{
		InitialCentroid = InitialCentroid / FFixedPoint::FromInt(CentroidCount);
	}

	// 3. Spawn the abstract broker entity.
	FSeinEntityHandle BrokerHandle = SpawnAbstractEntity(FFixedTransform(InitialCentroid), OwnerPlayerID);
	if (!BrokerHandle.IsValid()) return FSeinEntityHandle::Invalid();

	// 4. Build + inject FSeinCommandBrokerData with the first order pre-queued.
	FSeinCommandBrokerData BrokerData;
	BrokerData.Members = FilteredMembers;
	BrokerData.Centroid = InitialCentroid;
	BrokerData.Anchor = FirstOrder.TargetLocation;
	BrokerData.OrderQueue.Add(FirstOrder);
	BrokerData.bCapabilityMapDirty = true;

	// Resolver class resolution: plugin-setting soft class → framework default.
	TSubclassOf<USeinCommandBrokerResolver> ResolverClass;
	if (const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>())
	{
		if (!Settings->DefaultBrokerResolverClass.IsNull())
		{
			ResolverClass = Settings->DefaultBrokerResolverClass.LoadSynchronous();
		}
	}
	if (!ResolverClass || ResolverClass->HasAnyClassFlags(CLASS_Abstract))
	{
		ResolverClass = USeinDefaultCommandBrokerResolver::StaticClass();
	}
	// Phase 4 architecture: resolver is registered in the world's resolver
	// pool; component stores the int32 ID, not a TObjectPtr.
	USeinCommandBrokerResolver* ResolverInstance = NewObject<USeinCommandBrokerResolver>(this, ResolverClass);
	BrokerData.ResolverID = RegisterCommandBrokerResolver(ResolverInstance);

	AddComponent(BrokerHandle, BrokerData);

	// 5. Update each member's back-reference. Create the component if missing.
	for (const FSeinEntityHandle& M : FilteredMembers)
	{
		FSeinBrokerMembershipData* Memb = GetComponent<FSeinBrokerMembershipData>(M);
		if (Memb)
		{
			Memb->CurrentBrokerHandle = BrokerHandle;
		}
		else
		{
			FSeinBrokerMembershipData NewMemb;
			NewMemb.CurrentBrokerHandle = BrokerHandle;
			AddComponent(M, NewMemb);
		}
	}

	// 6. Inline-dispatch the first order (skips the 1-tick delay of waiting for
	// SeinCommandBrokerSystem's PostTick pass). Subsequent queue advancement runs
	// through the system. Under per-order parallelism the "first order" is at
	// index 0 — DispatchOrderAtIndex sets that order's bIsExecuting + stamps
	// LastDispatchTick so the system's completion check picks it up next tick.
	if (FSeinCommandBrokerData* BrokerPtr = GetComponent<FSeinCommandBrokerData>(BrokerHandle))
	{
		if (BrokerPtr->OrderQueue.Num() > 0)
		{
			SeinCommandBrokerDispatch::DispatchOrderAtIndex(*this, BrokerHandle, *BrokerPtr, 0);
		}
	}

	return BrokerHandle;
}

// ==================== Match Flow (DESIGN §18) ====================

void USeinWorldSubsystem::SetSimPaused(bool bPaused, bool bRejectCommandsWhilePaused)
{
	const bool bWasPaused = bSimPaused;
	bSimPaused = bPaused;
	bSimPausedHard = bPaused && bRejectCommandsWhilePaused;

	// Fire match-flow visual events for UI / scenario subscribers. Suppress
	// double-fires when the state didn't actually change.
	if (bWasPaused != bPaused)
	{
		if (bPaused)
		{
			if (MatchState == ESeinMatchState::Playing)
			{
				MatchState = ESeinMatchState::Paused;
			}
			EnqueueVisualEvent(FSeinVisualEvent::MakeMatchFlowEvent(ESeinVisualEventType::MatchPaused));
		}
		else
		{
			if (MatchState == ESeinMatchState::Paused)
			{
				MatchState = ESeinMatchState::Playing;
			}
			EnqueueVisualEvent(FSeinVisualEvent::MakeMatchFlowEvent(ESeinVisualEventType::MatchResumed));
		}
	}
}

void USeinWorldSubsystem::StartMatch(const FSeinMatchSettings& Settings)
{
	if (MatchState != ESeinMatchState::Lobby && MatchState != ESeinMatchState::Ended)
	{
		UE_LOG(LogSeinSim, Warning, TEXT("StartMatch: ignored — match state is already %d"),
			static_cast<int32>(MatchState));
		return;
	}
	// Snapshot settings — immutable from this point until next StartMatch.
	CurrentMatchSettings = Settings;
	MatchState = ESeinMatchState::Starting;

	// Framework no longer ships a pre-match countdown — `Starting` transitions
	// to `Playing` on the next tick (effectively instant). Designer who wants
	// a UI countdown delays calling StartMatch from their lobby BP for the
	// desired duration. Setting deadline = CurrentTick guarantees the
	// transition fires on the very next tick boundary.
	StartingStateDeadlineTick = CurrentTick;

	EnqueueVisualEvent(FSeinVisualEvent::MakeMatchFlowEvent(ESeinVisualEventType::MatchStarting));
	UE_LOG(LogSeinSim, Log, TEXT("StartMatch: Starting state begins, deadline at tick %d"), StartingStateDeadlineTick);
}

void USeinWorldSubsystem::EndMatch(FSeinPlayerID Winner, FGameplayTag Reason)
{
	if (MatchState == ESeinMatchState::Ended || MatchState == ESeinMatchState::Lobby)
	{
		return; // nothing in-flight to end
	}
	MatchState = ESeinMatchState::Ending;
	EnqueueVisualEvent(FSeinVisualEvent::MakeMatchFlowEvent(ESeinVisualEventType::MatchEnding, Winner, Reason));

	// Immediate Ending → Ended transition in V1; designers that want a staged
	// cleanup phase (fade-out cinematic, score screen pause) can schedule work
	// during Ending via scenario abilities, then route to a follow-up command
	// to finalize. Minimal polish can land when the first real game asks.
	MatchState = ESeinMatchState::Ended;
	EnqueueVisualEvent(FSeinVisualEvent::MakeMatchFlowEvent(ESeinVisualEventType::MatchEnded, Winner, Reason));
	UE_LOG(LogSeinSim, Log, TEXT("EndMatch: Winner=%s Reason=%s"),
		*Winner.ToString(), *Reason.ToString());
}

void USeinWorldSubsystem::TickMatchState()
{
	if (MatchState == ESeinMatchState::Starting)
	{
		if (CurrentTick >= StartingStateDeadlineTick)
		{
			MatchState = ESeinMatchState::Playing;
			MatchStartTick = CurrentTick;
			EnqueueVisualEvent(FSeinVisualEvent::MakeMatchFlowEvent(ESeinVisualEventType::MatchStarted));
			UE_LOG(LogSeinSim, Log, TEXT("Match transitioned to Playing at tick %d"), CurrentTick);
		}
	}
}

// ==================== Voting (DESIGN §18) ====================

void USeinWorldSubsystem::StartVote(FGameplayTag VoteType, ESeinVoteResolution Resolution, int32 RequiredThreshold, int32 ExpiresInTicks, FSeinPlayerID Initiator)
{
	if (!VoteType.IsValid()) return;
	if (ActiveVotes.Contains(VoteType))
	{
		UE_LOG(LogSeinSim, Warning, TEXT("StartVote: vote %s already active"), *VoteType.ToString());
		return;
	}
	FSeinVoteState Vote;
	Vote.VoteType = VoteType;
	Vote.Resolution = Resolution;
	Vote.RequiredThreshold = FMath::Max(1, RequiredThreshold);
	Vote.InitiatedAtTick = CurrentTick;
	Vote.ExpiresAtTick = (ExpiresInTicks > 0) ? CurrentTick + ExpiresInTicks : INT32_MAX;
	Vote.Initiator = Initiator;
	ActiveVotes.Add(VoteType, MoveTemp(Vote));

	EnqueueVisualEvent(FSeinVisualEvent::MakeVoteStartedEvent(VoteType, Initiator, RequiredThreshold));
}

void USeinWorldSubsystem::CastVote(FGameplayTag VoteType, FSeinPlayerID Voter, int32 VoteValue)
{
	if (!VoteType.IsValid()) return;
	FSeinVoteState* Vote = ActiveVotes.Find(VoteType);
	if (!Vote) return;
	Vote->Votes.Add(Voter, VoteValue);

	int32 Yes = 0, No = 0;
	for (const auto& Pair : Vote->Votes) { (Pair.Value > 0) ? ++Yes : ++No; }
	EnqueueVisualEvent(FSeinVisualEvent::MakeVoteProgressEvent(VoteType, Yes, No));

	EvaluateAndResolveVote(VoteType);
}

ESeinVoteStatus USeinWorldSubsystem::GetVoteStatus(FGameplayTag VoteType) const
{
	if (!VoteType.IsValid()) return ESeinVoteStatus::NotStarted;
	return ActiveVotes.Contains(VoteType) ? ESeinVoteStatus::Active : ESeinVoteStatus::NotStarted;
}

TArray<FSeinVoteState> USeinWorldSubsystem::GetActiveVotes() const
{
	TArray<FSeinVoteState> Out;
	Out.Reserve(ActiveVotes.Num());
	for (const auto& Pair : ActiveVotes) { Out.Add(Pair.Value); }
	return Out;
}

bool USeinWorldSubsystem::EvaluateAndResolveVote(FGameplayTag VoteType)
{
	FSeinVoteState* Vote = ActiveVotes.Find(VoteType);
	if (!Vote) return false;

	int32 Yes = 0, No = 0;
	for (const auto& Pair : Vote->Votes) { (Pair.Value > 0) ? ++Yes : ++No; }

	// Eligible voter count — V1 uses the live registered-player count (including
	// Neutral). More precise predicates (exclude spectators, exclude AI, etc.)
	// land when the match-settings-driven vote-eligibility policy does.
	const int32 Eligible = FMath::Max(1, PlayerStates.Num());

	bool bPassed = false;
	bool bResolveNow = false;
	switch (Vote->Resolution)
	{
	case ESeinVoteResolution::Majority:
		if (Yes * 2 > Eligible) { bPassed = true; bResolveNow = true; }
		else if (Yes + No >= Eligible) { bResolveNow = true; bPassed = false; }
		break;
	case ESeinVoteResolution::Unanimous:
		if (Yes >= Eligible) { bPassed = true; bResolveNow = true; }
		else if (No > 0) { bResolveNow = true; bPassed = false; }
		break;
	case ESeinVoteResolution::HostDecides:
		// V1: any "yes" passes, any "no" fails. Host designation lands with
		// §18 match-flow network plumbing; until then treat first vote as decisive.
		if (Yes > 0) { bPassed = true; bResolveNow = true; }
		else if (No > 0) { bResolveNow = true; bPassed = false; }
		break;
	case ESeinVoteResolution::Plurality:
		if (Yes + No >= Eligible)
		{
			bResolveNow = true;
			bPassed = (Yes > No);
		}
		break;
	}

	// Also check the explicit threshold (overrides resolution if passed first).
	if (!bResolveNow && Yes >= Vote->RequiredThreshold)
	{
		bResolveNow = true;
		bPassed = true;
	}

	if (bResolveNow)
	{
		const FGameplayTag Resolved = Vote->VoteType;
		EnqueueVisualEvent(FSeinVisualEvent::MakeVoteResolvedEvent(Resolved, bPassed));
		ActiveVotes.Remove(VoteType);
		return true;
	}
	return false;
}

void USeinWorldSubsystem::TickVotes()
{
	if (ActiveVotes.Num() == 0) return;
	TArray<FGameplayTag> Expired;
	for (const auto& Pair : ActiveVotes)
	{
		if (CurrentTick >= Pair.Value.ExpiresAtTick) { Expired.Add(Pair.Key); }
	}
	for (const FGameplayTag& Tag : Expired)
	{
		// Expired votes that haven't passed on their own fail deterministically.
		EnqueueVisualEvent(FSeinVisualEvent::MakeVoteResolvedEvent(Tag, /*bPassed=*/false));
		ActiveVotes.Remove(Tag);
	}
}

// ==================== AI (DESIGN §16) ====================

void USeinWorldSubsystem::RegisterAIController(USeinAIController* Controller, FSeinPlayerID OwnedPlayer)
{
	if (!Controller) return;
	// Idempotent — if already registered, reseat the owned player + subsystem.
	if (!AIControllers.Contains(Controller))
	{
		AIControllers.Add(Controller);
	}
	Controller->OwnedPlayerID = OwnedPlayer;
	Controller->WorldSubsystem = this;
	Controller->OnRegistered();
	UE_LOG(LogSeinSim, Log, TEXT("Registered AI controller %s for player %s"),
		*Controller->GetName(), *OwnedPlayer.ToString());
}

void USeinWorldSubsystem::UnregisterAIController(USeinAIController* Controller)
{
	if (!Controller) return;
	const int32 Removed = AIControllers.Remove(Controller);
	if (Removed > 0)
	{
		Controller->OnUnregistered();
		Controller->WorldSubsystem = nullptr;
	}
}

USeinAIController* USeinWorldSubsystem::GetAIControllerForPlayer(FSeinPlayerID PlayerID) const
{
	for (const TObjectPtr<USeinAIController>& Ctrl : AIControllers)
	{
		if (Ctrl && Ctrl->OwnedPlayerID == PlayerID)
		{
			return Ctrl;
		}
	}
	return nullptr;
}

void USeinWorldSubsystem::TickAIControllers(FFixedPoint DeltaTime)
{
	// Snapshot the list so Tick callbacks that register/unregister don't crash
	// the iteration; pending removals take effect next tick.
	TArray<TObjectPtr<USeinAIController>> Snapshot = AIControllers;

	// DETERMINISM: sort by OwnedPlayerID before ticking. Registration order depends
	// on actor spawn order + BeginPlay sequencing, which can differ across clients.
	// PlayerIDs are globally agreed (registered via RegisterPlayer); sorting by
	// PlayerID.Value pins tick order network-wide. If two controllers somehow share
	// a PlayerID, we fall back to pointer-index stability — indeterminate but rare
	// (would be a misconfiguration; log as warning).
	// UE 5.7 quirk: TArray<TObjectPtr<>>::StableSort dereferences via
	// TDereferenceWrapper before invoking the lambda, so the lambda's params
	// must be raw `T*` (not `const TObjectPtr<T>&`).
	Snapshot.StableSort([](const USeinAIController& A, const USeinAIController& B)
	{
		return A.OwnedPlayerID < B.OwnedPlayerID;
	});

	for (const TObjectPtr<USeinAIController>& Ctrl : Snapshot)
	{
		if (!Ctrl) continue;
		FSeinAITickContext Ctx;
		Ctx.CurrentTick = CurrentTick;
		Ctx.DeltaTime = DeltaTime;
		Ctx.OwnedPlayerID = Ctrl->OwnedPlayerID;
		Ctrl->Tick(Ctx);
	}
}

// ==================== Relationships (DESIGN §14) ====================

namespace
{
	// Assign the first invalid/free visual-slot index in a container's
	// TotalCapacity-sized VisualSlotAssignments array. Growing the array lazily
	// keeps cost proportional to actual occupant count; TotalCapacity just caps
	// the search. Returns INDEX_NONE if every slot is filled.
	int32 AssignFirstFreeVisualSlot(FSeinContainmentData& Container, FSeinEntityHandle Occupant)
	{
		if (!Container.bTracksVisualSlots) return INDEX_NONE;
		if (Container.VisualSlotAssignments.Num() < Container.TotalCapacity)
		{
			Container.VisualSlotAssignments.SetNum(Container.TotalCapacity);
		}
		for (int32 i = 0; i < Container.VisualSlotAssignments.Num(); ++i)
		{
			if (!Container.VisualSlotAssignments[i].IsValid())
			{
				Container.VisualSlotAssignments[i] = Occupant;
				return i;
			}
		}
		return INDEX_NONE;
	}
}

bool USeinWorldSubsystem::EnterContainer(FSeinEntityHandle Entity, FSeinEntityHandle Container)
{
	if (!EntityPool.IsValid(Entity) || !EntityPool.IsValid(Container))
	{
		return false;
	}
	if (Entity == Container)
	{
		UE_LOG(LogSeinSim, Warning, TEXT("EnterContainer: entity %s cannot enter itself"), *Entity.ToString());
		return false;
	}

	FSeinContainmentMemberData* MemComp = GetComponent<FSeinContainmentMemberData>(Entity);
	if (!MemComp)
	{
		UE_LOG(LogSeinSim, Warning, TEXT("EnterContainer: entity %s has no FSeinContainmentMemberData"), *Entity.ToString());
		return false;
	}
	if (MemComp->CurrentContainer.IsValid())
	{
		UE_LOG(LogSeinSim, Warning, TEXT("EnterContainer: entity %s already contained by %s"),
			*Entity.ToString(), *MemComp->CurrentContainer.ToString());
		return false;
	}

	FSeinContainmentData* ContComp = GetComponent<FSeinContainmentData>(Container);
	if (!ContComp)
	{
		UE_LOG(LogSeinSim, Warning, TEXT("EnterContainer: container %s has no FSeinContainmentData"), *Container.ToString());
		return false;
	}

	// Capacity
	if (ContComp->CurrentLoad + MemComp->Size > ContComp->TotalCapacity)
	{
		return false;
	}

	// Tag query (empty query = permissive)
	if (!ContComp->AcceptedEntityQuery.IsEmpty())
	{
		if (!ContComp->AcceptedEntityQuery.Matches(GetEntityTags(Entity)))
		{
			return false;
		}
	}

	// Commit state
	ContComp->Occupants.Add(Entity);
	ContComp->CurrentLoad += MemComp->Size;
	MemComp->CurrentContainer = Container;
	MemComp->VisualSlotIndex = AssignFirstFreeVisualSlot(*ContComp, Entity);
	// CurrentSlot stays empty — set by AttachToSlot when attachment is used.

	// Visibility-mode spatial effect: Hidden + Partial remove from grid; only
	// PositionedRelative stays registered (rendered via container + offset).
	if (ContComp->Visibility != ESeinContainmentVisibility::PositionedRelative)
	{
		if (SpatialGridUnregisterCallback.IsBound())
		{
			SpatialGridUnregisterCallback.Execute(Entity);
		}
	}

	EnqueueVisualEvent(FSeinVisualEvent::MakeEntityEnteredContainerEvent(
		Container, Entity, MemComp->VisualSlotIndex));
	return true;
}

bool USeinWorldSubsystem::ExitContainer(FSeinEntityHandle Entity, FFixedVector ExitLocation)
{
	if (!EntityPool.IsValid(Entity)) return false;

	FSeinContainmentMemberData* MemComp = GetComponent<FSeinContainmentMemberData>(Entity);
	if (!MemComp || !MemComp->CurrentContainer.IsValid())
	{
		return false;
	}
	const FSeinEntityHandle Container = MemComp->CurrentContainer;
	if (!EntityPool.IsValid(Container))
	{
		// Stale pointer — scrub and bail.
		MemComp->CurrentContainer = FSeinEntityHandle();
		MemComp->CurrentSlot = FGameplayTag();
		MemComp->VisualSlotIndex = INDEX_NONE;
		return false;
	}

	FSeinContainmentData* ContComp = GetComponent<FSeinContainmentData>(Container);
	if (!ContComp) return false;

	// Resolve exit world position.
	FFixedVector FinalExit = ExitLocation;
	if (FinalExit == FFixedVector())
	{
		FFixedVector ContainerLoc;
		if (const FSeinEntity* ContEntity = GetEntity(Container))
		{
			ContainerLoc = ContEntity->Transform.GetLocation();
		}
		FinalExit = ContainerLoc;
		if (const FSeinTransportSpec* TransportSpec = GetComponent<FSeinTransportSpec>(Container))
		{
			FinalExit = ContainerLoc + TransportSpec->DeployOffset;
		}
	}

	// Write the exiter's transform.
	if (FSeinEntity* ExiterEntity = EntityPool.Get(Entity))
	{
		FFixedTransform NewXfm = ExiterEntity->Transform;
		NewXfm.SetLocation(FinalExit);
		ExiterEntity->Transform = NewXfm;
	}

	// Attachment-slot cleanup, if any.
	if (MemComp->CurrentSlot.IsValid())
	{
		if (FSeinAttachmentSpec* Spec = GetComponent<FSeinAttachmentSpec>(Container))
		{
			Spec->Assignments.Remove(MemComp->CurrentSlot);
		}
		EnqueueVisualEvent(FSeinVisualEvent::MakeAttachmentSlotEmptiedEvent(
			Container, Entity, MemComp->CurrentSlot));
	}

	// Visual-slot cleanup.
	if (ContComp->bTracksVisualSlots && ContComp->VisualSlotAssignments.IsValidIndex(MemComp->VisualSlotIndex))
	{
		ContComp->VisualSlotAssignments[MemComp->VisualSlotIndex] = FSeinEntityHandle();
	}

	// Occupant-list cleanup.
	ContComp->Occupants.Remove(Entity);
	ContComp->CurrentLoad = FMath::Max(0, ContComp->CurrentLoad - MemComp->Size);
	MemComp->CurrentContainer = FSeinEntityHandle();
	MemComp->CurrentSlot = FGameplayTag();
	MemComp->VisualSlotIndex = INDEX_NONE;

	// Re-register in spatial grid if the container was Hidden/Partial.
	if (ContComp->Visibility != ESeinContainmentVisibility::PositionedRelative)
	{
		if (SpatialGridRegisterCallback.IsBound())
		{
			SpatialGridRegisterCallback.Execute(Entity);
		}
	}

	EnqueueVisualEvent(FSeinVisualEvent::MakeEntityExitedContainerEvent(Container, Entity, FinalExit));
	return true;
}

bool USeinWorldSubsystem::AttachToSlot(FSeinEntityHandle Entity, FSeinEntityHandle Container, FGameplayTag SlotTag)
{
	if (!EntityPool.IsValid(Entity) || !EntityPool.IsValid(Container)) return false;
	if (!SlotTag.IsValid()) return false;

	FSeinAttachmentSpec* Spec = GetComponent<FSeinAttachmentSpec>(Container);
	if (!Spec) return false;

	// Locate slot by tag.
	const FSeinAttachmentSlotDef* SlotDef = Spec->Slots.FindByPredicate(
		[&](const FSeinAttachmentSlotDef& S) { return S.SlotTag == SlotTag; });
	if (!SlotDef) return false;

	// Already filled?
	if (FSeinEntityHandle* Existing = Spec->Assignments.Find(SlotTag))
	{
		if (Existing->IsValid()) return false;
	}

	// Slot-level tag query (independent of container-level AcceptedEntityQuery).
	if (!SlotDef->AcceptedEntityQuery.IsEmpty())
	{
		if (!SlotDef->AcceptedEntityQuery.Matches(GetEntityTags(Entity)))
		{
			return false;
		}
	}

	// Attachment implies containment — run the standard enter path first.
	if (!EnterContainer(Entity, Container))
	{
		return false;
	}

	// Stamp slot assignment + member back-ref.
	Spec->Assignments.Add(SlotTag, Entity);
	if (FSeinContainmentMemberData* Mem = GetComponent<FSeinContainmentMemberData>(Entity))
	{
		Mem->CurrentSlot = SlotTag;
	}

	EnqueueVisualEvent(FSeinVisualEvent::MakeAttachmentSlotFilledEvent(Container, Entity, SlotTag));
	return true;
}

bool USeinWorldSubsystem::DetachFromSlot(FSeinEntityHandle Entity)
{
	if (!EntityPool.IsValid(Entity)) return false;
	FSeinContainmentMemberData* Mem = GetComponent<FSeinContainmentMemberData>(Entity);
	if (!Mem || !Mem->CurrentSlot.IsValid()) return false;
	// ExitContainer handles slot-assignment removal + visual event; simply delegate.
	return ExitContainer(Entity);
}

void USeinWorldSubsystem::PropagateContainerDeath(FSeinEntityHandle DyingContainer)
{
	FSeinContainmentData* Container = GetComponent<FSeinContainmentData>(DyingContainer);
	if (!Container) return;

	FFixedVector ContainerLoc;
	if (const FSeinEntity* ContEntity = GetEntity(DyingContainer))
	{
		ContainerLoc = ContEntity->Transform.GetLocation();
	}

	const bool bEject = Container->bEjectOnContainerDeath;
	const TSubclassOf<USeinEffect> OnEjectEffect = Container->OnEjectEffect;
	const TSubclassOf<USeinEffect> OnDeathEffect = Container->OnContainerDeathEffect;

	// Snapshot — occupants list is mutated while iterating when ExitContainer
	// runs, so copy first.
	TArray<FSeinEntityHandle> Occupants = Container->Occupants;
	for (const FSeinEntityHandle& Occ : Occupants)
	{
		if (!EntityPool.IsValid(Occ)) continue;

		if (bEject)
		{
			// Exit at container's last location; apply eject effect if authored.
			ExitContainer(Occ, ContainerLoc);
			if (OnEjectEffect)
			{
				ApplyEffect(Occ, OnEjectEffect, DyingContainer);
			}
		}
		else
		{
			// Occupant dies with container; optional effect first, then destroy.
			if (OnDeathEffect)
			{
				ApplyEffect(Occ, OnDeathEffect, DyingContainer);
			}
			DestroyEntity(Occ);
		}
	}

	// Container's Occupants now empty; its FSeinContainmentData is about to be
	// stripped by the surrounding ProcessDeferredDestroys sweep.
}

FSeinEntityHandle USeinWorldSubsystem::GetImmediateContainer(FSeinEntityHandle Entity) const
{
	const FSeinContainmentMemberData* Mem = GetComponent<FSeinContainmentMemberData>(Entity);
	return (Mem && EntityPool.IsValid(Mem->CurrentContainer)) ? Mem->CurrentContainer : FSeinEntityHandle();
}

FSeinEntityHandle USeinWorldSubsystem::GetRootContainer(FSeinEntityHandle Entity) const
{
	FSeinEntityHandle Cursor = GetImmediateContainer(Entity);
	if (!Cursor.IsValid()) return FSeinEntityHandle();
	// Walk up; cap at 32 to guard against pathological loops.
	for (int32 Depth = 0; Depth < 32; ++Depth)
	{
		const FSeinEntityHandle Next = GetImmediateContainer(Cursor);
		if (!Next.IsValid()) return Cursor;
		Cursor = Next;
	}
	UE_LOG(LogSeinSim, Warning, TEXT("GetRootContainer: depth limit hit on %s — likely a containment cycle"),
		*Entity.ToString());
	return Cursor;
}

bool USeinWorldSubsystem::IsContained(FSeinEntityHandle Entity) const
{
	const FSeinContainmentMemberData* Mem = GetComponent<FSeinContainmentMemberData>(Entity);
	return Mem && EntityPool.IsValid(Mem->CurrentContainer);
}

TArray<FSeinEntityHandle> USeinWorldSubsystem::GetAllNestedOccupants(FSeinEntityHandle Container) const
{
	TArray<FSeinEntityHandle> Out;
	const FSeinContainmentData* Cont = GetComponent<FSeinContainmentData>(Container);
	if (!Cont) return Out;

	TArray<FSeinEntityHandle> Frontier = Cont->Occupants;
	while (Frontier.Num() > 0)
	{
		FSeinEntityHandle Current = Frontier.Pop();
		if (!EntityPool.IsValid(Current)) continue;
		Out.Add(Current);
		if (const FSeinContainmentData* Nested = GetComponent<FSeinContainmentData>(Current))
		{
			Frontier.Append(Nested->Occupants);
		}
	}
	return Out;
}

FSeinContainmentTree USeinWorldSubsystem::BuildContainmentTree(FSeinEntityHandle Container) const
{
	FSeinContainmentTree Tree;

	// Iterative DFS emitting entries in pre-order so the flattened array encodes
	// the hierarchy: each child appears after its parent and is flagged with
	// Depth + ParentIndex. BP consumers walk sequentially to rebuild the tree.
	struct FFrame { FSeinEntityHandle Entity; int32 Depth; int32 ParentIndex; };
	TArray<FFrame> Stack;
	Stack.Reserve(8);
	Stack.Push({Container, 0, INDEX_NONE});

	while (Stack.Num() > 0)
	{
		const FFrame Frame = Stack.Pop();
		if (!EntityPool.IsValid(Frame.Entity)) continue;

		FSeinContainmentTreeEntry Entry;
		Entry.Entity = Frame.Entity;
		Entry.Depth = Frame.Depth;
		Entry.ParentIndex = Frame.ParentIndex;
		const int32 ThisIndex = Tree.Entries.Add(Entry);

		if (const FSeinContainmentData* Cont = GetComponent<FSeinContainmentData>(Frame.Entity))
		{
			// Push children in reverse order so stack-popped order matches original
			// occupant list order (deterministic).
			for (int32 i = Cont->Occupants.Num() - 1; i >= 0; --i)
			{
				Stack.Push({Cont->Occupants[i], Frame.Depth + 1, ThisIndex});
			}
		}
	}

	return Tree;
}
