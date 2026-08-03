/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarStateCodecRegistry.h
 * @brief   Reload-safe exact-state codecs for pluggable fog implementations.
 */

#pragma once

#include "CoreMinimal.h"
#include "Serialization/SeinCanonicalStateRegistry.h"
#include "StructUtils/InstancedStruct.h"

class UClass;
class USeinFogOfWar;
class USeinFogOfWarSubsystem;
class USeinWorldSubsystem;
class UScriptStruct;
struct FSeinFogOfWarCanonicalStateProvider;

/**
 * Whether one codec may serve classes below its exact SupportedClass.
 *
 * ExactClassOnly is the safe default. DataOnlyBlueprintGeneratedChildren is
 * an explicit opt-in for Blueprint classes that only override inherited
 * defaults: every intervening class must be Blueprint-generated, non-native,
 * and declare no new reflected instance properties or functions. A native
 * subclass always requires its own codec claim.
 */
enum class ESeinFogOfWarStateCodecSubclassPolicy : uint8
{
	ExactClassOnly,
	DataOnlyBlueprintGeneratedChildren
};

/**
 * Stable compatibility claim owned by the concrete implementation module.
 *
 * Revisions are independent on purpose: schema describes bytes, behavior
 * describes gameplay, and codec describes canonical framing. Subclass
 * compatibility is explicit and participates in the descriptor digest.
 */
struct SEINARTSFOGOFWAR_API FSeinFogOfWarStateCodecDescriptor
{
	const UClass* SupportedClass = nullptr;
	ESeinFogOfWarStateCodecSubclassPolicy SubclassPolicy =
		ESeinFogOfWarStateCodecSubclassPolicy::ExactClassOnly;
	FString StableImplementationId;
	uint32 StateSchemaVersion = 0;
	uint32 BehaviorRevision = 0;
	uint32 CodecRevision = 0;
	const UScriptStruct* PayloadStruct = nullptr;
	FGuid PayloadSchemaDigest;
	TArray<const UScriptStruct*> DynamicPayloadStructs;
	TArray<FName> AllowedNames;
	FSeinCanonicalStateLimits Limits;
};

struct SEINARTSFOGOFWAR_API FSeinFogOfWarStateCaptureContext
{
	const USeinWorldSubsystem& World;
	const USeinFogOfWar& Fog;
	int32 Tick = 0;
};

/**
 * Fallible pre-adoption input. Services is the abandoned world's read-only
 * service/static-environment surface; Candidate is the only permitted source
 * of entity/component/authoritative simulation state.
 */
struct SEINARTSFOGOFWAR_API FSeinFogOfWarStateStageContext
{
	int32 Tick = 0;
	const ISeinCanonicalStateCandidateView* Candidate = nullptr;
	const USeinWorldSubsystem& Services;
	const USeinFogOfWar& Fog;
};

struct SEINARTSFOGOFWAR_API FSeinFogOfWarStateCommitContext
{
	const USeinWorldSubsystem& World;
	USeinFogOfWar& Fog;
	int32 Tick = 0;
};

/** Concrete-module-owned candidate. It must never alias live mutable fog state. */
struct SEINARTSFOGOFWAR_API ISeinFogOfWarStateRestoreStage
{
	virtual ~ISeinFogOfWarStateRestoreStage() = default;
	virtual void GatherReferencedObjects(TArray<UObject*>&) const {}
};

/** Executable half of one exact concrete codec generation. */
struct SEINARTSFOGOFWAR_API FSeinFogOfWarStateCodecOps
{
	TFunction<bool(
		const USeinFogOfWar&,
		FGuid&,
		FString&)> ComputeStaticEnvironmentDigest;

	TFunction<bool(
		const FSeinFogOfWarStateCaptureContext&,
		FInstancedStruct&,
		FString&)> Capture;

	/** Optional high-frequency digest projection. A production codec used by
	 *  multiplayer must provide this without serializing its full snapshot.
	 *  ForceFullRebuild is the independent verifier path. */
	TFunction<bool(
		const FSeinFogOfWarStateCaptureContext&,
		bool /*bForceFullRebuild*/,
		FGuid& /*OutPayloadDigest*/,
		uint64& /*OutProjectedPayloadBytes*/,
		uint64& /*OutMutationRevision*/,
		FString&)> CaptureRoutineRoot;

	TFunction<bool(
		const FSeinFogOfWarStateStageContext&,
		const FInstancedStruct&,
		TUniquePtr<ISeinFogOfWarStateRestoreStage>&,
		FString&)> StageRestore;

