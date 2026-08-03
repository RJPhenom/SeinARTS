/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCanonicalStateRegistry.h
 * @brief   Composable, reload-safe deterministic state contributor contract.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinEntityHandle.h"
#include "StructUtils/InstancedStruct.h"
#include "Templates/SharedPointer.h"
#include "SeinCanonicalStateRegistry.generated.h"

class USeinWorldSubsystem;
class USeinAbility;
class UClass;
class UScriptStruct;
class UObject;
class FSeinLatentActionCodecRegistry;

/** Stable two-part identity. Neither field may change after a game ships. */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinCanonicalStateKey
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|State")
	FName StableDomainId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|State")
	FName StableContributorId;

	bool IsValid() const
	{
		return !StableDomainId.IsNone() && !StableContributorId.IsNone();
	}

	bool operator==(const FSeinCanonicalStateKey& Other) const
	{
		return StableDomainId == Other.StableDomainId
			&& StableContributorId == Other.StableContributorId;
	}
};

FORCEINLINE uint32 GetTypeHash(const FSeinCanonicalStateKey& Key)
{
	return HashCombineFast(
		GetTypeHash(Key.StableDomainId),
		GetTypeHash(Key.StableContributorId));
}

/** Persistence behavior promised by one contributor. */
UENUM(BlueprintType)
enum class ESeinCanonicalStateRole : uint8
{
	/** Persistent future-affecting state included in checkpoint and peer roots. */
	Authoritative,

	/** Active deterministic execution such as an ability or latent continuation. */
	Continuation,

	/** No payload: synchronously rebuilt from authoritative state before use. */
	DerivedCache,
};

/** Defensive bounds applied to one canonical state value. */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinCanonicalStateLimits
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|State",
		meta = (ClampMin = "1"))
	int32 MaxRecursionDepth = 64;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|State",
		meta = (ClampMin = "1"))
	int32 MaxEncodedBytes = 16 * 1024 * 1024;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|State",
		meta = (ClampMin = "1"))
	int32 MaxAggregateElements = 1024 * 1024;
};

/**
 * Designer-authored contract for one Core-owned deterministic value slot.
 *
 * Value slots are passive authoritative data. They are restored as part of
 * Core before native contributor staging/commit begins; executable
 * continuation and cache reconstruction belong to module-owned contributors.
 */
USTRUCT(BlueprintType)
struct SEINARTSCOREENTITY_API FSeinCanonicalStateValueSlotDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|State")
	FSeinCanonicalStateKey Key;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|State",
		meta = (ClampMin = "1"))
	int32 SchemaVersion = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|State",
		meta = (ClampMin = "1"))
	int32 ImplementationRevision = 1;

	/** Concrete types permitted inside nested FInstancedStruct fields. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|State",
		meta = (SeinDeterministicOnly))
	TArray<FInstancedStruct> DynamicPayloadSchemas;

	/** Raw FName values permitted recursively in this slot's wire payload. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|State")
	TArray<FName> AllowedNames;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|State")
	FSeinCanonicalStateLimits Limits;
};

/** Canonical wire payload for one passive authoritative state value slot. */
USTRUCT()
struct SEINARTSCOREENTITY_API FSeinCanonicalStateValueRecord
{
	GENERATED_BODY()

	UPROPERTY()
	FSeinCanonicalStateKey Key;

	UPROPERTY()
	int32 SchemaVersion = 0;

	UPROPERTY()
	int32 ImplementationRevision = 0;

	/**
	 * Exact reflected root type asserted against the locally declared slot.
	 * Imported records carry this as evidence only; they never choose or load
	 * the runtime schema used to decode the payload.
	 */
	UPROPERTY()
	FString PayloadStructPath;

	/** Canonically sorted concrete types permitted in nested FInstancedStructs. */
	UPROPERTY()
	TArray<FString> DynamicPayloadStructPaths;

	/** Canonically sorted raw names permitted by the bounded payload codec. */
	UPROPERTY()
	TArray<FName> AllowedNames;

	UPROPERTY()
	FSeinCanonicalStateLimits Limits;

	UPROPERTY()
	FGuid DescriptorDigest;

	UPROPERTY()
	TArray<uint8> PayloadBytes;

	UPROPERTY()
	FGuid LeafDigest;
};

/** Canonical payload emitted by one native module contributor. */
USTRUCT()
struct SEINARTSCOREENTITY_API FSeinCanonicalStateContributorRecord
{
	GENERATED_BODY()

