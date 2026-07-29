/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCanonicalStateRoot.h
 * @brief   Fallible BLAKE3-128 composition of canonical live-world leaves.
 */

#pragma once

#include "CoreMinimal.h"
#include "Serialization/SeinSnapshotEnvelopeCodec.h"

/**
 * Canonical identity and precomputed value leaf for one live-world section.
 *
 * The producer owns capture and leaf computation. The composer validates and
 * canonically orders this evidence; it never substitutes a marker for a failed
 * capture. PayloadBytes binds the reversible canonical payload length when the
 * producer has one; digest-only projections use zero and bind their projection
 * contract through SchemaDigest and DescriptorDigest.
 */
struct SEINARTSCOREENTITY_API FSeinCanonicalStateRootLeaf
{
	FString SectionId;
	ESeinSnapshotSectionRole Role =
		ESeinSnapshotSectionRole::Authoritative;
	ESeinSnapshotSectionCodec Codec =
		ESeinSnapshotSectionCodec::CanonicalBytes;
	uint32 SchemaVersion = 1;
	FGuid SchemaDigest;
	FGuid DescriptorDigest;
	uint64 PayloadBytes = 0;
	FGuid LeafDigest;
};

/** World identity that prevents equal values in incompatible sessions matching. */
struct SEINARTSCOREENTITY_API FSeinCanonicalStateRootIdentity
{
	int64 Tick = 0;
	FGuid CommandProtocolDigest;
	FGuid CompatibilityDigest;
};

/**
 * Pure, allocation-bounded live-world root composer.
 *
 * Authoritative and Continuation leaves contribute to the BLAKE3-128 root.
 * DerivedCache and Local leaves are validated for a coherent section catalog
 * but deliberately excluded. Input order is irrelevant: stable lowercase-ASCII
 * section IDs define the canonical bytewise order.
 *
 * This format has a distinct domain/version from snapshot-v13. A future
 * snapshot equivalence refactor must explicitly migrate both producers rather
 * than assuming the two current roots are byte-identical.
 */
class SEINARTSCOREENTITY_API FSeinCanonicalStateRootComposer
{
public:
	static constexpr uint32 CurrentFormatVersion = 1;
	static constexpr uint32 MaxSections =
		FSeinSnapshotEnvelopeCodec::MaxSections;
	static constexpr uint32 MaxSectionIdBytes =
		FSeinSnapshotEnvelopeCodec::MaxSectionIdBytes;
	static constexpr uint64 MaxSectionPayloadBytes =
		FSeinSnapshotEnvelopeCodec::MaxSectionPayloadBytes;

	/**
	 * Compose one canonical root. OutRoot is unchanged on failure; OutError
	 * describes the first rejected identity, section, or writer condition.
	 */
	static bool Compose(
		const FSeinCanonicalStateRootIdentity& Identity,
		TConstArrayView<FSeinCanonicalStateRootLeaf> Leaves,
		FGuid& OutRoot,
		FString& OutError);
};
