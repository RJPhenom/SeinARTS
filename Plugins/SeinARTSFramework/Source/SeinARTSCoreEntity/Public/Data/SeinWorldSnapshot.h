/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinWorldSnapshot.h
 * @brief   Portable USeinWorldSubsystem state snapshot — used for save/load,
 *          drop-in/drop-out catch-up, and (future) state-dump on desync.
 *
 * Captures enough information to reconstruct sim state at tick T on a fresh
 * world, including the consumed bootstrap identity and faction registry.
 * Restore adopts that continuation directly; it never reruns tick-zero work.
 *
 * The local developer `.seinsnapshot` console helper currently serializes the
 * outer USTRUCT via `FObjectAndNameAsStringProxyArchive`. That raw archive is
 * trusted-local tooling, not a bounded/authenticated multiplayer, campaign,
 * cloud-save, or replay-checkpoint envelope. Opaque pool and continuation
 * payloads are produced only by providers frozen into the match state contract.
 *
 * Design note: this is the SIM-side snapshot. Render-side actor positions,
 * particle effects, audio cues, etc. are NOT captured — they're rebuilt
 * by the actor bridge on restore as the sim repopulates its entity list
 * and fires visual events through the normal flow.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinEntityHandle.h"
#include "Core/SeinEntityPool.h"
#include "Core/SeinFactionID.h"
#include "Core/SeinPlayerID.h"
#include "Core/SeinPlayerState.h"
#include "Data/SeinMatchSettings.h"
#include "Data/SeinRelationshipTypes.h"
#include "Data/SeinCameraSnapshotData.h"
#include "Data/SeinSnapshotComponentStorageBlob.h"
#include "Data/SeinVoteState.h"
#include "GameplayTagContainer.h"
#include "Input/SeinCommand.h"
#include "Serialization/SeinCanonicalStateRegistry.h"
#include "Simulation/SeinMatchBootstrapBarrier.h"
#include "Types/Transform.h"
#include "UObject/GCObject.h"
#include "SeinWorldSnapshot.generated.h"

/**
 * Per-pool-slot record for an ability or broker-resolver UObject instance.
 * Imported class identity is only a lookup key into the world's frozen local
 * pool-codec manifest. The record also binds the globally unique provider,
 * exact native anchor, object kind, and all compatibility revisions; restore
 * never loads executable code selected by this data.
 */
USTRUCT()
struct SEINARTSCOREENTITY_API FSeinSnapshotPoolInstanceRecord
{
	GENERATED_BODY()

	/** Slot ID in the source pool. Free-list-recycled IDs survive round-trip. */
	UPROPERTY()
	int32 PoolID = INDEX_NONE;

	/** True iff the slot was occupied at capture. False slots are reserved
	 *  on restore so the index space + free-list match exactly. */
	UPROPERTY()
	bool bAlive = false;

	/** ESeinPoolObjectKind as a frozen raw byte. */
	UPROPERTY()
	uint8 ObjectKind = 0;

	/** Exact locally admitted class path (UClass::GetPathName). */
	UPROPERTY()
	FString ClassPath;

	/** Exact registered native anchor; Blueprint inheritance stops here. */
	UPROPERTY()
	FString NativeAnchorClassPath;

	/** Globally unique deterministic provider identity. */
	UPROPERTY()
	FString StableProviderID;

	UPROPERTY()
	uint32 StateSchemaVersion = 0;

	UPROPERTY()
	uint32 BehaviorRevision = 0;

	UPROPERTY()
	uint32 CodecRevision = 0;

	/** Binds all provider compatibility fields above. */
	UPROPERTY()
	FGuid ProviderDescriptorDigest;

	/** Binds the exact locally admitted native/BP reflected shape. */
	UPROPERTY()
	FGuid ExactClassSchemaDigest;

	/** Bounded provider-owned canonical state. */
	UPROPERTY()
	TArray<uint8> StateBytes;
};

/**
 * One active latent continuation in exact manager order.
 *
 * Imported identity never selects executable code or a schema. Restore first
 * resolves the action class in the locally frozen codec manifest, then compares
 * every compatibility field before decoding the bounded canonical payload.
 */
