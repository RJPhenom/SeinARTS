/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinTagAuditCommandlet.cpp
 * @author       RJ Macklem
 * @created      7 Sep 2026
 * @latest       7 Sep 2026
 * @brief        Writes a generated tag audit and optionally reconciles eligible entries.
 * @disclaimer   Generated with assistance from an AI language model.
 */
#include "Commandlets/SeinTagAuditCommandlet.h"
#include "Util/SeinTagAudit.h"
#include "Util/SeinAutoTagGenerator.h"
#include "Engine/Blueprint.h"
#include "GameplayTagsManager.h"
#include "UObject/SavePackage.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"

USeinTagAuditCommandlet::USeinTagAuditCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 USeinTagAuditCommandlet::Main(const FString& Params)
{
	FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get().SearchAllAssets(true);
	FString RepairAsset, PreviousTag;
	if (FParse::Value(*Params, TEXT("RepairAsset="), RepairAsset))
	{
		if (!FParse::Value(*Params, TEXT("PreviousTag="), PreviousTag)) return 1;
		auto* Blueprint = LoadObject<UBlueprint>(nullptr, *RepairAsset);
		if (!Blueprint) return 1;
		const auto Result = SeinAutoTag::ReconcileGeneratedIdentity(Blueprint, FName(*PreviousTag));
		UE_LOG(LogTemp, Display, TEXT("%s"), *Result.ToUserMessage(Blueprint).ToString());
		if (Result.IsFailure()) return 1;
		SeinAutoTag::FinishPendingTagMigrations();
		if (Blueprint->Status == BS_Error) return 1;
		const FString File = FPackageName::LongPackageNameToFilename(Blueprint->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		if (!UPackage::SavePackage(Blueprint->GetOutermost(), Blueprint, *File, SaveArgs)) return 1;
	}
	FString VerifyAsset, ExpectedTag, RetiredTag;
	if (FParse::Value(*Params, TEXT("VerifyAsset="), VerifyAsset))
	{
		if (!FParse::Value(*Params, TEXT("ExpectedTag="), ExpectedTag)
			|| !FParse::Value(*Params, TEXT("RetiredTag="), RetiredTag)) return 1;
		auto* Blueprint = LoadObject<UBlueprint>(nullptr, *VerifyAsset);
		FGameplayTag Identity;
		bool bGenerated = false;
		auto& Manager = UGameplayTagsManager::Get();
		FGameplayTagContainer Dictionary;
		Manager.RequestAllGameplayTags(Dictionary, false);
		bool bRetiredPresent = false;
		for (const auto& Tag : Dictionary)
			bRetiredPresent |= Tag.ToString() == RetiredTag || Tag.ToString().StartsWith(RetiredTag + TEXT("."));
		if (!SeinAutoTag::ReadAssetIdentity(Blueprint, Identity, bGenerated) || !bGenerated
			|| Identity.GetTagName() != FName(*ExpectedTag) || bRetiredPresent
			|| Manager.RequestGameplayTag(FName(*RetiredTag), false) != Identity
			|| Blueprint->Status == BS_Error)
		{
			UE_LOG(LogTemp, Error, TEXT("Persisted identity/redirect verification failed for %s."), *VerifyAsset);
			return 1;
		}
		UE_LOG(LogTemp, Display, TEXT("Verified saved generated identity %s; retired hierarchy %s is absent from the picker and resolves to the current identity."), *Identity.ToString(), *RetiredTag);
	}

	const auto Rows = SeinTagAudit::Inspect();
	FString Report = SeinTagAudit::Format(Rows);
	FString CleanupTag;
	const bool bSingleTag = FParse::Value(*Params, TEXT("CleanupTag="), CleanupTag);
	if (bSingleTag || FParse::Param(*Params, TEXT("Cleanup")))
	{
		const FString Blocker = SeinTagAudit::MutationBlocker();
		if (!Blocker.IsEmpty()) { UE_LOG(LogTemp, Error, TEXT("%s"), *Blocker); return 1; }
		TArray<FName> Candidates;
		for (const auto& Row : Rows)
			if (Row.bCanDelete && (!bSingleTag || Row.Tag == FName(*CleanupTag))) Candidates.Add(Row.Tag);
		Report += TEXT("\nCleanup receipt\n") + SeinTagAudit::Cleanup(Candidates);
		Report += TEXT("\nAfter cleanup\n") + SeinTagAudit::Format(SeinTagAudit::Inspect());
	}
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("TagAudit");
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString Path = Directory / (FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".txt"));
	if (!FFileHelper::SaveStringToFile(Report, *Path)) return 1;
	UE_LOG(LogTemp, Display, TEXT("Tag audit: %s"), *FPaths::ConvertRelativePathToFull(Path));
	return 0;
}
