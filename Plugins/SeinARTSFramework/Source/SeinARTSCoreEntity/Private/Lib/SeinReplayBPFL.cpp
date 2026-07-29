/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinReplayBPFL.cpp
 */

#include "Lib/SeinReplayBPFL.h"
#include "Serialization/SeinCanonicalStateCodec.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "HAL/FileManager.h"
#include "Hash/Blake3.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/Archive.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinReplayMetadata, Log, All);

namespace
{
	constexpr uint32 HeaderMetadataFormatVersion = 6;
	constexpr uint64 MaxHeaderMetadataBodyBytes = 1ULL * 1024ULL * 1024ULL;
	constexpr int32 MaxHeaderMetadataAggregateElements = 64 * 1024;
	constexpr int32 MaxHeaderMetadataStringBytes = 1024;
	constexpr int32 MaxHeaderMetadataRecursionDepth = 64;
	constexpr int32 HeaderMetadataPrefixBytes = 68;
	constexpr uint8 HeaderMetadataMagic[8] = {
		'S', 'E', 'I', 'N', 'R', 'P', 'H', '1'};
	const FSeinStructWireLimits HeaderMetadataWireLimits{
		static_cast<int32>(MaxHeaderMetadataBodyBytes),
		MaxHeaderMetadataAggregateElements,
		MaxHeaderMetadataStringBytes,
		MaxHeaderMetadataRecursionDepth};

	/** Canonical catalog of already-loaded types trusted by project settings.
	 *  The wire contains only indices into this list: it cannot nominate a path,
	 *  load a package, or select a different generation of the same type. */
	void UpdateUInt32(FBlake3& Hasher, uint32 Value)
	{
		const uint8 Bytes[4] = {
			static_cast<uint8>(Value >> 24),
			static_cast<uint8>(Value >> 16),
			static_cast<uint8>(Value >> 8),
			static_cast<uint8>(Value)};
		Hasher.Update(Bytes, UE_ARRAY_COUNT(Bytes));
	}

	FGuid GuidFromBlake3(const FBlake3Hash& Hash)
	{
		const uint8* Bytes = Hash.GetBytes();
		auto ReadWord = [](const uint8* Word)
		{
			return (static_cast<uint32>(Word[0]) << 24)
				| (static_cast<uint32>(Word[1]) << 16)
				| (static_cast<uint32>(Word[2]) << 8)
				| static_cast<uint32>(Word[3]);
		};
		FGuid Result(
			ReadWord(Bytes), ReadWord(Bytes + 4),
			ReadWord(Bytes + 8), ReadWord(Bytes + 12));
		if (!Result.IsValid()) Result.D = 1;
		return Result;
	}

