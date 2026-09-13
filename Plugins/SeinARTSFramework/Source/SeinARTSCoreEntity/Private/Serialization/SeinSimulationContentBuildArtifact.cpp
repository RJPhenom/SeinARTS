/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinSimulationContentBuildArtifact.cpp
 * @author       RJ Macklem
 * @created      6 Sep 2026
 * @latest       6 Sep 2026
 * @brief        Encodes and validates build-owned compatibility data with bounded, endian-stable framing.
 * @disclaimer   Generated with assistance from an AI language model.
 */
#include "Serialization/SeinSimulationContentBuildArtifact.h"
#include "HAL/FileManager.h"
#include "Serialization/Archive.h"

namespace
{
	constexpr uint32 Magic = 0x5341434d;
	constexpr uint32 Version = 1;
	void WriteUInt(TArray<uint8>& Bytes, uint32 Value)
	{
		for (int32 Shift = 24; Shift >= 0; Shift -= 8) Bytes.Add(static_cast<uint8>(Value >> Shift));
	}
	void WriteGuid(TArray<uint8>& Bytes, const FGuid& Value)
	{
		WriteUInt(Bytes, Value.A); WriteUInt(Bytes, Value.B);
		WriteUInt(Bytes, Value.C); WriteUInt(Bytes, Value.D);
	}
	void WriteString(TArray<uint8>& Bytes, const FString& Value)
	{
		const FTCHARToUTF8 Utf8(*Value, Value.Len());
		WriteUInt(Bytes, Utf8.Length());
		Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	}
	struct FReader
	{
		TConstArrayView<uint8> Bytes;
		int32 Offset = 0;
		bool UInt(uint32& Out)
		{
			if (Bytes.Num() - Offset < 4) return false;
			Out = 0;
			for (int32 I = 0; I < 4; ++I) Out = (Out << 8) | Bytes[Offset++];
			return true;
		}
		bool PositiveInt(int32& Out)
		{
			uint32 Value;
			if (!UInt(Value) || Value == 0 || Value > MAX_int32) return false;
			Out = static_cast<int32>(Value);
			return true;
		}
		bool Guid(FGuid& Out) { return UInt(Out.A) && UInt(Out.B) && UInt(Out.C) && UInt(Out.D); }
		bool String(FString& Out, uint32 MaxCharacters)
		{
			uint32 Size;
			if (!UInt(Size) || Size > MaxCharacters * 4 || Size > static_cast<uint32>(Bytes.Num() - Offset)) return false;
			const auto* Data = reinterpret_cast<const UTF8CHAR*>(Bytes.GetData() + Offset);
			const FUTF8ToTCHAR Converted(Data, static_cast<int32>(Size));
			if (Converted.Length() > static_cast<int32>(MaxCharacters)) return false;
			Out = FString(Converted.Length(), Converted.Get());
			const FTCHARToUTF8 RoundTrip(*Out, Out.Len());
			if (RoundTrip.Length() != static_cast<int32>(Size)
				|| FMemory::Memcmp(RoundTrip.Get(), Data, Size) != 0) return false;
			Offset += static_cast<int32>(Size);
			return true;
		}
	};
}

bool FSeinSimulationContentBuildArtifact::Encode(
	const FSeinSimulationContentManifestProfile& Profile, TArray<uint8>& OutBytes, FString& OutError)
{
	OutBytes.Reset();
	if (!FSeinSimulationContentManifestCodec::ValidateProfile(
		FSeinSimulationContentManifestCodec::CurrentFormatVersion, Profile, OutError)) return false;
	TArray<uint8> Bytes;
	WriteUInt(Bytes, Magic); WriteUInt(Bytes, Version);
	WriteUInt(Bytes, Profile.BuilderRevision); WriteGuid(Bytes, Profile.RootDigest);
	WriteUInt(Bytes, Profile.Contributors.Num());
	for (const auto& C : Profile.Contributors)
	{
		WriteString(Bytes, C.StableContributorId);
		WriteUInt(Bytes, C.ContributorRevision); WriteGuid(Bytes, C.DiscoveryContractDigest);
	}
	WriteUInt(Bytes, Profile.Records.Num());
	for (const auto& R : Profile.Records)
	{
		WriteString(Bytes, R.StableRecordKindId); WriteUInt(Bytes, R.RecordRevision);
		WriteString(Bytes, R.CanonicalRecordId); WriteGuid(Bytes, R.ContentDigest);
		if (Bytes.Num() > MaxBytes)
		{
			OutError = TEXT("Build compatibility data exceeds its size limit.");
			return false;
		}
	}
	OutBytes = MoveTemp(Bytes);
	OutError.Reset();
	return true;
}