USTRUCT()
struct SEINARTSCOREENTITY_API FSeinSnapshotLatentActionRecord
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Ordinal = INDEX_NONE;

	UPROPERTY()
	int64 ActionID = 0;

	UPROPERTY()
	int32 AbilityPoolID = INDEX_NONE;

	UPROPERTY()
	int64 AbilityActivationID = 0;

	UPROPERTY()
	FSeinEntityHandle OwnerEntity;

	UPROPERTY()
	FString ActionClassPath;

	UPROPERTY()
	FString StableCodecID;

	UPROPERTY()
	uint32 StateSchemaVersion = 0;

	UPROPERTY()
	uint32 BehaviorRevision = 0;

	UPROPERTY()
	uint32 CodecRevision = 0;

	UPROPERTY()
	FGuid CodecDescriptorDigest;

	UPROPERTY()
	FGuid PayloadSchemaDigest;

	UPROPERTY()
	TArray<uint8> PayloadBytes;

	/** Canonical payload-only digest under its exact schema. */
	UPROPERTY()
	FGuid PayloadDigest;

	/** Digest of ordinal, identities, codec contract, and payload digest. */
	UPROPERTY()
	FGuid RecordDigest;
};

/** Per-entity record captured in the snapshot. Contains the handle's
 *  index/generation, transform, and owner — enough to recreate the entity
 *  pool entry. Component data is captured separately (per-storage). */
USTRUCT()
struct SEINARTSCOREENTITY_API FSeinSnapshotEntityRecord
{
	GENERATED_BODY()

	UPROPERTY()
	int32 SlotIndex = 0;

	UPROPERTY()
	int32 Generation = 0;

	UPROPERTY()
	FFixedTransform Transform;

	UPROPERTY()
	FSeinPlayerID Owner;

	UPROPERTY()
	bool bAlive = false;

	/** Stable class path the entity was originally spawned from. Lets the
	 *  actor bridge re-attach a render actor (and the component injector
	 *  re-walk the CDO) on restore. Empty for legacy / placed-actor entries. */
	UPROPERTY()
	FString ActorClassPath;
};

/** Frozen semantic contribution that participated in the tick-zero receipt. */
USTRUCT()
struct SEINARTSCOREENTITY_API FSeinSnapshotInitialStateContribution
{
	GENERATED_BODY()

	UPROPERTY()
	FName StableContributorID;

	UPROPERTY()
	uint32 SchemaVersion = 0;

	UPROPERTY()
	FGuid ValueDigest;
};

/** Exact faction asset installed under a deterministic faction ID at tick zero. */
USTRUCT()
struct SEINARTSCOREENTITY_API FSeinSnapshotFactionRegistration
{
	GENERATED_BODY()

	UPROPERTY()
	FSeinFactionID FactionID;

	/** Exact object path; redirects are rejected during checkpoint adoption. */
	UPROPERTY()
	FString FactionAssetPath;
};

/** One positive refcount in a canonically ordered entity-tag record. */
USTRUCT()
struct SEINARTSCOREENTITY_API FSeinSnapshotTagRefCount
{
	GENERATED_BODY()

	UPROPERTY()
	FGameplayTag Tag;

	UPROPERTY()
	int32 RefCount = 0;
};

/** Authoritative per-entity tags. CombinedTags is derived from TagRefCounts. */
USTRUCT()
struct SEINARTSCOREENTITY_API FSeinSnapshotEntityTagState
{
	GENERATED_BODY()

	UPROPERTY()
	FSeinEntityHandle Entity;

	/** Exact membership in canonical tag-name order. */
	UPROPERTY()
	TArray<FGameplayTag> BaseTags;

	/** Positive refcounts in canonical tag-name order. */
	UPROPERTY()
	TArray<FSeinSnapshotTagRefCount> RefCounts;
};

/**
 * One global tag-index bucket. Entity order is authoritative because
 * LookupFirstEntityByTag exposes the first element to gameplay.
 */
USTRUCT()
struct SEINARTSCOREENTITY_API FSeinSnapshotTagIndexBucket
{
	GENERATED_BODY()

	UPROPERTY()
	FGameplayTag Tag;

	UPROPERTY()
	TArray<FSeinEntityHandle> Entities;
};

