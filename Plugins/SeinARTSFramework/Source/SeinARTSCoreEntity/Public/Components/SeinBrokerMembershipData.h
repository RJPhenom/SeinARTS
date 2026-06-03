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
#include "Components/SeinComponent.h"
#include "SeinBrokerMembershipData.generated.h"

/**
 * Back-reference stamped on each entity when it joins a broker. Lets the
 * dispatcher find-and-evict in O(1) instead of walking every broker in the
 * pool. Value is invalid when the entity has no current broker.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic, SeinSubData))
struct SEINARTSCOREENTITY_API FSeinBrokerMembershipData : public FSeinComponent
{
	GENERATED_BODY()

	/** Handle of the broker this entity currently belongs to. Invalid means
	 *  "not in any broker." */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Broker")
	FSeinEntityHandle CurrentBrokerHandle;
};

FORCEINLINE uint32 GetTypeHash(const FSeinBrokerMembershipData& Data)
{
	return GetTypeHash(Data.CurrentBrokerHandle);
}
