/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSimulationContentRegistry.cpp
 */

#include "Serialization/SeinSimulationContentRegistry.h"

#include "HAL/CriticalSection.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeLock.h"
#include "Serialization/SeinCanonicalInitialStateDigest.h"
#include "Serialization/SeinSimulationContentManifest.h"

namespace
{
	struct FRegisteredSimulationContentContributor
	{
		TArray<uint64> Tokens;
		FSeinFrozenSimulationContentContributor Descriptor;
	};

	struct FAggregatedDiscoveryRootClaim
	{
		FSeinSimulationContentDiscoveryRoot Root;
		int32 RefCount = 0;
	};

	struct FAggregatedPackageRootClaim
	{
		FString PackagePath;
		int32 RefCount = 0;
	};

	struct FSimulationContentRegistryState
	{
		FCriticalSection Mutex;
		TArray<FRegisteredSimulationContentContributor> Entries;
		TMap<FString, FAggregatedDiscoveryRootClaim> DiscoveryClaims;
		TMap<FString, FAggregatedPackageRootClaim> PackageClaims;
		TSet<uint64> FailedClaimTokens;
		uint64 NextToken = 1;
		bool bUnleasedFailure = false;
	};

	FSimulationContentRegistryState& GetRegistryState()
	{
		static FSimulationContentRegistryState State;
		return State;
	}

	int32 CompareUtf8(const FString& Left, const FString& Right)
	{
		const FTCHARToUTF8 LeftUtf8(*Left, Left.Len());
		const FTCHARToUTF8 RightUtf8(*Right, Right.Len());
		const int32 SharedLength = LeftUtf8.Length() < RightUtf8.Length()
			? LeftUtf8.Length()
			: RightUtf8.Length();
		if (SharedLength > 0)
		{
			const int32 Comparison = FMemory::Memcmp(
				LeftUtf8.Get(), RightUtf8.Get(), SharedLength);
			if (Comparison != 0)
			{
				return Comparison;
			}
		}
		return LeftUtf8.Length() - RightUtf8.Length();
	}

	FString FoldAsciiCase(const FString& Value)
	{
		FString Result = Value;
		for (TCHAR& Character : Result)
		{
			if (Character >= TEXT('A') && Character <= TEXT('Z'))
			{
				Character += TEXT('a') - TEXT('A');
			}
		}
		return Result;
	}

	bool ValidateRootClassPath(const FString& Path, FString& OutError)
	{
		int32 DotCount = 0;
		for (const TCHAR Character : Path)
		{
			DotCount += Character == TEXT('.') ? 1 : 0;
		}
		if (Path.IsEmpty()
			|| Path.Len()
				> FSeinSimulationContentRegistry::
					MaxRootClassPathCharacters
			|| Path.Len() != FCString::Strlen(*Path)
			|| Path[0] != TEXT('/')
			|| Path.EndsWith(TEXT("/"))
			|| Path.EndsWith(TEXT("."))
			|| DotCount != 1
			|| Path.Contains(TEXT("\\"))
			|| Path.Contains(TEXT("//"))
			|| Path.Contains(TEXT(":")))
		{
			OutError = FString::Printf(
				TEXT("Discovery root '%s' is not a canonical top-level class path."),
				*Path);
			return false;
		}
		for (const TCHAR Character : Path)
		{
			if (Character <= TEXT(' ') || Character == TCHAR(0x7f))
			{
				OutError = FString::Printf(
					TEXT("Discovery root '%s' contains a control character."),
					*Path);
				return false;
			}
		}
		return true;
	}

	bool ValidatePackagePath(const FString& Path, FString& OutError)
	{
		if (Path.IsEmpty()
			|| Path.Len()
				> FSeinSimulationContentRegistry::
					MaxPackagePathCharacters
			|| Path.Len() != FCString::Strlen(*Path)
			|| !FPackageName::IsValidLongPackageName(Path))
		{
			OutError = FString::Printf(
				TEXT("Explicit root '%s' is not a canonical long package name."),
				*Path);
			return false;
		}
		return true;
	}