	bool BuildHeaderMetadataWireCatalog(
		const USeinWorldSubsystem* SessionWorld,
		bool bAllowProjectSettingsFallback,
		TArray<const UScriptStruct*>& OutDynamicStructs,
		TArray<FName>& OutNames,
		FGuid& OutManifestDigest,
		FString& OutError)
	{
		OutDynamicStructs.Reset();
		OutNames.Reset();
		OutManifestDigest.Invalidate();
		OutError.Reset();

		TArray<FName> TrustedNames;
		if (SessionWorld)
		{
			if (!SessionWorld->GetCommandProtocolDigest().IsValid())
			{
				OutError = TEXT(
					"the supplied world's frozen command protocol is unavailable");
				return false;
			}
			for (const UScriptStruct* Struct :
				SessionWorld->GetCommandAdditionalDynamicPayloadStructs())
			{
				OutDynamicStructs.Add(Struct);
			}
			for (const FName Name : SessionWorld->GetCommandAdditionalWireNames())
			{
				TrustedNames.Add(Name);
			}
		}
		else if (bAllowProjectSettingsFallback)
		{
			// Worldless metadata tooling has no session to bind to. Preserve the
			// public null-context API by deriving a canonical catalog from current
			// project defaults; a supplied-but-invalid context never reaches here.
			const USeinARTSCoreSettings* Settings =
				GetDefault<USeinARTSCoreSettings>();
			if (!Settings)
			{
				OutError = TEXT("core project settings are unavailable");
				return false;
			}

			auto AppendTypes = [&OutDynamicStructs](
				const TArray<FInstancedStruct>& Values)
			{
				for (const FInstancedStruct& Value : Values)
				{
					if (Value.IsValid())
					{
						OutDynamicStructs.Add(Value.GetScriptStruct());
					}
				}
			};
			AppendTypes(Settings->DefaultMatchExtensions);
			AppendTypes(Settings->CommandDynamicPayloadSchemas);
			TrustedNames = Settings->AdditionalWireNames;
		}
		else
		{
			OutError = TEXT(
				"the supplied world context has no frozen simulation subsystem");
			return false;
		}

		OutDynamicStructs.Sort([](const UScriptStruct& A, const UScriptStruct& B)
		{
			return A.GetPathName().Compare(
				B.GetPathName(), ESearchCase::CaseSensitive) < 0;
		});

		for (int32 Index = OutDynamicStructs.Num() - 1; Index > 0; --Index)
		{
			if (OutDynamicStructs[Index]->GetPathName()
				!= OutDynamicStructs[Index - 1]->GetPathName())
			{
				continue;
			}
			if (OutDynamicStructs[Index] != OutDynamicStructs[Index - 1])
			{
				OutDynamicStructs.Reset();
				OutError = TEXT(
					"dynamic metadata catalog contains conflicting type generations");
				return false;
			}
			OutDynamicStructs.RemoveAt(Index);
		}

		FString NameManifest;
		SeinBuildCanonicalWireNameCatalog(
			TrustedNames, OutNames, NameManifest);

		// Bind each catalog index to both its exact path and the bounded wire's
		// runtime schema digest. This remains available in Shipping and excludes
		// editor-only fields, so cooked and Editor catalogs share one identity.
		FBlake3 ManifestHasher;
		constexpr uint8 ManifestDomain[] = {
			'S', 'E', 'I', 'N', 'R', 'P', 'H', 'C', 3};
		ManifestHasher.Update(ManifestDomain, UE_ARRAY_COUNT(ManifestDomain));
		auto AppendStructIdentity = [&ManifestHasher, &OutError](
			const UScriptStruct& Struct)
		{
			const FString Path = Struct.GetPathName();
			FTCHARToUTF8 Utf8Path(*Path, Path.Len());
			UpdateUInt32(
				ManifestHasher, static_cast<uint32>(Utf8Path.Length()));
			ManifestHasher.Update(Utf8Path.Get(), Utf8Path.Length());
			FGuid SchemaDigest;
			if (!FSeinCanonicalStateCodec::ComputeSchemaDigest(
				&Struct, SchemaDigest, OutError))
			{
				return false;
			}
			UpdateUInt32(ManifestHasher, SchemaDigest.A);
			UpdateUInt32(ManifestHasher, SchemaDigest.B);
			UpdateUInt32(ManifestHasher, SchemaDigest.C);
			UpdateUInt32(ManifestHasher, SchemaDigest.D);
			return true;
		};
		if (!AppendStructIdentity(*FSeinReplayHeader::StaticStruct())) return false;
		UpdateUInt32(
			ManifestHasher, static_cast<uint32>(OutDynamicStructs.Num()));
		for (const UScriptStruct* Struct : OutDynamicStructs)
		{
			if (!AppendStructIdentity(*Struct)) return false;
		}
		FTCHARToUTF8 Utf8NameManifest(*NameManifest, NameManifest.Len());
		UpdateUInt32(
			ManifestHasher, static_cast<uint32>(Utf8NameManifest.Length()));
		ManifestHasher.Update(
			Utf8NameManifest.Get(), Utf8NameManifest.Length());
		OutManifestDigest = GuidFromBlake3(ManifestHasher.Finalize());
		return true;
	}