	UPROPERTY()
	FSeinCanonicalStateKey Key;

	UPROPERTY()
	int32 SchemaVersion = 0;

	UPROPERTY()
	FGuid DescriptorDigest;

	UPROPERTY()
	TArray<uint8> PayloadBytes;

	UPROPERTY()
	FGuid LeafDigest;
};

/** Digest-only live projection used by routine peer checks. A provider may
 * maintain this incrementally; snapshot payloads remain independently exact
 * and reversible. MutationRevision is cache evidence only and never enters the
 * root. */
struct SEINARTSCOREENTITY_API FSeinCanonicalStateRoutineRootRecord
{
	FSeinCanonicalStateKey Key;
	uint32 SchemaVersion = 0;
	FGuid DescriptorDigest;
	uint64 MutationRevision = 0;
	uint64 ProjectedPayloadBytes = 0;
	FGuid LeafDigest;
};

/**
 * Frozen schema and ordering contract. PayloadStruct is null only for a
 * DerivedCache. RestoreAfter forms a dependency DAG within the full manifest.
 */
struct SEINARTSCOREENTITY_API FSeinCanonicalStateDescriptor
{
	FSeinCanonicalStateKey Key;
	uint32 SchemaVersion = 0;
	uint32 ImplementationRevision = 0;
	ESeinCanonicalStateRole Role = ESeinCanonicalStateRole::Authoritative;
	const UScriptStruct* PayloadStruct = nullptr;
	TArray<const UScriptStruct*> DynamicPayloadStructs;
	TArray<FName> AllowedNames;
	FSeinCanonicalStateLimits Limits;
	TArray<FSeinCanonicalStateKey> RestoreAfter;

	/**
	 * Declares that a deterministic service outside the registered-system list
	 * (a subsystem, codec manifest, or conditionally-enabled module) owns this
	 * contributor's lifecycle. Execution-topology freeze rejects any frozen
	 * contributor that is neither externally owned nor named by at least one
	 * registered system's coverage claim, so orphaned captured state is a
	 * bootstrap error instead of a silent accident. Folded into the descriptor
	 * digest: ownership semantics are part of the exact contract.
	 */
	bool bExternallyOwned = false;

	/**
	 * Narrow external-ownership exemption for a contributor normally claimed
	 * by a conditionally registered simulation system. When no live system
	 * claims it, the provider must prove through QueryWorldInactive that its
	 * owning feature is explicitly disabled in this world. This prevents an
	 * enabled subsystem from silently losing its system/state ownership edge.
	 * Requires bExternallyOwned; folded into the descriptor digest.
	 */
	bool bExternalOwnershipOnlyWhenWorldInactive = false;
};

/** Read-only capture input supplied only on the game thread. */
struct SEINARTSCOREENTITY_API FSeinCanonicalStateCaptureContext
{
	const USeinWorldSubsystem& World;
	int32 Tick = 0;
};

/**
 * Whether a world-binding capture is the committing tick-zero declaration or
 * a provisional declaration used to validate an imported checkpoint.
 *
 * Restore must remain transactional: a rejected checkpoint may not seal
 * provider-local world state. Providers may commit a binding only for
 * BootstrapCommit; restore candidates are adopted later by CommitRestore.
 */
enum class ESeinCanonicalStateWorldBindingDisposition : uint8
{
	Provisional,
	BootstrapCommit,
};

/**
 * One-world compatibility binding input.
 *
 * This callback may freeze provider-local generation leases and inspect
 * immutable services/static environment. It must not read or mutate live
 * authoritative simulation state. BindingDisposition explicitly distinguishes
 * the normal bootstrap commit from a retryable restore declaration.
 */
struct SEINARTSCOREENTITY_API FSeinCanonicalStateWorldBindingContext
{
	/** Const on purpose: preparation providers receive the world lookup seam
	 *  needed to resolve their own static-environment subsystem, but cannot
	 *  directly borrow Core's mutable entity/component services. Native
	 *  providers remain trusted code and must not cast this contract away. */
	const USeinWorldSubsystem& Services;
	ESeinCanonicalStateWorldBindingDisposition BindingDisposition =
		ESeinCanonicalStateWorldBindingDisposition::Provisional;
};

/**
 * Read-only view of the fully staged Core candidate. It never aliases the
 * abandoned live timeline. Module contributors use it to reject invalid
 * handles, component references, pool IDs, classes, or Blueprint state-slot
 * references before the restore transaction becomes infallible.
 *
 * Returned pointers remain valid only for the StageRestore/StageDerived call.
 * A contributor must copy any required information into its own restore stage.
 */