/** One designer-facing named-entity binding. */
USTRUCT()
struct SEINARTSCOREENTITY_API FSeinSnapshotNamedEntity
{
	GENERATED_BODY()

	UPROPERTY()
	FName Name;

	UPROPERTY()
	FSeinEntityHandle Entity;
};

/** One cast vote in canonical player-ID order. */
USTRUCT()
struct SEINARTSCOREENTITY_API FSeinSnapshotVoteCast
{
	GENERATED_BODY()

	UPROPERTY()
	FSeinPlayerID Voter;

	UPROPERTY()
	int32 Value = 0;
};

/** One active vote, stored without unordered reflected maps. */
USTRUCT()
struct SEINARTSCOREENTITY_API FSeinSnapshotActiveVote
{
	GENERATED_BODY()

	UPROPERTY()
	FGameplayTag VoteType;

	UPROPERTY()
	TArray<FSeinSnapshotVoteCast> Votes;

	UPROPERTY()
	int32 RequiredThreshold = 1;

	UPROPERTY()
	ESeinVoteResolution Resolution = ESeinVoteResolution::Majority;

	UPROPERTY()
	int32 InitiatedAtTick = 0;

	UPROPERTY()
	int32 ExpiresAtTick = 0;

	UPROPERTY()
	FSeinPlayerID Initiator;
};

/**
 * Exact one-shot bootstrap identity carried by a resumable checkpoint.
 * Checkpoints are continuations of a match whose authorization was already
 * consumed; they never reopen or replay the tick-zero materialization phase.
 */
USTRUCT()
struct SEINARTSCOREENTITY_API FSeinSnapshotBootstrapCheckpoint
{
	GENERATED_BODY()

	static constexpr int32 CurrentFormatVersion = 1;

	UPROPERTY()
	int32 FormatVersion = CurrentFormatVersion;

	UPROPERTY()
	ESeinMatchBootstrapState BootstrapState =
		ESeinMatchBootstrapState::Awaiting;

	UPROPERTY()
	FSeinMatchBootstrapReceipt Receipt;

	UPROPERTY()
	FGuid AuthorizationContextDigest;

	/** Canonically ordered digests retained after native callbacks are frozen. */
	UPROPERTY()
	TArray<FSeinSnapshotInitialStateContribution> InitialStateContributions;

	/** Canonically ordered faction registry materialized by the bootstrap. */
	UPROPERTY()
	TArray<FSeinSnapshotFactionRegistration> FactionRegistrations;

	bool IsValidConsumedCheckpoint() const
	{
		return FormatVersion == CurrentFormatVersion
			&& BootstrapState == ESeinMatchBootstrapState::Consumed
			&& Receipt.IsValid()
			&& AuthorizationContextDigest.IsValid();
	}
};

/**
 * Top-level deterministic snapshot body. The local developer `.seinsnapshot`
 * command serializes this USTRUCT through an unbounded trusted-local archive.
 * Production disk/cloud persistence and network catch-up require their own
 * bounded, versioned, authenticated envelope before this body is decoded.
 */
USTRUCT()
struct SEINARTSCOREENTITY_API FSeinWorldSnapshot
{
	GENERATED_BODY()

	// v17: FSeinSquadComponent gained ReinforceRefundPolicy +
	// PartialRefundPercent (destruction settlement ruling) — component blob
	// layout change; older snapshots fail closed.
	static constexpr int32 CurrentVersion = 17;
	/** Defensive reconstruction bound for an imported checkpoint. The runtime
	 *  pool remains independently extensible; snapshots above this generous
	 *  simultaneous-entity ceiling fail before allocating slot-indexed state. */
	static constexpr int32 MaxSupportedEntitySlotIndex = 262144;
	/** Imported checkpoints are bounded independently from the live pools. */
	static constexpr int32 MaxSupportedObjectPoolSlots =
		MaxSupportedEntitySlotIndex * 16;
	static constexpr int32 MaxSupportedComponentStorageTypes = 4096;
	static constexpr int32 MaxSupportedPoolObjectStateBytes =
		16 * 1024 * 1024;
	static constexpr int32 MaxSupportedComponentBlobBytes =
		256 * 1024 * 1024;
	static constexpr int64 MaxSupportedAggregateOpaqueStateBytes =
		1024ll * 1024ll * 1024ll;