	int32 CompareRoots(
		const FSeinSimulationContentDiscoveryRoot& Left,
		const FSeinSimulationContentDiscoveryRoot& Right)
	{
		const int32 PathOrder =
			CompareUtf8(Left.RootClassPath, Right.RootClassPath);
		if (PathOrder != 0)
		{
			return PathOrder;
		}
		const int32 KindOrder = CompareUtf8(
			Left.StableRecordKindId,
			Right.StableRecordKindId);
		if (KindOrder != 0)
		{
			return KindOrder;
		}
		return Left.RecordRevision < Right.RecordRevision
			? -1
			: Left.RecordRevision > Right.RecordRevision ? 1 : 0;
	}

	bool CanonicalizeRoots(
		TConstArrayView<FSeinSimulationContentDiscoveryRoot> Roots,
		int32 MaxRoots,
		TArray<FSeinSimulationContentDiscoveryRoot>& OutRoots,
		FString& OutError)
	{
		OutRoots.Reset();
		OutError.Reset();
		if (MaxRoots < 0 || Roots.Num() > MaxRoots)
		{
			OutError =
				TEXT("A simulation-content contributor exceeds its discovery-root bound.");
			return false;
		}

		TArray<FSeinSimulationContentDiscoveryRoot> SortedRoots;
		SortedRoots.Reserve(Roots.Num());
		for (const FSeinSimulationContentDiscoveryRoot& Root : Roots)
		{
			FSeinSimulationContentDiscoveryRoot Canonical = Root;
			if (Canonical.RecordRevision
					!= FSeinSimulationContentManifestCodec::
						CurrentRecordRevision
				|| !ValidateRootClassPath(
					Canonical.RootClassPath,
					OutError)
				|| !FSeinSimulationContentManifestCodec::
					CanonicalizeStableId(
						Root.StableRecordKindId,
						Canonical.StableRecordKindId,
						OutError))
			{
				if (OutError.IsEmpty())
				{
					OutError =
						TEXT("Discovery roots require the v1 saved-package record revision.");
				}
				return false;
			}
			if (Canonical.StableRecordKindId
				!= FSeinSimulationContentManifestCodec::
					GetCurrentRecordKindId())
			{
				OutError = FString::Printf(
					TEXT("Simulation-content format v1 discovery supports only record kind '%s'."),
					FSeinSimulationContentManifestCodec::
						GetCurrentRecordKindId());
				return false;
			}
			SortedRoots.Add(MoveTemp(Canonical));
		}
		SortedRoots.Sort(
			[](const FSeinSimulationContentDiscoveryRoot& Left,
				const FSeinSimulationContentDiscoveryRoot& Right)
			{
				return CompareRoots(Left, Right) < 0;
			});

		TArray<FSeinSimulationContentDiscoveryRoot> CanonicalRoots;
		CanonicalRoots.Reserve(SortedRoots.Num());
		TMap<FString, int32> RootIndexByFoldedPath;
		for (FSeinSimulationContentDiscoveryRoot& Root : SortedRoots)
		{
			const FString FoldedPath =
				FoldAsciiCase(Root.RootClassPath);
			if (const int32* ExistingIndex =
				RootIndexByFoldedPath.Find(FoldedPath))
			{
				const FSeinSimulationContentDiscoveryRoot& Existing =
					CanonicalRoots[*ExistingIndex];
				if (Existing.RootClassPath != Root.RootClassPath)
				{
					OutError = FString::Printf(
						TEXT("Discovery roots '%s' and '%s' collide by ASCII case."),
						*Existing.RootClassPath,
						*Root.RootClassPath);
					return false;
				}
				if (Existing.StableRecordKindId
						!= Root.StableRecordKindId
					|| Existing.RecordRevision
						!= Root.RecordRevision)
				{
					OutError = FString::Printf(
						TEXT("Discovery root '%s' has incompatible record-kind semantics."),
						*Root.RootClassPath);
					return false;
				}
				// Identical overlapping roots are one semantic claim.
				continue;
			}
			const int32 NewIndex =
				CanonicalRoots.Add(MoveTemp(Root));
			RootIndexByFoldedPath.Add(FoldedPath, NewIndex);
		}
		OutRoots = MoveTemp(CanonicalRoots);
		return true;
	}

