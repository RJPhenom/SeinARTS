/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSimulationContentManifest.cpp
 */

#include "Serialization/SeinSimulationContentManifest.h"

#include "Misc/PackageName.h"
#include "Serialization/SeinCanonicalInitialStateDigest.h"

namespace
{
	constexpr TCHAR RecordDigestDomain[] =
		TEXT("SeinARTS.SimulationContent.Record");
	constexpr TCHAR ManifestRootDomain[] =
		TEXT("SeinARTS.SimulationContent.Manifest");

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

	bool ValidateCanonicalRecordId(
		const FString& Value,
		FString& OutError)
	{
		if (Value.IsEmpty()
			|| Value.Len()
				> FSeinSimulationContentManifestCodec::
					MaxCanonicalRecordIdCharacters
			|| Value.Len() != FCString::Strlen(*Value)
			|| !FPackageName::IsValidLongPackageName(Value))
		{
			OutError =
				TEXT("Simulation-content v1 record IDs must be canonical long package names.");
			return false;
		}
		return true;
	}

	int32 CompareContributorSets(
		TConstArrayView<FSeinSimulationContentContributorRecord> Left,
		TConstArrayView<FSeinSimulationContentContributorRecord> Right)
	{
		const int32 SharedCount =
			Left.Num() < Right.Num() ? Left.Num() : Right.Num();
		for (int32 Index = 0; Index < SharedCount; ++Index)
		{
			const int32 IdOrder = CompareUtf8(
				Left[Index].StableContributorId,
				Right[Index].StableContributorId);
			if (IdOrder != 0)
			{
				return IdOrder;
			}
			if (Left[Index].ContributorRevision
				!= Right[Index].ContributorRevision)
			{
				return Left[Index].ContributorRevision
						< Right[Index].ContributorRevision
					? -1
					: 1;
			}
			if (Left[Index].DiscoveryContractDigest
				!= Right[Index].DiscoveryContractDigest)
			{
				const FGuid& LeftDigest =
					Left[Index].DiscoveryContractDigest;
				const FGuid& RightDigest =
					Right[Index].DiscoveryContractDigest;
				if (LeftDigest.A != RightDigest.A)
				{
					return LeftDigest.A < RightDigest.A ? -1 : 1;
				}
				if (LeftDigest.B != RightDigest.B)
				{
					return LeftDigest.B < RightDigest.B ? -1 : 1;
				}
				if (LeftDigest.C != RightDigest.C)
				{
					return LeftDigest.C < RightDigest.C ? -1 : 1;
				}
				return LeftDigest.D < RightDigest.D ? -1 : 1;
			}
		}
		return Left.Num() < Right.Num()
			? -1
			: Left.Num() > Right.Num() ? 1 : 0;
	}

	bool WriteCanonicalRoot(
		uint32 BuilderRevision,
		TConstArrayView<FSeinSimulationContentContributorRecord> Contributors,
		TConstArrayView<FSeinSimulationContentRecord> Records,
		FGuid& OutRootDigest,
		FString& OutError)
	{
		OutRootDigest.Invalidate();
		FSeinCanonicalDigestWriter Writer(
			ManifestRootDomain,
			FSeinSimulationContentManifestCodec::CurrentFormatVersion);
		if (!Writer.WriteUInt32(BuilderRevision)
			|| !Writer.WriteUInt32(
				static_cast<uint32>(Contributors.Num())))
		{
			OutError = Writer.GetError();
			return false;
		}

		for (const FSeinSimulationContentContributorRecord& Contributor :
			Contributors)
		{
			if (!Writer.WriteString(Contributor.StableContributorId)
				|| !Writer.WriteUInt32(
					static_cast<uint32>(
						Contributor.ContributorRevision))
				|| !Writer.WriteGuid(
					Contributor.DiscoveryContractDigest))
			{
				OutError = Writer.GetError();
				return false;
			}
		}

		if (!Writer.WriteUInt32(static_cast<uint32>(Records.Num())))
		{
			OutError = Writer.GetError();
			return false;
		}
		for (const FSeinSimulationContentRecord& Record : Records)
		{
			if (!Writer.WriteString(Record.StableRecordKindId)
				|| !Writer.WriteUInt32(
					static_cast<uint32>(Record.RecordRevision))
				|| !Writer.WriteString(Record.CanonicalRecordId)
				|| !Writer.WriteGuid(Record.ContentDigest))
			{
				OutError = Writer.GetError();
				return false;
			}
		}
		return Writer.Finalize(OutRootDigest, OutError);
	}
}