	void AppendUInt32(TArray<uint8>& OutBytes, uint32 Value)
	{
		OutBytes.Add(static_cast<uint8>(Value >> 24));
		OutBytes.Add(static_cast<uint8>(Value >> 16));
		OutBytes.Add(static_cast<uint8>(Value >> 8));
		OutBytes.Add(static_cast<uint8>(Value));
	}

	void AppendUInt64(TArray<uint8>& OutBytes, uint64 Value)
	{
		AppendUInt32(OutBytes, static_cast<uint32>(Value >> 32));
		AppendUInt32(OutBytes, static_cast<uint32>(Value));
	}

	void AppendGuid(TArray<uint8>& OutBytes, const FGuid& Value)
	{
		AppendUInt32(OutBytes, Value.A);
		AppendUInt32(OutBytes, Value.B);
		AppendUInt32(OutBytes, Value.C);
		AppendUInt32(OutBytes, Value.D);
	}

	uint32 ReadUInt32(const uint8* Bytes)
	{
		return (static_cast<uint32>(Bytes[0]) << 24)
			| (static_cast<uint32>(Bytes[1]) << 16)
			| (static_cast<uint32>(Bytes[2]) << 8)
			| static_cast<uint32>(Bytes[3]);
	}

	uint64 ReadUInt64(const uint8* Bytes)
	{
		return (static_cast<uint64>(ReadUInt32(Bytes)) << 32)
			| static_cast<uint64>(ReadUInt32(Bytes + 4));
	}

	FGuid ReadGuid(const uint8* Bytes)
	{
		return FGuid(
			ReadUInt32(Bytes),
			ReadUInt32(Bytes + 4),
			ReadUInt32(Bytes + 8),
			ReadUInt32(Bytes + 12));
	}

	FGuid ComputeBodyDigest(const uint8* Bytes, int32 NumBytes)
	{
		return GuidFromBlake3(FBlake3::HashBuffer(Bytes, NumBytes));
	}

	bool BuildHeaderMetadataFile(
		const TArray<uint8>& Body,
		const FGuid& CatalogManifestDigest,
		const FGuid& SimulationContentDigest,
		TArray<uint8>& OutFileBytes,
		FString& OutError)
	{
		OutFileBytes.Reset();
		OutError.Reset();
		if (!CatalogManifestDigest.IsValid())
		{
			OutError = TEXT("wire catalog manifest digest is invalid");
			return false;
		}
		if (!SimulationContentDigest.IsValid())
		{
			OutError = TEXT("simulation content digest is invalid");
			return false;
		}
		if (Body.IsEmpty()
			|| static_cast<uint64>(Body.Num()) > MaxHeaderMetadataBodyBytes)
		{
			OutError = FString::Printf(
				TEXT("wire body size %d is outside the supported range 1..%llu"),
				Body.Num(),
				static_cast<unsigned long long>(MaxHeaderMetadataBodyBytes));
			return false;
		}

		OutFileBytes.Reserve(HeaderMetadataPrefixBytes + Body.Num());
		OutFileBytes.Append(
			HeaderMetadataMagic, UE_ARRAY_COUNT(HeaderMetadataMagic));
		AppendUInt32(OutFileBytes, HeaderMetadataFormatVersion);
		AppendUInt64(OutFileBytes, static_cast<uint64>(Body.Num()));
		AppendGuid(OutFileBytes, ComputeBodyDigest(Body.GetData(), Body.Num()));
		AppendGuid(OutFileBytes, CatalogManifestDigest);
		AppendGuid(OutFileBytes, SimulationContentDigest);
		check(OutFileBytes.Num() == HeaderMetadataPrefixBytes);
		OutFileBytes.Append(Body);
		return true;
	}

