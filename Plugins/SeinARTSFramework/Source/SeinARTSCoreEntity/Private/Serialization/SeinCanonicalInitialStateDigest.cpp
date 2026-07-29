/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCanonicalInitialStateDigest.cpp
 */

#include "Serialization/SeinCanonicalInitialStateDigest.h"

#include "HAL/CriticalSection.h"
#include "Hash/Blake3.h"
#include "Misc/ScopeLock.h"

namespace
{
	struct FRegisteredInitialStateContributor
	{
		struct FClaim
		{
			uint64 Token = 0;
			FSeinCanonicalInitialStateContributor Capture;
		};

		FName StableContributorID;
		uint32 SchemaVersion = 0;
		TArray<FClaim> Claims;
	};

	struct FInitialStateContributorRegistry
	{
		FCriticalSection Mutex;
		TArray<FRegisteredInitialStateContributor> Entries;
		uint64 NextToken = 1;
	};

	FInitialStateContributorRegistry& GetContributorRegistry()
	{
		static FInitialStateContributorRegistry Registry;
		return Registry;
	}

	uint32 ReadUInt32BigEndian(const uint8* Bytes)
	{
		return (static_cast<uint32>(Bytes[0]) << 24)
			| (static_cast<uint32>(Bytes[1]) << 16)
			| (static_cast<uint32>(Bytes[2]) << 8)
			| static_cast<uint32>(Bytes[3]);
	}
}

FSeinCanonicalDigestWriter::FSeinCanonicalDigestWriter(
	const FString& Domain,
	uint32 FormatVersion)
{
	if (Domain.IsEmpty() || FormatVersion == 0)
	{
		Error = TEXT("Canonical digest writers require a domain and non-zero format version.");
		return;
	}
	WriteString(Domain);
	WriteUInt32(FormatVersion);
}

bool FSeinCanonicalDigestWriter::Append(const void* Data, int32 NumBytes)
{
	if (!Error.IsEmpty())
	{
		return false;
	}
	if (NumBytes < 0 || NumBytes > MaxCanonicalBytes - Bytes.Num())
	{
		Error = TEXT("Canonical digest byte limit exceeded.");
		return false;
	}
	if (NumBytes > 0)
	{
		Bytes.Append(static_cast<const uint8*>(Data), NumBytes);
	}
	return true;
}

bool FSeinCanonicalDigestWriter::WriteUnsignedBigEndian(
	uint64 Value,
	int32 Width)
{
	if (Width != 1 && Width != 4 && Width != 8)
	{
		Error = TEXT("Unsupported canonical integer width.");
		return false;
	}
	uint8 Encoded[8];
	for (int32 Index = 0; Index < Width; ++Index)
	{
		Encoded[Index] = static_cast<uint8>(
			Value >> ((Width - 1 - Index) * 8));
	}
	return Append(Encoded, Width);
}

bool FSeinCanonicalDigestWriter::WriteBool(bool Value)
{
	return WriteUInt8(Value ? 1 : 0);
}

bool FSeinCanonicalDigestWriter::WriteUInt8(uint8 Value)
{
	return WriteUnsignedBigEndian(Value, sizeof(Value));
}

bool FSeinCanonicalDigestWriter::WriteUInt32(uint32 Value)
{
	return WriteUnsignedBigEndian(Value, sizeof(Value));
}

bool FSeinCanonicalDigestWriter::WriteInt32(int32 Value)
{
	return WriteUInt32(BitCast<uint32>(Value));
}

bool FSeinCanonicalDigestWriter::WriteUInt64(uint64 Value)
{
	return WriteUnsignedBigEndian(Value, sizeof(Value));
}

bool FSeinCanonicalDigestWriter::WriteInt64(int64 Value)
{
	return WriteUInt64(BitCast<uint64>(Value));
}

bool FSeinCanonicalDigestWriter::WriteGuid(const FGuid& Value)
{
	return WriteUInt32(Value.A)
		&& WriteUInt32(Value.B)
		&& WriteUInt32(Value.C)
		&& WriteUInt32(Value.D);
}

bool FSeinCanonicalDigestWriter::WriteString(const FString& Value)
{
	const FTCHARToUTF8 Utf8(*Value, Value.Len());
	return WriteUInt64(static_cast<uint64>(Utf8.Length()))
		&& Append(Utf8.Get(), Utf8.Length());
}

bool FSeinCanonicalDigestWriter::WriteName(FName Value)
{
	return WriteString(
		FSeinCanonicalInitialStateDigest::CanonicalContributorID(Value));
}