bool USeinSimulationContentManifest::Validate(FString& OutError) const
{
	return FSeinSimulationContentManifestCodec::ValidateContainer(
		*this,
		OutError);
}

bool FSeinSimulationContentManifestCodec::CanonicalizeStableId(
	const FString& StableId,
	FString& OutCanonicalId,
	FString& OutError)
{
	const FString InputId = StableId;
	OutCanonicalId.Reset();
	OutError.Reset();
	if (InputId.IsEmpty()
		|| InputId.Len() > MaxStableIdCharacters
		|| InputId.Len() != FCString::Strlen(*InputId))
	{
		OutError =
			TEXT("Stable simulation-content IDs must be non-empty, bounded ASCII strings.");
		return false;
	}

	OutCanonicalId = FoldAsciiCase(InputId);
	for (const TCHAR Character : OutCanonicalId)
	{
		const bool bValid =
			(Character >= TEXT('a') && Character <= TEXT('z'))
			|| (Character >= TEXT('0') && Character <= TEXT('9'))
			|| Character == TEXT('.')
			|| Character == TEXT('_')
			|| Character == TEXT('-');
		if (!bValid)
		{
			OutCanonicalId.Reset();
			OutError =
				TEXT("Stable simulation-content IDs may contain only ASCII letters, digits, '.', '_', and '-'.");
			return false;
		}
	}
	return true;
}

bool FSeinSimulationContentManifestCodec::ComputeRecordDigest(
	const FString& StableRecordKindId,
	uint32 RecordRevision,
	const FString& CanonicalRecordId,
	TConstArrayView<uint8> CanonicalPayload,
	FGuid& OutDigest,
	FString& OutError)
{
	OutDigest.Invalidate();
	OutError.Reset();

	FString CanonicalKindId;
	if (RecordRevision != CurrentRecordRevision
		|| CanonicalPayload.Num() != SavedPackageHashBytes
		|| !CanonicalizeStableId(
			StableRecordKindId,
			CanonicalKindId,
			OutError)
		|| !ValidateCanonicalRecordId(CanonicalRecordId, OutError))
	{
		if (OutError.IsEmpty())
		{
			OutError =
				TEXT("Simulation-content record revision or saved-package hash width does not match the v1 codec.");
		}
		return false;
	}
	bool bHasSavedHashByte = false;
	for (const uint8 Byte : CanonicalPayload)
	{
		bHasSavedHashByte |= Byte != 0;
	}
	if (!bHasSavedHashByte)
	{
		OutError =
			TEXT("Simulation-content records reject the invalid all-zero saved-package hash.");
		return false;
	}
	if (CanonicalKindId != GetCurrentRecordKindId())
	{
		OutError = FString::Printf(
			TEXT("Simulation-content format v1 supports only record kind '%s'."),
			GetCurrentRecordKindId());
		return false;
	}

	FSeinCanonicalDigestWriter Writer(
		RecordDigestDomain,
		CurrentFormatVersion);
	if (!Writer.WriteString(CanonicalKindId)
		|| !Writer.WriteUInt32(RecordRevision)
		|| !Writer.WriteString(CanonicalRecordId)
		|| !Writer.WriteBytes(CanonicalPayload))
	{
		OutError = Writer.GetError();
		return false;
	}
	return Writer.Finalize(OutDigest, OutError);
}

