/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinPoolObjectCodecRegistry.h
 * @brief   Frozen, reload-safe snapshot codecs for pooled sim UObjects.
 */

#pragma once

#include "CoreMinimal.h"
#include "Serialization/SeinSimulationContentManifest.h"
#include "Templates/SharedPointer.h"

class UClass;
class UObject;
struct FSeinSnapshotPoolInstanceRecord;

/** The two authoritative UObject pools covered by this registry. */
enum class ESeinPoolObjectKind : uint8
{
	Ability = 1,
	CommandBrokerResolver = 2,
};

/**
 * One provider's compatibility and admission contract.
 *
 * Native subclasses never inherit a provider: every concrete native class
 * must register its own exact anchor. Blueprint classes may inherit only from
 * their nearest native anchor, and only when that provider opts in. This keeps
 * Blueprint authoring broad without allowing a newly loaded native module to
 * acquire an accidental snapshot ABI.
 */
struct SEINARTSCOREENTITY_API FSeinPoolObjectCodecDescriptor
{
	const UClass* NativeAnchor = nullptr;
	ESeinPoolObjectKind Kind = ESeinPoolObjectKind::Ability;
	FString StableProviderId;
	uint32 StateSchemaVersion = 0;
	uint32 BehaviorRevision = 0;
	uint32 CodecRevision = 0;
	int32 MaxStateBytes = 0;
	bool bAllowBlueprintChildren = false;
	/** True for MakeReflectedOps. Set false only when explicit callbacks own
	 *  every field and native-serializer purity themselves. */
	bool bUsesReflectedState = true;
};

struct SEINARTSCOREENTITY_API FSeinPoolObjectCaptureContext
{
	const UObject& Object;
};

struct SEINARTSCOREENTITY_API FSeinPoolObjectMaterializeContext
{
	UObject& FinalOuter;
	const UClass& ExactClass;
	TConstArrayView<uint8> StateBytes;
};

/**
 * Executable state-provider callbacks.
 *
 * These callbacks run on the game thread inside the world's snapshot
 * transaction and the cross-registry provider guard. They must be
 * deterministic, self-contained, and free of externally visible side effects.
 * Capture may inspect only Object. Materialize must construct exactly one
 * candidate with FinalOuter, initialize only that candidate, and return it.
 * It may not mutate the world, register/unregister providers, enqueue work,
 * invoke gameplay delegates, or retain borrowed context. Core can preserve
 * authoritative-world atomicity, but cannot undo arbitrary process side
 * effects performed by third-party constructors or serializers.
 */
struct SEINARTSCOREENTITY_API FSeinPoolObjectCodecOps
{
	TFunction<bool(
		const FSeinPoolObjectCaptureContext&,
		TArray<uint8>&,
		FString&)> Capture;

	TFunction<UObject*(
		const FSeinPoolObjectMaterializeContext&,
		FString&)> Materialize;
};

/** Move-only lease for one hot-reload generation. */
class SEINARTSCOREENTITY_API FSeinPoolObjectCodecRegistrationHandle
{
public:
	FSeinPoolObjectCodecRegistrationHandle() = default;
	~FSeinPoolObjectCodecRegistrationHandle();

	FSeinPoolObjectCodecRegistrationHandle(
		const FSeinPoolObjectCodecRegistrationHandle&) = delete;
	FSeinPoolObjectCodecRegistrationHandle& operator=(
		const FSeinPoolObjectCodecRegistrationHandle&) = delete;

	FSeinPoolObjectCodecRegistrationHandle(
		FSeinPoolObjectCodecRegistrationHandle&& Other) noexcept;
	FSeinPoolObjectCodecRegistrationHandle& operator=(
		FSeinPoolObjectCodecRegistrationHandle&& Other) noexcept;

	bool IsValid() const { return Token != 0; }
	void Reset();

private:
	explicit FSeinPoolObjectCodecRegistrationHandle(uint64 InToken)
		: Token(InToken)
	{
	}

	uint64 Token = 0;
	friend class FSeinPoolObjectCodecRegistry;
};

#if WITH_DEV_AUTOMATION_TESTS
/**
 * Test-only lease admitting one transient, explicitly named local BP class.
 * Production discovery remains limited to the frozen content profile.
 */