	bool CanonicalizePackageRoots(
		TConstArrayView<FString> Roots,
		int32 MaxRoots,
		TArray<FString>& OutRoots,
		FString& OutError)
	{
		OutRoots.Reset();
		OutError.Reset();
		if (MaxRoots < 0 || Roots.Num() > MaxRoots)
		{
			OutError =
				TEXT("A simulation-content contributor exceeds its explicit-package-root bound.");
			return false;
		}

		TArray<FString> SortedRoots;
		SortedRoots.Reserve(Roots.Num());
		for (const FString& Root : Roots)
		{
			if (!ValidatePackagePath(Root, OutError))
			{
				return false;
			}
			SortedRoots.Add(Root);
		}
		SortedRoots.Sort(
			[](const FString& Left, const FString& Right)
			{
				return CompareUtf8(Left, Right) < 0;
			});

		TMap<FString, FString> ExactPathByFoldedPath;
		TArray<FString> CanonicalRoots;
		CanonicalRoots.Reserve(SortedRoots.Num());
		for (FString& Root : SortedRoots)
		{
			const FString FoldedPath = FoldAsciiCase(Root);
			if (const FString* Existing =
				ExactPathByFoldedPath.Find(FoldedPath))
			{
				if (*Existing != Root)
				{
					OutError = FString::Printf(
						TEXT("Explicit package roots '%s' and '%s' collide by ASCII case."),
						**Existing,
						*Root);
					return false;
				}
				continue;
			}
			ExactPathByFoldedPath.Add(FoldedPath, Root);
			CanonicalRoots.Add(MoveTemp(Root));
		}
		OutRoots = MoveTemp(CanonicalRoots);
		return true;
	}

	bool CanonicalizeDescriptor(
		const FSeinSimulationContentContributorDescriptor& Descriptor,
		FSeinFrozenSimulationContentContributor& OutDescriptor,
		FString& OutError)
	{
		OutError.Reset();
		OutDescriptor = {};
		if (Descriptor.ContributorRevision == 0
			|| Descriptor.ContributorRevision > MAX_int32
			|| !FSeinSimulationContentManifestCodec::CanonicalizeStableId(
				Descriptor.StableContributorId,
				OutDescriptor.StableContributorId,
				OutError)
			|| !CanonicalizeRoots(
				Descriptor.DiscoveryRoots,
				FSeinSimulationContentRegistry::
					MaxDiscoveryRootsPerContributor,
				OutDescriptor.DiscoveryRoots,
				OutError)
			|| !CanonicalizePackageRoots(
				Descriptor.ExplicitPackageRoots,
				FSeinSimulationContentRegistry::
					MaxExplicitPackageRootsPerContributor,
				OutDescriptor.ExplicitPackageRoots,
				OutError))
		{
			if (OutError.IsEmpty())
			{
				OutError =
					TEXT("Simulation-content contributors require a positive revision.");
			}
			return false;
		}

		OutDescriptor.ContributorRevision =
			Descriptor.ContributorRevision;
		FSeinCanonicalDigestWriter Writer(
			TEXT("SeinARTS.SimulationContent.DiscoveryContract"),
			1);
		if (!Writer.WriteString(OutDescriptor.StableContributorId)
			|| !Writer.WriteUInt32(OutDescriptor.ContributorRevision)
			|| !Writer.WriteUInt32(
				static_cast<uint32>(
					OutDescriptor.DiscoveryRoots.Num())))
		{
			OutError = Writer.GetError();
			return false;
		}
		for (const FSeinSimulationContentDiscoveryRoot& Root :
			OutDescriptor.DiscoveryRoots)
		{
			if (!Writer.WriteString(Root.RootClassPath)
				|| !Writer.WriteString(Root.StableRecordKindId)
				|| !Writer.WriteUInt32(Root.RecordRevision))
			{
				OutError = Writer.GetError();
				return false;
			}
		}
		if (!Writer.WriteUInt32(
			static_cast<uint32>(
				OutDescriptor.ExplicitPackageRoots.Num())))
		{
			OutError = Writer.GetError();
			return false;
		}
		for (const FString& PackageRoot :
			OutDescriptor.ExplicitPackageRoots)
		{
			if (!Writer.WriteString(PackageRoot))
			{
				OutError = Writer.GetError();
				return false;
			}
		}
		return Writer.Finalize(
			OutDescriptor.DiscoveryContractDigest,
			OutError);
	}