	bool ParseHeaderMetadataFile(
		const TArray<uint8>& FileBytes,
		const FGuid& LocalCatalogManifestDigest,
		const FGuid& LocalSimulationContentDigest,
		FGuid& OutStoredSimulationContentDigest,
		TArrayView<const uint8>& OutBody,
		FString& OutError)
	{
		OutStoredSimulationContentDigest.Invalidate();
		OutBody = TArrayView<const uint8>();
		OutError.Reset();
		if (FileBytes.Num() < HeaderMetadataPrefixBytes)
		{
			OutError = TEXT("file is smaller than the replay-header metadata prefix");
			return false;
		}
		if (FMemory::Memcmp(
				FileBytes.GetData(),
				HeaderMetadataMagic,
				UE_ARRAY_COUNT(HeaderMetadataMagic)) != 0)
		{
			OutError = TEXT("magic mismatch (not replay-header metadata)");
			return false;
		}

		const uint8* Cursor =
			FileBytes.GetData() + UE_ARRAY_COUNT(HeaderMetadataMagic);
		const uint32 Version = ReadUInt32(Cursor);
		Cursor += 4;
		if (Version != HeaderMetadataFormatVersion)
		{
			OutError = FString::Printf(
				TEXT("unsupported replay-header metadata version %u (expected %u)"),
				Version, HeaderMetadataFormatVersion);
			return false;
		}

		const uint64 BodyBytes = ReadUInt64(Cursor);
		Cursor += 8;
		const FGuid ExpectedDigest = ReadGuid(Cursor);
		Cursor += 16;
		const FGuid StoredCatalogManifestDigest = ReadGuid(Cursor);
		Cursor += 16;
		OutStoredSimulationContentDigest = ReadGuid(Cursor);
		Cursor += 16;
		check(Cursor == FileBytes.GetData() + HeaderMetadataPrefixBytes);
		if (BodyBytes == 0 || BodyBytes > MaxHeaderMetadataBodyBytes)
		{
			OutError = FString::Printf(
				TEXT("declared body size %llu is outside the supported range"),
				static_cast<unsigned long long>(BodyBytes));
			return false;
		}
		if (BodyBytes
			!= static_cast<uint64>(FileBytes.Num() - HeaderMetadataPrefixBytes))
		{
			OutError = TEXT("declared body size does not exactly match file length");
			return false;
		}
		if (!ExpectedDigest.IsValid())
		{
			OutError = TEXT("prefix contains an invalid body digest");
			return false;
		}
		if (!StoredCatalogManifestDigest.IsValid()
			|| !LocalCatalogManifestDigest.IsValid()
			|| StoredCatalogManifestDigest != LocalCatalogManifestDigest)
		{
			OutError = TEXT(
				"wire catalog manifest mismatch with the local trusted schema");
			return false;
		}
		if (!OutStoredSimulationContentDigest.IsValid())
		{
			OutError = TEXT("prefix contains an invalid simulation content digest");
			return false;
		}
		if (LocalSimulationContentDigest.IsValid()
			&& OutStoredSimulationContentDigest != LocalSimulationContentDigest)
		{
			OutError = TEXT(
				"simulation content digest mismatch with the active session");
			return false;
		}

		const uint8* BodyData =
			FileBytes.GetData() + HeaderMetadataPrefixBytes;
		if (ComputeBodyDigest(BodyData, static_cast<int32>(BodyBytes))
			!= ExpectedDigest)
		{
			OutError = TEXT("replay-header metadata checksum mismatch");
			return false;
		}

		OutBody = TArrayView<const uint8>(
			BodyData, static_cast<int32>(BodyBytes));
		return true;
	}

