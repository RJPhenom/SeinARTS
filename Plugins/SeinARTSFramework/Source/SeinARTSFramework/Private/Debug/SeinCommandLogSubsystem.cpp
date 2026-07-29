/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCommandLogSubsystem.cpp
 * @brief   Debug command log — data capture only, HUD does the rendering.
 */

#include "Debug/SeinCommandLogSubsystem.h"
#include "Brokers/SeinBrokerTypes.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "StructUtils/InstancedStruct.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinCommandLog, Log, All);

// ==================== Console Commands ====================

// Filter for observer commands (CameraUpdate, SelectionChanged, etc). Default
// off so the overlay shows only sim-mutating commands — those are identical
// across every client by lockstep, so the overlay matches between windows.
// Toggle on to also include this client's local observer commands (per-POV).
bool GShowObserverCommandsInLog = false;

static FAutoConsoleCommand CmdToggleObserverFilter(
	TEXT("Sein.Commands.ShowLog.Observer"),
	TEXT("Toggle inclusion of observer commands (CameraUpdate, SelectionChanged) in the SeinARTS command log overlay. Default: OFF (sim-mutating only)."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		GShowObserverCommandsInLog = !GShowObserverCommandsInLog;
		UE_LOG(LogSeinCommandLog, Log, TEXT("Command log observer commands: %s"),
			GShowObserverCommandsInLog ? TEXT("INCLUDED") : TEXT("FILTERED OUT"));
	})
);

static FAutoConsoleCommand CmdToggleCommandLog(
	TEXT("Sein.Commands.ShowLog"),
	TEXT("Toggle the SeinARTS command transaction log overlay"),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		if (!GEngine) return;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World())
			{
				if (USeinCommandLogSubsystem* Sub = Context.World()->GetSubsystem<USeinCommandLogSubsystem>())
				{
					Sub->bShowOverlay = !Sub->bShowOverlay;
					UE_LOG(LogSeinCommandLog, Log, TEXT("Command log overlay: %s"),
						Sub->bShowOverlay ? TEXT("ON") : TEXT("OFF"));
				}
			}
		}
	})
);

static FAutoConsoleCommand CmdClearCommandLog(
	TEXT("Sein.Commands.ClearLog"),
	TEXT("Clear the SeinARTS command transaction log"),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		if (!GEngine) return;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World())
			{
				if (USeinCommandLogSubsystem* Sub = Context.World()->GetSubsystem<USeinCommandLogSubsystem>())
				{
					Sub->ClearLog();
					UE_LOG(LogSeinCommandLog, Log, TEXT("Command log cleared"));
				}
			}
		}
	})
);

// ==================== Lifecycle ====================

void USeinCommandLogSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	SimSubsystem = GetWorld()->GetSubsystem<USeinWorldSubsystem>();
	if (SimSubsystem.IsValid())
	{
		CommandsDelegateHandle = SimSubsystem->OnCommandsProcessing.AddUObject(
			this, &USeinCommandLogSubsystem::OnCommandsProcessing);
		BrokerDispatchedDelegateHandle = SimSubsystem->OnBrokerOrderDispatched.AddUObject(
			this, &USeinCommandLogSubsystem::OnBrokerOrderDispatched);
	}
}

void USeinCommandLogSubsystem::Deinitialize()
{
	ReleaseModuleOwnedStateForModuleUnload();
	Super::Deinitialize();
}

void USeinCommandLogSubsystem::ReleaseModuleOwnedStateForModuleUnload()
{
	if (SimSubsystem.IsValid())
	{
		SimSubsystem->OnCommandsProcessing.Remove(CommandsDelegateHandle);
		SimSubsystem->OnBrokerOrderDispatched.Remove(BrokerDispatchedDelegateHandle);
	}
	CommandsDelegateHandle.Reset();
	BrokerDispatchedDelegateHandle.Reset();
	LogEntries.Empty();
	SimSubsystem.Reset();
	bShowOverlay = false;
}

// ==================== Command Capture ====================

