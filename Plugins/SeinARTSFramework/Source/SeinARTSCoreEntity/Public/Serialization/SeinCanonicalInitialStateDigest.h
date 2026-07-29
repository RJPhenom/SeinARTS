/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCanonicalInitialStateDigest.h
 * @brief   Canonical tick-zero digest framing and extension contributors.
 */

#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"

class USeinWorldSubsystem;

/**
 * Bounded, endian-stable byte writer used by native tick-zero contributors.
 * Contributors write semantic values, never UObject addresses, raw structs, or
 * snapshot blobs. Each writer carries a domain and format version so extending
 * one contribution is an explicit schema change.
 */
class SEINARTSCOREENTITY_API FSeinCanonicalDigestWriter
{
public:
	explicit FSeinCanonicalDigestWriter(
		const FString& Domain,
		uint32 FormatVersion = 1);

	bool WriteBool(bool Value);
	bool WriteUInt8(uint8 Value);
	bool WriteUInt32(uint32 Value);
	bool WriteInt32(int32 Value);
	bool WriteUInt64(uint64 Value);
	bool WriteInt64(int64 Value);
	bool WriteGuid(const FGuid& Value);
	bool WriteString(const FString& Value);
	bool WriteName(FName Value);
	/** Length-framed opaque bytes already produced by a canonical codec. */
	bool WriteBytes(TConstArrayView<uint8> Value);

	bool Finalize(FGuid& OutDigest, FString& OutError) const;
	bool IsValid() const { return Error.IsEmpty(); }
	const FString& GetError() const { return Error; }

private:
	bool Append(const void* Data, int32 NumBytes);
	bool WriteUnsignedBigEndian(uint64 Value, int32 Width);

	static constexpr int32 MaxCanonicalBytes = 64 * 1024 * 1024;
	TArray<uint8> Bytes;
	FString Error;
};

using FSeinCanonicalInitialStateContributor = TFunction<bool(
	const USeinWorldSubsystem& /*World*/,
	FSeinCanonicalDigestWriter& /*OutState*/,
	FString& /*OutError*/)>;

/** Immutable native contributor captured when one world's bootstrap begins. */
struct SEINARTSCOREENTITY_API FSeinCanonicalInitialStateNativeContribution
{
	FName StableContributorID;
	uint32 SchemaVersion = 0;
	FSeinCanonicalInitialStateContributor Capture;
};

/**
 * Receipt-only reflected bootstrap evidence registered while Applying.
 * This is not persistent state; checkpoints retain only the canonical digest.
 */
struct SEINARTSCOREENTITY_API FSeinCanonicalInitialStateValueContribution
{
	FName StableContributorID;
	uint32 SchemaVersion = 0;
	FGuid ValueDigest;
};

/**
 * Move-only lifetime claim for one module-owned native contributor. Destroying
 * or resetting the handle unregisters it for future worlds. A world that has
 * begun bootstrap retains the callback only until its local receipt seals;
 * Core then keeps the resulting digest, never module-owned executable code.
 */
class SEINARTSCOREENTITY_API FSeinCanonicalInitialStateContributorHandle
{
public:
	FSeinCanonicalInitialStateContributorHandle() = default;
	~FSeinCanonicalInitialStateContributorHandle();

	FSeinCanonicalInitialStateContributorHandle(
		const FSeinCanonicalInitialStateContributorHandle&) = delete;
	FSeinCanonicalInitialStateContributorHandle& operator=(
		const FSeinCanonicalInitialStateContributorHandle&) = delete;

	FSeinCanonicalInitialStateContributorHandle(
		FSeinCanonicalInitialStateContributorHandle&& Other) noexcept;
	FSeinCanonicalInitialStateContributorHandle& operator=(
		FSeinCanonicalInitialStateContributorHandle&& Other) noexcept;

	bool IsValid() const { return Token != 0; }
	void Reset();

private:
	explicit FSeinCanonicalInitialStateContributorHandle(uint64 InToken)
		: Token(InToken)
	{
	}

	uint64 Token = 0;
	friend class FSeinCanonicalInitialStateDigest;
};

/** Registry and canonical framing contract for the Core tick-zero root. */
class SEINARTSCOREENTITY_API FSeinCanonicalInitialStateDigest
{
public:
	static constexpr uint32 CurrentFormatVersion = 4;
	static constexpr int32 MaxReloadClaimsPerContributor = 64;

	/** Register one stable module-owned contribution for future worlds.
	 *  Exact schema generations may overlap safely during module reload; the
	 *  newest live generation supplies the callback. */
	static FSeinCanonicalInitialStateContributorHandle RegisterNativeContributor(
		FName StableContributorID,
		uint32 SchemaVersion,
		FSeinCanonicalInitialStateContributor Capture,
		FString* OutError = nullptr);

	/** Freeze the process registry into one world's one-shot bootstrap. */
	static bool CaptureNativeContributors(
		TArray<FSeinCanonicalInitialStateNativeContribution>& OutContributors,
		FString& OutError);

	/** ASCII case fold used for identity comparison and canonical ordering. */
	static FString CanonicalContributorID(FName StableContributorID);

private:
	static void UnregisterNativeContributor(uint64 Token);
	friend class FSeinCanonicalInitialStateContributorHandle;
};