class SEINARTSCOREENTITY_API
	FSeinPoolObjectLocalClassAdmissionHandle
{
public:
	FSeinPoolObjectLocalClassAdmissionHandle() = default;
	~FSeinPoolObjectLocalClassAdmissionHandle();
	FSeinPoolObjectLocalClassAdmissionHandle(
		const FSeinPoolObjectLocalClassAdmissionHandle&) = delete;
	FSeinPoolObjectLocalClassAdmissionHandle& operator=(
		const FSeinPoolObjectLocalClassAdmissionHandle&) = delete;
	FSeinPoolObjectLocalClassAdmissionHandle(
		FSeinPoolObjectLocalClassAdmissionHandle&& Other) noexcept;
	FSeinPoolObjectLocalClassAdmissionHandle& operator=(
		FSeinPoolObjectLocalClassAdmissionHandle&& Other) noexcept;
	bool IsValid() const { return Token != 0; }
	void Reset();

private:
	explicit FSeinPoolObjectLocalClassAdmissionHandle(uint64 InToken)
		: Token(InToken)
	{
	}
	uint64 Token = 0;
	friend class FSeinPoolObjectCodecRegistry;
};
#endif

/**
 * Immutable per-world provider and exact-class catalog.
 *
 * Class paths imported from a checkpoint are lookup keys into this catalog.
 * They are never passed to LoadClass/LoadObject during capture or restore.
 */
class SEINARTSCOREENTITY_API FSeinPoolObjectCodecManifest
{
public:
	/** Opaque immutable implementation; exposed only so translation-unit
	 *  helpers can operate without publishing its fields. */
	struct FData;

	bool IsValid() const { return Data.IsValid(); }
	int32 NumProviders() const;
	int32 NumAdmittedClasses() const;
	const FString& GetCanonicalManifest() const;
	FGuid GetDigest() const;
	bool VerifyProviderLeases(FString& OutError) const;

#if WITH_DEV_AUTOMATION_TESTS
	bool IsClassAdmittedForTests(
		const UClass* ExactClass,
		ESeinPoolObjectKind Kind) const;
#endif

private:
	TSharedPtr<const FData, ESPMode::ThreadSafe> Data;

	friend class FSeinPoolObjectCodecRegistry;
};

/**
 * Process registry and the sole encoder/materializer for pool records.
 *
 * The built-in reflected ops preserve ordinary Blueprint authorability through
 * a canonical wire with pre-allocation limits. Unsupported state fails at
 * manifest freeze; its native anchor must instead install an explicit codec.
 */
class SEINARTSCOREENTITY_API FSeinPoolObjectCodecRegistry
{
public:
	static constexpr int32 MaxReloadClaimsPerAnchor = 64;
	static constexpr int32 MaxStateBytes = 16 * 1024 * 1024;

	static FSeinPoolObjectCodecRegistrationHandle Register(
		FName OwnerModuleId,
		const FSeinPoolObjectCodecDescriptor& Descriptor,
		FSeinPoolObjectCodecOps Ops,
		FString* OutError = nullptr);

	static bool Unregister(
		FSeinPoolObjectCodecRegistrationHandle& Handle);

	/**
	 * Freeze providers and locally admit Blueprint classes found only in the
	 * selected, already-validated simulation-content profile.
	 */
	static FSeinPoolObjectCodecManifest CaptureManifest(
		const FSeinSimulationContentManifestProfile& ContentProfile,
		FString* OutError = nullptr);

	static bool CaptureObject(
		const FSeinPoolObjectCodecManifest& Manifest,
		const UObject& Object,
		ESeinPoolObjectKind ExpectedKind,
		int32 PoolId,
		FSeinSnapshotPoolInstanceRecord& OutRecord,
		FString& OutError);

	/**
	 * Validate the imported descriptor against the local catalog, then create
	 * the final candidate exactly once. The caller must retain/adopt that same
	 * object or discard it on transaction failure.
	 */
	static UObject* MaterializeObject(
		const FSeinPoolObjectCodecManifest& Manifest,
		const FSeinSnapshotPoolInstanceRecord& Record,
		ESeinPoolObjectKind ExpectedKind,
		UObject& FinalOuter,
		FString& OutError);

	/** UPROPERTY-only state codec used by framework and opt-in native anchors. */
	static FSeinPoolObjectCodecOps MakeReflectedOps();

	/** Prove a class contains only the bounded reflected subset: integer/bool
	 *  scalars, bounded existing names/strings, deterministic structs,
	 *  optionals, and ordered arrays. References, delegates, floats,
	 *  post-serialized structs, maps, and sets fail. */
	static bool ValidateReflectedClassSchema(
		const UClass* ExactClass,
		FString& OutError);

	static int32 GetRegisteredProviderCount();

#if WITH_DEV_AUTOMATION_TESTS
	static FSeinPoolObjectLocalClassAdmissionHandle
		RegisterExplicitLocalClassForTests(
			const UClass* ExactClass,
			FString* OutError = nullptr);
#endif

private:
	static bool UnregisterToken(uint64 Token);
#if WITH_DEV_AUTOMATION_TESTS
	static bool UnregisterTestLocalClass(uint64 Token);
	friend class FSeinPoolObjectLocalClassAdmissionHandle;
#endif
	friend class FSeinPoolObjectCodecRegistrationHandle;
};
