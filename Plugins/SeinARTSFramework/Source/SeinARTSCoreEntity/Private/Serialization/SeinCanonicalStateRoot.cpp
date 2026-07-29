#include "Serialization/SeinCanonicalStateRoot.h"

#include "Serialization/SeinCanonicalInitialStateDigest.h"

namespace
{
	bool Fail(FString& OutError, const FString& Message)
	{
		OutError = Message;
		return false;
	}

	bool IsAlphaNumericASCII(const TCHAR Character)
	{
		return (Character >= TEXT('a') && Character <= TEXT('z'))
			|| (Character >= TEXT('0') && Character <= TEXT('9'));
	}

	bool ValidateSectionId(
		const FString& SectionId,
		FString& OutError)
	{
		if (SectionId.IsEmpty()
			|| SectionId.Len()
				> static_cast<int32>(
					FSeinCanonicalStateRootComposer::MaxSectionIdBytes)
			|| !IsAlphaNumericASCII(SectionId[0])
			|| !IsAlphaNumericASCII(SectionId[SectionId.Len() - 1]))
		{
			return Fail(
				OutError,
				TEXT("Canonical state root section ID length or boundary is invalid."));
		}

		for (const TCHAR Character : SectionId)
		{
			const bool bAllowed = IsAlphaNumericASCII(Character)
				|| Character == TEXT('.')
				|| Character == TEXT('_')
				|| Character == TEXT('/')
				|| Character == TEXT('-');
			if (!bAllowed)
			{
				return Fail(
					OutError,
					TEXT("Canonical state root section ID violates the lowercase-ASCII contract."));
			}
		}
		return true;
	}

	bool IsKnownRole(const ESeinSnapshotSectionRole Role)
	{
		switch (Role)
		{
		case ESeinSnapshotSectionRole::Authoritative:
		case ESeinSnapshotSectionRole::Continuation:
		case ESeinSnapshotSectionRole::DerivedCache:
		case ESeinSnapshotSectionRole::Local:
			return true;
		default:
			return false;
		}
	}

	bool ContributesToRoot(const ESeinSnapshotSectionRole Role)
	{
		return Role == ESeinSnapshotSectionRole::Authoritative
			|| Role == ESeinSnapshotSectionRole::Continuation;
	}

	bool ValidateLeaf(
		const FSeinCanonicalStateRootLeaf& Leaf,
		FString& OutError)
	{
		if (!ValidateSectionId(Leaf.SectionId, OutError))
		{
			return false;
		}
		if (!IsKnownRole(Leaf.Role))
		{
			return Fail(
				OutError,
				TEXT("Canonical state root section role is unknown."));
		}
		if (Leaf.Codec != ESeinSnapshotSectionCodec::CanonicalBytes)
		{
			return Fail(
				OutError,
				TEXT("Canonical state root section codec is unknown or compressed."));
		}
		if (Leaf.SchemaVersion == 0
			|| !Leaf.SchemaDigest.IsValid()
			|| !Leaf.DescriptorDigest.IsValid()
			|| !Leaf.LeafDigest.IsValid())
		{
			return Fail(
				OutError,
				TEXT("Canonical state root section digest or schema contract is invalid."));
		}
		if (Leaf.PayloadBytes
			> FSeinCanonicalStateRootComposer::MaxSectionPayloadBytes)
		{
			return Fail(
				OutError,
				TEXT("Canonical state root section payload exceeds its bound."));
		}
		return true;
	}

	bool WriteLeaf(
		FSeinCanonicalDigestWriter& Writer,
		const FSeinCanonicalStateRootLeaf& Leaf)
	{
		return Writer.WriteString(Leaf.SectionId)
			&& Writer.WriteUInt8(static_cast<uint8>(Leaf.Role))
			&& Writer.WriteUInt8(static_cast<uint8>(Leaf.Codec))
			&& Writer.WriteUInt32(Leaf.SchemaVersion)
			&& Writer.WriteGuid(Leaf.SchemaDigest)
			&& Writer.WriteGuid(Leaf.DescriptorDigest)
			&& Writer.WriteUInt64(Leaf.PayloadBytes)
			&& Writer.WriteGuid(Leaf.LeafDigest);
	}
}

bool FSeinCanonicalStateRootComposer::Compose(
	const FSeinCanonicalStateRootIdentity& Identity,
	TConstArrayView<FSeinCanonicalStateRootLeaf> Leaves,
	FGuid& OutRoot,
	FString& OutError)
{
	FString CandidateError;
	if (Identity.Tick < 0
		|| !Identity.CommandProtocolDigest.IsValid()
		|| !Identity.CompatibilityDigest.IsValid())
	{
		return Fail(
			OutError,
			TEXT("Canonical state root requires a non-negative tick and valid world identity digests."));
	}
	if (Leaves.Num() > static_cast<int32>(MaxSections))
	{
		return Fail(
			OutError,
			TEXT("Canonical state root section count exceeds its bound."));
	}

	TArray<const FSeinCanonicalStateRootLeaf*> CanonicalLeaves;
	CanonicalLeaves.Reserve(Leaves.Num());
	for (const FSeinCanonicalStateRootLeaf& Leaf : Leaves)
	{
		if (!ValidateLeaf(Leaf, CandidateError))
		{
			OutError = MoveTemp(CandidateError);
			return false;
		}
		CanonicalLeaves.Add(&Leaf);
	}
	CanonicalLeaves.Sort(
		[](const FSeinCanonicalStateRootLeaf& A,
			const FSeinCanonicalStateRootLeaf& B)
		{
			return A.SectionId.Compare(
				B.SectionId, ESearchCase::CaseSensitive) < 0;
		});
	for (int32 Index = 1; Index < CanonicalLeaves.Num(); ++Index)
	{
		if (CanonicalLeaves[Index - 1]->SectionId
			== CanonicalLeaves[Index]->SectionId)
		{
			return Fail(
				OutError,
				TEXT("Canonical state root section IDs must be unique."));
		}
	}

	uint32 ContributingLeaves = 0;
	for (const FSeinCanonicalStateRootLeaf* Leaf : CanonicalLeaves)
	{
		ContributingLeaves += ContributesToRoot(Leaf->Role) ? 1u : 0u;
	}

	FSeinCanonicalDigestWriter Writer(
		TEXT("SeinARTS.LiveWorldStateRoot"),
		CurrentFormatVersion);
	if (!Writer.WriteInt64(Identity.Tick)
		|| !Writer.WriteGuid(Identity.CommandProtocolDigest)
		|| !Writer.WriteGuid(Identity.CompatibilityDigest)
		|| !Writer.WriteUInt32(ContributingLeaves))
	{
		return Fail(OutError, Writer.GetError());
	}
	for (const FSeinCanonicalStateRootLeaf* Leaf : CanonicalLeaves)
	{
		if (ContributesToRoot(Leaf->Role)
			&& !WriteLeaf(Writer, *Leaf))
		{
			return Fail(OutError, Writer.GetError());
		}
	}

	FGuid CandidateRoot;
	if (!Writer.Finalize(CandidateRoot, CandidateError))
	{
		OutError = MoveTemp(CandidateError);
		return false;
	}
	OutRoot = CandidateRoot;
	OutError.Reset();
	return true;
}
