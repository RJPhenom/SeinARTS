/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSimulationContentManifest.h
 * @brief   Generated simulation-content compatibility manifest and root codec.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SeinSimulationContentManifest.generated.h"

/**
 * One native module or opt-in extension whose discovery rules contributed to
 * the generated manifest. IDs are frozen, lowercase-compatible ASCII.
 */
USTRUCT(BlueprintType)
struct SEINARTSCOREENTITY_API FSeinSimulationContentContributorRecord
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		Category = "SeinARTS|Simulation Content")
	FString StableContributorId;

	/** Bump whenever this contributor's discovery or inclusion semantics change. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		Category = "SeinARTS|Simulation Content")
	int32 ContributorRevision = 0;

	/** Registry-computed digest of this contributor's canonical discovery roots. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		Category = "SeinARTS|Simulation Content")
	FGuid DiscoveryContractDigest;

	bool operator==(
		const FSeinSimulationContentContributorRecord& Other) const
	{
		return StableContributorId == Other.StableContributorId
			&& ContributorRevision == Other.ContributorRevision
			&& DiscoveryContractDigest
				== Other.DiscoveryContractDigest;
	}
};

/**
 * One future-affecting authored-content record.
 *
 * In v1 CanonicalRecordId is a canonical long package name and ContentDigest
 * binds its saved-package hash through ComputeRecordDigest. Later manifest
 * formats may define other stable UTF-8 identities. A record is never keyed by
 * a UObject pointer, FName comparison index, timestamp, or platform-local path.
 */
USTRUCT(BlueprintType)
struct SEINARTSCOREENTITY_API FSeinSimulationContentRecord
{
	GENERATED_BODY()

	/** Frozen ASCII semantic kind, for example "unreal.package". */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		Category = "SeinARTS|Simulation Content")
	FString StableRecordKindId;

	/** V1 is 1; changing payload semantics requires a manifest format revision. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		Category = "SeinARTS|Simulation Content")
	int32 RecordRevision = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		Category = "SeinARTS|Simulation Content")
	FString CanonicalRecordId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		Category = "SeinARTS|Simulation Content")
	FGuid ContentDigest;

	bool operator==(const FSeinSimulationContentRecord& Other) const
	{
		return StableRecordKindId == Other.StableRecordKindId
			&& RecordRevision == Other.RecordRevision
			&& CanonicalRecordId == Other.CanonicalRecordId
			&& ContentDigest == Other.ContentDigest;
	}
};

/**
 * One exact plugin/contributor-set profile. Framework-only and All-extension
 * builds intentionally produce different profiles rather than invalidating one
 * another. RootDigest binds this profile only.
 */
USTRUCT(BlueprintType)
struct SEINARTSCOREENTITY_API FSeinSimulationContentManifestProfile
{
	GENERATED_BODY()

	/** Bump when the editor builder's inclusion/canonicalization behavior changes. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		Category = "SeinARTS|Simulation Content")
	int32 BuilderRevision = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		Category = "SeinARTS|Simulation Content")
	TArray<FSeinSimulationContentContributorRecord> Contributors;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		Category = "SeinARTS|Simulation Content")
	TArray<FSeinSimulationContentRecord> Records;

	/** BLAKE3-128 over the canonical v1 manifest stream. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		Category = "SeinARTS|Simulation Content")
	FGuid RootDigest;

	bool operator==(const FSeinSimulationContentManifestProfile& Other) const
	{
		return BuilderRevision == Other.BuilderRevision
			&& Contributors == Other.Contributors
			&& Records == Other.Records
			&& RootDigest == Other.RootDigest;
	}
};

/**
 * Generated, cooked compatibility evidence for all supported plugin profiles.
 * Blueprint-authored gameplay remains the source material; this asset is a
 * read-only build product and must not become a second authoring surface.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Simulation Content Manifest"))
class SEINARTSCOREENTITY_API USeinSimulationContentManifest
	: public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		Category = "SeinARTS|Simulation Content")
	int32 FormatVersion = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		Category = "SeinARTS|Simulation Content")
	TArray<FSeinSimulationContentManifestProfile> Profiles;

	/** Strict full-container validation for editor/cook tooling. */
	bool Validate(FString& OutError) const;
};

