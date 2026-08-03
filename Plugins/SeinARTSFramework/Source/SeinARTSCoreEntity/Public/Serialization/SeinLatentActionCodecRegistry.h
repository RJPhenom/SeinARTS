/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLatentActionCodecRegistry.h
 * @brief   Exact, reload-safe checkpoint codecs for latent continuations.
 */

#pragma once

#include "CoreMinimal.h"
#include "Serialization/SeinCanonicalStateRegistry.h"
#include "StructUtils/InstancedStruct.h"
#include "Templates/SharedPointer.h"

class UClass;
class USeinAbility;
class USeinLatentAction;
class USeinLatentActionManager;
class USeinWorldSubsystem;
class UScriptStruct;
struct FSeinSnapshotLatentActionRecord;

/**
 * Frozen compatibility claim for one exact latent-action class.
 *
 * There is intentionally no reflected fallback. A native or Blueprint action
 * subclass with different future behavior registers its own exact codec.
 */
struct SEINARTSCOREENTITY_API FSeinLatentActionCodecDescriptor
{
	const UClass* SupportedClass = nullptr;
	FString StableCodecId;
	uint32 StateSchemaVersion = 0;
	uint32 BehaviorRevision = 0;
	uint32 CodecRevision = 0;
	const UScriptStruct* PayloadStruct = nullptr;
	FGuid PayloadSchemaDigest;
	TArray<const UScriptStruct*> DynamicPayloadStructs;
	TArray<FName> AllowedNames;
	FSeinCanonicalStateLimits Limits;

	/**
	 * Native contributors whose already-staged payloads this codec may inspect.
	 * Keys are canonicalized and become part of the match state contract.
	 */
	TArray<FSeinCanonicalStateKey> RequiredNativeContributors;
};

struct SEINARTSCOREENTITY_API FSeinLatentActionCaptureContext
{
	const USeinWorldSubsystem& World;
	const USeinLatentAction& Action;
	int32 Tick = 0;
	int32 AbilityPoolId = INDEX_NONE;
	int64 AbilityActivationId = 0;
};

/**
 * Fallible data-only restore input. Candidate contains the staged Core world;
 * Dependencies exposes only RequiredNativeContributors. Codec staging must not
 * create proxy/action UObjects or retain either borrowed view.
 */
struct SEINARTSCOREENTITY_API FSeinLatentActionStageContext
{
	int32 Tick = 0;
	const ISeinCanonicalStateCandidateView* Candidate = nullptr;
	const ISeinCanonicalStateStagedPayloadView* Dependencies = nullptr;
	const USeinWorldSubsystem* Services = nullptr;
	const FSeinSnapshotLatentActionRecord* Record = nullptr;
};

struct SEINARTSCOREENTITY_API FSeinLatentActionCommitContext
{
	USeinWorldSubsystem& World;
	USeinAbility& OwningAbility;
	int32 Tick = 0;
	int64 ActionId = 0;
	int64 AbilityActivationId = 0;
};

/** Codec-owned, data-only candidate retained until authoritative commit. */
struct SEINARTSCOREENTITY_API ISeinLatentActionRestoreStage
{
	virtual ~ISeinLatentActionRestoreStage() = default;

	/** Revalidate any codec-private nested generation immediately pre-commit. */
	virtual bool VerifyExternalLeases(FString&) const { return true; }
};

struct SEINARTSCOREENTITY_API FSeinLatentActionCodecOps
{
	TFunction<bool(
		const FSeinLatentActionCaptureContext&,
		FInstancedStruct&,
		FString&)> Capture;

	TFunction<bool(
		const FSeinLatentActionStageContext&,
		const FInstancedStruct&,
		TUniquePtr<ISeinLatentActionRestoreStage>&,
		FString&)> StageRestore;