	bool CanonicalizeHeaderMetadata(
		const FSeinReplayHeader& Header,
		FSeinReplayHeader& OutCanonical,
		FString& OutError)
	{
		OutCanonical = Header;
		OutError.Reset();
		FGuid CanonicalSettingsDigest;
		if (!SeinCanonicalizeAndDigestMatchSettings(
				OutCanonical.SettingsSnapshot,
				CanonicalSettingsDigest,
				nullptr))
		{
			OutError = TEXT("match settings cannot be canonically represented");
			return false;
		}
		if (CanonicalSettingsDigest != OutCanonical.MatchSettingsDigest)
		{
			OutError = TEXT("match settings snapshot disagrees with its declared digest");
			return false;
		}
		if (!OutCanonical.BootstrapReceipt.IsValid()
			|| OutCanonical.BootstrapReceipt.ContractDigest
				!= OutCanonical.MatchSettingsDigest)
		{
			OutError = TEXT("bootstrap receipt is invalid or disagrees with match settings");
			return false;
		}

		for (int32 Index = 1;
			Index < OutCanonical.SettingsSnapshot.Slots.Num(); ++Index)
		{
			if (OutCanonical.SettingsSnapshot.Slots[Index - 1].SlotIndex
				== OutCanonical.SettingsSnapshot.Slots[Index].SlotIndex)
			{
				OutError = TEXT("match settings contain duplicate slot identities");
				return false;
			}
		}
		for (int32 Index = 1;
			Index < OutCanonical.SettingsSnapshot.Extensions.Num(); ++Index)
		{
			const UScriptStruct* Previous =
				OutCanonical.SettingsSnapshot.Extensions[Index - 1]
					.GetScriptStruct();
			const UScriptStruct* Current =
				OutCanonical.SettingsSnapshot.Extensions[Index]
					.GetScriptStruct();
			if (Previous && Current
				&& Previous->GetPathName() == Current->GetPathName())
			{
				OutError = TEXT("match settings contain duplicate extension types");
				return false;
			}
		}

		OutCanonical.Players.Sort(
			[](const FSeinPlayerRegistration& A,
				const FSeinPlayerRegistration& B)
			{
				return A.PlayerID.Value < B.PlayerID.Value;
			});
		for (int32 Index = 1; Index < OutCanonical.Players.Num(); ++Index)
		{
			if (OutCanonical.Players[Index - 1].PlayerID
				== OutCanonical.Players[Index].PlayerID)
			{
				OutError = TEXT("player metadata contains duplicate identities");
				return false;
			}
		}
		return true;
	}

	bool SaveHeaderMetadataFileAtomically(
		const TArray<uint8>& FileBytes,
		const FString& FilePath,
		FString& OutError)
	{
		OutError.Reset();
		IFileManager& FileManager = IFileManager::Get();
		const FString UniqueSuffix =
			FGuid::NewGuid().ToString(EGuidFormats::Digits);
		const FString TempPath = FilePath + TEXT(".tmp.") + UniqueSuffix;
		const FString BackupPath = FilePath + TEXT(".bak.") + UniqueSuffix;

		if (!FFileHelper::SaveArrayToFile(FileBytes, *TempPath))
		{
			FileManager.Delete(*TempPath, false, true, true);
			OutError = TEXT("could not write and close the sibling temporary file");
			return false;
		}

		const bool bHadExistingFile = FileManager.FileExists(*FilePath);
		if (bHadExistingFile
			&& !FileManager.Move(
				*BackupPath, *FilePath,
				/*Replace=*/false, /*EvenIfReadOnly=*/false,
				/*Attributes=*/false, /*DoNotRetryOrError=*/true))
		{
			FileManager.Delete(*TempPath, false, true, true);
			OutError = TEXT("could not preserve the existing destination before replacement");
			return false;
		}

		if (!FileManager.Move(
			*FilePath, *TempPath,
			/*Replace=*/false, /*EvenIfReadOnly=*/false,
			/*Attributes=*/false, /*DoNotRetryOrError=*/true))
		{
			const bool bRestored = !bHadExistingFile
				|| FileManager.Move(
					*FilePath, *BackupPath,
					/*Replace=*/true, /*EvenIfReadOnly=*/false,
					/*Attributes=*/false, /*DoNotRetryOrError=*/true);
			FileManager.Delete(*TempPath, false, true, true);
			OutError = bRestored
				? TEXT("could not replace the destination; the previous file was restored")
				: TEXT("could not replace the destination and rollback also failed");
			return false;
		}

		if (bHadExistingFile
			&& !FileManager.Delete(*BackupPath, false, true, true))
		{
			UE_LOG(LogSeinReplayMetadata, Warning,
				TEXT("SaveReplayHeaderMetadata: replacement succeeded but stale backup cleanup failed: %s."),
				*BackupPath);
		}
		return true;
	}
}