class SEINARTSCOREENTITY_API ISeinCanonicalStateCandidateView
{
public:
	virtual ~ISeinCanonicalStateCandidateView() = default;

	virtual bool IsEntityValid(FSeinEntityHandle Handle) const = 0;
	virtual const void* FindComponentRaw(
		FSeinEntityHandle Handle,
		const UScriptStruct* ComponentType) const = 0;
	virtual const UClass* FindEntityActorClass(
		FSeinEntityHandle Handle) const = 0;
	virtual const UClass* FindAbilityClass(int32 PoolID) const = 0;
	virtual const USeinAbility* FindAbility(int32 PoolID) const = 0;
	virtual const UClass* FindCommandBrokerResolverClass(
		int32 PoolID) const = 0;
	virtual bool GetCanonicalStateValue(
		const FSeinCanonicalStateKey& Key,
		FInstancedStruct& OutValue) const = 0;

	bool HasComponent(
		FSeinEntityHandle Handle,
		const UScriptStruct* ComponentType) const
	{
		return FindComponentRaw(Handle, ComponentType) != nullptr;
	}

	template<typename ComponentType>
	const ComponentType* FindComponent(FSeinEntityHandle Handle) const
	{
		return static_cast<const ComponentType*>(
			FindComponentRaw(Handle, ComponentType::StaticStruct()));
	}
};

/**
 * Read-only payloads from explicit RestoreAfter dependencies whose staging has
 * already succeeded. This lets one plugin validate or prebuild against
 * another plugin's canonical candidate without touching live state or sharing
 * private restore-stage implementations.
 *
 * Only persistent Authoritative/Continuation payloads are exposed. A
 * DerivedCache dependency contributes ordering only because it has no
 * canonical payload; derived caches should publish reusable state through an
 * authoritative/continuation contributor when another plugin must consume it.
 */
class SEINARTSCOREENTITY_API ISeinCanonicalStateStagedPayloadView
{
public:
	virtual ~ISeinCanonicalStateStagedPayloadView() = default;

	/**
	 * Copy one explicitly declared dependency payload. Returns false for an
	 * undeclared, not-yet-staged, derived, or unknown contributor.
	 */
	/**
	 * Borrow one explicitly declared dependency payload for this callback only.
	 * The pointer must never be retained after StageRestore/StageDerived returns.
	 */
	virtual const FInstancedStruct* FindStagedPayload(
		const FSeinCanonicalStateKey& Key) const = 0;

	bool GetStagedPayload(
		const FSeinCanonicalStateKey& Key,
		FInstancedStruct& OutPayload) const
	{
		OutPayload.Reset();
		const FInstancedStruct* Found = FindStagedPayload(Key);
		if (!Found)
		{
			return false;
		}
		OutPayload = *Found;
		return true;
	}
};

/**
 * Restore metadata available while the transaction remains fallible. The
 * candidate view is null only for isolated registry tests; production world
 * restoration always supplies the fully staged Core candidate.
 *
 * Cross-contributor order is expressed by RestoreAfter and the hard role
 * barriers Authoritative -> Continuation -> DerivedCache. Commit callbacks
 * see the adopted live world and must remain infallible. They must not
 * register, unregister, or reload canonical-state providers while the commit
 * sequence is in progress. Candidate and Dependencies are borrowed for the
 * current stage callback only and must never be retained.
 */
struct SEINARTSCOREENTITY_API FSeinCanonicalStateStageContext
{
	int32 Tick = 0;
	const ISeinCanonicalStateCandidateView* Candidate = nullptr;
	const ISeinCanonicalStateStagedPayloadView* Dependencies = nullptr;

	/**
	 * Pre-adoption world services and object-outer access. This is deliberately
	 * not the candidate simulation: authoritative/component/entity reads must
	 * use Candidate so abandoned live state never contaminates staging.
	 */
	const USeinWorldSubsystem* Services = nullptr;
};

/**
 * Private live-world commit input. Commit callbacks must be infallible swaps
 * and must not mutate Core simulation state or canonical-state provider
 * registration. The Core world is const so contributors can resolve their own
 * subsystem without directly borrowing mutable entity/component services.
 */
struct SEINARTSCOREENTITY_API FSeinCanonicalStateCommitContext
{
	const USeinWorldSubsystem& World;
	int32 Tick = 0;
};