bool FSeinSimulationContentManifestCodec::Canonicalize(
	TConstArrayView<FSeinSimulationContentContributorRecord> Contributors,
	TConstArrayView<FSeinSimulationContentRecord> Records,
	TArray<FSeinSimulationContentContributorRecord>& OutContributors,
	TArray<FSeinSimulationContentRecord>& OutRecords,
	FString& OutError)
{
	OutContributors.Reset();
	OutRecords.Reset();
	OutError.Reset();
	if (Contributors.IsEmpty()
		|| Contributors.Num() > MaxContributors
		|| Records.Num() > MaxRecords)
	{
		OutError =
			TEXT("Simulation-content profiles require contributors and bounded record counts.");
		return false;
	}

	TArray<FSeinSimulationContentContributorRecord> CanonicalContributors;
	CanonicalContributors.Reserve(Contributors.Num());
	for (const FSeinSimulationContentContributorRecord& Contributor :
		Contributors)
	{
		FSeinSimulationContentContributorRecord Canonical = Contributor;
		if (Canonical.ContributorRevision <= 0
			|| !Canonical.DiscoveryContractDigest.IsValid()
			|| !CanonicalizeStableId(
				Contributor.StableContributorId,
				Canonical.StableContributorId,
				OutError))
		{
			if (OutError.IsEmpty())
			{
				OutError =
					TEXT("Simulation-content contributors require a positive revision and valid discovery-contract digest.");
			}
			return false;
		}
		CanonicalContributors.Add(MoveTemp(Canonical));
	}
	CanonicalContributors.Sort(
		[](const FSeinSimulationContentContributorRecord& Left,
			const FSeinSimulationContentContributorRecord& Right)
		{
			return CompareUtf8(
				Left.StableContributorId,
				Right.StableContributorId) < 0;
		});
	for (int32 Index = 1; Index < CanonicalContributors.Num(); ++Index)
	{
		if (CanonicalContributors[Index - 1].StableContributorId
			== CanonicalContributors[Index].StableContributorId)
		{
			OutError = FString::Printf(
				TEXT("Duplicate simulation-content contributor ID '%s'."),
				*CanonicalContributors[Index].StableContributorId);
			return false;
		}
	}

	TArray<FSeinSimulationContentRecord> SortedRecords;
	SortedRecords.Reserve(Records.Num());
	for (const FSeinSimulationContentRecord& Record : Records)
	{
		FSeinSimulationContentRecord Canonical = Record;
		if (Canonical.RecordRevision
				!= static_cast<int32>(CurrentRecordRevision)
			|| !Canonical.ContentDigest.IsValid()
			|| !CanonicalizeStableId(
				Record.StableRecordKindId,
				Canonical.StableRecordKindId,
				OutError)
			|| !ValidateCanonicalRecordId(
				Canonical.CanonicalRecordId,
				OutError))
		{
			if (OutError.IsEmpty())
			{
				OutError =
					TEXT("Simulation-content records require the v1 saved-package revision and a valid content digest.");
			}
			return false;
		}
		if (Canonical.StableRecordKindId
			!= GetCurrentRecordKindId())
		{
			OutError = FString::Printf(
				TEXT("Simulation-content format v1 supports only record kind '%s'."),
				GetCurrentRecordKindId());
			return false;
		}
		SortedRecords.Add(MoveTemp(Canonical));
	}
	SortedRecords.Sort(
		[](const FSeinSimulationContentRecord& Left,
			const FSeinSimulationContentRecord& Right)
		{
			const int32 KindOrder = CompareUtf8(
				Left.StableRecordKindId,
				Right.StableRecordKindId);
			return KindOrder != 0
				? KindOrder < 0
				: CompareUtf8(
					Left.CanonicalRecordId,
					Right.CanonicalRecordId) < 0;
		});

	TArray<FSeinSimulationContentRecord> CanonicalRecords;
	CanonicalRecords.Reserve(SortedRecords.Num());
	TMap<FString, FString> CaseFoldedRecordIds;
	for (FSeinSimulationContentRecord& Record : SortedRecords)
	{
		FString CaseFoldedKey = Record.StableRecordKindId;
		CaseFoldedKey.AppendChar(TCHAR(0x1f));
		CaseFoldedKey += FoldAsciiCase(Record.CanonicalRecordId);
		if (const FString* ExistingId =
			CaseFoldedRecordIds.Find(CaseFoldedKey))
		{
			if (*ExistingId != Record.CanonicalRecordId)
			{
				OutError = FString::Printf(
					TEXT("Case-colliding simulation-content record IDs '%s' and '%s'."),
					**ExistingId,
					*Record.CanonicalRecordId);
				return false;
			}
		}
		else
		{
			CaseFoldedRecordIds.Add(
				CaseFoldedKey,
				Record.CanonicalRecordId);
		}

		if (!CanonicalRecords.IsEmpty())
		{
			const FSeinSimulationContentRecord& Previous =
				CanonicalRecords.Last();
			const bool bSameKey =
				Previous.StableRecordKindId
					== Record.StableRecordKindId
				&& Previous.CanonicalRecordId
					== Record.CanonicalRecordId;
			if (bSameKey)
			{
				if (Previous.RecordRevision != Record.RecordRevision
					|| Previous.ContentDigest != Record.ContentDigest)
				{
					OutError = FString::Printf(
						TEXT("Conflicting simulation-content record claim '%s:%s'."),
						*Record.StableRecordKindId,
						*Record.CanonicalRecordId);
					return false;
				}
				continue;
			}
		}
		CanonicalRecords.Add(MoveTemp(Record));
	}

	OutContributors = MoveTemp(CanonicalContributors);
	OutRecords = MoveTemp(CanonicalRecords);
	return true;
}