USeinWorldSubsystem* USeinReplayBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

FSeinReplayHeader USeinReplayBPFL::SeinBuildReplayHeader(const UObject* WorldContextObject, FName MapIdentifier)
{
	FSeinReplayHeader Header;
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return Header;

	SeinReplayCompatibility::StampCurrent(Header, Sub->GetWorld());
	if (!MapIdentifier.IsNone()
		&& MapIdentifier.ToString() != Header.MapIdentifier)
	{
		UE_LOG(LogSeinReplayMetadata, Warning,
			TEXT("BuildReplayHeader ignored non-canonical map identity '%s'; using long package name '%s'."),
			*MapIdentifier.ToString(), *Header.MapIdentifier);
	}
	Header.CommandProtocolDigest = Sub->GetCommandProtocolDigest();
	Header.MatchSettingsDigest = Sub->GetMatchSettingsDigest();
	Header.BootstrapReceipt = Sub->GetMatchBootstrapReceipt();
	Header.ConfigFingerprint = Sub->GetConfigFingerprint();
	Header.RandomSeed = Sub->GetSessionSeed();
	Header.SettingsSnapshot = Sub->GetMatchSettings();
	for (const FSeinMatchSlot& Slot : Header.SettingsSnapshot.Slots)
	{
		if (Slot.State != ESeinSlotState::Human
			&& Slot.State != ESeinSlotState::AI)
		{
			continue;
		}

		FSeinPlayerRegistration& Player =
			Header.Players.AddDefaulted_GetRef();
		Player.PlayerID = Slot.SlotIndex > 0 && Slot.SlotIndex <= MAX_uint8
			? FSeinPlayerID(static_cast<uint8>(Slot.SlotIndex))
			: FSeinPlayerID::Neutral();
		Player.FactionID = Slot.FactionID;
		Player.TeamID = Slot.TeamID;
		Player.bIsAI = Slot.State == ESeinSlotState::AI;
		Player.bIsSpectator = false;
	}
	Header.Players.Sort(
		[](const FSeinPlayerRegistration& A,
			const FSeinPlayerRegistration& B)
		{
			return A.PlayerID < B.PlayerID;
		});
	Header.StartTick = 0;
	Header.EndTick = Sub->GetCurrentTick();
	Header.RecordedAt = FDateTime::UtcNow();
	return Header;
}