	TFunction<void(
		FSeinFogOfWarStateCommitContext&,
		TUniquePtr<ISeinFogOfWarStateRestoreStage>&&)> CommitRestore;
};

/** Move-only module claim. Destruction withdraws only this generation. */
class SEINARTSFOGOFWAR_API FSeinFogOfWarStateCodecRegistrationHandle
{
public:
	FSeinFogOfWarStateCodecRegistrationHandle() = default;
	~FSeinFogOfWarStateCodecRegistrationHandle();

	FSeinFogOfWarStateCodecRegistrationHandle(
		const FSeinFogOfWarStateCodecRegistrationHandle&) = delete;
	FSeinFogOfWarStateCodecRegistrationHandle& operator=(
		const FSeinFogOfWarStateCodecRegistrationHandle&) = delete;

	FSeinFogOfWarStateCodecRegistrationHandle(
		FSeinFogOfWarStateCodecRegistrationHandle&& Other) noexcept;
	FSeinFogOfWarStateCodecRegistrationHandle& operator=(
		FSeinFogOfWarStateCodecRegistrationHandle&& Other) noexcept;

	bool IsValid() const { return Token != 0; }
	void Reset();

private:
	explicit FSeinFogOfWarStateCodecRegistrationHandle(uint64 InToken)
		: Token(InToken)
	{
	}

	uint64 Token = 0;
	friend class FSeinFogOfWarStateCodecRegistry;
};

/**
 * Process registry for concrete codecs.
 *
 * A world binds the newest matching generation once during subsystem
 * initialization. Later reloads affect only later worlds; withdrawal of a
 * generation tears down every world still bound to it before module code can
 * unload.
 */
class SEINARTSFOGOFWAR_API FSeinFogOfWarStateCodecRegistry
{
public:
	static FSeinFogOfWarStateCodecRegistrationHandle Register(
		FName OwnerModuleId,
		const FSeinFogOfWarStateCodecDescriptor& Descriptor,
		FSeinFogOfWarStateCodecOps Ops,
		FString* OutError = nullptr);

	static bool Unregister(
		FSeinFogOfWarStateCodecRegistrationHandle& Handle);

	static int32 GetRegisteredCodecCount();

private:
	struct FResolvedClaim
	{
		uint64 Token = 0;
		FSeinFogOfWarStateCodecDescriptor Descriptor;
		FGuid CodecDescriptorDigest;
		FSeinFogOfWarStateCodecOps Ops;
	};

	static bool FreezeForClass(
		const UClass* FogClass,
		uint64& OutToken,
		FString& OutError);
	static bool Resolve(
		uint64 Token,
		FResolvedClaim& OutClaim,
		FString& OutError);
	static bool ResolveForClass(
		uint64 Token,
		const UClass* FogClass,
		FResolvedClaim& OutClaim,
		FString& OutError);
	static bool IsTokenAvailable(uint64 Token);
	static bool ComputeStaticEnvironmentDigest(
		uint64 Token,
		const USeinFogOfWar& Fog,
		FGuid& OutDigest,
		FString& OutError);
	static bool CapturePayload(
		uint64 Token,
		const FSeinFogOfWarStateCaptureContext& Context,
		FInstancedStruct& OutPayload,
		FString& OutError);
	static bool CaptureRoutineRoot(
		uint64 Token,
		const FSeinFogOfWarStateCaptureContext& Context,
		bool bForceFullRebuild,
		FGuid& OutPayloadDigest,
		uint64& OutProjectedPayloadBytes,
		uint64& OutMutationRevision,
		FString& OutError);
	static bool StagePayload(
		uint64 Token,
		const FSeinFogOfWarStateStageContext& Context,
		const FInstancedStruct& Payload,
		TUniquePtr<ISeinFogOfWarStateRestoreStage>& OutStage,
		FString& OutError);
	static void CommitPayload(
		uint64 Token,
		FSeinFogOfWarStateCommitContext& Context,
		TUniquePtr<ISeinFogOfWarStateRestoreStage>&& Stage);
	static bool EncodePayload(
		const FResolvedClaim& Claim,
		const FInstancedStruct& Payload,
		TArray<uint8>& OutBytes,
		FString& OutError);
	static bool DecodePayload(
		const FResolvedClaim& Claim,
		TConstArrayView<uint8> Bytes,
		FInstancedStruct& OutPayload,
		FString& OutError);
	static bool UnregisterToken(uint64 Token);

	friend class FSeinFogOfWarStateCodecRegistrationHandle;
	friend class USeinFogOfWarSubsystem;
	friend struct FSeinFogOfWarCanonicalStateProvider;
};