bool FSeinSimulationContentManifestCodec::ComputeRootDigest(
	uint32 FormatVersion,
	uint32 BuilderRevision,
	TConstArrayView<FSeinSimulationContentContributorRecord> Contributors,
	TConstArrayView<FSeinSimulationContentRecord> Records,
	FGuid& OutRootDigest,
	FString& OutError)
{
	OutRootDigest.Invalidate();
	OutError.Reset();
	if (FormatVersion != CurrentFormatVersion
		|| BuilderRevision != CurrentBuilderRevision)
	{
		OutError =
			TEXT("Simulation-content roots require the exact supported format and builder revision.");
		return false;
	}

	TArray<FSeinSimulationContentContributorRecord> CanonicalContributors;
	TArray<FSeinSimulationContentRecord> CanonicalRecords;
	if (!Canonicalize(
		Contributors,
		Records,
		CanonicalContributors,
		CanonicalRecords,
		OutError))
	{
		return false;
	}
	return WriteCanonicalRoot(
		BuilderRevision,
		CanonicalContributors,
		CanonicalRecords,
		OutRootDigest,
		OutError);
}

bool FSeinSimulationContentManifestCodec::SealProfile(
	uint32 FormatVersion,
	FSeinSimulationContentManifestProfile& Profile,
	FString& OutError)
{
	Profile.RootDigest.Invalidate();
	OutError.Reset();
	if (FormatVersion != CurrentFormatVersion
		|| Profile.BuilderRevision
			!= static_cast<int32>(CurrentBuilderRevision))
	{
		OutError =
			TEXT("Cannot seal a simulation-content profile with an unsupported format or builder revision.");
		return false;
	}

	TArray<FSeinSimulationContentContributorRecord> CanonicalContributors;
	TArray<FSeinSimulationContentRecord> CanonicalRecords;
	if (!Canonicalize(
		Profile.Contributors,
		Profile.Records,
		CanonicalContributors,
		CanonicalRecords,
		OutError))
	{
		return false;
	}

	FGuid RootDigest;
	if (!WriteCanonicalRoot(
		static_cast<uint32>(Profile.BuilderRevision),
		CanonicalContributors,
		CanonicalRecords,
		RootDigest,
		OutError))
	{
		return false;
	}

	Profile.Contributors = MoveTemp(CanonicalContributors);
	Profile.Records = MoveTemp(CanonicalRecords);
	Profile.RootDigest = RootDigest;
	return true;
}

bool FSeinSimulationContentManifestCodec::ValidateProfile(
	uint32 FormatVersion,
	const FSeinSimulationContentManifestProfile& Profile,
	FString& OutError)
{
	OutError.Reset();
	if (!Profile.RootDigest.IsValid()
		|| FormatVersion != CurrentFormatVersion
		|| Profile.BuilderRevision
			!= static_cast<int32>(CurrentBuilderRevision))
	{
		OutError =
			TEXT("Simulation-content profile header or stored root is invalid.");
		return false;
	}

	TArray<FSeinSimulationContentContributorRecord> CanonicalContributors;
	TArray<FSeinSimulationContentRecord> CanonicalRecords;
	if (!Canonicalize(
		Profile.Contributors,
		Profile.Records,
		CanonicalContributors,
		CanonicalRecords,
		OutError))
	{
		return false;
	}
	if (Profile.Contributors != CanonicalContributors
		|| Profile.Records != CanonicalRecords)
	{
		OutError =
			TEXT("Simulation-content profile records are not stored canonically.");
		return false;
	}

	FGuid ExpectedRoot;
	if (!WriteCanonicalRoot(
		static_cast<uint32>(Profile.BuilderRevision),
		CanonicalContributors,
		CanonicalRecords,
		ExpectedRoot,
		OutError))
	{
		return false;
	}
	if (ExpectedRoot != Profile.RootDigest)
	{
		OutError =
			TEXT("Simulation-content profile root does not match its canonical records.");
		return false;
	}
	return true;
}

