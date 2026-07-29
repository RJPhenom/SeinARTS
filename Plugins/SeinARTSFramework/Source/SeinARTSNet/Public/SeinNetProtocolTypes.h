/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file SeinNetProtocolTypes.h
 * @brief Topology-neutral identities and participant capabilities for lockstep traffic.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinPlayerID.h"
#include "Input/SeinCommand.h"
#include "SeinNetProtocolTypes.generated.h"

/** Hard protocol limits for membership and canonical turn traffic. */
namespace SeinNetProtocolLimits
{
	/** Processes in one frozen match manifest, including non-authoring peers. */
	constexpr int32 MaxParticipants = 64;

	/** Independent command authors (participant + owned gameplay slot). */
	constexpr int32 MaxCommandAuthors = 16;
	constexpr int32 MaxCommandsPerAuthor =
		SeinCommandProtocolLimits::MaxCommandsPerAuthor;
	constexpr int32 MaxCommandsPerCanonicalTurn =
		MaxCommandAuthors * MaxCommandsPerAuthor;
}

/** What a world transition means to match-scoped network state. */
UENUM(BlueprintType)
enum class ESeinMatchTravelIntent : uint8
{
	/** Allocate a fresh match identity, seed, roster lifecycle, and replay. */
	NewMatch,
	/** Preserve match state while starting a new lockstep tick/turn epoch. */
	ContinueMatch,
};

/**
 * Untrusted transport envelope for one locally authored command draft.
 * PlayerID, IssuerKind, and Tick inside Command are never authoritative.
 * Match-administration is an explicit request that ingress grants only when
 * the authenticated participant owns that independent capability and the
 * registered command schema is MatchControl.
 */
USTRUCT()
struct SEINARTSNET_API FSeinCommandSubmissionDraft
{
	GENERATED_BODY()

	UPROPERTY()
	FSeinCommand Command;

	UPROPERTY()
	bool bRequestMatchAdministration = false;

	FSeinCommandSubmissionDraft() = default;
	FSeinCommandSubmissionDraft(
		const FSeinCommand& InCommand,
		bool bInRequestMatchAdministration = false)
		: Command(InCommand)
		, bRequestMatchAdministration(bInRequestMatchAdministration)
	{
	}
};

/**
 * RPC-safe opaque command bytes. Its custom net serializer checks the claimed
 * byte count before TArray allocation; command/schema decoding happens later at
 * the subsystem boundary.
 */
USTRUCT()
struct SEINARTSNET_API FSeinOpaqueCommandBatch
{
	GENERATED_BODY()

	static constexpr uint32 MaxBytes = 8u * 1024u * 1024u;

	UPROPERTY()
	TArray<uint8> Bytes;

	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template<>
struct TStructOpsTypeTraits<FSeinOpaqueCommandBatch>
	: public TStructOpsTypeTraitsBase2<FSeinOpaqueCommandBatch>
{
	enum
	{
		WithNetSerializer = true,
	};
};

/** Opaque identity shared by every participant in one logical match. */
USTRUCT(BlueprintType)
struct SEINARTSNET_API FSeinMatchInstanceID
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Network")
	FGuid Value;

	FSeinMatchInstanceID() = default;
	explicit FSeinMatchInstanceID(const FGuid& InValue) : Value(InValue) {}

	static FSeinMatchInstanceID Invalid() { return FSeinMatchInstanceID(); }
	bool IsValid() const { return Value.IsValid(); }
	FString ToCanonicalString() const;

	bool operator==(const FSeinMatchInstanceID& Other) const { return Value == Other.Value; }
	bool operator!=(const FSeinMatchInstanceID& Other) const { return !(*this == Other); }
};

FORCEINLINE uint32 GetTypeHash(const FSeinMatchInstanceID& ID)
{
	return GetTypeHash(ID.Value);
}

/**
 * Stable identity of a network participant/process. This is deliberately not
 * a gameplay player slot: a dedicated referee or spectator can participate in
 * simulation/hash exchange without owning a FSeinPlayerID, while one
 * participant may be granted more than one command slot by policy.
 */
USTRUCT(BlueprintType)
struct SEINARTSNET_API FSeinNetworkParticipantID
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Network")
	FGuid Value;

	FSeinNetworkParticipantID() = default;
	explicit FSeinNetworkParticipantID(const FGuid& InValue) : Value(InValue) {}

	static FSeinNetworkParticipantID Invalid() { return FSeinNetworkParticipantID(); }
	bool IsValid() const { return Value.IsValid(); }
	FString ToCanonicalString() const;

	bool operator==(const FSeinNetworkParticipantID& Other) const { return Value == Other.Value; }
	bool operator!=(const FSeinNetworkParticipantID& Other) const { return !(*this == Other); }
};

FORCEINLINE uint32 GetTypeHash(const FSeinNetworkParticipantID& ID)
{
	return GetTypeHash(ID.Value);
}