	/**
	 * Infallible materialization after Core and every native contributor have
	 * committed. The returned action is adopted in checkpoint ordinal order;
	 * RegisterAction is deliberately bypassed.
	 */
	TFunction<USeinLatentAction*(
		FSeinLatentActionCommitContext&,
		TUniquePtr<ISeinLatentActionRestoreStage>&&)> CommitRestore;
};

/** Move-only claim; destruction withdraws only this hot-reload generation. */
class SEINARTSCOREENTITY_API FSeinLatentActionCodecRegistrationHandle
{
public:
	FSeinLatentActionCodecRegistrationHandle() = default;
	~FSeinLatentActionCodecRegistrationHandle();

	FSeinLatentActionCodecRegistrationHandle(
		const FSeinLatentActionCodecRegistrationHandle&) = delete;
	FSeinLatentActionCodecRegistrationHandle& operator=(
		const FSeinLatentActionCodecRegistrationHandle&) = delete;

	FSeinLatentActionCodecRegistrationHandle(
		FSeinLatentActionCodecRegistrationHandle&& Other) noexcept;
	FSeinLatentActionCodecRegistrationHandle& operator=(
		FSeinLatentActionCodecRegistrationHandle&& Other) noexcept;

	bool IsValid() const { return Token != 0; }
	void Reset();

private:
	explicit FSeinLatentActionCodecRegistrationHandle(uint64 InToken)
		: Token(InToken)
	{
	}

	uint64 Token = 0;
	friend class FSeinLatentActionCodecRegistry;
};

/** Immutable per-world codec catalog; reflected types remain strongly rooted. */
class SEINARTSCOREENTITY_API FSeinLatentActionCodecManifest
{
public:
	bool IsValid() const { return Data.IsValid(); }
	int32 Num() const;
	const FString& GetCanonicalManifest() const;
	FGuid GetDigest() const;

private:
	struct FData;
	TSharedPtr<const FData, ESPMode::ThreadSafe> Data;

	friend class FSeinLatentActionCodecRegistry;
};

/**
 * Fully staged latent restore. Only the world restore transaction may retain
 * this plan, and only on the game thread until commit or unwind.
 */
class SEINARTSCOREENTITY_API FSeinLatentActionRestorePlan
{
public:
	FSeinLatentActionRestorePlan();
	~FSeinLatentActionRestorePlan();

	FSeinLatentActionRestorePlan(const FSeinLatentActionRestorePlan&) = delete;
	FSeinLatentActionRestorePlan& operator=(
		const FSeinLatentActionRestorePlan&) = delete;
	FSeinLatentActionRestorePlan(
		FSeinLatentActionRestorePlan&&) noexcept;
	FSeinLatentActionRestorePlan& operator=(
		FSeinLatentActionRestorePlan&&) noexcept;

private:
	struct FData;
	TUniquePtr<FData> Data;

	bool IsReady() const;
	void Reset();
	bool VerifyProviderLeases(FString& OutError) const;
	void Commit(
		USeinWorldSubsystem& World,
		USeinLatentActionManager& Manager,
		int32 Tick);

	friend class FSeinLatentActionCodecRegistry;
	friend class USeinWorldSubsystem;
};

/** Process registry plus the shared snapshot/root/hash record encoder. */
class SEINARTSCOREENTITY_API FSeinLatentActionCodecRegistry
{
public:
	static constexpr int32 MaxActiveActions = 262144;
	static constexpr int32 MaxAggregatePayloadBytes = 64 * 1024 * 1024;
	static constexpr int32 MaxPayloadBytes = 16 * 1024 * 1024;
	static constexpr int32 MaxReloadClaimsPerClass = 64;

	static FSeinLatentActionCodecRegistrationHandle Register(
		FName OwnerModuleId,
		const FSeinLatentActionCodecDescriptor& Descriptor,
		FSeinLatentActionCodecOps Ops,
		FString* OutError = nullptr);

	static bool Unregister(
		FSeinLatentActionCodecRegistrationHandle& Handle);