/** Type-erased contributor-owned staging result. It never aliases live state. */
struct SEINARTSCOREENTITY_API ISeinCanonicalStateRestoreStage
{
	virtual ~ISeinCanonicalStateRestoreStage() = default;

	/**
	 * Enumerate every UObject reachable only through this stage.
	 *
	 * Core invokes this exactly once on the game thread, immediately after the
	 * stage callback succeeds; GC never invokes a contributor vtable. The
	 * contributor must keep each object alive through the return of this
	 * callback. Core then validates and retains each object with a strong
	 * pointer until commit or discard.
	 */
	virtual void GatherReferencedObjects(TArray<UObject*>&) const {}

	/**
	 * Revalidate contributor-private nested generation leases immediately
	 * before Core adopts any staged state. The default has no nested lease.
	 */
	virtual bool VerifyExternalLeases(FString&) const { return true; }
};

/**
 * Native executable half of a contributor. A frozen world stores only a claim
 * token and resolves these callbacks on the invoking stack. It never retains
 * unloadable module code outside the synchronous capture/restore transaction.
 *
 * The registration-handle owner must outlive every synchronous callback and
 * restore stage. The registry does not trace UObject pointers hidden inside
 * TFunction captures; such captures require explicitly rooted ownership for
 * the full registration lifetime.
 */
struct SEINARTSCOREENTITY_API FSeinCanonicalStateContributorOps
{
	/**
	 * Required only by descriptors whose external-ownership exemption is
	 * conditional. Return true when the query itself succeeded and set
	 * bOutInactive only when the owning feature is explicitly disabled for
	 * this world. The callback is a read-only bootstrap query.
	 */
	TFunction<bool(
		const FSeinCanonicalStateWorldBindingContext&,
		bool& /*bOutInactive*/,
		FString&)> QueryWorldInactive;

	/**
	 * Prepare provider-local immutable world inputs before any compatibility
	 * frame is inspected. This may synchronously load local baked/static data,
	 * but must not consume imported checkpoint values or mutate authoritative
	 * simulation state. Optional when the provider needs no world preparation.
	 */
	TFunction<bool(
		const FSeinCanonicalStateWorldBindingContext&,
		FString&)> PrepareWorldBinding;

	/**
	 * Freeze one provider-specific world compatibility frame. The registry
	 * binds the result to the contributor key before folding it into the
	 * match StateContract. Optional when the descriptor is world-invariant.
	 */
	TFunction<bool(
		const FSeinCanonicalStateWorldBindingContext&,
		FString&,
		FString&)> FreezeWorldBinding;

	TFunction<bool(
		const FSeinCanonicalStateCaptureContext&,
		FInstancedStruct&,
		FString&)> Capture;

	/** Optional high-frequency live-root projection. Providers with large or
	 * frequently changing payloads should maintain indexed leaf digests and
	 * make this O(changes). When absent, Core falls back to exact Capture for
	 * correctness; profiling exposes that fallback so it cannot hide. */
	TFunction<bool(
		const FSeinCanonicalStateCaptureContext&,
		bool /*bForceFullRebuild*/,
		FSeinCanonicalStateRoutineRootRecord&,
		FString&)> CaptureRoutineRoot;

	TFunction<bool(
		const FSeinCanonicalStateStageContext&,
		const FInstancedStruct&,
		TUniquePtr<ISeinCanonicalStateRestoreStage>&,
		FString&)> StageRestore;

	TFunction<void(
		FSeinCanonicalStateCommitContext&,
		TUniquePtr<ISeinCanonicalStateRestoreStage>&&)> CommitRestore;

	TFunction<bool(
		const FSeinCanonicalStateStageContext&,
		TUniquePtr<ISeinCanonicalStateRestoreStage>&,
		FString&)> StageDerived;

	TFunction<void(
		FSeinCanonicalStateCommitContext&,
		TUniquePtr<ISeinCanonicalStateRestoreStage>&&)> CommitDerived;
};

/**
 * Core-owned fully staged native restore.
 *
 * Construction and transaction driving are private so an ordinary plugin
 * consumer cannot retain module-owned callbacks or restore-stage vtables
 * beyond the synchronous USeinWorldSubsystem restore stack.
 */
class SEINARTSCOREENTITY_API FSeinCanonicalStateRestorePlan
{
public:
	~FSeinCanonicalStateRestorePlan();

private:
	FSeinCanonicalStateRestorePlan();