	bool DescriptorsMatch(
		const FSeinFrozenSimulationContentContributor& Left,
		const FSeinFrozenSimulationContentContributor& Right)
	{
		return Left.StableContributorId == Right.StableContributorId
			&& Left.ContributorRevision == Right.ContributorRevision
			&& Left.DiscoveryContractDigest
				== Right.DiscoveryContractDigest
			&& Left.DiscoveryRoots == Right.DiscoveryRoots
			&& Left.ExplicitPackageRoots
				== Right.ExplicitPackageRoots;
	}

}

FSeinSimulationContentRegistrationHandle::
	~FSeinSimulationContentRegistrationHandle()
{
	Reset();
}

FSeinSimulationContentRegistrationHandle::
	FSeinSimulationContentRegistrationHandle(
		FSeinSimulationContentRegistrationHandle&& Other) noexcept
	: Token(Other.Token)
	, bRegistrationSucceeded(Other.bRegistrationSucceeded)
{
	Other.Token = 0;
	Other.bRegistrationSucceeded = false;
}

FSeinSimulationContentRegistrationHandle&
FSeinSimulationContentRegistrationHandle::operator=(
	FSeinSimulationContentRegistrationHandle&& Other) noexcept
{
	if (this != &Other)
	{
		Reset();
		Token = Other.Token;
		bRegistrationSucceeded =
			Other.bRegistrationSucceeded;
		Other.Token = 0;
		Other.bRegistrationSucceeded = false;
	}
	return *this;
}

void FSeinSimulationContentRegistrationHandle::Reset()
{
	if (Token != 0)
	{
		FSeinSimulationContentRegistry::UnregisterContributor(Token);
		Token = 0;
	}
	bRegistrationSucceeded = false;
}