	/**
	 * Freeze the newest exact generation per class and validate every declared
	 * native dependency against this world's already-frozen native schema.
	 */
	static FSeinLatentActionCodecManifest CaptureManifest(
		const FSeinCanonicalStateSchemaSnapshot& NativeSchema,
		FString* OutError = nullptr);

	/**
	 * Shared exact encoder used by snapshot capture, the canonical root, and
	 * the legacy StateHash projection. Empty active state still binds allocator
	 * cursors through OutSequenceDigest.
	 */
	static bool CaptureRecords(
		const FSeinLatentActionCodecManifest& Manifest,
		const USeinWorldSubsystem& World,
		const USeinLatentActionManager* Manager,
		int32 Tick,
		int64 NextActionId,
		int64 NextAbilityActivationId,
		TArray<FSeinSnapshotLatentActionRecord>& OutRecords,
		FGuid& OutSequenceDigest,
		FString& OutError);

	/** Capture one already-validated live action for the incremental root.
	 *  Snapshot capture continues to use CaptureRecords; this seam lets the
	 *  routine root reuse unchanged record digests instead of walking every
	 *  action through its codec on a checkpoint frame. */
	static bool CaptureRecordForVerifiedRoot(
		const FSeinLatentActionCodecManifest& Manifest,
		const USeinWorldSubsystem& World,
		const USeinLatentAction& Action,
		int32 Tick,
		int32 Ordinal,
		int64 NextActionId,
		int64 NextAbilityActivationId,
		FSeinSnapshotLatentActionRecord& OutRecord,
		FString& OutError);

	/** Canonically fold cached records and allocator cursors. */
	static bool ComputeSequenceDigestForVerifiedRoot(
		int64 NextActionId,
		int64 NextAbilityActivationId,
		TConstArrayView<FSeinSnapshotLatentActionRecord> Records,
		FGuid& OutSequenceDigest,
		FString& OutError);

	/** Rebind a cached record after its manager ordinal changes. Payload bytes
	 *  and payload digest remain exact; only the framed record digest changes. */
	static bool RecomputeRecordDigestForVerifiedRoot(
		FSeinSnapshotLatentActionRecord& InOutRecord,
		FString& OutError);

	static bool StageRecords(
		const FSeinLatentActionCodecManifest& Manifest,
		const ISeinCanonicalStateCandidateView& Candidate,
		const FSeinCanonicalStateRestorePlan& NativeState,
		const USeinWorldSubsystem& Services,
		int32 Tick,
		int64 NextActionId,
		int64 NextAbilityActivationId,
		TConstArrayView<FSeinSnapshotLatentActionRecord> Records,
		const FGuid& ExpectedSequenceDigest,
		FSeinLatentActionRestorePlan& OutPlan,
		FString& OutError);

	static int32 GetRegisteredCodecCount();

	/** Authoring-time exact-class admission query. This inspects the live
	 *  process registry, not a world's frozen manifest, and exists so editor
	 *  compiler/save/cook gates can prove an async factory's declared action
	 *  class actually has a codec before admitting the node. */
	static bool HasRegisteredCodecForExactClass(
		const UClass* ExactClass,
		FString* OutError = nullptr);

#if WITH_DEV_AUTOMATION_TESTS
	/**
	 * Test-only tamper helper. Preserves record order/ordinals and recomputes
	 * payload, record, then sequence digests after an intentional mutation.
	 */
	static bool RecomputeRecordDigestsForTests(
		int64 NextActionId,
		int64 NextAbilityActivationId,
		TArray<FSeinSnapshotLatentActionRecord>& InOutRecords,
		FGuid& OutSequenceDigest,
		FString& OutError);
#endif

private:
	static int32 FindManifestEntryIndex(
		const FSeinLatentActionCodecManifest& Manifest,
		const UClass* ExactClass);
	static int32 FindManifestEntryIndexByPath(
		const FSeinLatentActionCodecManifest& Manifest,
		const FString& ExactClassPath);
	static bool UnregisterToken(uint64 Token);
	friend class FSeinLatentActionCodecRegistrationHandle;
};
