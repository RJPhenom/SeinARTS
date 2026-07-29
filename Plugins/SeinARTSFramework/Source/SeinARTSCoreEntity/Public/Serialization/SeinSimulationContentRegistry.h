/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSimulationContentRegistry.h
 * @brief   Extension-safe native simulation-content discovery registry.
 */

#pragma once

#include "CoreMinimal.h"
#include "Serialization/SeinSimulationContentManifest.h"

/**
 * One native editor-discovery root.
 *
 * RootClassPath is a frozen derived-gameplay base class path such as
 * "/Script/SeinARTSCoreEntity.SeinAbility"; it is not a request to scan every
 * Blueprint through "/Script/Engine.Blueprint". V1 discovery emits the one
 * "unreal.package" saved-hash record kind. The editor builder follows
 * transitive runtime/game dependencies across /Game and mounts claimed by
 * registered/configured roots. A native class root claims its owning plugin's
 * content mount; unrelated enabled/test plugins remain outside the profile.
 * This lets project and extension assets compose without hardcoded module
 * knowledge. A data-only mount or content reached through an opaque lookup
 * needs an explicit root. No UObject or gameplay state is retained here.
 */
struct SEINARTSCOREENTITY_API FSeinSimulationContentDiscoveryRoot
{
	FString RootClassPath;
	FString StableRecordKindId;
	/** V1 requires FSeinSimulationContentManifestCodec::CurrentRecordRevision. */
	uint32 RecordRevision = 0;

	bool operator==(const FSeinSimulationContentDiscoveryRoot& Other) const
	{
		return RootClassPath == Other.RootClassPath
			&& StableRecordKindId == Other.StableRecordKindId
			&& RecordRevision == Other.RecordRevision;
	}
};

/** One framework/extension module's frozen discovery contract. */
struct SEINARTSCOREENTITY_API FSeinSimulationContentContributorDescriptor
{
	FString StableContributorId;
	uint32 ContributorRevision = 0;
	TArray<FSeinSimulationContentDiscoveryRoot> DiscoveryRoots;
	/** Canonical long package names that seed dependency closure directly. */
	TArray<FString> ExplicitPackageRoots;
};

/** Canonical registered contributor plus its self-proving discovery contract. */
struct SEINARTSCOREENTITY_API FSeinFrozenSimulationContentContributor
{
	FString StableContributorId;
	uint32 ContributorRevision = 0;
	FGuid DiscoveryContractDigest;
	TArray<FSeinSimulationContentDiscoveryRoot> DiscoveryRoots;
	TArray<FString> ExplicitPackageRoots;
};

/**
 * Registration-order-independent registry snapshot for the editor builder.
 * DiscoveryRoots and ExplicitPackageRoots are globally deduplicated unions.
 * Contributor-local roots remain available to prove the discovery contract;
 * origin labels are not copied onto manifest content records.
 */
struct SEINARTSCOREENTITY_API FSeinSimulationContentRegistrySnapshot
{
	TArray<FSeinFrozenSimulationContentContributor> Contributors;
	TArray<FSeinSimulationContentDiscoveryRoot> DiscoveryRoots;
	TArray<FString> ExplicitPackageRoots;
};

/**
 * Move-only module-generation lease. Successful claims register a contributor;
 * failed claims retain a poison lease so capture cannot silently omit that
 * live module. Destruction removes only this exact generation.
 */
class SEINARTSCOREENTITY_API FSeinSimulationContentRegistrationHandle
{
public:
	FSeinSimulationContentRegistrationHandle() = default;
	~FSeinSimulationContentRegistrationHandle();

	FSeinSimulationContentRegistrationHandle(
		const FSeinSimulationContentRegistrationHandle&) = delete;
	FSeinSimulationContentRegistrationHandle& operator=(
		const FSeinSimulationContentRegistrationHandle&) = delete;

	FSeinSimulationContentRegistrationHandle(
		FSeinSimulationContentRegistrationHandle&& Other) noexcept;
	FSeinSimulationContentRegistrationHandle& operator=(
		FSeinSimulationContentRegistrationHandle&& Other) noexcept;

	/** True only for a successful contributor claim. Failed claims may still
	 *  retain an internal lease so the registry remains poisoned until that
	 *  exact module generation unloads. */
	bool IsValid() const
	{
		return Token != 0 && bRegistrationSucceeded;
	}
	void Reset();

private:
	explicit FSeinSimulationContentRegistrationHandle(
		uint64 InToken,
		bool bInRegistrationSucceeded)
		: Token(InToken)
		, bRegistrationSucceeded(bInRegistrationSucceeded)
	{
	}

	uint64 Token = 0;
	bool bRegistrationSucceeded = false;
	friend class FSeinSimulationContentRegistry;
};

/**
 * Process-local discovery registry.
 *
 * Contributor IDs are unique semantically. Exact duplicate generations may
 * overlap during module reload and receive independent lifetime leases;
 * incompatible reuse of an ID fails registration. Overlapping roots with
 * identical kind/revision semantics collapse globally. Registration owns no
 * executable callback, UObject, or mutable gameplay state.
 */
class SEINARTSCOREENTITY_API FSeinSimulationContentRegistry
{
public:
	static constexpr int32 MaxReloadClaimsPerContributor = 64;
	static constexpr int32 MaxDiscoveryRootsPerContributor = 4096;
	static constexpr int32 MaxGlobalDiscoveryRoots = 64 * 1024;
	static constexpr int32 MaxExplicitPackageRootsPerContributor = 4096;
	static constexpr int32 MaxGlobalExplicitPackageRoots = 64 * 1024;
	static constexpr int32 MaxRootClassPathCharacters = 1024;
	static constexpr int32 MaxPackagePathCharacters = 1024;

	static FSeinSimulationContentRegistrationHandle RegisterContributor(
		const FSeinSimulationContentContributorDescriptor& Descriptor,
		FString* OutError = nullptr);

	/** Capture a canonical immutable value snapshot for editor discovery. */
	static bool CaptureSnapshot(
		FSeinSimulationContentRegistrySnapshot& OutSnapshot,
		FString& OutError);

	/** Convert a captured registry snapshot into an exact manifest profile key. */
	static bool BuildManifestContributorRecords(
		const FSeinSimulationContentRegistrySnapshot& Snapshot,
		TArray<FSeinSimulationContentContributorRecord>& OutContributors,
		FString& OutError);

	static int32 GetRegisteredContributorCount();

private:
	static void UnregisterContributor(uint64 Token);
	friend class FSeinSimulationContentRegistrationHandle;
};
