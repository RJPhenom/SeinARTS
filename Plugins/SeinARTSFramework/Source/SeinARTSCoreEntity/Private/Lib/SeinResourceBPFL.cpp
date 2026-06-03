/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinResourceBPFL.cpp
 * @brief   Implementation of the unified resource BPFL.
 */

#include "Lib/SeinResourceBPFL.h"
#include "Core/SeinPlayerState.h"
#include "Core/SeinSimContext.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinResource, Log, All);

namespace SeinResourceInternal
{
	/** Locate a catalog entry by resource tag. Returns nullptr if not catalogued. */
	static const FSeinResourceDefinition* FindCatalogEntry(FGameplayTag ResourceTag)
	{
		if (!ResourceTag.IsValid()) { return nullptr; }

		const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
		return Settings->ResourceCatalog.FindByPredicate(
			[&](const FSeinResourceDefinition& D) { return D.ResourceTag == ResourceTag; });
	}

	/** Resolve the effective cap for a resource on a given player: per-player override wins
	 *  over catalog default. FFixedPoint::Zero means uncapped. */
	static FFixedPoint ResolveCap(const FSeinPlayerState& State, FGameplayTag ResourceTag)
	{
		if (const FFixedPoint* Override = State.ResourceCaps.Find(ResourceTag))
		{
			return *Override;
		}
		const FSeinResourceDefinition* Def = FindCatalogEntry(ResourceTag);
		return Def ? Def->DefaultCap : FFixedPoint::Zero;
	}

	/** Apply a positive-amount entry to a player's balance, respecting catalog overflow policy.
	 *  Cost direction is not consulted here — callers decide deduct vs add based on intent.
	 *  Cap is respected per catalog OverflowBehavior. */
	static void AddWithCap(FSeinPlayerState& State, FGameplayTag ResourceTag, FFixedPoint Amount)
	{
		if (!ResourceTag.IsValid() || Amount == FFixedPoint::Zero) { return; }

		FFixedPoint& Current = State.Resources.FindOrAdd(ResourceTag);
		const FFixedPoint Candidate = Current + Amount;
		const FFixedPoint Cap = ResolveCap(State, ResourceTag);

		const FSeinResourceDefinition* Def = FindCatalogEntry(ResourceTag);
		const ESeinResourceOverflowBehavior Policy = Def ? Def->OverflowBehavior
			: ESeinResourceOverflowBehavior::ClampAtCap;

		if (Cap == FFixedPoint::Zero || Policy == ESeinResourceOverflowBehavior::AllowUnbounded)
		{
			Current = Candidate;
		}
		else
		{
			// ClampAtCap (and RedirectOverflow pre-handler) both clamp here for V1.
			Current = Candidate > Cap ? Cap : Candidate;
		}
	}
}

USeinWorldSubsystem* USeinResourceBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) { return nullptr; }
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

// ==================== Reads ====================

FFixedPoint USeinResourceBPFL::SeinGetResource(const UObject* WorldContextObject, FSeinPlayerID PlayerID, FGameplayTag ResourceTag)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) { return FFixedPoint::Zero; }

	const FSeinPlayerState* State = Subsystem->GetPlayerState(PlayerID);
	return State ? State->GetResource(ResourceTag) : FFixedPoint::Zero;
}

TMap<FGameplayTag, FFixedPoint> USeinResourceBPFL::SeinGetAllResources(const UObject* WorldContextObject, FSeinPlayerID PlayerID)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) { return {}; }

	const FSeinPlayerState* State = Subsystem->GetPlayerState(PlayerID);
	return State ? State->Resources : TMap<FGameplayTag, FFixedPoint>();
}

FFixedPoint USeinResourceBPFL::SeinGetResourceCap(const UObject* WorldContextObject, FSeinPlayerID PlayerID, FGameplayTag ResourceTag)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) { return FFixedPoint::Zero; }

	const FSeinPlayerState* State = Subsystem->GetPlayerState(PlayerID);
	return State ? SeinResourceInternal::ResolveCap(*State, ResourceTag) : FFixedPoint::Zero;
}

bool USeinResourceBPFL::SeinCanAfford(const UObject* WorldContextObject, FSeinPlayerID PlayerID, const FSeinResourceCost& Cost)
{
	if (Cost.IsEmpty()) { return true; }

	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) { return false; }

	const FSeinPlayerState* State = Subsystem->GetPlayerState(PlayerID);
	if (!State) { return false; }

	for (const auto& Entry : Cost.Amounts)
	{
		const FSeinResourceDefinition* Def = SeinResourceInternal::FindCatalogEntry(Entry.Key);
		const ESeinCostDirection Direction = Def ? Def->CostDirection : ESeinCostDirection::DeductFromBalance;
		const FFixedPoint Balance = State->GetResource(Entry.Key);

		if (Direction == ESeinCostDirection::AddTowardCap)
		{
			const FFixedPoint Cap = SeinResourceInternal::ResolveCap(*State, Entry.Key);
			// If cap is uncapped (Zero) treat AddTowardCap as always-affordable; otherwise check room.
			if (Cap > FFixedPoint::Zero && (Balance + Entry.Value) > Cap)
			{
				return false;
			}
		}
		else // DeductFromBalance
		{
			const ESeinResourceSpendBehavior Spend = Def ? Def->SpendBehavior : ESeinResourceSpendBehavior::RejectOnInsufficient;
			if (Spend == ESeinResourceSpendBehavior::RejectOnInsufficient && Balance < Entry.Value)
			{
				return false;
			}
		}
	}
	return true;
}

