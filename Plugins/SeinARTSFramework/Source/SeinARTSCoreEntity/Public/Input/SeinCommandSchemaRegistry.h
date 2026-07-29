/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCommandSchemaRegistry.h
 * @brief   Load-order-independent command schema and handler registration.
 *
 *          USeinWorldSubsystem validates every command against this registry,
 *          applies context and authority policy, then invokes the registered
 *          stateless handler CDO.
 */

#pragma once

#include "CoreMinimal.h"
#include "Input/SeinCommand.h"
#include "Templates/SharedPointer.h"
#include "Templates/SubclassOf.h"
#include "SeinCommandSchemaRegistry.generated.h"

class USeinWorldSubsystem;

/** The deterministic subject the active authority policy must authorize. */
UENUM(BlueprintType)
enum class ESeinCommandAuthorityScope : uint8
{
	PublicObserver,
	Self,
	Entity,
	EntitySet,
	MatchControl,
	DerivedSystem,
};

/** Exceptional match contexts in which a schema remains eligible to execute. */
UENUM(BlueprintType, meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class ESeinCommandExecutionAllowance : uint8
{
	None               = 0,
	Spectator          = 1 << 0,
	HardPause          = 1 << 1,
	Starting           = 1 << 2,
	FrozenPauseControl = 1 << 3,
};
ENUM_CLASS_FLAGS(ESeinCommandExecutionAllowance);

/** Exact cursor for the next canonical command frame accepted while sim time is frozen. */
USTRUCT(meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinPauseControlCursor
{
	GENERATED_BODY()

	/** Monotonic identity of the current/most-recent pause interval. Zero means never paused. */
	UPROPERTY()
	int64 PauseEpoch = 0;

	/** Zero-based frame sequence within PauseEpoch. */
	UPROPERTY()
	int64 Sequence = 0;

	/** Sim tick at which this pause interval froze. */
	UPROPERTY()
	int32 FrozenTick = INDEX_NONE;

	bool operator==(const FSeinPauseControlCursor& Other) const
	{
		return PauseEpoch == Other.PauseEpoch
			&& Sequence == Other.Sequence
			&& FrozenTick == Other.FrozenTick;
	}

	bool operator!=(const FSeinPauseControlCursor& Other) const
	{
		return !(*this == Other);
	}
};

/** Topology-neutral, atomically preflighted command frame processed outside ordinary sim ticks. */
USTRUCT(meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinPauseControlFrame
{
	GENERATED_BODY()

	UPROPERTY()
	FSeinPauseControlCursor Cursor;

	UPROPERTY()
	TArray<FSeinCommand> Commands;
};

/** Structural validation result. Gameplay/authority rejection is intentionally separate. */
UENUM(BlueprintType)
enum class ESeinCommandStructureResult : uint8
{
	Valid,
	InvalidCommandType,
	InvalidSchemaVersion,
	UnknownCommandType,
	UnsupportedSchemaVersion,
	UnexpectedPayload,
	MissingPayload,
	NonDeterministicPayload,
	WrongPayloadType,
	UnsupportedPayloadField,
	PayloadNameOutsideCatalog,
	PayloadTooLarge,
	EntityListTooLarge,
	TargeterPointsTooLarge,
};

struct FSeinCommandSchemaDescriptor;

/**
 * Stateless command implementation type.
 *
 * Registered handler classes are strategy types, not state containers. The
 * dispatcher invokes the class default object only; do not add mutable match,
 * player, entity, or tick state to handler UObject fields. Future-affecting state
 * belongs in USeinWorldSubsystem-owned deterministic storage so it can be hashed,
 * snapshotted, restored, reset, and replayed. `Const` also keeps Blueprint handler
 * graphs from treating the CDO as mutable per-world storage.
 *
 * Authority has already been resolved before ExecuteCommand is invoked. Handlers
 * must not recover topology or connection ownership from UWorld or net mode.
 */
UCLASS(Abstract, Blueprintable, Const)
class SEINARTSCOREENTITY_API USeinCommandHandler : public UObject
{
	GENERATED_BODY()

public:
	/** Frozen, globally unique schema ID authored on this handler CDO. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Command|Schema")
	FName StableSchemaId;

	/** Exact command type authored on this handler CDO. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Command|Schema")
	FGameplayTag CommandType;

	/** Positive wire version. Bump only with an explicit migration path. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Command|Schema",
		meta = (ClampMin = "1"))
	int32 SchemaVersion = 1;

	/**
	 * Positive behavior revision. Bump whenever deterministic handler or
	 * authority semantics change without changing the serialized payload shape.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Command|Schema",
		meta = (ClampMin = "1"))
	int32 ImplementationRevision = 1;

	/**
	 * Optional payload TYPE declaration. The authored value is ignored; only its
	 * exact UScriptStruct identifies the wire schema. Empty explicitly means that
	 * payloads are forbidden for this command.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Command|Schema",
		meta = (SeinDeterministicOnly))
	FInstancedStruct PayloadSchema;

	/**
	 * Concrete types permitted inside nested FInstancedStruct payload fields.
	 * Values are ignored; their exact types and recursive layouts are frozen into
	 * the command protocol. Empty means this schema permits no dynamic inner type.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Command|Schema",
		meta = (SeinDeterministicOnly))
	TArray<FInstancedStruct> DynamicPayloadSchemas;

	/**
	 * Raw FName identifiers permitted anywhere recursively in this command's
	 * payload. NAME_None is always permitted and need not be listed. Gameplay
	 * tags use their registered tag dictionary and are intentionally separate.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Command|Schema")
	TArray<FName> AllowedPayloadNames;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Command|Schema")
	ESeinCommandAuthorityScope AuthorityScope = ESeinCommandAuthorityScope::Entity;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Command|Schema",
		meta = (ClampMin = "0"))
	int32 MaxEntityListEntries = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Command|Schema",
		meta = (ClampMin = "0"))
	int32 MaxTargeterPoints = 0;

	/** Maximum deterministic canonical payload bytes accepted after deserialization. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Command|Schema",
		meta = (ClampMin = "0"))
	int32 MaxPayloadBytes = 4096;

	/** Maximum total elements across all nested payload containers. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Command|Schema",
		meta = (ClampMin = "0"))
	int32 MaxPayloadAggregateElements = 256;

	/** Context exceptions; absence means ordinary active-player/unpaused execution only. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Command|Schema",
		meta = (Bitmask, BitmaskEnum = "/Script/SeinARTSCoreEntity.ESeinCommandExecutionAllowance"))
	int32 AllowedExecutionContexts = 0;

	/** Execute a command after structural, context, and authority validation. */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Command")
	bool ExecuteCommand(
		USeinWorldSubsystem* World,
		const FSeinCommand& Command,
		FGameplayTag& OutRejectionReason) const;

	/** Build the immutable native descriptor from this class default object. */
	FSeinCommandSchemaDescriptor BuildSchemaDescriptor() const;
};

/** One immutable command wire schema and its stateless implementation type. */
struct SEINARTSCOREENTITY_API FSeinCommandSchemaDescriptor
{
	/** Frozen, globally unique content identifier. Never rename after shipping. */
	FName StableSchemaId;

	/** Exact command tag handled by this schema. Parent/descendant matching is not used. */
	FGameplayTag CommandType;

	/** Positive wire schema version. There is no nearest-version fallback. */
	int32 SchemaVersion = 1;

	/** Positive deterministic behavior revision included in compatibility. */
	int32 ImplementationRevision = 1;

	/** Exact required payload type. Null explicitly means this command permits no payload. */
	const UScriptStruct* PayloadStruct = nullptr;

	/** Exact concrete types permitted in nested FInstancedStruct fields. */
	TArray<const UScriptStruct*> DynamicPayloadStructs;

	/** Canonical frozen representatives permitted in raw FName payload fields. */
	TArray<FName> AllowedPayloadNames;

	/** Subject category consumed by the topology-neutral authority policy. */
	ESeinCommandAuthorityScope AuthorityScope = ESeinCommandAuthorityScope::Entity;

	/** Maximum common-envelope recipients accepted by structural validation. */
	int32 MaxEntityListEntries = 0;

	/** Maximum captured targeter points accepted by structural validation. */
	int32 MaxTargeterPoints = 0;

	/** Maximum deterministic canonical payload bytes accepted after deserialization. */
	int32 MaxPayloadBytes = 4096;

	/** Maximum total logical elements across recursively nested payload containers. */
	int32 MaxPayloadAggregateElements = 256;

	/** Bitmask of ESeinCommandExecutionAllowance context exceptions. */
	int32 AllowedExecutionContexts = 0;

	/** Concrete native or Blueprint stateless strategy class. */
	TSubclassOf<USeinCommandHandler> HandlerClass;
};

/**
 * Immutable registry view captured for one world/session.
 *
 * Exact lookup is O(1). The snapshot owns strong references to its payload and
 * handler types, so a later module reload or registry mutation cannot change
 * the schemas, manifest, digest, or UObject generations observed by the world.
 */
class SEINARTSCOREENTITY_API FSeinCommandSchemaSnapshot
{
public:
	FSeinCommandSchemaSnapshot() = default;

	bool IsValid() const { return Data.IsValid(); }
	int32 GetSchemaCount() const;

	/** Exact lookup by (CommandType, positive SchemaVersion). */
	bool FindSchema(
		FGameplayTag CommandType,
		int32 SchemaVersion,
		FSeinCommandSchemaDescriptor& OutDescriptor) const;

	/** Structural validation against only the schemas captured in this snapshot. */
	ESeinCommandStructureResult ValidateStructure(
		const FSeinCommand& Command,
		FSeinCommandSchemaDescriptor* OutDescriptor = nullptr) const;

	const FString& GetCanonicalManifest() const;
	FGuid GetCanonicalManifestDigest() const;

	/** Canonical global additions captured from project settings for this world. */
	TConstArrayView<const UScriptStruct*> GetAdditionalDynamicPayloadStructs() const;
	TConstArrayView<FName> GetAdditionalWireNames() const;

private:
	struct FData;
	TSharedPtr<const FData, ESPMode::ThreadSafe> Data;

	friend class FSeinCommandSchemaRegistry;
};

/**
 * Build the always-enforced compatibility identity for command admission.
 * The normalized per-author cap is included even when optional config-parity
 * checks are disabled, because peers with different caps cannot share a safe
 * reliable turn-submission contract.
 */
SEINARTSCOREENTITY_API FGuid SeinComputeCommandProtocolDigest(
	const FGuid& SchemaDigest,
	const FString& AuthorityPolicyPath,
	int32 AuthorityPolicyRevision,
	int32 MaxCommandsPerSubmission);

/**
 * Process-local ownership token for one registration claim.
 *
 * Exact duplicate registration creates another token without another manifest
 * entry. Therefore a newly loaded module can register before the old module shuts
 * down, and the old token cannot remove the replacement's identical schema.
 */
struct SEINARTSCOREENTITY_API FSeinCommandSchemaRegistrationHandle
{
public:
	bool IsValid() const { return Token != 0 && !OwnerId.IsNone(); }

private:
	void Reset() { Token = 0; OwnerId = NAME_None; }

	FName OwnerId;
	uint64 Token = 0;

	friend class FSeinCommandSchemaRegistry;
};

/**
 * Process-global command schema registration source. Registration lifecycle is
 * module startup/shutdown; active worlds retain an immutable captured snapshot.
 */
class SEINARTSCOREENTITY_API FSeinCommandSchemaRegistry
{
public:
	/**
	 * Register a schema owned by a frozen module/plugin ID. A conflicting exact
	 * key or StableSchemaId is rejected. An exact duplicate by the same owner is
	 * idempotent in the manifest but receives an independent unload-safe token.
	 */
	static FSeinCommandSchemaRegistrationHandle RegisterSchema(
		FName OwnerId,
		const FSeinCommandSchemaDescriptor& Descriptor);

	/** Build and register the schema authored by a native or Blueprint handler CDO. */
	static FSeinCommandSchemaRegistrationHandle RegisterHandlerClass(
		FName OwnerId,
		TSubclassOf<USeinCommandHandler> HandlerClass);

	/** Release only this claim. The schema disappears after its final token is released. */
	static bool UnregisterSchema(FSeinCommandSchemaRegistrationHandle& Handle);

	/** Exact lookup by (CommandType, positive SchemaVersion). */
	static bool FindSchema(
		FGameplayTag CommandType,
		int32 SchemaVersion,
		FSeinCommandSchemaDescriptor& OutDescriptor);

	/**
	 * Validate the envelope against an exact registered schema. This performs no
	 * ownership, match-state, affordability, target, or handler-specific checks.
	 */
	static ESeinCommandStructureResult ValidateStructure(
		const FSeinCommand& Command,
		FSeinCommandSchemaDescriptor* OutDescriptor = nullptr);

	/**
	 * Capture an immutable, strongly rooted per-world registry view. Worlds should
	 * retain this snapshot for the full session instead of consulting later
	 * process-global module registrations.
	 */
	static FSeinCommandSchemaSnapshot CaptureSnapshot(
		TConstArrayView<const UScriptStruct*> AdditionalDynamicPayloadStructs = {},
		TConstArrayView<FName> AdditionalWireNames = {});

	/** Canonical, length-framed descriptor manifest sorted by tag then version. */
	static FString BuildCanonicalManifest();

	/** BLAKE3-128 of the canonical UTF-8 manifest; independent of registration order. */
	static FGuid ComputeCanonicalManifestDigest();

	/** Number of unique schema entries (duplicate ownership tokens do not increase it). */
	static int32 GetRegisteredSchemaCount();

	/**
	 * True when the payload type carries the SeinDeterministic marker. Metadata is
	 * unavailable in cooked builds; there the trusted registered exact type is the
	 * contract, while mismatched payload types are still rejected by stable path.
	 */
	static bool IsDeterministicPayloadStruct(const UScriptStruct* PayloadStruct);
};

/**
 * Build the process/load-order-independent raw-FName catalog used by command,
 * replay, and metadata wires. Entries are case-normalized, numbered-name aware,
 * sorted, deduplicated, and NAME_None is omitted (wire index zero is reserved).
 */
SEINARTSCOREENTITY_API void SeinBuildCanonicalWireNameCatalog(
	TConstArrayView<FName> Names,
	TArray<FName>& OutCanonicalNames,
	FString& OutCanonicalManifest);

/** Validate a command directly against an already-frozen descriptor. */
SEINARTSCOREENTITY_API ESeinCommandStructureResult SeinValidateCommandAgainstSchema(
	const FSeinCommand& Command,
	const FSeinCommandSchemaDescriptor& Descriptor);

/** Rewrite accepted payload FNames to the descriptor's canonical representatives. */
SEINARTSCOREENTITY_API bool SeinCanonicalizeCommandPayloadNames(
	FSeinCommand& Command,
	const FSeinCommandSchemaDescriptor& Descriptor);
