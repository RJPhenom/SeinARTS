/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSquadReinforcementService.h
 * @brief   Deterministic reinforcement request identity and accounting.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinEntityHandle.h"
#include "Components/SeinSquadPayload.h"

class USeinWorldSubsystem;

/** Native reinforcement transaction service shared by the starter ability,
 * mutation API, lifecycle system, and focused tests. */
class SEINARTSSQUAD_API FSeinSquadReinforcementService
{
public:
	/** Lowest tag-name metadata on a slot. Tag container order is never identity. */
	static FGameplayTag ResolveCanonicalSlotTag(const FSeinSquadSlot& Slot);

	/** Pure structural eligibility. Affordability is checked atomically by enqueue. */
	static bool IsSlotEnqueueable(
		const FSeinSquadPayload& Squad,
		int32 SlotIndex);

	/** First structurally eligible slot in declaration order. */
	static int32 FindFirstEnqueueableSlot(
		const FSeinSquadPayload& Squad);

	/** Validate, deduct, allocate one monotonic ID, and append atomically. */
	static bool TryEnqueue(
		USeinWorldSubsystem& World,
		FSeinEntityHandle SquadHandle,
		int32 SlotIndex,
		int64& OutRequestID);

	/** Cancel one exact request and refund its snapshotted payer/cost. */
	static bool CancelByRequestID(
		USeinWorldSubsystem& World,
		FSeinEntityHandle SquadHandle,
		int64 RequestID);

	/** Cancel/refund every request targeting one exact slot. */
	static int32 CancelForSlot(
		USeinWorldSubsystem& World,
		FSeinEntityHandle SquadHandle,
		int32 SlotIndex);
};