	// ========== Header ==========

	UPROPERTY()
	int32 SnapshotVersion = CurrentVersion;

	UPROPERTY()
	FString FrameworkVersion;

	UPROPERTY()
	FString GameVersion;

	UPROPERTY()
	FName MapIdentifier;

	UPROPERTY()
	FDateTime CapturedAt;

	/** Frozen command schema + authority-policy identity required to restore. */
	UPROPERTY()
	FGuid CommandProtocolDigest;

	/** Generated Blueprint/native authored-content identity required to restore. */
	UPROPERTY()
	FGuid SimulationContentDigest;

	/** Canonical digest of MatchSettings. Detects corruption and semantic drift. */
	UPROPERTY()
	FGuid MatchSettingsDigest;

	/** Core plus extension settings fingerprint required to restore. */
	UPROPERTY()
	int32 ConfigFingerprint = 0;

	/** Exact consumed tick-zero authorization this checkpoint continues. */
	UPROPERTY()
	FSeinSnapshotBootstrapCheckpoint BootstrapCheckpoint;

	// ========== Sim metadata ==========

	UPROPERTY()
	int32 CurrentTick = 0;

	UPROPERTY()
	int64 SessionSeed = 0;

	/** PRNG state (FFixedRandom is xorshift128+; State0 + State1 fully
	 *  determine the next roll). */
	UPROPERTY()
	int64 PRNGState0 = 0;

	UPROPERTY()
	int64 PRNGState1 = 0;

	/** Next world-global effect instance ID. Future allocation order is
	 *  authoritative state even when every previously allocated effect expired. */
	UPROPERTY()
	int64 NextEffectInstanceID = 1;

	/**
	 * Monotonic world-global identities. Zero is invalid and IDs are not reused.
	 * MAX_int64 is the valid exhausted cursor and is never issued as an ID.
	 */
	UPROPERTY()
	int64 NextLatentActionID = 1;

	UPROPERTY()
	int64 NextAbilityActivationID = 1;

	// ========== Match flow ==========

	UPROPERTY()
	FSeinMatchSettings MatchSettings;

	UPROPERTY()
	uint8 MatchState = 0; // ESeinMatchState as raw uint8

	UPROPERTY()
	int32 MatchStartTick = 0;

	UPROPERTY()
	int32 StartingStateDeadlineTick = 0;

	UPROPERTY()
	bool bSimPaused = false;

	UPROPERTY()
	bool bSimPausedHard = false;

	/** Monotonic identity of the current/most-recent frozen pause interval. */
	UPROPERTY()
	int64 PauseEpoch = 0;

	/** Tick at which PauseEpoch froze, or INDEX_NONE before the first pause. */
	UPROPERTY()
	int32 PauseFrozenTick = INDEX_NONE;

	/** Last atomically accepted frame in PauseEpoch; -1 means none accepted. */
	UPROPERTY()
	int64 LastAppliedPauseControlSequence = -1;

	/** Ordinary canonical commands waiting for the next advancing sim tick. */
	UPROPERTY()
	TArray<FSeinCommand> PendingCommands;

	/** Standalone frozen-control commands accepted but not yet framed/applied. */
	UPROPERTY()
	TArray<FSeinCommand> PendingStandalonePauseControlCommands;

	// ========== Player + entity state ==========

	UPROPERTY()
	TMap<FSeinPlayerID, FSeinPlayerState> PlayerStates;

	/** Authoritative directional player-pair capability grants. Effective
	 *  caches are rebuilt from this source-record list on restore. */
	UPROPERTY()
	TArray<FSeinPairCapabilityGrantRecord> PairCapabilityGrants;

	/** Per-entity tag state in canonical entity-handle order. */
	UPROPERTY()
	TArray<FSeinSnapshotEntityTagState> EntityTagStates;

	/** Canonical tag-key order; each bucket preserves gameplay-visible order. */
	UPROPERTY()
	TArray<FSeinSnapshotTagIndexBucket> EntityTagIndex;

	/** Named bindings in canonical name order. */
	UPROPERTY()
	TArray<FSeinSnapshotNamedEntity> NamedEntities;

	/** Active votes in canonical vote-tag order. */
	UPROPERTY()
	TArray<FSeinSnapshotActiveVote> ActiveVotes;

