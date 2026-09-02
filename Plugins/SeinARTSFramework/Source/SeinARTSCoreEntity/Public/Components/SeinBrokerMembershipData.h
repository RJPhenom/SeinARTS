/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinBrokerMembershipData.h
 * @brief   Back-reference component placed on broker members (DESIGN §5).
 *          Drives the "one broker per member" invariant: adding an entity to
 *          a new broker first evicts it from the broker pointed to here.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinEntityHandle.h"
#include "Components/SeinPayload.h"
#include "SeinBrokerMembershipData.generated.h"

/**
 * Back-reference stamped on each entity when it joins a broker. Lets the
 * dispatcher find-and-evict in O(1) instead of walking every broker in the
 * pool. Value is invalid when the entity has no current broker.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic, SeinSubData))
struct SEINARTSCOREENTITY_API FSeinBrokerMembershipData : public FSeinPayload
{
	GENERATED_BODY()

	/** Handle of the broker this entity currently belongs to. Invalid means
	 *  "not in any broker." */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Broker")
	FSeinEntityHandle CurrentBrokerHandle;

	/** Per-ORDER cohesion group id. Identifies the whole selection an entity was
	 *  ordered with, ACROSS the separate dispatch tracks a selection splits into —
	 *  loose units land in one ephemeral broker, each squad's members keep their own
	 *  squad broker, so `CurrentBrokerHandle` alone is single-level and won't tell a
	 *  squad member it was co-selected with another squad (or with loose units). The
	 *  command processor stamps the SAME id on every participating unit of a multi-
	 *  element order so the local-avoidance cohesion skip treats them as one group
	 *  (they pack instead of steering around each other); the hard floor still
	 *  prevents overlap. 0 = not in any cohesion group (solo order / never grouped).
	 *
	 *  Deterministic by construction: minted from (CurrentTick, within-tick order
	 *  index) during CommandProcessing, identical on every client and after a snapshot
	 *  restore (the id rides this serialized component; nothing re-mints on restore). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Broker")
	int64 CohesionGroupId = 0;
};

FORCEINLINE uint32 GetTypeHash(const FSeinBrokerMembershipData& Data)
{
	uint32 Hash = GetTypeHash(Data.CurrentBrokerHandle);
	Hash = HashCombine(Hash, GetTypeHash(Data.CohesionGroupId));
	return Hash;
}