bool USeinReplayBPFL::SeinSaveReplayHeaderMetadata(
	const UObject* WorldContextObject,
	const FSeinReplayHeader& Header,
	const FString& FilePath)
{
	if (FilePath.IsEmpty())
	{
		UE_LOG(LogSeinReplayMetadata, Error,
			TEXT("SaveReplayHeaderMetadata: file path is empty."));
		return false;
	}

	TArray<const UScriptStruct*> DynamicCatalog;
	TArray<FName> NameCatalog;
	FGuid CatalogManifestDigest;
	TArray<uint8> Body;
	FString WireError;
	FSeinReplayHeader CanonicalHeader;
	const USeinWorldSubsystem* SessionWorld = WorldContextObject
		? GetWorldSubsystem(WorldContextObject)
		: nullptr;
	if (SessionWorld
		&& (!SessionWorld->IsSimulationContentReady()
			|| Header.BootstrapReceipt.SimulationContentDigest
				!= SessionWorld->GetSimulationContentDigest()))
	{
		UE_LOG(LogSeinReplayMetadata, Error,
			TEXT("SaveReplayHeaderMetadata: header content identity is unavailable or disagrees with the active session: %s."),
			*FilePath);
		return false;
	}
	if (!CanonicalizeHeaderMetadata(Header, CanonicalHeader, WireError)
		|| !BuildHeaderMetadataWireCatalog(
			SessionWorld,
			WorldContextObject == nullptr,
			DynamicCatalog, NameCatalog, CatalogManifestDigest, WireError)
		|| !FSeinCanonicalStateCodec::Encode(
			FSeinReplayHeader::StaticStruct(), &CanonicalHeader,
			{ DynamicCatalog, NameCatalog },
			HeaderMetadataWireLimits, Body, WireError))
	{
		UE_LOG(LogSeinReplayMetadata, Error,
			TEXT("SaveReplayHeaderMetadata: bounded serialization failed for %s: %s."),
			*FilePath, *WireError);
		return false;
	}

	TArray<uint8> FileBytes;
	FString FormatError;
	if (!BuildHeaderMetadataFile(
			Body,
			CatalogManifestDigest,
			CanonicalHeader.BootstrapReceipt.SimulationContentDigest,
			FileBytes,
			FormatError))
	{
		UE_LOG(LogSeinReplayMetadata, Error,
			TEXT("SaveReplayHeaderMetadata: refused %s: %s."),
			*FilePath, *FormatError);
		return false;
	}

	const FString ParentDirectory = FPaths::GetPath(FilePath);
	if (!ParentDirectory.IsEmpty()
		&& !IFileManager::Get().MakeDirectory(*ParentDirectory, /*Tree=*/true))
	{
		UE_LOG(LogSeinReplayMetadata, Error,
			TEXT("SaveReplayHeaderMetadata: could not create directory %s."),
			*ParentDirectory);
		return false;
	}
	FString SaveError;
	if (!SaveHeaderMetadataFileAtomically(FileBytes, FilePath, SaveError))
	{
		UE_LOG(LogSeinReplayMetadata, Error,
			TEXT("SaveReplayHeaderMetadata: failed to replace %s: %s."),
			*FilePath, *SaveError);
		return false;
	}
	return true;
}