	/** Persistent native extension/framework state in canonical key order. */
	UPROPERTY()
	TArray<FSeinCanonicalStateContributorRecord>
		NativeCanonicalStateRecords;

	/** Core-owned Blueprint value slots, including their frozen schemas. */
	UPROPERTY()
	TArray<FSeinCanonicalStateValueRecord> CanonicalStateValueRecords;

	/** Exact slot generations, retirement state, capacity, and LIFO allocator. */
	UPROPERTY()
	FSeinEntityPoolExactState EntityPoolState;

	/** Alive roster plus render-actor class identity. Core slot data above is authoritative. */
	UPROPERTY()
	TArray<FSeinSnapshotEntityRecord> Entities;

	// ========== Ability + resolver pools ==========

	/** Full per-slot snapshot of `USeinWorldSubsystem::AbilityPool`. Live
	 *  entries bind a locally frozen provider and exact admitted class. */
	UPROPERTY()
	TArray<FSeinSnapshotPoolInstanceRecord> AbilityPoolRecords;

	/** Exact LIFO free-slot order; Last() is the next stable pool ID. */
	UPROPERTY()
	TArray<int32> AbilityPoolFreeList;

	// ========== Active latent continuations ==========

	/** Exact USeinLatentActionManager order; ordinal is explicit defense in depth. */
	UPROPERTY()
	TArray<FSeinSnapshotLatentActionRecord> LatentActionRecords;

	/** Binds both allocator cursors and every ordered RecordDigest. */
	UPROPERTY()
	FGuid LatentActionSequenceDigest;

	/** Same shape for `USeinWorldSubsystem::CommandBrokerResolverPool`. */
	UPROPERTY()
	TArray<FSeinSnapshotPoolInstanceRecord> ResolverPoolRecords;

	/** Exact LIFO free-slot order; Last() is the next stable pool ID. */
	UPROPERTY()
	TArray<int32> ResolverPoolFreeList;

	// ========== Component storages (serialized opaquely via per-storage hook) ==========

	/** One blob per UScriptStruct that has live components. Outer key is the
	 *  struct's package path (resolves via FindObject<UScriptStruct> on
	 *  restore). Value is a raw byte array produced by
	 *  `ISeinComponentStorage::SerializeFromArchive`. */
	UPROPERTY()
	TMap<FString, FSeinSnapshotComponentStorageBlob> ComponentStorageBlobs;

	// ========== Local-only render state (camera) ==========
	//
	// Per-PC camera state for save-game behavior. LOCAL-ONLY: in a multi-peer
	// resync (drop-in/drop-out catch-up), each peer keeps its own camera and
	// ignores this field. Populated by whatever local actor implements
	// ISeinSnapshotCameraProvider — typically the camera pawn, but designers
	// can opt in any pawn / view-target / PC. Pure POD struct so designers
	// see a tidy BP-friendly interface (not the giant snapshot struct).
	UPROPERTY()
	FSeinCameraSnapshotData CameraState;
};

/**
 * Keeps reflected references inside a stack-owned snapshot visible to Unreal
 * GC while a synchronous capture, archive, validation, or restore operation is
 * in flight. A raw FSeinWorldSnapshot retained across frames must be stored in
 * a reflected owner or accompanied by a guard with the same lifetime.
 *
 * Snapshot v12 retains reflected bootstrap/command/component values, so this
 * guard remains part of the synchronous C++ lifetime contract.
 */
class SEINARTSCOREENTITY_API FSeinWorldSnapshotReferenceGuard final
	: public FGCObject
{
public:
	explicit FSeinWorldSnapshotReferenceGuard(
		const FSeinWorldSnapshot& InSnapshot)
		: Snapshot(const_cast<FSeinWorldSnapshot&>(InSnapshot))
	{
	}

	virtual void AddReferencedObjects(
		FReferenceCollector& Collector) override
	{
		Collector.AddPropertyReferencesWithStructARO(
			FSeinWorldSnapshot::StaticStruct(), &Snapshot);
	}

	virtual FString GetReferencerName() const override
	{
		return TEXT("SeinWorldSnapshot");
	}

private:
	FSeinWorldSnapshot& Snapshot;
};