bool FSeinSimulationContentBuildArtifact::Decode(TConstArrayView<uint8> Bytes,
	FSeinSimulationContentManifestProfile& OutProfile, FString& OutError)
{
	OutProfile = {};
	OutError = TEXT("Invalid or truncated build compatibility data.");
	if (Bytes.IsEmpty() || Bytes.Num() > MaxBytes) return false;
	FReader Reader{Bytes};
	uint32 Header, Format, Count;
	FSeinSimulationContentManifestProfile Candidate;
	if (!Reader.UInt(Header) || Header != Magic || !Reader.UInt(Format) || Format != Version
		|| !Reader.PositiveInt(Candidate.BuilderRevision) || !Reader.Guid(Candidate.RootDigest)
		|| !Reader.UInt(Count) || Count > FSeinSimulationContentManifestCodec::MaxContributors) return false;
	for (uint32 I = 0; I < Count; ++I)
	{
		auto& C = Candidate.Contributors.AddDefaulted_GetRef();
		if (!Reader.String(C.StableContributorId, FSeinSimulationContentManifestCodec::MaxStableIdCharacters)
			|| !Reader.PositiveInt(C.ContributorRevision) || !Reader.Guid(C.DiscoveryContractDigest)) return false;
	}
	if (!Reader.UInt(Count) || Count > FSeinSimulationContentManifestCodec::MaxRecords) return false;
	for (uint32 I = 0; I < Count; ++I)
	{
		auto& R = Candidate.Records.AddDefaulted_GetRef();
		if (!Reader.String(R.StableRecordKindId, FSeinSimulationContentManifestCodec::MaxStableIdCharacters)
			|| !Reader.PositiveInt(R.RecordRevision)
			|| !Reader.String(R.CanonicalRecordId, FSeinSimulationContentManifestCodec::MaxCanonicalRecordIdCharacters)
			|| !Reader.Guid(R.ContentDigest)) return false;
	}
	if (Reader.Offset != Bytes.Num()
		|| Candidate.BuilderRevision != static_cast<int32>(FSeinSimulationContentManifestCodec::CurrentBuilderRevision)
		|| !FSeinSimulationContentManifestCodec::ValidateProfile(
			FSeinSimulationContentManifestCodec::CurrentFormatVersion, Candidate, OutError)) return false;
	OutProfile = MoveTemp(Candidate);
	OutError.Reset();
	return true;
}

bool FSeinSimulationContentBuildArtifact::Load(const FString& Filename,
	FSeinSimulationContentManifestProfile& OutProfile, FString& OutError)
{
	OutProfile = {};
	TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*Filename));
	if (!Reader || Reader->TotalSize() <= 0 || Reader->TotalSize() > MaxBytes)
	{
		OutError = FString::Printf(TEXT("Build compatibility data is missing or unreadable: %s. Rebuild the packaged game; cooking generates this data automatically."), *Filename);
		return false;
	}
	TArray<uint8> Bytes;
	Bytes.SetNumUninitialized(static_cast<int32>(Reader->TotalSize()));
	Reader->Serialize(Bytes.GetData(), Bytes.Num());
	if (Reader->IsError())
	{
		OutError = TEXT("Cannot read cooked compatibility data.");
		return false;
	}
	return Decode(Bytes, OutProfile, OutError);
}