bool USeinReplayBPFL::SeinLoadReplayHeaderMetadata(
	const UObject* WorldContextObject,
	const FString& FilePath,
	FSeinReplayHeader& OutHeader)
{
	TUniquePtr<FArchive> FileReader(
		IFileManager::Get().CreateFileReader(*FilePath, FILEREAD_Silent));
	if (!FileReader)
	{
		UE_LOG(LogSeinReplayMetadata, Error,
			TEXT("LoadReplayHeaderMetadata: failed to open %s."), *FilePath);
		return false;
	}

	const int64 FileBytesOnDisk = FileReader->TotalSize();
	const int64 MaxFileBytes = HeaderMetadataPrefixBytes
		+ static_cast<int64>(MaxHeaderMetadataBodyBytes);
	if (FileBytesOnDisk < HeaderMetadataPrefixBytes
		|| FileBytesOnDisk > MaxFileBytes)
	{
		UE_LOG(LogSeinReplayMetadata, Error,
			TEXT("LoadReplayHeaderMetadata: rejected file size %lld before allocation (allowed %d..%lld): %s."),
			static_cast<long long>(FileBytesOnDisk),
			HeaderMetadataPrefixBytes,
			static_cast<long long>(MaxFileBytes),
			*FilePath);
		return false;
	}

	TArray<uint8> FileBytes;
	FileBytes.SetNumUninitialized(static_cast<int32>(FileBytesOnDisk));
	FileReader->Serialize(FileBytes.GetData(), FileBytes.Num());
	const bool bClosed = FileReader->Close();
	if (!bClosed || FileReader->IsError() || FileReader->IsCriticalError())
	{
		UE_LOG(LogSeinReplayMetadata, Error,
			TEXT("LoadReplayHeaderMetadata: failed to read and close %s."),
			*FilePath);
		return false;
	}

	TArray<const UScriptStruct*> DynamicCatalog;
	TArray<FName> NameCatalog;
	FGuid CatalogManifestDigest;
	FString WireError;
	const USeinWorldSubsystem* SessionWorld = WorldContextObject
		? GetWorldSubsystem(WorldContextObject)
		: nullptr;
	if (!BuildHeaderMetadataWireCatalog(
			SessionWorld,
			WorldContextObject == nullptr,
			DynamicCatalog, NameCatalog, CatalogManifestDigest, WireError))
	{
		UE_LOG(LogSeinReplayMetadata, Error,
			TEXT("LoadReplayHeaderMetadata: local schema catalog is unavailable for %s: %s."),
			*FilePath, *WireError);
		return false;
	}

	FGuid LocalSimulationContentDigest;
	if (SessionWorld)
	{
		if (!SessionWorld->IsSimulationContentReady())
		{
			UE_LOG(LogSeinReplayMetadata, Error,
				TEXT("LoadReplayHeaderMetadata: active simulation content identity is unavailable for %s."),
				*FilePath);
			return false;
		}
		LocalSimulationContentDigest =
			SessionWorld->GetSimulationContentDigest();
	}

	FGuid StoredSimulationContentDigest;
	TArrayView<const uint8> Body;
	FString FormatError;
	if (!ParseHeaderMetadataFile(
			FileBytes,
			CatalogManifestDigest,
			LocalSimulationContentDigest,
			StoredSimulationContentDigest,
			Body,
			FormatError))
	{
		UE_LOG(LogSeinReplayMetadata, Error,
			TEXT("LoadReplayHeaderMetadata: rejected %s before deserialization: %s."),
			*FilePath, *FormatError);
		return false;
	}

	FSeinReplayHeader LoadedHeader;
	if (!FSeinCanonicalStateCodec::Decode(
			Body, FSeinReplayHeader::StaticStruct(), &LoadedHeader,
			{ DynamicCatalog, NameCatalog },
			HeaderMetadataWireLimits, WireError))
	{
		UE_LOG(LogSeinReplayMetadata, Error,
			TEXT("LoadReplayHeaderMetadata: bounded deserialization failed for %s: %s."),
			*FilePath, *WireError);
		return false;
	}
	if (LoadedHeader.BootstrapReceipt.SimulationContentDigest
		!= StoredSimulationContentDigest)
	{
		UE_LOG(LogSeinReplayMetadata, Error,
			TEXT("LoadReplayHeaderMetadata: rejected %s because the authenticated prefix and replay header disagree on simulation content."),
			*FilePath);
		return false;
	}

	FSeinReplayHeader CanonicalHeader;
	FString CanonicalError;
	if (!CanonicalizeHeaderMetadata(
			LoadedHeader, CanonicalHeader, CanonicalError)
		|| !FSeinReplayHeader::StaticStruct()->CompareScriptStruct(
			&LoadedHeader, &CanonicalHeader, PPF_None))
	{
		if (CanonicalError.IsEmpty())
		{
			CanonicalError = TEXT("arrays are not in canonical identity order");
		}
		UE_LOG(LogSeinReplayMetadata, Error,
			TEXT("LoadReplayHeaderMetadata: rejected non-canonical metadata in %s: %s."),
			*FilePath, *CanonicalError);
		return false;
	}

	OutHeader = MoveTemp(CanonicalHeader);
	return true;
}

bool USeinReplayBPFL::SeinSaveReplay(
	const UObject* WorldContextObject,
	const FSeinReplayHeader& Header,
	const FString& FilePath)
{
	return SeinSaveReplayHeaderMetadata(
		WorldContextObject, Header, FilePath);
}

bool USeinReplayBPFL::SeinLoadReplay(
	const UObject* WorldContextObject,
	const FString& FilePath,
	FSeinReplayHeader& OutHeader)
{
	return SeinLoadReplayHeaderMetadata(
		WorldContextObject, FilePath, OutHeader);
}