bool FSeinSimulationContentManifestCodec::UpsertProfile(
	USeinSimulationContentManifest& Manifest,
	const FSeinSimulationContentManifestProfile& Profile,
	FString& OutError)
{
	OutError.Reset();
	if (Manifest.FormatVersion
		!= static_cast<int32>(CurrentFormatVersion))
	{
		OutError =
			TEXT("Cannot update a simulation-content container with an unsupported format.");
		return false;
	}
	if (Manifest.Profiles.Num() > MaxProfiles)
	{
		OutError =
			TEXT("Simulation-content container exceeds its bounded profile count.");
		return false;
	}

	FSeinSimulationContentManifestProfile SealedProfile = Profile;
	if (!SealProfile(
		CurrentFormatVersion,
		SealedProfile,
		OutError))
	{
		return false;
	}

	TArray<FSeinSimulationContentManifestProfile> UpdatedProfiles;
	UpdatedProfiles.Reserve(
		Manifest.Profiles.Num()
			+ (Manifest.Profiles.Num() < MaxProfiles ? 1 : 0));
	bool bReplaced = false;
	int64 TotalRecords = SealedProfile.Records.Num();
	for (const FSeinSimulationContentManifestProfile& Existing :
		Manifest.Profiles)
	{
		TArray<FSeinSimulationContentContributorRecord>
			CanonicalExistingContributors;
		TArray<FSeinSimulationContentRecord> IgnoredRecords;
		if (!Canonicalize(
			Existing.Contributors,
			TConstArrayView<FSeinSimulationContentRecord>(),
			CanonicalExistingContributors,
			IgnoredRecords,
			OutError))
		{
			return false;
		}

		if (CompareContributorSets(
			CanonicalExistingContributors,
			SealedProfile.Contributors) == 0)
		{
			if (bReplaced)
			{
				OutError =
					TEXT("Simulation-content container has duplicate profiles for one contributor set.");
				return false;
			}
			UpdatedProfiles.Add(SealedProfile);
			bReplaced = true;
			continue;
		}

		if (!ValidateProfile(
			CurrentFormatVersion,
			Existing,
			OutError))
		{
			return false;
		}
		TotalRecords += Existing.Records.Num();
		if (TotalRecords > MaxTotalRecords)
		{
			OutError =
				TEXT("Simulation-content container exceeds its total record bound.");
			return false;
		}
		UpdatedProfiles.Add(Existing);
	}

	if (!bReplaced)
	{
		if (UpdatedProfiles.Num() >= MaxProfiles)
		{
			OutError =
				TEXT("Simulation-content container has no room for another contributor-set profile.");
			return false;
		}
		UpdatedProfiles.Add(MoveTemp(SealedProfile));
	}
	UpdatedProfiles.Sort(
		[](const FSeinSimulationContentManifestProfile& Left,
			const FSeinSimulationContentManifestProfile& Right)
		{
			return CompareContributorSets(
				Left.Contributors,
				Right.Contributors) < 0;
		});

	Manifest.Profiles = MoveTemp(UpdatedProfiles);
	return true;
}