/** Terminal reason that a lockstep epoch can no longer continue safely. */
UENUM(BlueprintType)
enum class ESeinDeterminismSessionFailureKind : uint8
{
	None,
	CanonicalRootCaptureFailed,
	CanonicalRootCheckpointExpired,
	ExecutionTopologyInvalidated,
};

/**
 * Topology-neutral terminal determinism-health result. The coordinator
 * distributes this exact value regardless of whether its transport is a
 * dedicated server, listen host, peer authority, or custom adapter.
 */
USTRUCT(BlueprintType)
struct SEINARTSNET_API FSeinDeterminismSessionFailure
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Network|Determinism")
	ESeinDeterminismSessionFailureKind Kind =
		ESeinDeterminismSessionFailureKind::None;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Network|Determinism")
	int32 Turn = INDEX_NONE;

	/** Authenticated reporter, or first canonically missing reporter. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Network|Determinism")
	FSeinNetworkParticipantID ParticipantID;

	bool RequiresCanonicalRootCheckpoint() const
	{
		return Kind == ESeinDeterminismSessionFailureKind::CanonicalRootCaptureFailed
			|| Kind == ESeinDeterminismSessionFailureKind::CanonicalRootCheckpointExpired;
	}

	bool IsParticipantReportable() const
	{
		return Kind == ESeinDeterminismSessionFailureKind::CanonicalRootCaptureFailed
			|| Kind == ESeinDeterminismSessionFailureKind::ExecutionTopologyInvalidated;
	}

	bool IsValid() const
	{
		const bool bKnownKind =
			Kind == ESeinDeterminismSessionFailureKind::CanonicalRootCaptureFailed
			|| Kind == ESeinDeterminismSessionFailureKind::CanonicalRootCheckpointExpired
			|| Kind == ESeinDeterminismSessionFailureKind::ExecutionTopologyInvalidated;
		return bKnownKind
			&& (RequiresCanonicalRootCheckpoint() ? Turn > 0 : Turn >= 0)
			&& ParticipantID.IsValid();
	}

	bool operator==(const FSeinDeterminismSessionFailure& Other) const
	{
		return Kind == Other.Kind
			&& Turn == Other.Turn
			&& ParticipantID == Other.ParticipantID;
	}
	bool operator!=(const FSeinDeterminismSessionFailure& Other) const
	{
		return !(*this == Other);
	}
};

/** Identity namespace carried by every lockstep protocol message. */
USTRUCT(BlueprintType)
struct SEINARTSNET_API FSeinProtocolContext
{
	GENERATED_BODY()

	/** Increment when the wire contract changes incompatibly. */
	static constexpr int32 CurrentProtocolVersion = 10;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Network")
	int32 ProtocolVersion = CurrentProtocolVersion;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Network")
	FSeinMatchInstanceID MatchInstanceID;