bool FSeinCanonicalDigestWriter::WriteBytes(TConstArrayView<uint8> Value)
{
	if (Value.Num() < 0
		|| !WriteUInt64(static_cast<uint64>(Value.Num())))
	{
		return false;
	}
	return Append(Value.GetData(), Value.Num());
}

bool FSeinCanonicalDigestWriter::Finalize(
	FGuid& OutDigest,
	FString& OutError) const
{
	OutDigest.Invalidate();
	OutError = Error;
	if (!Error.IsEmpty() || Bytes.IsEmpty())
	{
		if (OutError.IsEmpty())
		{
			OutError = TEXT("Canonical digest stream is empty.");
		}
		return false;
	}

	const FBlake3Hash Hash = FBlake3::HashBuffer(Bytes.GetData(), Bytes.Num());
	const uint8* HashBytes = Hash.GetBytes();
	OutDigest = FGuid(
		ReadUInt32BigEndian(HashBytes),
		ReadUInt32BigEndian(HashBytes + 4),
		ReadUInt32BigEndian(HashBytes + 8),
		ReadUInt32BigEndian(HashBytes + 12));
	if (!OutDigest.IsValid())
	{
		OutError = TEXT("Canonical digest unexpectedly produced the invalid GUID sentinel.");
		return false;
	}
	return true;
}

FSeinCanonicalInitialStateContributorHandle::~FSeinCanonicalInitialStateContributorHandle()
{
	Reset();
}

FSeinCanonicalInitialStateContributorHandle::FSeinCanonicalInitialStateContributorHandle(
	FSeinCanonicalInitialStateContributorHandle&& Other) noexcept
	: Token(Other.Token)
{
	Other.Token = 0;
}

FSeinCanonicalInitialStateContributorHandle&
FSeinCanonicalInitialStateContributorHandle::operator=(
	FSeinCanonicalInitialStateContributorHandle&& Other) noexcept
{
	if (this != &Other)
	{
		Reset();
		Token = Other.Token;
		Other.Token = 0;
	}
	return *this;
}

void FSeinCanonicalInitialStateContributorHandle::Reset()
{
	if (Token != 0)
	{
		FSeinCanonicalInitialStateDigest::UnregisterNativeContributor(Token);
		Token = 0;
	}
}

FSeinCanonicalInitialStateContributorHandle
FSeinCanonicalInitialStateDigest::RegisterNativeContributor(
	FName StableContributorID,
	uint32 SchemaVersion,
	FSeinCanonicalInitialStateContributor Capture,
	FString* OutError)
{
	if (OutError)
	{
		OutError->Reset();
	}
	const FString CanonicalID = CanonicalContributorID(StableContributorID);
	if (StableContributorID.IsNone() || CanonicalID.IsEmpty()
		|| SchemaVersion == 0 || !Capture)
	{
		if (OutError)
		{
			*OutError = TEXT("Initial-state contributors require a stable ID, non-zero schema version, and callback.");
		}
		return {};
	}

	FInitialStateContributorRegistry& Registry = GetContributorRegistry();
	FScopeLock Lock(&Registry.Mutex);
	bool bAddedEntry = false;
	FRegisteredInitialStateContributor* Entry =
		Registry.Entries.FindByPredicate(
			[&CanonicalID](
				const FRegisteredInitialStateContributor& Existing)
			{
				return CanonicalContributorID(
					Existing.StableContributorID) == CanonicalID;
			});
	if (Entry)
	{
		if (Entry->SchemaVersion != SchemaVersion)
		{
			if (OutError)
			{
				*OutError = FString::Printf(
					TEXT("Initial-state contributor ID '%s' conflicts with a live schema generation."),
					*CanonicalID);
			}
			return {};
		}
		if (Entry->Claims.Num() >= MaxReloadClaimsPerContributor)
		{
			if (OutError)
			{
				*OutError = FString::Printf(
					TEXT("Initial-state contributor ID '%s' has too many overlapping reload generations."),
					*CanonicalID);
			}
			return {};
		}
	}
	else
	{
		FRegisteredInitialStateContributor& NewEntry =
			Registry.Entries.AddDefaulted_GetRef();
		NewEntry.StableContributorID = StableContributorID;
		NewEntry.SchemaVersion = SchemaVersion;
		Entry = &NewEntry;
		bAddedEntry = true;
	}

	if (Registry.NextToken == 0 || Registry.NextToken == MAX_uint64)
	{
		if (OutError)
		{
			*OutError = TEXT("Initial-state contributor registration token space is exhausted.");
		}
		if (bAddedEntry)
		{
			Registry.Entries.RemoveAt(Registry.Entries.Num() - 1);
		}
		return {};
	}
	const uint64 Token = Registry.NextToken++;
	FRegisteredInitialStateContributor::FClaim& Claim =
		Entry->Claims.AddDefaulted_GetRef();
	Claim.Token = Token;
	Claim.Capture = MoveTemp(Capture);
	return FSeinCanonicalInitialStateContributorHandle(Token);
}