FSeinSimulationContentRegistrationHandle
FSeinSimulationContentRegistry::RegisterContributor(
	const FSeinSimulationContentContributorDescriptor& Descriptor,
	FString* OutError)
{
	if (OutError)
	{
		OutError->Reset();
	}

	FString Error;
	FSeinFrozenSimulationContentContributor CanonicalDescriptor;
	const bool bCanonicalDescriptorValid = CanonicalizeDescriptor(
		Descriptor,
		CanonicalDescriptor,
		Error);

	FSimulationContentRegistryState& State = GetRegistryState();
	FScopeLock Lock(&State.Mutex);
	const auto MakeFailedClaimLease = [&State]()
	{
		if (State.NextToken == 0
			|| State.NextToken == MAX_uint64)
		{
			State.bUnleasedFailure = true;
			return FSeinSimulationContentRegistrationHandle();
		}
		const uint64 FailedToken = State.NextToken++;
		State.FailedClaimTokens.Add(FailedToken);
		return FSeinSimulationContentRegistrationHandle(
			FailedToken, false);
	};

	if (!bCanonicalDescriptorValid)
	{
		if (OutError)
		{
			*OutError = MoveTemp(Error);
		}
		return MakeFailedClaimLease();
	}

	FRegisteredSimulationContentContributor* ExistingContributor =
		State.Entries.FindByPredicate(
			[&CanonicalDescriptor](
				const FRegisteredSimulationContentContributor& Existing)
			{
				return Existing.Descriptor.StableContributorId
					== CanonicalDescriptor.StableContributorId;
			});
	if (ExistingContributor
		&& !DescriptorsMatch(
			ExistingContributor->Descriptor,
			CanonicalDescriptor))
	{
		if (OutError)
		{
			*OutError = FString::Printf(
				TEXT("Simulation-content contributor ID '%s' is already registered with a different discovery contract."),
				*CanonicalDescriptor.StableContributorId);
		}
		return MakeFailedClaimLease();
	}
	if (ExistingContributor
		&& ExistingContributor->Tokens.Num()
			>= MaxReloadClaimsPerContributor)
	{
		if (OutError)
		{
			*OutError = FString::Printf(
				TEXT("Simulation-content contributor ID '%s' exceeds its reload-generation bound."),
				*CanonicalDescriptor.StableContributorId);
		}
		return MakeFailedClaimLease();
	}
	if (!ExistingContributor
		&& State.Entries.Num()
			>= FSeinSimulationContentManifestCodec::MaxContributors)
	{
		if (OutError)
		{
			*OutError =
				TEXT("Simulation-content registry exceeds its contributor bound.");
		}
		return MakeFailedClaimLease();
	}

	int32 NewDiscoveryClaims = 0;
	for (const FSeinSimulationContentDiscoveryRoot& Root :
		CanonicalDescriptor.DiscoveryRoots)
	{
		const FString Key = FoldAsciiCase(Root.RootClassPath);
		const FAggregatedDiscoveryRootClaim* Existing =
			State.DiscoveryClaims.Find(Key);
		if (!Existing)
		{
			++NewDiscoveryClaims;
			continue;
		}
		if (!(Existing->Root == Root))
		{
			if (OutError)
			{
				*OutError = FString::Printf(
					TEXT("Discovery root '%s' is already claimed with incompatible path casing or record semantics."),
					*Root.RootClassPath);
			}
			return MakeFailedClaimLease();
		}
	}
	if (NewDiscoveryClaims
		> MaxGlobalDiscoveryRoots - State.DiscoveryClaims.Num())
	{
		if (OutError)
		{
			*OutError =
				TEXT("Simulation-content registry exceeds its global unique discovery-root bound.");
		}
		return MakeFailedClaimLease();
	}

	int32 NewPackageClaims = 0;
	for (const FString& PackageRoot :
		CanonicalDescriptor.ExplicitPackageRoots)
	{
		const FString Key = FoldAsciiCase(PackageRoot);
		const FAggregatedPackageRootClaim* Existing =
			State.PackageClaims.Find(Key);
		if (!Existing)
		{
			++NewPackageClaims;
			continue;
		}
		if (Existing->PackagePath != PackageRoot)
		{
			if (OutError)
			{
				*OutError = FString::Printf(
					TEXT("Explicit package root '%s' collides with an existing claim by ASCII case."),
					*PackageRoot);
			}
			return MakeFailedClaimLease();
		}
	}
	if (NewPackageClaims
		> MaxGlobalExplicitPackageRoots - State.PackageClaims.Num())
	{
		if (OutError)
		{
			*OutError =
				TEXT("Simulation-content registry exceeds its global unique explicit-package-root bound.");
		}
		return MakeFailedClaimLease();
	}

	if (State.NextToken == 0 || State.NextToken == MAX_uint64)
	{
		if (OutError)
		{
			*OutError =
				TEXT("Simulation-content registration token space is exhausted.");
		}
		State.bUnleasedFailure = true;
		return {};
	}
	const uint64 Token = State.NextToken++;
	if (ExistingContributor)
	{
		ExistingContributor->Tokens.Add(Token);
		return FSeinSimulationContentRegistrationHandle(
			Token, true);
	}

	FRegisteredSimulationContentContributor& Added =
		State.Entries.AddDefaulted_GetRef();
	Added.Tokens.Add(Token);
	Added.Descriptor = MoveTemp(CanonicalDescriptor);
	const FSeinFrozenSimulationContentContributor& Registered =
		State.Entries.Last().Descriptor;
	for (const FSeinSimulationContentDiscoveryRoot& Root :
		Registered.DiscoveryRoots)
	{
		const FString Key = FoldAsciiCase(Root.RootClassPath);
		FAggregatedDiscoveryRootClaim& Claim =
			State.DiscoveryClaims.FindOrAdd(Key);
		if (Claim.RefCount == 0)
		{
			Claim.Root = Root;
		}
		++Claim.RefCount;
	}
	for (const FString& PackageRoot :
		Registered.ExplicitPackageRoots)
	{
		const FString Key = FoldAsciiCase(PackageRoot);
		FAggregatedPackageRootClaim& Claim =
			State.PackageClaims.FindOrAdd(Key);
		if (Claim.RefCount == 0)
		{
			Claim.PackagePath = PackageRoot;
		}
		++Claim.RefCount;
	}
	return FSeinSimulationContentRegistrationHandle(Token, true);
}

