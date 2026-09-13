/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinConsumerDependencyAuditCommandlet.cpp
 * @author       RJ Macklem
 * @created      12 Sep 2026
 * @latest       12 Sep 2026
 * @brief        Checks serialized consumer dependencies using disposable package copies.
 * @disclaimer   This code was generated in part with the assistance of an AI language model.
 */
#include "Commandlets/SeinConsumerDependencyAuditCommandlet.h"
#include "Actor/SeinEntityBridgeComponent.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/FeedbackContext.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinConsumerAudit, Log, All);

namespace
{
	bool ContainsHostPath(const TArray<uint8>& Bytes)
	{
		// Check both serialized ANSI and UTF-16 strings.
		const FString Needle = TEXT("/Game/") TEXT("SeinARTSExamples");
		const FTCHARToUTF8 Ansi(*Needle);
		for (int32 Index = 0; Index < Bytes.Num(); ++Index)
		{
			if (Bytes.Num() - Index >= Ansi.Length()
				&& FMemory::Memcmp(Bytes.GetData() + Index, Ansi.Get(), Ansi.Length()) == 0)
				return true;
			if (Bytes.Num() - Index >= Needle.Len() * sizeof(TCHAR)
				&& FMemory::Memcmp(Bytes.GetData() + Index, *Needle, Needle.Len() * sizeof(TCHAR)) == 0)
				return true;
		}
		return false;
	}
}

USeinConsumerDependencyAuditCommandlet::USeinConsumerDependencyAuditCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 USeinConsumerDependencyAuditCommandlet::Main(const FString& Params)
{
	FString Input;
	if (!FParse::Value(*Params, TEXT("Input="), Input)) return 1;
	TArray<FString> Files;
	if (!FFileHelper::LoadFileToStringArray(Files, *Input) || Files.IsEmpty()) return 1;
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	Registry.SearchAllAssets(true);
	const FString Scratch = FPaths::ProjectSavedDir() / TEXT("ConsumerDependencyAudit") / FGuid::NewGuid().ToString(EGuidFormats::Digits);
	IFileManager::Get().MakeDirectory(*Scratch, true);
	int32 Audited = 0;
	for (const FString& Filename : Files)
	{
		FString PackageName;
		if (!FPackageName::TryConvertFilenameToLongPackageName(Filename, PackageName)) return 1;
		TArray<FName> Dependencies;
		Registry.GetDependencies(FName(*PackageName), Dependencies, UE::AssetRegistry::EDependencyCategory::Package);
		for (FName Dependency : Dependencies)
		{
			if (Dependency.ToString().Contains(TEXT("/Game/") TEXT("SeinARTSExamples")))
			{
				UE_LOG(LogSeinConsumerAudit, Error, TEXT("%s depends on host package %s"), *PackageName, *Dependency.ToString());
				return 1;
			}
		}
		const int32 ErrorsBefore = GWarn->GetNumErrors();
		UPackage* Package = LoadPackage(nullptr, *PackageName, LOAD_None);
		if (!Package || GWarn->GetNumErrors() != ErrorsBefore) return 1;
		TArray<UObject*> Objects;
		GetObjectsWithPackage(Package, Objects, true);
		UWorld* World = nullptr;
		for (UObject* Object : Objects)
		{
			if (UWorld* Candidate = Cast<UWorld>(Object)) World = Candidate;
			if (ULevel* Level = Cast<ULevel>(Object))
			{
				// FURL.Map is the level's originating package identity, not a travel target.
				Level->URL.Map = PackageName;
			}
			if (USeinEntityBridgeComponent* Bridge = Cast<USeinEntityBridgeComponent>(Object))
			{
				// These two editor-only arrays reconcile unopened instances. Exclude
				// them only in this disposable process; never save over the source.
				for (const TCHAR* Name : { TEXT("ComponentDataDefaultChangeHistory"), TEXT("ComponentDataStructuralChangeHistory") })
				{
					FArrayProperty* History = FindFProperty<FArrayProperty>(Bridge->GetClass(), Name);
					if (!History) return 1;
					History->ClearValue_InContainer(Bridge);
				}
			}
		}
		// Inspect loaded values before ordinary PreSave callbacks can rebake
		// authoring data differently from cook. Names and object paths stay textual.
		for (UObject* Object : Objects)
		{
			TArray<uint8> LiveBytes;
			FMemoryWriter Writer(LiveBytes, true);
			FObjectAndNameAsStringProxyArchive Archive(Writer, false);
			Archive.ArNoDelta = true;
			Object->Serialize(Archive);
			if (Archive.IsError() || ContainsHostPath(LiveBytes))
			{
				UE_LOG(LogSeinConsumerAudit, Error, TEXT("%s retains a host path outside level origin and bridge history in live serialization."), *PackageName);
				return 1;
			}
		}
		const FString Copy = Scratch / FString::FromInt(Audited) + (World ? TEXT(".umap") : TEXT(".uasset"));
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		if (!UPackage::SavePackage(Package, World, *Copy, Args)
			|| GWarn->GetNumErrors() != ErrorsBefore) return 1;
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Copy)) return 1;
		if (ContainsHostPath(Bytes))
		{
			UE_LOG(LogSeinConsumerAudit, Error, TEXT("%s retains a host path outside level origin and bridge history."), *PackageName);
			return 1;
		}
		++Audited;
	}
	UE_LOG(LogSeinConsumerAudit, Display, TEXT("Passed %d candidate packages; source packages were not saved."), Audited);
	return 0;
}