void USeinCommandLogSubsystem::OnCommandsProcessing(int32 Tick, const TArray<FSeinCommand>& Commands)
{
	for (const FSeinCommand& Cmd : Commands)
	{
		// Filter observer commands by default — they're per-client (camera /
		// selection POV) and would make every window's overlay diverge.
		// Sim-mutating commands stay visible because lockstep guarantees they
		// match across every client. Toggle inclusion via Sein.Commands.ShowLog.Observer.
		if (!GShowObserverCommandsInLog && Cmd.IsObserverCommand())
		{
			continue;
		}

		FSeinCommandLogEntry Entry;
		Entry.Tick = Tick;
		Entry.PlayerIndex = static_cast<int32>(Cmd.PlayerID.Value);
		Entry.EntityIndex = Cmd.EntityHandle.IsValid() ? Cmd.EntityHandle.Index : -1;
		// EntityList carries the member set for BrokerOrder + SelectionChanged.
		// Stored separately from EntityIndex so the HUD can show "E×N" instead
		// of misleading "E-01" when the command targets a list rather than a
		// single entity.
		Entry.MemberCount = Cmd.EntityList.Num();
		Entry.CommandType = Cmd.CommandType;
		Entry.Description = DescribeCommand(Cmd);
		Entry.DisplayColor = GetCommandColor(Cmd.CommandType);

		if (LogEntries.Num() >= MaxLogEntries)
		{
			LogEntries.RemoveAt(0);
		}
		LogEntries.Add(Entry);
	}
}

void USeinCommandLogSubsystem::OnBrokerOrderDispatched(
	int32 /*Tick*/, FSeinPlayerID PlayerID, const TArray<FSeinBrokerResolvedAbility>& UniqueResolved)
{
	// Walk newest-to-oldest looking for a BrokerOrder entry from this player
	// that hasn't been resolved yet. Sequential matching: each unresolved entry
	// corresponds to the next dispatch event from the same player. Works for
	// both immediate dispatches (entry was just added) and queue-popped dispatches
	// (entry is older). If the entry has already aged out of MaxLogEntries, the
	// resolution is silently dropped — debug overlay only, not load-bearing.
	const int32 PIdx = static_cast<int32>(PlayerID.Value);
	for (int32 i = LogEntries.Num() - 1; i >= 0; --i)
	{
		FSeinCommandLogEntry& E = LogEntries[i];
		if (E.PlayerIndex == PIdx
			&& E.CommandType == SeinARTSTags::Command_Type_BrokerOrder
			&& E.ResolvedAbilities.Num() == 0)
		{
			E.ResolvedAbilities = UniqueResolved;
			return;
		}
	}
}

// ==================== Helpers ====================