bool FSeinSimulationContentRegistry::CaptureSnapshot(
	FSeinSimulationContentRegistrySnapshot& OutSnapshot,
	FString& OutError)
{
	OutSnapshot = {};
	OutError.Reset();

	TArray<FSeinFrozenSimulationContentContributor> Contributors;
	TArray<FSeinSimulationContentDiscoveryRoot> RegisteredRoots;
	TArray<FString> RegisteredPackageRoots;
	{
		FSimulationContentRegistryState& State = GetRegistryState();
		FScopeLock Lock(&State.Mutex);
		if (State.bUnleasedFailure
			|| !State.FailedClaimTokens.IsEmpty())
		{
			OutError =
				TEXT("Simulation-content registry contains a failed live module-generation claim.");
			return false;
		}
		Contributors.Reserve(State.Entries.Num());
		for (const FRegisteredSimulationContentContributor& Entry :
			State.Entries)
		{
			if (Entry.Tokens.IsEmpty())
			{
				OutError =
					TEXT("Simulation-content registry contains a contributor without a live generation.");
				return false;
			}
			Contributors.Add(Entry.Descriptor);
		}
		RegisteredRoots.Reserve(State.DiscoveryClaims.Num());
		for (const TPair<FString, FAggregatedDiscoveryRootClaim>& Pair :
			State.DiscoveryClaims)
		{
			if (Pair.Value.RefCount <= 0)
			{
				OutError =
					TEXT("Simulation-content registry has an invalid discovery-root reference count.");
				return false;
			}
			RegisteredRoots.Add(Pair.Value.Root);
		}
		RegisteredPackageRoots.Reserve(State.PackageClaims.Num());
		for (const TPair<FString, FAggregatedPackageRootClaim>& Pair :
			State.PackageClaims)
		{
			if (Pair.Value.RefCount <= 0)
			{
				OutError =
					TEXT("Simulation-content registry has an invalid package-root reference count.");
				return false;
			}
			RegisteredPackageRoots.Add(Pair.Value.PackagePath);
		}
	}
	if (Contributors.Num()
		> FSeinSimulationContentManifestCodec::MaxContributors)
	{
		OutError =
			TEXT("Simulation-content registry exceeds its contributor bound.");
		return false;
	}
	if (Contributors.IsEmpty())
	{
		OutError =
			TEXT("Simulation-content registry has no contributors.");
		return false;
	}

	Contributors.Sort(
		[](const FSeinFrozenSimulationContentContributor& Left,
			const FSeinFrozenSimulationContentContributor& Right)
		{
			return CompareUtf8(
				Left.StableContributorId,
				Right.StableContributorId) < 0;
		});
	for (int32 Index = 1; Index < Contributors.Num(); ++Index)
	{
		if (Contributors[Index - 1].StableContributorId
			== Contributors[Index].StableContributorId)
		{
			OutError =
				TEXT("Simulation-content registry contains a duplicate contributor ID.");
			return false;
		}
	}

	TArray<FSeinSimulationContentDiscoveryRoot> CanonicalRoots;
	if (!CanonicalizeRoots(
		RegisteredRoots,
		MaxGlobalDiscoveryRoots,
		CanonicalRoots,
		OutError))
	{
		return false;
	}
	TArray<FString> CanonicalPackageRoots;
	if (!CanonicalizePackageRoots(
		RegisteredPackageRoots,
		MaxGlobalExplicitPackageRoots,
		CanonicalPackageRoots,
		OutError))
	{
		return false;
	}

	OutSnapshot.Contributors = MoveTemp(Contributors);
	OutSnapshot.DiscoveryRoots = MoveTemp(CanonicalRoots);
	OutSnapshot.ExplicitPackageRoots =
		MoveTemp(CanonicalPackageRoots);
	TArray<FSeinSimulationContentContributorRecord>
		IgnoredManifestContributors;
	if (!BuildManifestContributorRecords(
		OutSnapshot,
		IgnoredManifestContributors,
		OutError))
	{
		OutSnapshot = {};
		return false;
	}
	return true;
}