bool FSeinSimulationContentManifestCodec::SelectExactProfile(
	const USeinSimulationContentManifest& Manifest,
	uint32 ExpectedBuilderRevision,
	TConstArrayView<FSeinSimulationContentContributorRecord>
		ActiveContributors,
	FSeinSimulationContentManifestProfile& OutProfile,
	FString& OutError)
{
	OutProfile = {};
	OutError.Reset();
	if (Manifest.FormatVersion
			!= static_cast<int32>(CurrentFormatVersion)
		|| ExpectedBuilderRevision != CurrentBuilderRevision
		|| Manifest.Profiles.IsEmpty()
		|| Manifest.Profiles.Num() > MaxProfiles)
	{
		OutError =
			TEXT("Simulation-content container header or profile count is invalid.");
		return false;
	}

	TArray<FSeinSimulationContentContributorRecord>
		CanonicalActiveContributors;
	TArray<FSeinSimulationContentRecord> IgnoredRecords;
	if (!Canonicalize(
		ActiveContributors,
		TConstArrayView<FSeinSimulationContentRecord>(),
		CanonicalActiveContributors,
		IgnoredRecords,
		OutError))
	{
		return false;
	}

	const FSeinSimulationContentManifestProfile* Match = nullptr;
	int64 TotalRecords = 0;
	for (const FSeinSimulationContentManifestProfile& Profile :
		Manifest.Profiles)
	{
		TotalRecords += Profile.Records.Num();
		if (TotalRecords > MaxTotalRecords)
		{
			OutError =
				TEXT("Simulation-content container exceeds its total record bound.");
			return false;
		}

		TArray<FSeinSimulationContentContributorRecord>
			CanonicalProfileContributors;
		if (!Canonicalize(
			Profile.Contributors,
			TConstArrayView<FSeinSimulationContentRecord>(),
			CanonicalProfileContributors,
			IgnoredRecords,
			OutError))
		{
			return false;
		}
		if (CompareContributorSets(
			CanonicalProfileContributors,
			CanonicalActiveContributors) != 0)
		{
			continue;
		}
		if (Profile.BuilderRevision
			!= static_cast<int32>(ExpectedBuilderRevision))
		{
			OutError =
				TEXT("The profile for the active contributor set was generated by a different builder revision.");
			return false;
		}
		if (Match)
		{
			OutError =
				TEXT("Simulation-content container has ambiguous duplicate profiles for the active contributor set.");
			return false;
		}
		Match = &Profile;
	}

	if (!Match)
	{
		OutError =
			TEXT("Simulation-content container has no exact profile for the active contributor set.");
		return false;
	}
	if (!ValidateProfile(
		CurrentFormatVersion,
		*Match,
		OutError))
	{
		return false;
	}
	OutProfile = *Match;
	return true;
}

bool FSeinSimulationContentManifestCodec::ValidateContainer(
	const USeinSimulationContentManifest& Manifest,
	FString& OutError)
{
	OutError.Reset();
	if (Manifest.FormatVersion
			!= static_cast<int32>(CurrentFormatVersion)
		|| Manifest.Profiles.IsEmpty()
		|| Manifest.Profiles.Num() > MaxProfiles)
	{
		OutError =
			TEXT("Simulation-content container header or profile count is invalid.");
		return false;
	}

	int64 TotalRecords = 0;
	for (const FSeinSimulationContentManifestProfile& Profile :
		Manifest.Profiles)
	{
		if (!ValidateProfile(
			CurrentFormatVersion,
			Profile,
			OutError))
		{
			return false;
		}
		TotalRecords += Profile.Records.Num();
		if (TotalRecords > MaxTotalRecords)
		{
			OutError =
				TEXT("Simulation-content container exceeds its total record bound.");
			return false;
		}
	}

	TArray<FSeinSimulationContentManifestProfile> SortedProfiles =
		Manifest.Profiles;
	SortedProfiles.Sort(
		[](const FSeinSimulationContentManifestProfile& Left,
			const FSeinSimulationContentManifestProfile& Right)
		{
			return CompareContributorSets(
				Left.Contributors,
				Right.Contributors) < 0;
		});
	for (int32 Index = 1; Index < SortedProfiles.Num(); ++Index)
	{
		if (CompareContributorSets(
			SortedProfiles[Index - 1].Contributors,
			SortedProfiles[Index].Contributors) == 0)
		{
			OutError =
				TEXT("Simulation-content container has duplicate contributor-set profiles.");
			return false;
		}
	}
	if (Manifest.Profiles != SortedProfiles)
	{
		OutError =
			TEXT("Simulation-content profiles are not stored canonically.");
		return false;
	}
	return true;
}
