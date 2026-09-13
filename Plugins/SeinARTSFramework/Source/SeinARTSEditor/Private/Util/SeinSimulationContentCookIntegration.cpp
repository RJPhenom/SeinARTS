/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinSimulationContentCookIntegration.cpp
 * @author       RJ Macklem
 * @created      6 Sep 2026
 * @latest       6 Sep 2026
 * @brief        Builds compatibility evidence in the cook sandbox without source asset writes.
 * @disclaimer   Generated with assistance from an AI language model.
 */
#include "Util/SeinSimulationContentEditorGuards.h"
#include "Util/SeinSimulationContentManifestBuilder.h"
#include "Serialization/SeinSimulationContentBuildArtifact.h"
#include "Serialization/SeinSimulationContentRegistry.h"
#include "AssetRegistry/AssetRegistryHelpers.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "CookOnTheSide/CookOnTheFlyServer.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "UObject/ICookInfo.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinContentCook, Log, All);

namespace
{
	bool OwnsOutput(const UE::Cook::ICookInfo& Info)
	{
		return Info.GetProcessType() != UE::Cook::EProcessType::Worker
			&& Info.GetCookType() == UE::Cook::ECookType::ByTheBook;
	}
	FString OutputPath(const UE::Cook::ICookInfo& Info, const ITargetPlatform* Platform)
	{
		return Info.GetCookOutputFolder(Platform) / FApp::GetProjectName()
			/ TEXT("Content") / FSeinSimulationContentBuildArtifact::RelativeFilename();
	}
	void RemoveOutput(const UE::Cook::ICookInfo& Info)
	{
		for (const ITargetPlatform* Platform : Info.GetSessionPlatforms())
		{
			const FString Path = OutputPath(Info, Platform);
			if (!IFileManager::Get().Delete(*Path, false, true))
				UE_LOG(LogSeinContentCook, Error, TEXT("Cannot invalidate old cook compatibility data: %s"), *Path);
		}
	}
}

struct FSeinSimulationContentCookIntegration::FState final : FOutputDevice
{
	struct FSavedInput { FString Name; FIoHash Hash; };
	TMap<FName, FSavedInput> SavedInputs;
	TSet<FName> DirtyInputs;
	struct FTargetInputs
	{
		FSeinSimulationContentRegistrySnapshot Snapshot;
		FSeinSimulationContentManifestProfile Profile;
		FSeinSimulationContentManifestBuildResult Discovery;
	};
	TMap<const ITargetPlatform*, FTargetInputs> Targets;
	TAtomic<bool> Active{false};
	TAtomic<bool> Failed{false};
	bool Started = false;
	bool Discovered = false;

	virtual bool CanBeUsedOnAnyThread() const override { return true; }
	virtual void Serialize(const TCHAR*, ELogVerbosity::Type Verbosity, const FName&) override
	{
		if (Active.Load() && Verbosity <= ELogVerbosity::Error) Failed.Store(true);
	}
	bool Discover(UE::Cook::ICookInfo& Info)
	{
		if (Discovered) return true;
		FString Error;
		FSeinSimulationContentRegistrySnapshot All;
		if (!FSeinSimulationContentRegistry::CaptureSnapshot(All, Error))
		{
			UE_LOG(LogSeinContentCook, Error, TEXT("Cannot capture cook contributors: %s"), *Error);
			return false;
		}
		for (const ITargetPlatform* Platform : Info.GetSessionPlatforms())
		{
			const auto* Enabled = Info.GetEnabledPlugins(Platform);
			if (!Enabled)
			{
				UE_LOG(LogSeinContentCook, Error, TEXT("Cook target plugin admission is unavailable."));
				return false;
			}
			auto& Target = Targets.Add(Platform);
			for (const auto& Contributor : All.Contributors)
			{
				if (Contributor.OwnerModule.IsNone())
				{
					UE_LOG(LogSeinContentCook, Error, TEXT("Simulation contributor '%s' must declare its OwnerModule for cook target admission."), *Contributor.StableContributorId);
					return false;
				}
				const auto Owner = IPluginManager::Get().GetModuleOwnerPlugin(Contributor.OwnerModule);
				if (Owner && !Enabled->Contains(Owner.Get())) continue;
				Target.Snapshot.Contributors.Add(Contributor);
				for (const auto& Root : Contributor.DiscoveryRoots) Target.Snapshot.DiscoveryRoots.AddUnique(Root);
				for (const auto& Root : Contributor.ExplicitPackageRoots) Target.Snapshot.ExplicitPackageRoots.AddUnique(Root);
			}
			// Filtering the canonical global unions preserves the registry's order.
			const auto ClaimedRoots = MoveTemp(Target.Snapshot.DiscoveryRoots);
			const auto ClaimedPackages = MoveTemp(Target.Snapshot.ExplicitPackageRoots);
			Target.Snapshot.DiscoveryRoots = All.DiscoveryRoots.FilterByPredicate(
				[&](const auto& Root) { return ClaimedRoots.Contains(Root); });
			Target.Snapshot.ExplicitPackageRoots = All.ExplicitPackageRoots.FilterByPredicate(
				[&](const auto& Root) { return ClaimedPackages.Contains(Root); });
			if (!FSeinSimulationContentManifestBuilder::BuildCookInputProfile(Target.Snapshot, Target.Profile, Target.Discovery, Error))
			{
				UE_LOG(LogSeinContentCook, Error, TEXT("Cannot prepare simulation inputs for cook: %s"), *Error);
				return false;
			}
			for (FName Package : Target.Discovery.ContentPackages)
			{
				FString Filename;
				if (!FPackageName::DoesPackageExist(Package.ToString(), &Filename))
				{
					UE_LOG(LogSeinContentCook, Error, TEXT("Cook input disappeared: %s"), *Package.ToString());
					return false;
				}
				FPaths::MakeStandardFilename(Filename);
				static_cast<UCookOnTheFlyServer&>(Info).RequestPackage(FName(*Filename),
					TArrayView<const ITargetPlatform* const>(&Platform, 1), false);
			}
		}
		Discovered = true;
		return true;
	}
};