bool FSeinSimulationContentRegistry::BuildManifestContributorRecords(
	const FSeinSimulationContentRegistrySnapshot& Snapshot,
	TArray<FSeinSimulationContentContributorRecord>& OutContributors,
	FString& OutError)
{
	OutContributors.Reset();
	OutError.Reset();
	if (Snapshot.Contributors.Num()
		> FSeinSimulationContentManifestCodec::MaxContributors)
	{
		OutError =
			TEXT("Simulation-content snapshot exceeds its contributor bound.");
		return false;
	}

	TArray<FSeinSimulationContentDiscoveryRoot>
		CanonicalSnapshotRoots;
	if (!CanonicalizeRoots(
		Snapshot.DiscoveryRoots,
		MaxGlobalDiscoveryRoots,
		CanonicalSnapshotRoots,
		OutError)
		|| CanonicalSnapshotRoots != Snapshot.DiscoveryRoots)
	{
		if (OutError.IsEmpty())
		{
			OutError =
				TEXT("Simulation-content snapshot discovery roots are not canonical.");
		}
		return false;
	}
	TMap<FString, FSeinSimulationContentDiscoveryRoot>
		SnapshotRootByFoldedPath;
	for (const FSeinSimulationContentDiscoveryRoot& Root :
		CanonicalSnapshotRoots)
	{
		SnapshotRootByFoldedPath.Add(
			FoldAsciiCase(Root.RootClassPath),
			Root);
	}

	TArray<FString> CanonicalSnapshotPackageRoots;
	if (!CanonicalizePackageRoots(
		Snapshot.ExplicitPackageRoots,
		MaxGlobalExplicitPackageRoots,
		CanonicalSnapshotPackageRoots,
		OutError)
		|| CanonicalSnapshotPackageRoots
			!= Snapshot.ExplicitPackageRoots)
	{
		if (OutError.IsEmpty())
		{
			OutError =
				TEXT("Simulation-content snapshot explicit package roots are not canonical.");
		}
		return false;
	}
	TMap<FString, FString> SnapshotPackageByFoldedPath;
	for (const FString& PackageRoot :
		CanonicalSnapshotPackageRoots)
	{
		SnapshotPackageByFoldedPath.Add(
			FoldAsciiCase(PackageRoot),
			PackageRoot);
	}

	TArray<FSeinSimulationContentContributorRecord> Records;
	Records.Reserve(Snapshot.Contributors.Num());
	TSet<FString> ReferencedDiscoveryRoots;
	TSet<FString> ReferencedPackageRoots;
	for (const FSeinFrozenSimulationContentContributor& Contributor :
		Snapshot.Contributors)
	{
		FSeinSimulationContentContributorDescriptor InputDescriptor;
		InputDescriptor.StableContributorId =
			Contributor.StableContributorId;
		InputDescriptor.ContributorRevision =
			Contributor.ContributorRevision;
		InputDescriptor.DiscoveryRoots =
			Contributor.DiscoveryRoots;
		InputDescriptor.ExplicitPackageRoots =
			Contributor.ExplicitPackageRoots;
		FSeinFrozenSimulationContentContributor CanonicalContributor;
		if (!CanonicalizeDescriptor(
			InputDescriptor,
			CanonicalContributor,
			OutError)
			|| CanonicalContributor.StableContributorId
				!= Contributor.StableContributorId
			|| CanonicalContributor.ContributorRevision
				!= Contributor.ContributorRevision
			|| CanonicalContributor.DiscoveryContractDigest
				!= Contributor.DiscoveryContractDigest
			|| CanonicalContributor.DiscoveryRoots
				!= Contributor.DiscoveryRoots
			|| CanonicalContributor.ExplicitPackageRoots
				!= Contributor.ExplicitPackageRoots)
		{
			if (OutError.IsEmpty())
			{
				OutError =
					TEXT("Simulation-content snapshot contains a non-canonical or forged contributor contract.");
			}
			return false;
		}
		for (const FSeinSimulationContentDiscoveryRoot& Root :
			CanonicalContributor.DiscoveryRoots)
		{
			const FString Key =
				FoldAsciiCase(Root.RootClassPath);
			const FSeinSimulationContentDiscoveryRoot* GlobalRoot =
				SnapshotRootByFoldedPath.Find(Key);
			if (!GlobalRoot || !(*GlobalRoot == Root))
			{
				OutError =
					TEXT("Simulation-content contributor references a discovery root outside the snapshot union.");
				return false;
			}
			ReferencedDiscoveryRoots.Add(Key);
		}
		for (const FString& PackageRoot :
			CanonicalContributor.ExplicitPackageRoots)
		{
			const FString Key = FoldAsciiCase(PackageRoot);
			const FString* GlobalPackage =
				SnapshotPackageByFoldedPath.Find(Key);
			if (!GlobalPackage || *GlobalPackage != PackageRoot)
			{
				OutError =
					TEXT("Simulation-content contributor references an explicit package outside the snapshot union.");
				return false;
			}
			ReferencedPackageRoots.Add(Key);
		}

		FSeinSimulationContentContributorRecord& Record =
			Records.AddDefaulted_GetRef();
		Record.StableContributorId =
			CanonicalContributor.StableContributorId;
		if (CanonicalContributor.ContributorRevision > MAX_int32)
		{
			OutError =
				TEXT("Simulation-content contributor revision exceeds the manifest range.");
			return false;
		}
		Record.ContributorRevision =
			static_cast<int32>(
				CanonicalContributor.ContributorRevision);
		Record.DiscoveryContractDigest =
			CanonicalContributor.DiscoveryContractDigest;
	}

	if (ReferencedDiscoveryRoots.Num()
			!= SnapshotRootByFoldedPath.Num()
		|| ReferencedPackageRoots.Num()
			!= SnapshotPackageByFoldedPath.Num())
	{
		OutError =
			TEXT("Simulation-content snapshot root unions contain unclaimed entries.");
		return false;
	}

	TArray<FSeinSimulationContentContributorRecord>
		CanonicalContributors;
	TArray<FSeinSimulationContentRecord> IgnoredRecords;
	if (!FSeinSimulationContentManifestCodec::Canonicalize(
		Records,
		TConstArrayView<FSeinSimulationContentRecord>(),
		CanonicalContributors,
		IgnoredRecords,
		OutError))
	{
		return false;
	}
	OutContributors = MoveTemp(CanonicalContributors);
	return true;
}