	FSeinCanonicalStateRestorePlan(
		const FSeinCanonicalStateRestorePlan&) = delete;
	FSeinCanonicalStateRestorePlan& operator=(
		const FSeinCanonicalStateRestorePlan&) = delete;

	FSeinCanonicalStateRestorePlan(
		FSeinCanonicalStateRestorePlan&&) noexcept;
	FSeinCanonicalStateRestorePlan& operator=(
		FSeinCanonicalStateRestorePlan&&) noexcept;

	bool IsReady() const;
	void Reset();

	/** Revalidate every exact provider generation before world adoption begins. */
	bool VerifyProviderLeases(FString& OutError) const;

	/** Copy one already-staged persistent payload for latent codec validation. */
	const FInstancedStruct* FindStagedPayload(
		const FSeinCanonicalStateKey& Key) const;

	/** Infallible contributor swaps/rebuilds in frozen dependency order. */
	void Commit(FSeinCanonicalStateCommitContext& Context);

	struct FData;
	TUniquePtr<FData> Data;
	friend class FSeinCanonicalStateRegistry;
	friend class FSeinLatentActionCodecRegistry;
	friend class USeinWorldSubsystem;
};

/**
 * Move-only game-thread module claim.
 * Destruction unregisters only this generation.
 */
class SEINARTSCOREENTITY_API FSeinCanonicalStateRegistrationHandle
{
public:
	FSeinCanonicalStateRegistrationHandle() = default;
	~FSeinCanonicalStateRegistrationHandle();

	FSeinCanonicalStateRegistrationHandle(
		const FSeinCanonicalStateRegistrationHandle&) = delete;
	FSeinCanonicalStateRegistrationHandle& operator=(
		const FSeinCanonicalStateRegistrationHandle&) = delete;

	FSeinCanonicalStateRegistrationHandle(
		FSeinCanonicalStateRegistrationHandle&& Other) noexcept;
	FSeinCanonicalStateRegistrationHandle& operator=(
		FSeinCanonicalStateRegistrationHandle&& Other) noexcept;

	bool IsValid() const { return Token != 0; }
	void Reset();

private:
	explicit FSeinCanonicalStateRegistrationHandle(uint64 InToken)
		: Token(InToken)
	{
	}

	uint64 Token = 0;
	friend class FSeinCanonicalStateRegistry;
};

/** One frozen descriptor and the exact provider generation selected by a world. */
struct SEINARTSCOREENTITY_API FSeinFrozenCanonicalStateContributor
{
	FSeinCanonicalStateDescriptor Descriptor;
	FGuid DescriptorDigest;
	uint64 ProviderToken = 0;
};

/**
 * Immutable per-world schema view. Descriptor types are strongly rooted.
 * Callback code is deliberately absent; ProviderToken is resolved per call.
 */
class SEINARTSCOREENTITY_API FSeinCanonicalStateSchemaSnapshot
{
public:
	bool IsValid() const { return Data.IsValid(); }
	int32 GetContributorCount() const;
	const FString& GetCanonicalManifest() const;
	FGuid GetContractDigest() const;
	TConstArrayView<FSeinFrozenCanonicalStateContributor> GetContributors() const;

private:
	struct FData;
	TSharedPtr<const FData, ESPMode::ThreadSafe> Data;
	friend class FSeinCanonicalStateRegistry;
};

/** Process registry and canonical manifest builder for native contributors. */
class SEINARTSCOREENTITY_API FSeinCanonicalStateRegistry
{
public:
	/**
	 * Register one module-owned provider generation. Exact duplicate descriptors
	 * from the same owner coexist for hot reload; conflicts fail closed.
	 * Game-thread only. The handle and every explicitly rooted UObject captured
	 * by Ops must outlive all synchronous capture/restore calls. Provider
	 * registration is rejected while any provider callback transaction is active.
	 */
	static FSeinCanonicalStateRegistrationHandle Register(
		FName OwnerModuleId,
		const FSeinCanonicalStateDescriptor& Descriptor,
		FSeinCanonicalStateContributorOps Ops,
		FString* OutError = nullptr);

	/** Unregister one provider generation. Game-thread only. */
	static bool Unregister(FSeinCanonicalStateRegistrationHandle& Handle);

	/**
	 * Freeze descriptors and select the newest exact provider claim per key.
	 * Registration, unregistration, and schema freezing are game-thread only.
	 */
	static FSeinCanonicalStateSchemaSnapshot CaptureSchemaSnapshot(
		FString* OutError = nullptr);