FSeinSimulationContentCookIntegration::FSeinSimulationContentCookIntegration()
	: State(MakeUnique<FState>())
{
	GLog->AddOutputDevice(State.Get());
	ConfigureHandle = UE::Cook::FDelegates::ConfigureCookSession.AddLambda([this](UE::Cook::ICookInfo& Info)
	{
		State->Active.Store(false);
		State->Failed.Store(false);
		State->Started = false;
		State->Discovered = false;
		State->SavedInputs.Reset();
		State->Targets.Reset();
		State->DirtyInputs.Reset();
		if (!OwnsOutput(Info)) return;
		State->Active.Store(true);
		RemoveOutput(Info);
		if (Info.GetCookingDLC() == UE::Cook::ECookingDLC::Yes)
		{
			UE_LOG(LogSeinContentCook, Error, TEXT("SeinARTS compatibility generation requires a complete base-game cook. DLC profile composition is not supported."));
		}
	});
	StartedHandle = UE::Cook::FDelegates::CookStarted.AddLambda([this](UE::Cook::ICookInfo& Info)
	{
		if (!OwnsOutput(Info)) return;
		State->Started = true;
		RemoveOutput(Info);
		for (const ITargetPlatform* Platform : Info.GetSessionPlatforms())
		{
			// Cancellation broadcasts CookFinished but does not produce this marker.
			const FString Marker = Info.GetCookMetadataOutputFolder(Platform) / UE::Cook::GetReferencedSetFilename();
			if (!IFileManager::Get().Delete(*Marker, false, true))
				UE_LOG(LogSeinContentCook, Error, TEXT("Cannot reset cook completion evidence: %s"), *Marker);
		}
		if (!State->Failed.Load()) State->Discover(Info);
		if (State->Failed.Load())
		{
			static_cast<UCookOnTheFlyServer&>(Info).QueueCancelCookByTheBook();
			return;
		}
		// Capture disk evidence before the cooker changes loaded objects. Later
		// cooker-induced dirtiness must not be confused with unsaved authoring.
		for (TObjectIterator<UPackage> It; It; ++It)
			if (It->IsDirty()) State->DirtyInputs.Add(It->GetFName());
		IAssetRegistry& Registry = IAssetRegistry::GetChecked();
		TArray<FAssetData> Assets;
		Registry.GetAllAssets(Assets, true);
		for (const FAssetData& Asset : Assets)
		{
			if (State->SavedInputs.Contains(Asset.PackageName)) continue;
			const TOptional<FAssetPackageData> Data = Registry.GetAssetPackageDataCopy(Asset.PackageName);
			if (Data.IsSet()) State->SavedInputs.Add(Asset.PackageName,
				{Asset.PackageName.ToString(), Data->GetPackageSavedHash()});
		}
	});
	FinishedHandle = UE::Cook::FDelegates::CookFinished.AddLambda([this](UE::Cook::ICookInfo& Info)
	{
		if (!OwnsOutput(Info)) return;
		if (!State->Started || State->Failed.Load())
		{
			RemoveOutput(Info);
			State->Active.Store(false);
			return;
		}
		FString Error;
		for (const ITargetPlatform* Platform : Info.GetSessionPlatforms())
		{
			if (State->Failed.Load()) break;
			const auto& Target = State->Targets.FindChecked(Platform);
			FSeinSimulationContentManifestProfile Profile;
			Profile.BuilderRevision = FSeinSimulationContentManifestCodec::CurrentBuilderRevision;
			Profile.Contributors = Target.Profile.Contributors;
			TArray<FString> Referenced;
			const FString Marker = Info.GetCookMetadataOutputFolder(Platform) / UE::Cook::GetReferencedSetFilename();
			if (!FFileHelper::LoadFileToStringArray(Referenced, *Marker)
				|| Referenced.IsEmpty() || Referenced[0] != TEXT("# Version 1"))
			{
				UE_LOG(LogSeinContentCook, Error, TEXT("Cook did not produce a completed referenced-package set; compatibility data was not emitted."));
				break;
			}
			TArray<FName> CookedPackages;
			for (const FString& Name : Referenced)
				if (!Name.IsEmpty() && !Name.StartsWith(TEXT("#"))) CookedPackages.Add(FName(*Name));
			TArray<FName> SourcePackages;
			if (!FSeinSimulationContentManifestBuilder::CollectCookSourcePackages(
				Target.Snapshot, Target.Discovery.ContentPackages, CookedPackages, SourcePackages, Error))
			{
				UE_LOG(LogSeinContentCook, Error, TEXT("Cannot complete cook source graph: %s"), *Error);
				break;
			}
			TArray<FName> AddedSources = SourcePackages.FilterByPredicate(
				[&](FName Package) { return !Target.Discovery.ContentPackages.Contains(Package); });
			if (!FSeinSimulationContentManifestBuilder::ValidateCookSourceContracts(AddedSources, Error))
			{
				UE_LOG(LogSeinContentCook, Error, TEXT("Cook source contract validation failed: %s"), *Error);
				break;
			}
			IAssetRegistry& Registry = IAssetRegistry::GetChecked();
			for (FName Package : SourcePackages)
			{
				const FState::FSavedInput* Input = State->SavedInputs.Find(Package);
				if (!Input)
				{
					UE_LOG(LogSeinContentCook, Error, TEXT("Simulation source '%s' was not present in the saved-input snapshot."), *Package.ToString());
					break;
				}
				const TOptional<FAssetPackageData> Current = Registry.GetAssetPackageDataCopy(Package);
				if (State->DirtyInputs.Contains(Package) || Input->Hash.IsZero()
					|| !Current.IsSet() || Current->GetPackageSavedHash() != Input->Hash)
				{
					UE_LOG(LogSeinContentCook, Error, TEXT("Cook input '%s' was unsaved, unhashed, or changed during cooking. Save the source and cook again."), *Input->Name);
					break;
				}
				auto& Record = Profile.Records.AddDefaulted_GetRef();
				Record.CanonicalRecordId = Input->Name;
				Record.StableRecordKindId = FSeinSimulationContentManifestCodec::GetCurrentRecordKindId();
				Record.RecordRevision = FSeinSimulationContentManifestCodec::CurrentRecordRevision;
				if (!FSeinSimulationContentManifestCodec::ComputeRecordDigest(Record.StableRecordKindId,
					Record.RecordRevision, Record.CanonicalRecordId,
					MakeArrayView(Input->Hash.GetBytes(), FSeinSimulationContentManifestCodec::SavedPackageHashBytes),
					Record.ContentDigest, Error))
					UE_LOG(LogSeinContentCook, Error, TEXT("Cannot hash cook input: %s"), *Error);
			}
			TArray<uint8> Bytes;
			if (State->Failed.Load()) break;
			if (!FSeinSimulationContentManifestCodec::SealProfile(FSeinSimulationContentManifestCodec::CurrentFormatVersion, Profile, Error)
				|| !FSeinSimulationContentBuildArtifact::Encode(Profile, Bytes, Error))
			{
				UE_LOG(LogSeinContentCook, Error, TEXT("Cannot seal cooked compatibility data: %s"), *Error);
				break;
			}
			const FString Destination = OutputPath(Info, Platform);
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Destination), true);
			const FString Temporary = Destination + TEXT(".tmp");
			if (!FFileHelper::SaveArrayToFile(Bytes, *Temporary)
				|| !IFileManager::Get().Move(*Destination, *Temporary, true, true))
			{
				IFileManager::Get().Delete(*Temporary, false, true);
				UE_LOG(LogSeinContentCook, Error, TEXT("Cannot write cooked compatibility data: %s"), *Destination);
				break;
			}
			UE_LOG(LogSeinContentCook, Display, TEXT("Generated cooked compatibility data (%d packages): %s"), Profile.Records.Num(), *Destination);
		}
		if (State->Failed.Load()) RemoveOutput(Info);
		State->Active.Store(false);
	});
}

FSeinSimulationContentCookIntegration::~FSeinSimulationContentCookIntegration()
{
	UE::Cook::FDelegates::ConfigureCookSession.Remove(ConfigureHandle);
	UE::Cook::FDelegates::CookStarted.Remove(StartedHandle);
	UE::Cook::FDelegates::CookFinished.Remove(FinishedHandle);
	GLog->RemoveOutputDevice(State.Get());
}