FString USeinCommandLogSubsystem::DescribeCommand(const FSeinCommand& Cmd)
{
	const FGameplayTag& T = Cmd.CommandType;

	if (T == SeinARTSTags::Command_Type_ActivateAbility)
	{
		const FString TagStr = Cmd.AbilityTag.IsValid() ? Cmd.AbilityTag.ToString() : TEXT("None");
		const FVector Loc = Cmd.TargetLocation.ToVector();
		FString Desc = FString::Printf(TEXT("ActivateAbility [%s] @ (%.0f, %.0f, %.0f)"),
			*TagStr, Loc.X, Loc.Y, Loc.Z);
		if (Cmd.TargetEntity.IsValid())
		{
			Desc += FString::Printf(TEXT(" -> E%d"), Cmd.TargetEntity.Index);
		}
		if (Cmd.bQueueCommand)
		{
			Desc += TEXT(" [Q]");
		}
		return Desc;
	}
	if (T == SeinARTSTags::Command_Type_CancelAbility)
	{
		return TEXT("CancelAbility");
	}
	// Command_Type_QueueProduction + Command_Type_SetRallyPoint removed
	// (refactored 2026-05-05): production unified into ActivateAbility, rally
	// authoring into SA_SetRallyPoint abilities.
	if (T == SeinARTSTags::Command_Type_CancelProduction)
	{
		return FString::Printf(TEXT("CancelProduction idx=%d"), Cmd.QueueIndex);
	}
	if (T == SeinARTSTags::Command_Type_Ping)
	{
		const FVector Loc = Cmd.TargetLocation.ToVector();
		FString Desc = FString::Printf(TEXT("PING (%.0f, %.0f, %.0f)"), Loc.X, Loc.Y, Loc.Z);
		if (Cmd.TargetEntity.IsValid())
		{
			Desc += FString::Printf(TEXT(" on E%d"), Cmd.TargetEntity.Index);
		}
		return Desc;
	}
	if (T == SeinARTSTags::Command_Type_Observer_CameraUpdate)
	{
		const FVector Loc = Cmd.TargetLocation.ToVector();
		const float Yaw = Cmd.AuxA.ToFloat();
		const float Zoom = Cmd.AuxB.ToFloat();
		const float Pitch = Cmd.AuxLocation.X.ToFloat();
		return FString::Printf(TEXT("CameraUpdate pos=(%.0f,%.0f,%.0f) yaw=%.1f pitch=%.1f zoom=%.0f"),
			Loc.X, Loc.Y, Loc.Z, Yaw, Pitch, Zoom);
	}
	if (T == SeinARTSTags::Command_Type_Observer_SelectionChanged)
	{
		return FString::Printf(TEXT("SelectionChanged (%d ents, focus=%d)"),
			Cmd.EntityList.Num(), Cmd.ActiveFocusIndex);
	}
	if (T == SeinARTSTags::Command_Type_BrokerOrder)
	{
		// Pull the dominant Target.* tag from the click context (Ground / Friendly
		// / Neutral / Enemy). That's the most actionable bit for a debugging
		// player — confirms ground-vs-entity hit detection without spelunking
		// the full payload. Designer-added tags layered on top are ignored here
		// for compactness; if needed, toggle a verbose flag later.
		FString ContextStr = TEXT("?");
		FString DragSuffix;
		if (const FSeinBrokerOrderPayload* Payload = Cmd.Payload.GetPtr<FSeinBrokerOrderPayload>())
		{
			const FGameplayTagContainer& Ctx = Payload->CommandContext;
			if (Ctx.HasTag(SeinARTSTags::Command_Context_Target_Enemy))         ContextStr = TEXT("Enemy");
			else if (Ctx.HasTag(SeinARTSTags::Command_Context_Target_Friendly)) ContextStr = TEXT("Friendly");
			else if (Ctx.HasTag(SeinARTSTags::Command_Context_Target_Neutral))  ContextStr = TEXT("Neutral");
			else if (Ctx.HasTag(SeinARTSTags::Command_Context_Target_Ground))   ContextStr = TEXT("Ground");

			// Drag-order endpoint: zero = single click, non-zero = drag formation.
			const FFixedVector& End = Payload->FormationEnd;
			if (End.X != FFixedPoint::Zero || End.Y != FFixedPoint::Zero)
			{
				const FVector EndV = End.ToVector();
				DragSuffix = FString::Printf(TEXT(" drag→(%.0f,%.0f)"), EndV.X, EndV.Y);
			}
		}

		const FVector Loc = Cmd.TargetLocation.ToVector();
		// `{R}` is a placeholder substituted by the HUD at render time once the
		// broker dispatches and OnBrokerOrderDispatched fires (see ResolvedAbilities
		// on the entry). Pre-resolution it renders as nothing; post-resolution
		// it expands to "Resolved to <Tag> (<Name>), … ".
		FString Desc = FString::Printf(TEXT("BrokerOrder [%s] {R}@ (%.0f,%.0f,%.0f)"),
			*ContextStr, Loc.X, Loc.Y, Loc.Z);
		if (Cmd.TargetEntity.IsValid())
		{
			Desc += FString::Printf(TEXT(" -> E%d"), Cmd.TargetEntity.Index);
		}
		Desc += DragSuffix;
		if (Cmd.bQueueCommand)
		{
			Desc += TEXT(" [Q]");
		}
		return Desc;
	}
	// Unknown / designer-extended command type — log the raw tag.
	return FString::Printf(TEXT("%s"), T.IsValid() ? *T.ToString() : TEXT("???"));
}

FColor USeinCommandLogSubsystem::GetCommandColor(FGameplayTag CommandType)
{
	if (CommandType == SeinARTSTags::Command_Type_ActivateAbility)  return FColor(80, 255, 80);
	if (CommandType == SeinARTSTags::Command_Type_CancelAbility)    return FColor(255, 160, 0);
	if (CommandType == SeinARTSTags::Command_Type_CancelProduction) return FColor(255, 255, 80);
	if (CommandType == SeinARTSTags::Command_Type_Ping)             return FColor(255, 80, 255);
	if (CommandType == SeinARTSTags::Command_Type_BrokerOrder)      return FColor(180, 220, 140);
	if (CommandType.MatchesTag(SeinARTSTags::Command_Type_Observer)) return FColor(120, 120, 120);
	return FColor::White;
}