	/**
	 * Prepare provider-local immutable world inputs in canonical contributor
	 * order before any provider world-binding frame is inspected.
	 */
	static bool PrepareWorldBindings(
		const FSeinCanonicalStateSchemaSnapshot& Schema,
		const FSeinCanonicalStateWorldBindingContext& Context,
		FString& OutError);

	/**
	 * Freeze provider-specific world identities in canonical contributor
	 * order. Exact implementation/static-environment bindings belong here,
	 * not in checkpoint-supplied schema.
	 */
	static bool CaptureWorldBindingFrames(
		const FSeinCanonicalStateSchemaSnapshot& Schema,
		const FSeinCanonicalStateWorldBindingContext& Context,
		TArray<FString>& OutFrames,
		FString& OutError);

	/**
	 * Close the reverse ownership edge for the current world. Ordinary
	 * contributors must be system-claimed, unconditional external services
	 * use bExternallyOwned, and conditional-system contributors may use their
	 * exemption only when QueryWorldInactive proves the feature is disabled.
	 */
	static bool ValidateWorldOwnershipClaims(
		const FSeinCanonicalStateSchemaSnapshot& Schema,
		const FSeinCanonicalStateWorldBindingContext& Context,
		TConstArrayView<FString> SystemClaimedCanonicalKeys,
		FString& OutError);

	/** Capture all persistent native contributors into canonical key order. */
	static bool CaptureContributorRecords(
		const FSeinCanonicalStateSchemaSnapshot& Schema,
		const FSeinCanonicalStateCaptureContext& Context,
		TArray<FSeinCanonicalStateContributorRecord>& OutRecords,
		FString& OutError);

	/** Capture digest-only routine roots in canonical key order. Custom
	 * incremental projections are preferred; missing projections fall back to
	 * the exact reversible capture and report the fallback count. */
	static bool CaptureRoutineRootRecords(
		const FSeinCanonicalStateSchemaSnapshot& Schema,
		const FSeinCanonicalStateCaptureContext& Context,
		bool bForceFullRebuild,
		TArray<FSeinCanonicalStateRoutineRootRecord>& OutRecords,
		int32& OutSynchronousFallbackCount,
		FString& OutError);

	static int32 GetRegisteredContributorCount();

	/** ASCII-lowercase dotted identity used for comparisons and manifests. */
	static FString CanonicalKey(const FSeinCanonicalStateKey& Key);

	/** Validate and frame one descriptor for a world-local Blueprint value slot. */
	static bool BuildDescriptorIdentity(
		const FSeinCanonicalStateDescriptor& Descriptor,
		FString& OutCanonicalDescriptor,
		FGuid& OutDescriptorDigest,
		FString& OutError);

	/** Compose native and world-local descriptor frames into one contract root. */
	static bool BuildCombinedContractIdentity(
		const FSeinCanonicalStateSchemaSnapshot& NativeSnapshot,
		TConstArrayView<FString> AdditionalCanonicalDescriptors,
		FString& OutCanonicalManifest,
		FGuid& OutContractDigest,
		FString& OutError);

	/**
	 * Validate one combined descriptor DAG and return its stable restore order.
	 * Role boundaries are implicit dependencies: every Authoritative
	 * contributor precedes every Continuation, which precedes every
	 * DerivedCache. An explicit dependency may not point backward across them.
	 */
	static bool BuildCanonicalRestoreOrder(
		TConstArrayView<const FSeinCanonicalStateDescriptor*> Descriptors,
		TArray<int32>& OutOrder,
		FString& OutError);

private:
	/**
	 * Resolve one frozen provider only while Core owns the invoking
	 * game-thread stack. Provider callbacks must never escape to consumers.
	 */
	static bool ResolveProvider(
		uint64 ProviderToken,
		FSeinCanonicalStateContributorOps& OutOps,
		FString* OutError = nullptr);

	/**
	 * Decode and stage every persistent contributor plus every derived cache.
	 * Only the world restore transaction may retain the resulting plan, and
	 * only until it commits or unwinds on the same game-thread stack.
	 */
	static bool TryStageContributorRestore(
		const FSeinCanonicalStateSchemaSnapshot& Schema,
		const FSeinCanonicalStateStageContext& Context,
		TConstArrayView<FSeinCanonicalStateContributorRecord> Records,
		FSeinCanonicalStateRestorePlan& OutPlan,
		FString& OutError);

	static bool UnregisterToken(uint64 Token);
	friend class FSeinCanonicalStateRegistrationHandle;
	friend class USeinWorldSubsystem;
};