	/** Tick/turn namespace within the match. Zero is invalid. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Network")
	int64 LockstepEpoch = 0;

	/** Participant currently responsible for canonical protocol coordination. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Network")
	FSeinNetworkParticipantID CoordinatorParticipantID;

	/** Coordinator-election namespace within the match. Zero is invalid. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Network")
	int64 CoordinatorTerm = 0;

	/** Monotonic roster/role namespace within the lockstep epoch. Zero is invalid. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Network")
	int64 MembershipRevision = 0;

	/** Canonical 128-bit digest of the participant bindings. Zero is invalid. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Network")
	FGuid MembershipDigest;

	/** PIE-independent identity of the world that may materialize tick zero. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Network")
	FGuid DestinationWorldDigest;

	/** Canonical identity of the immutable match-settings snapshot. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Network")
	FGuid MatchSettingsDigest;

	/** Generated Blueprint/native simulation-content identity. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Network")
	FGuid SimulationContentDigest;

	/** Frozen command-schema + selected authority-policy identity. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Network")
	FGuid CommandProtocolDigest;

	FSeinProtocolContext() = default;
	FSeinProtocolContext(
		FSeinMatchInstanceID InMatchInstanceID,
		int64 InLockstepEpoch,
		FSeinNetworkParticipantID InCoordinatorParticipantID,
		int64 InCoordinatorTerm,
		int64 InMembershipRevision,
		FGuid InMembershipDigest,
		FGuid InDestinationWorldDigest,
		FGuid InMatchSettingsDigest,
		FGuid InSimulationContentDigest,
		FGuid InCommandProtocolDigest,
		int32 InProtocolVersion = CurrentProtocolVersion)
		: ProtocolVersion(InProtocolVersion)
		, MatchInstanceID(InMatchInstanceID)
		, LockstepEpoch(InLockstepEpoch)
		, CoordinatorParticipantID(InCoordinatorParticipantID)
		, CoordinatorTerm(InCoordinatorTerm)
		, MembershipRevision(InMembershipRevision)
		, MembershipDigest(InMembershipDigest)
		, DestinationWorldDigest(InDestinationWorldDigest)
		, MatchSettingsDigest(InMatchSettingsDigest)
		, SimulationContentDigest(InSimulationContentDigest)
		, CommandProtocolDigest(InCommandProtocolDigest)
	{
	}

	bool IsValid() const;
	FString ToCanonicalDebugString() const;

	bool operator==(const FSeinProtocolContext& Other) const
	{
		return ProtocolVersion == Other.ProtocolVersion
			&& MatchInstanceID == Other.MatchInstanceID
			&& LockstepEpoch == Other.LockstepEpoch
			&& CoordinatorParticipantID == Other.CoordinatorParticipantID
			&& CoordinatorTerm == Other.CoordinatorTerm
			&& MembershipRevision == Other.MembershipRevision
			&& MembershipDigest == Other.MembershipDigest
			&& DestinationWorldDigest == Other.DestinationWorldDigest
			&& MatchSettingsDigest == Other.MatchSettingsDigest
			&& SimulationContentDigest == Other.SimulationContentDigest
			&& CommandProtocolDigest == Other.CommandProtocolDigest;
	}
	bool operator!=(const FSeinProtocolContext& Other) const { return !(*this == Other); }
};

FORCEINLINE uint32 GetTypeHash(const FSeinProtocolContext& Context)
{
	uint32 Hash = GetTypeHash(Context.ProtocolVersion);
	Hash = HashCombineFast(Hash, GetTypeHash(Context.MatchInstanceID));
	Hash = HashCombineFast(Hash, GetTypeHash(Context.LockstepEpoch));
	Hash = HashCombineFast(Hash, GetTypeHash(Context.CoordinatorParticipantID));
	Hash = HashCombineFast(Hash, GetTypeHash(Context.CoordinatorTerm));
	Hash = HashCombineFast(Hash, GetTypeHash(Context.MembershipRevision));
	Hash = HashCombineFast(Hash, GetTypeHash(Context.MembershipDigest));
	Hash = HashCombineFast(Hash, GetTypeHash(Context.DestinationWorldDigest));
	Hash = HashCombineFast(Hash, GetTypeHash(Context.MatchSettingsDigest));
	Hash = HashCombineFast(Hash, GetTypeHash(Context.SimulationContentDigest));
	return HashCombineFast(Hash, GetTypeHash(Context.CommandProtocolDigest));
}

/**
 * Canonical identity used to bind one tick-zero authorization to the exact
 * protocol namespace and deterministic session seed. The seed is deliberately
 * included here because it is transported beside (not inside) ProtocolContext
 * and changes the materialized PRNG state before tick zero.
 */
SEINARTSNET_API FGuid SeinComputeBootstrapAuthorizationContextDigest(
	const FSeinProtocolContext& Context,
	int64 SessionSeed);

/** Persistent participant-to-gameplay-role binding for one match. */
USTRUCT(BlueprintType)
struct SEINARTSNET_API FSeinParticipantBinding
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Network")
	FSeinNetworkParticipantID ParticipantID;

	/** Gameplay slots from which this participant may author command batches. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Network")
	TArray<FSeinPlayerID> CommandSlots;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Network")
	bool bSimulates = false;

	/** World-state-root reporters must also simulate. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Network")
	bool bReportsWorldRoots = false;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Network")
	bool bCanCoordinate = false;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Network")
	bool bCanAdministerMatch = false;

	bool IsValid() const;
};

/** Canonical PIE-stripped identity of one long world-package name. */
SEINARTSNET_API FGuid SeinComputeDestinationWorldDigest(
	const FString& WorldPackageName);

/**
 * Stable order-independent digest of a valid participant manifest. Binding
 * order and per-binding slot order do not affect the result. Zero is reserved
 * and is never returned.
 */
SEINARTSNET_API FGuid SeinComputeMembershipDigest(
	const TArray<FSeinParticipantBinding>& Bindings);

/** One independently expected command author within a participant binding. */
USTRUCT(BlueprintType)
struct SEINARTSNET_API FSeinTurnAuthor
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Network")
	FSeinNetworkParticipantID ParticipantID;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Network")
	FSeinPlayerID CommandSlot;

	FSeinTurnAuthor() = default;
	FSeinTurnAuthor(FSeinNetworkParticipantID InParticipantID, FSeinPlayerID InCommandSlot)
		: ParticipantID(InParticipantID), CommandSlot(InCommandSlot)
	{
	}

	bool IsValid() const { return ParticipantID.IsValid() && CommandSlot.IsValid(); }
	FString ToCanonicalDebugString() const;

	/** Slot-first preserves the framework's existing gameplay command order. */
	static bool CanonicalLess(const FSeinTurnAuthor& A, const FSeinTurnAuthor& B);

	bool operator==(const FSeinTurnAuthor& Other) const
	{
		return ParticipantID == Other.ParticipantID && CommandSlot == Other.CommandSlot;
	}
	bool operator!=(const FSeinTurnAuthor& Other) const { return !(*this == Other); }
};

FORCEINLINE uint32 GetTypeHash(const FSeinTurnAuthor& Author)
{
	return HashCombineFast(GetTypeHash(Author.ParticipantID), GetTypeHash(Author.CommandSlot));
}