bool FSeinCanonicalInitialStateDigest::CaptureNativeContributors(
	TArray<FSeinCanonicalInitialStateNativeContribution>& OutContributors,
	FString& OutError)
{
	OutContributors.Reset();
	OutError.Reset();

	FInitialStateContributorRegistry& Registry = GetContributorRegistry();
	FScopeLock Lock(&Registry.Mutex);
	OutContributors.Reserve(Registry.Entries.Num());
	for (const FRegisteredInitialStateContributor& Registered : Registry.Entries)
	{
		const FRegisteredInitialStateContributor::FClaim* ActiveClaim =
			Registered.Claims.IsEmpty()
				? nullptr
				: &Registered.Claims.Last();
		if (Registered.StableContributorID.IsNone()
			|| Registered.SchemaVersion == 0
			|| !ActiveClaim
			|| !ActiveClaim->Capture)
		{
			OutError = TEXT("The native initial-state contributor registry contains an invalid entry.");
			OutContributors.Reset();
			return false;
		}
		FSeinCanonicalInitialStateNativeContribution& Snapshot =
			OutContributors.AddDefaulted_GetRef();
		Snapshot.StableContributorID = Registered.StableContributorID;
		Snapshot.SchemaVersion = Registered.SchemaVersion;
		Snapshot.Capture = ActiveClaim->Capture;
	}

	OutContributors.Sort(
		[](const FSeinCanonicalInitialStateNativeContribution& A,
			const FSeinCanonicalInitialStateNativeContribution& B)
		{
			return FSeinCanonicalInitialStateDigest::CanonicalContributorID(
				A.StableContributorID)
				< FSeinCanonicalInitialStateDigest::CanonicalContributorID(
					B.StableContributorID);
		});
	for (int32 Index = 1; Index < OutContributors.Num(); ++Index)
	{
		if (FSeinCanonicalInitialStateDigest::CanonicalContributorID(
			OutContributors[Index - 1].StableContributorID)
			== FSeinCanonicalInitialStateDigest::CanonicalContributorID(
				OutContributors[Index].StableContributorID))
		{
			OutError = TEXT("The native initial-state contributor registry contains duplicate canonical IDs.");
			OutContributors.Reset();
			return false;
		}
	}
	return true;
}

FString FSeinCanonicalInitialStateDigest::CanonicalContributorID(
	FName StableContributorID)
{
	FString Result = StableContributorID.ToString();
	for (TCHAR& Character : Result)
	{
		if (Character >= TCHAR('A') && Character <= TCHAR('Z'))
		{
			Character += TCHAR('a') - TCHAR('A');
		}
	}
	return Result;
}

void FSeinCanonicalInitialStateDigest::UnregisterNativeContributor(uint64 Token)
{
	if (Token == 0)
	{
		return;
	}
	FInitialStateContributorRegistry& Registry = GetContributorRegistry();
	FScopeLock Lock(&Registry.Mutex);
	const int32 EntryIndex = Registry.Entries.IndexOfByPredicate(
		[Token](const FRegisteredInitialStateContributor& Entry)
		{
			return Entry.Claims.ContainsByPredicate(
				[Token](
					const FRegisteredInitialStateContributor::FClaim& Claim)
				{
					return Claim.Token == Token;
				});
		});
	if (EntryIndex == INDEX_NONE)
	{
		return;
	}
	FRegisteredInitialStateContributor& Entry =
		Registry.Entries[EntryIndex];
	const int32 RemovedClaims = Entry.Claims.RemoveAll(
		[Token](
			const FRegisteredInitialStateContributor::FClaim& Claim)
		{
			return Claim.Token == Token;
		});
	check(RemovedClaims == 1);
	if (Entry.Claims.IsEmpty())
	{
		Registry.Entries.RemoveAt(EntryIndex);
	}
}