/**
 * Canonical v1 record/root framing.
 *
 * Strings are explicit UTF-8 with uint64 byte lengths, integers are big-endian,
 * and GUIDs are four big-endian uint32 values. Exact duplicate content records
 * produced by overlapping discovery roots collapse to one record. A duplicate
 * key with different semantics or content is rejected.
 */
class SEINARTSCOREENTITY_API FSeinSimulationContentManifestCodec
{
public:
	static constexpr uint32 CurrentFormatVersion = 1;
	/** Exact editor inclusion/canonicalization contract accepted at runtime. */
	static constexpr uint32 CurrentBuilderRevision = 1;
	static const TCHAR* GetCurrentRecordKindId()
	{
		return TEXT("unreal.package");
	}
	static constexpr uint32 CurrentRecordRevision = 1;
	/** FIoHash byte width used by Asset Registry PackageSavedHash. */
	static constexpr int32 SavedPackageHashBytes = 20;
	static constexpr int32 MaxStableIdCharacters = 128;
	static constexpr int32 MaxCanonicalRecordIdCharacters = 2048;
	static constexpr int32 MaxContributors = 4096;
	static constexpr int32 MaxRecords = 256 * 1024;
	static constexpr int32 MaxProfiles = 64;
	static constexpr int32 MaxTotalRecords = 1024 * 1024;

	/** ASCII-only case fold plus frozen-identifier grammar validation. */
	static bool CanonicalizeStableId(
		const FString& StableId,
		FString& OutCanonicalId,
		FString& OutError);

	/**
	 * Produce one v1 leaf digest from the exact 20 PackageSavedHash bytes.
	 */
	static bool ComputeRecordDigest(
		const FString& StableRecordKindId,
		uint32 RecordRevision,
		const FString& CanonicalRecordId,
		TConstArrayView<uint8> CanonicalPayload,
		FGuid& OutDigest,
		FString& OutError);

	/**
	 * Normalize IDs and order, collapse exact duplicate records, and reject
	 * duplicate contributor IDs or conflicting record claims.
	 */
	static bool Canonicalize(
		TConstArrayView<FSeinSimulationContentContributorRecord> Contributors,
		TConstArrayView<FSeinSimulationContentRecord> Records,
		TArray<FSeinSimulationContentContributorRecord>& OutContributors,
		TArray<FSeinSimulationContentRecord>& OutRecords,
		FString& OutError);

	/** Compute an order-invariant root without mutating the supplied records. */
	static bool ComputeRootDigest(
		uint32 FormatVersion,
		uint32 BuilderRevision,
		TConstArrayView<FSeinSimulationContentContributorRecord> Contributors,
		TConstArrayView<FSeinSimulationContentRecord> Records,
		FGuid& OutRootDigest,
		FString& OutError);

	/** Canonicalize and seal one contributor-set profile in place. */
	static bool SealProfile(
		uint32 FormatVersion,
		FSeinSimulationContentManifestProfile& Profile,
		FString& OutError);

	/** Validate canonical profile storage and its stored root exactly. */
	static bool ValidateProfile(
		uint32 FormatVersion,
		const FSeinSimulationContentManifestProfile& Profile,
		FString& OutError);

	/**
	 * Replace the profile for the same exact contributor set, or append it if
	 * absent. Other Framework/extension profiles are preserved.
	 */
	static bool UpsertProfile(
		USeinSimulationContentManifest& Manifest,
		const FSeinSimulationContentManifestProfile& Profile,
		FString& OutError);

	/**
	 * Select and validate the sole profile whose contributor IDs, revisions,
	 * discovery digests, and builder revision exactly match the active runtime.
	 * Records are returned by value so runtime consumers can retain an immutable
	 * world-local copy. ActiveContributors must come from
	 * FSeinSimulationContentRegistry::BuildManifestContributorRecords.
	 */
	static bool SelectExactProfile(
		const USeinSimulationContentManifest& Manifest,
		uint32 ExpectedBuilderRevision,
		TConstArrayView<FSeinSimulationContentContributorRecord>
			ActiveContributors,
		FSeinSimulationContentManifestProfile& OutProfile,
		FString& OutError);

	/** Validate every stored profile and reject duplicate contributor sets. */
	static bool ValidateContainer(
		const USeinSimulationContentManifest& Manifest,
		FString& OutError);
};