int32 FSeinSimulationContentRegistry::GetRegisteredContributorCount()
{
	FSimulationContentRegistryState& State = GetRegistryState();
	FScopeLock Lock(&State.Mutex);
	return State.Entries.Num();
}

void FSeinSimulationContentRegistry::UnregisterContributor(uint64 Token)
{
	if (Token == 0)
	{
		return;
	}
	FSimulationContentRegistryState& State = GetRegistryState();
	FScopeLock Lock(&State.Mutex);
	if (State.FailedClaimTokens.Remove(Token) == 1)
	{
		return;
	}
	const int32 EntryIndex = State.Entries.IndexOfByPredicate(
		[Token](const FRegisteredSimulationContentContributor& Entry)
		{
			return Entry.Tokens.Contains(Token);
		});
	if (EntryIndex == INDEX_NONE)
	{
		return;
	}

	FRegisteredSimulationContentContributor& Entry =
		State.Entries[EntryIndex];
	const int32 RemovedClaims = Entry.Tokens.RemoveSingle(Token);
	check(RemovedClaims == 1);
	if (!Entry.Tokens.IsEmpty())
	{
		return;
	}

	const FSeinFrozenSimulationContentContributor& Descriptor =
		Entry.Descriptor;
	for (const FSeinSimulationContentDiscoveryRoot& Root :
		Descriptor.DiscoveryRoots)
	{
		const FString Key = FoldAsciiCase(Root.RootClassPath);
		if (FAggregatedDiscoveryRootClaim* Claim =
			State.DiscoveryClaims.Find(Key))
		{
			--Claim->RefCount;
			if (Claim->RefCount <= 0)
			{
				State.DiscoveryClaims.Remove(Key);
			}
		}
	}
	for (const FString& PackageRoot :
		Descriptor.ExplicitPackageRoots)
	{
		const FString Key = FoldAsciiCase(PackageRoot);
		if (FAggregatedPackageRootClaim* Claim =
			State.PackageClaims.Find(Key))
		{
			--Claim->RefCount;
			if (Claim->RefCount <= 0)
			{
				State.PackageClaims.Remove(Key);
			}
		}
	}
	State.Entries.RemoveAtSwap(
		EntryIndex,
		1,
		EAllowShrinking::No);
}
