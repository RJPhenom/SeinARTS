/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinFormationOrderToken.h
 * @author       RJ Macklem
 * @created      22 Aug 2026
 * @latest       22 Aug 2026
 * @brief        Opaque Blueprint token for exact formation preview and issue parity.
 *
 *               The token is transient presentation/input state. It freezes a
 *               complete BrokerOrder draft and its displayed world-space
 *               destinations without entering canonical simulation state.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Brokers/SeinBrokerTypes.h"
#include "Simulation/SeinMatchBootstrapBarrier.h"
#include "SeinFormationOrderToken.generated.h"

class USeinCommandBrokerBPFL;
class USeinWorldSubsystem;

/** Result of planning or issuing an exact formation order token. */
UENUM(BlueprintType)
enum class ESeinFormationOrderTokenResult : uint8
{
	/** The token was created or submitted successfully. */
	Success,

	/** One or more required inputs were missing, invalid, or over protocol limits. */
	InvalidInput,

	/** No launched, running match was available for planning. */
	MatchNotReady,

	/** A destination provider could not produce a complete exact plan. */
	PlanningFailed,

	/** The token was issued against a different world. */
	WrongWorld,

	/** The world restored or changed match session after the token was planned. */
	StaleSession,

	/** The live recipient membership no longer matches the displayed plan. */
	StaleRecipients,

	/** The authenticated player cannot control every live planned recipient. */
	Unauthorized,

	/** The active topology adapter or standalone ingress rejected the draft. */
	SubmissionRejected,

	/** The token has already submitted its one allowed command draft. */
	AlreadyIssued,
};

/**
 * Holds one exact displayed formation order until it is issued.
 *
 * Designers create this object with Plan Formation Order and pass it unchanged
 * to Issue Formation Order. Its contents are deliberately not exposed: this
 * prevents a Blueprint from accidentally separating the displayed destinations
 * from the command that consumes them. A token is valid for one submission in
 * the world/session that created it. Provider movement or destruction does not
 * invalidate its frozen world-space destinations.
 */
UCLASS(BlueprintType, NotBlueprintable, Transient,
	meta = (DisplayName = "SeinARTS Formation Order Token",
		DontUseGenericSpawnObject = "true"))
class SEINARTSCOREENTITY_API USeinFormationOrderToken final : public UObject
{
	GENERATED_BODY()

private:
	friend class USeinCommandBrokerBPFL;

	TWeakObjectPtr<USeinWorldSubsystem> World;
	FSeinMatchBootstrapReceipt MatchReceipt;
	uint64 SessionEpoch = 0;
	FSeinPlayerID PlayerID = FSeinPlayerID::Neutral();
	TArray<FSeinEntityHandle> Recipients;
	TArray<FSeinEntityHandle> PlannedMembers;
	TArray<int32> PlannedMemberCountsByRecipient;
	FFixedVector TargetLocation;
	bool bQueueCommand = false;
	FSeinBrokerOrderPayload Payload;
	bool bInitialized = false;
	bool bIssued = false;
};
