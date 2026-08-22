/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinConnectionAdmission.h
 * @author       RJ Macklem
 * @created      21 Aug 2026
 * @latest       21 Aug 2026
 * @brief        Declares the optional server admission seam ahead of relay ownership.
 *
 *               Net owns the hook and exact-seat enforcement. Optional online
 *               providers register without creating a reverse dependency.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinPlayerID.h"
#include "GameFramework/PlayerState.h"
#include "SeinNetProtocolTypes.h"

/** Untrusted connection material presented before a controller receives a relay. */
struct SEINARTSNET_API FSeinConnectionAdmissionRequest
{
	/** Non-secret correlation identity from the SeinAdmission URL option. */
	FString AdmissionID;

	/**
	 * Raw login options. Unreal logs this value, so admission authorities must
	 * never require bearer credentials or other secrets in connection options.
	 */
	FString Options;

	/** Remote transport address. Never use as stable product identity. */
	FString Address;

	/** Platform identity supplied by the active transport, when available. */
	FUniqueNetIdRepl PlatformIdentity;
};

/** Sanitized decision returned by one optional admission authority. */
struct SEINARTSNET_API FSeinConnectionAdmissionDecision
{
	/** True only after provider-owned account, match, participant, and seat validation. */
	bool bAccepted = false;

	/** Exact positive gameplay slot authorized for the connection. */
	FSeinPlayerID AssignedSlot;

	/** Exact active lockstep match authenticated by the authority. */
	FSeinMatchInstanceID MatchID;

	/** Exact active lockstep participant authenticated by the authority. */
	FSeinNetworkParticipantID ParticipantID;

	/** Safe client-facing refusal text. It must not contain credentials. */
	FString ErrorMessage = TEXT("Online admission rejected");
};

/** Synchronous resolution of non-secret admission identity to one exact seat. */
DECLARE_DELEGATE_RetVal_OneParam(
	FSeinConnectionAdmissionDecision,
	FSeinConnectionAdmissionAuthorizer,
	const FSeinConnectionAdmissionRequest&);
