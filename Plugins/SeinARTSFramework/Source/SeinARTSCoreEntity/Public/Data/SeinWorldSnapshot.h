/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinWorldSnapshot.h
 * @brief   Portable USeinWorldSubsystem state snapshot — used for save/load,
 *          drop-in/drop-out catch-up, and (future) state-dump on desync.
 *
 * Phase 4 architecture. Captures enough information to reconstruct sim
 * state at tick T on a fresh world: entity pool, component data, player
 * states, ability/resolver pools, PRNG state, match settings + flow state.
 *
 * The snapshot serializes via `FObjectAndNameAsStringProxyArchive` (same
 * as the replay writer) — TObjectPtr / FInstancedStruct / soft refs all
 * round-trip cleanly through the proxy archive. Component-storage raw
 * bytes need an explicit serialize hook on `ISeinComponentStorage` (added
 * in this phase).
 *
 * Design note: this is the SIM-side snapshot. Render-side actor positions,
 * particle effects, audio cues, etc. are NOT captured — they're rebuilt
 * by the actor bridge on restore as the sim repopulates its entity list
 * and fires visual events through the normal flow.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinEntityHandle.h"
#include "Core/SeinPlayerID.h"
#include "Core/SeinPlayerState.h"
#include "Data/SeinMatchSettings.h"
#include "Data/SeinCameraSnapshotData.h"
#include "Data/SeinSnapshotComponentStorageBlob.h"
#include "Types/Transform.h"
#include "SeinWorldSnapshot.generated.h"

/**
 * Per-pool-slot record for an ability or broker-resolver UObject instance.
 * Captures the class path + raw bytes of all UPROPERTY-tagged fields via
 * UObject reflection serialization, so on restore we can NewObject the right
 * subclass and replay every property (cooldowns, active flags, BP state, etc.).
 * Same-shape struct is reused for both pools — `PoolID` is the slot index in
 * `USeinWorldSubsystem::AbilityPool` or `CommandBrokerResolverPool`.
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

	/** Asset-resolvable class path (UClass::GetPathName). */
	UPROPERTY()
	FString ClassPath;

	/** Raw serialized UPROPERTY state — produced by `UObject::Serialize`
	 *  through an `FObjectAndNameAsStringProxyArchive`. Restored the same way. */
	UPROPERTY()
	TArray<uint8> StateBytes;
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

/**
 * Top-level snapshot blob. Serialized as a USTRUCT via
 * `FObjectAndNameAsStringProxyArchive`. Wire-compatible with disk save files
 * (`.seinsnapshot`) and the future drop-in/drop-out catch-up RPC payload.
 */
USTRUCT()
struct SEINARTSCOREENTITY_API FSeinWorldSnapshot
{
	GENERATED_BODY()

	// ========== Header ==========

	UPROPERTY()
	int32 SnapshotVersion = 1;

	UPROPERTY()
	FString FrameworkVersion;

	UPROPERTY()
	FString GameVersion;

	UPROPERTY()
	FName MapIdentifier;

	UPROPERTY()
	FDateTime CapturedAt;

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

	// ========== Match flow ==========

	UPROPERTY()
	FSeinMatchSettings MatchSettings;

	UPROPERTY()
	uint8 MatchState = 0; // ESeinMatchState as raw uint8

	UPROPERTY()
	int32 MatchStartTick = 0;

	UPROPERTY()
	int32 StartingStateDeadlineTick = 0;

	// ========== Player + entity state ==========

	UPROPERTY()
	TMap<FSeinPlayerID, FSeinPlayerState> PlayerStates;

	UPROPERTY()
	TArray<FSeinSnapshotEntityRecord> Entities;

	// ========== Ability + resolver pools ==========

	/** Full per-slot snapshot of `USeinWorldSubsystem::AbilityPool`. Each entry
	 *  has the class path + UObject-reflected state bytes; restore recreates
	 *  the instance via NewObject(class) + UObject::Serialize. Free slots are
	 *  written too so the free-list reconstructs exactly. */
	UPROPERTY()
	TArray<FSeinSnapshotPoolInstanceRecord> AbilityPoolRecords;

	/** Same shape for `USeinWorldSubsystem::CommandBrokerResolverPool`. */
	UPROPERTY()
	TArray<FSeinSnapshotPoolInstanceRecord> ResolverPoolRecords;

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
