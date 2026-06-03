/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCommandLogSubsystem.h
 * @brief   Debug subsystem that captures command transactions from the sim
 *          and provides data for the HUD overlay to render.
 *          Toggle: Sein.Commands.ShowLog  |  Clear: Sein.Commands.ClearLog
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Input/SeinCommand.h"
#include "Core/SeinPlayerID.h"
#include "Simulation/SeinWorldSubsystem.h"  // FSeinBrokerResolvedAbility, FOnBrokerOrderDispatched
#include "GameplayTagContainer.h"
#include "SeinCommandLogSubsystem.generated.h"

class USeinWorldSubsystem;

/**
 * A single entry in the debug command log.
 */
USTRUCT()
struct FSeinCommandLogEntry
{
	GENERATED_BODY()

	int32 Tick = 0;
	int32 PlayerIndex = -1;
	int32 EntityIndex = -1;
	/** For commands that target a list (BrokerOrder, SelectionChanged) — the
	 *  list size. The HUD shows "E×N" instead of "E-01" when this is > 0 and
	 *  EntityIndex is invalid. 0 means "no list applies; render EntityIndex." */
	int32 MemberCount = 0;
	FGameplayTag CommandType;
	FString Description;
	/** Filled by USeinWorldSubsystem::OnBrokerOrderDispatched — the unique abilities
	 *  the broker's resolver picked (tag + display name pulled from
	 *  USeinAbility::AbilityName at dispatch time). Empty until the dispatch
	 *  event fires (immediate for new brokers, deferred for shift-queued orders
	 *  that wait for the queue to advance). The HUD substitutes these into the
	 *  BrokerOrder description's `{R}` placeholder at render time. */
	TArray<FSeinBrokerResolvedAbility> ResolvedAbilities;
	FColor DisplayColor = FColor::White;
};

/**
 * Data-only debug subsystem: captures commands via OnCommandsProcessing delegate.
 * Rendering is done by ASeinHUD which reads from this subsystem.
 */
UCLASS()
class SEINARTSFRAMEWORK_API USeinCommandLogSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Whether the overlay should be drawn by the HUD. */
	bool bShowOverlay = false;

	/** Clear the log. */
	void ClearLog() { LogEntries.Empty(); }

	/** Read log entries (most recent last). */
	const TArray<FSeinCommandLogEntry>& GetLogEntries() const { return LogEntries; }

	/** Max entries to keep. */
	int32 MaxLogEntries = 256;

	/** Max entries to display on screen. */
	int32 MaxDisplayEntries = 24;

private:
	TArray<FSeinCommandLogEntry> LogEntries;
	TWeakObjectPtr<USeinWorldSubsystem> SimSubsystem;
	FDelegateHandle CommandsDelegateHandle;
	FDelegateHandle BrokerDispatchedDelegateHandle;

	void OnCommandsProcessing(int32 Tick, const TArray<FSeinCommand>& Commands);
	void OnBrokerOrderDispatched(int32 Tick, FSeinPlayerID PlayerID, const TArray<FSeinBrokerResolvedAbility>& UniqueResolved);
	static FString DescribeCommand(const FSeinCommand& Cmd);
	static FColor GetCommandColor(FGameplayTag CommandType);
};