void USeinResourceBPFL::SeinSplitCostByCatalog(const UObject* /*WorldContextObject*/,
	const FSeinResourceCost& InCost,
	FSeinResourceCost& OutAtEnqueueCost,
	FSeinResourceCost& OutAtCompletionCost)
{
	OutAtEnqueueCost.Amounts.Reset();
	OutAtCompletionCost.Amounts.Reset();
	if (InCost.IsEmpty()) { return; }

	OutAtEnqueueCost.Amounts.Reserve(InCost.Amounts.Num());

	for (const auto& Pair : InCost.Amounts)
	{
		const FSeinResourceDefinition* Def = SeinResourceInternal::FindCatalogEntry(Pair.Key);
		const ESeinProductionDeductionTiming Timing = Def
			? Def->ProductionDeductionTiming
			: ESeinProductionDeductionTiming::AtEnqueue;
		if (Timing == ESeinProductionDeductionTiming::AtCompletion)
		{
			OutAtCompletionCost.Amounts.Add(Pair.Key, Pair.Value);
		}
		else
		{
			OutAtEnqueueCost.Amounts.Add(Pair.Key, Pair.Value);
		}
	}
}

// ==================== Writes ====================

bool USeinResourceBPFL::SeinDeduct(const UObject* WorldContextObject, FSeinPlayerID PlayerID, const FSeinResourceCost& Cost)
{
	SEIN_CHECK_SIM();

	if (Cost.IsEmpty()) { return true; }

	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) { return false; }

	FSeinPlayerState* State = Subsystem->GetPlayerState(PlayerID);
	if (!State) { return false; }

	if (!SeinCanAfford(WorldContextObject, PlayerID, Cost)) { return false; }

	for (const auto& Entry : Cost.Amounts)
	{
		const FSeinResourceDefinition* Def = SeinResourceInternal::FindCatalogEntry(Entry.Key);
		const ESeinCostDirection Direction = Def ? Def->CostDirection : ESeinCostDirection::DeductFromBalance;

		if (Direction == ESeinCostDirection::AddTowardCap)
		{
			SeinResourceInternal::AddWithCap(*State, Entry.Key, Entry.Value);
		}
		else
		{
			FFixedPoint& Current = State->Resources.FindOrAdd(Entry.Key);
			Current = Current - Entry.Value;
		}
	}
	return true;
}

void USeinResourceBPFL::SeinRefund(const UObject* WorldContextObject, FSeinPlayerID PlayerID, const FSeinResourceCost& Cost)
{
	SEIN_CHECK_SIM();

	if (Cost.IsEmpty()) { return; }

	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) { return; }

	FSeinPlayerState* State = Subsystem->GetPlayerState(PlayerID);
	if (!State) { return; }

	for (const auto& Entry : Cost.Amounts)
	{
		const FSeinResourceDefinition* Def = SeinResourceInternal::FindCatalogEntry(Entry.Key);
		const ESeinCostDirection Direction = Def ? Def->CostDirection : ESeinCostDirection::DeductFromBalance;

		if (Direction == ESeinCostDirection::AddTowardCap)
		{
			// Refund of a cap-bound resource is subtractive (frees up the cap slot).
			FFixedPoint& Current = State->Resources.FindOrAdd(Entry.Key);
			Current = Current - Entry.Value;
		}
		else
		{
			SeinResourceInternal::AddWithCap(*State, Entry.Key, Entry.Value);
		}
	}
}

void USeinResourceBPFL::SeinGrantIncome(const UObject* WorldContextObject, FSeinPlayerID PlayerID, const FSeinResourceCost& Amount)
{
	SEIN_CHECK_SIM();

	if (Amount.IsEmpty()) { return; }

	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) { return; }

	FSeinPlayerState* State = Subsystem->GetPlayerState(PlayerID);
	if (!State) { return; }

	for (const auto& Entry : Amount.Amounts)
	{
		SeinResourceInternal::AddWithCap(*State, Entry.Key, Entry.Value);
	}
}

bool USeinResourceBPFL::SeinTransfer(const UObject* WorldContextObject, FSeinPlayerID FromPlayer, FSeinPlayerID ToPlayer, const FSeinResourceCost& Amount)
{
	SEIN_CHECK_SIM();

	if (Amount.IsEmpty()) { return true; }
	if (FromPlayer == ToPlayer) { return true; }

	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) { return false; }

	// SeinTransfer is an unconditional primitive — affordability check
	// remains, but any gameplay gate ("no mid-match transfers", "only
	// allied players can transfer", etc.) is designer-authored at the
	// caller. Wrap this BPFL in a project-side BPFL/BP graph that checks
	// whatever rule struct or per-pair tag your game uses, then calls
	// here for the actual deduct + grant.

	FSeinPlayerState* FromState = Subsystem->GetPlayerState(FromPlayer);
	FSeinPlayerState* ToState = Subsystem->GetPlayerState(ToPlayer);
	if (!FromState || !ToState) { return false; }

	if (!SeinCanAfford(WorldContextObject, FromPlayer, Amount)) { return false; }

	// Deduct from sender, grant to receiver. Honor catalog caps on the receive side.
	for (const auto& Entry : Amount.Amounts)
	{
		FFixedPoint& Current = FromState->Resources.FindOrAdd(Entry.Key);
		Current = Current - Entry.Value;

		SeinResourceInternal::AddWithCap(*ToState, Entry.Key, Entry.Value);
	}
	return true;
}
