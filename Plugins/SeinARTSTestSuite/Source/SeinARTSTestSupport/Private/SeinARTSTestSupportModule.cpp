#include "CoreGlobals.h"
#include "Misc/CoreDelegates.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Serialization/SeinSimulationContentManifest.h"
#include "Serialization/SeinSimulationContentRegistry.h"
#include "Settings/PluginSettings.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinARTSTestSupport, Log, All);

namespace
{
	constexpr TCHAR TransientWorldPackage[] = TEXT("/Engine/Transient");

	// The production record codec requires the same non-zero 20-byte payload
	// shape as an Asset Registry PackageSavedHash. These fixture bytes are
	// intentionally synthetic: tests exercise the production manifest and
	// compatibility paths, not editor package hashing.
	constexpr uint8 SyntheticSavedPackageHash[
		FSeinSimulationContentManifestCodec::SavedPackageHashBytes] = {
			0x53, 0x65, 0x69, 0x6e, 0x41, 0x52, 0x54, 0x53,
			0x54, 0x65, 0x73, 0x74, 0x46, 0x69, 0x78, 0x74,
			0x75, 0x72, 0x65, 0x01};

	void AddValidPackage(
		const FString& PackageName,
		TArray<FString>& InOutPackages)
	{
		if (FPackageName::IsValidLongPackageName(PackageName))
		{
			InOutPackages.AddUnique(PackageName);
		}
	}

	void AddValidPackage(
		const FSoftObjectPath& ObjectPath,
		TArray<FString>& InOutPackages)
	{
		if (ObjectPath.IsValid())
		{
			AddValidPackage(
				ObjectPath.GetLongPackageName(),
				InOutPackages);
		}
	}

	bool BuildSyntheticRecord(
		const FString& PackageName,
		FSeinSimulationContentRecord& OutRecord,
		FString& OutError)
	{
		OutRecord = {};
		OutRecord.StableRecordKindId =
			FSeinSimulationContentManifestCodec::
				GetCurrentRecordKindId();
		OutRecord.RecordRevision =
			static_cast<int32>(
				FSeinSimulationContentManifestCodec::
					CurrentRecordRevision);
		OutRecord.CanonicalRecordId = PackageName;
		return FSeinSimulationContentManifestCodec::
			ComputeRecordDigest(
				OutRecord.StableRecordKindId,
				FSeinSimulationContentManifestCodec::
					CurrentRecordRevision,
				PackageName,
				MakeArrayView(SyntheticSavedPackageHash),
				OutRecord.ContentDigest,
				OutError);
	}
}

class FSeinARTSTestSupportModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		if (IsEngineStartupModuleLoadingComplete())
		{
			InstallSimulationContentFixture();
			return;
		}

		AllModulesLoadedHandle =
			FCoreDelegates::OnAllModuleLoadingPhasesComplete.AddRaw(
				this,
				&FSeinARTSTestSupportModule::
					OnAllModuleLoadingPhasesComplete);
	}

	virtual void ShutdownModule() override
	{
		RemoveAllModulesLoadedDelegate();
		RestoreSimulationContentSetting();
	}

private:
	void OnAllModuleLoadingPhasesComplete()
	{
		RemoveAllModulesLoadedDelegate();
		InstallSimulationContentFixture();
	}

	void RemoveAllModulesLoadedDelegate()
	{
		if (AllModulesLoadedHandle.IsValid())
		{
			FCoreDelegates::OnAllModuleLoadingPhasesComplete.Remove(
				AllModulesLoadedHandle);
			AllModulesLoadedHandle.Reset();
		}
	}

	void InstallSimulationContentFixture()
	{
		if (TransientManifest.IsValid())
		{
			return;
		}

		FString Error;
		FSeinSimulationContentRegistrySnapshot RegistrySnapshot;
		TArray<FSeinSimulationContentContributorRecord> Contributors;
		if (!FSeinSimulationContentRegistry::CaptureSnapshot(
				RegistrySnapshot,
				Error)
			|| !FSeinSimulationContentRegistry::
				BuildManifestContributorRecords(
					RegistrySnapshot,
					Contributors,
					Error))
		{
			UE_LOG(
				LogSeinARTSTestSupport,
				Error,
				TEXT("Could not create the test Simulation Content fixture from the active production registry: %s"),
				*Error);
			return;
		}

		USeinARTSCoreSettings* Settings =
			GetMutableDefault<USeinARTSCoreSettings>();
		if (!Settings)
		{
			UE_LOG(
				LogSeinARTSTestSupport,
				Error,
				TEXT("Could not create the test Simulation Content fixture because the SeinARTS settings CDO is unavailable."));
			return;
		}

		TArray<FString> CoveredPackages;
		AddValidPackage(TransientWorldPackage, CoveredPackages);
		for (const FSeinLobbyMapEntry& Entry : Settings->AvailableMaps)
		{
			AddValidPackage(
				Entry.Map.ToSoftObjectPath(),
				CoveredPackages);
		}
		AddValidPackage(
			Settings->DefaultGameplayMap.ToSoftObjectPath(),
			CoveredPackages);
		for (const FSoftObjectPath& ExplicitRoot :
			Settings->AdditionalSimulationContentRoots)
		{
			AddValidPackage(ExplicitRoot, CoveredPackages);
		}

		FSeinSimulationContentManifestProfile Profile;
		Profile.BuilderRevision = static_cast<int32>(
			FSeinSimulationContentManifestCodec::
				CurrentBuilderRevision);
		Profile.Contributors = MoveTemp(Contributors);
		Profile.Records.Reserve(CoveredPackages.Num());
		for (const FString& PackageName : CoveredPackages)
		{
			FSeinSimulationContentRecord& Record =
				Profile.Records.AddDefaulted_GetRef();
			if (!BuildSyntheticRecord(PackageName, Record, Error))
			{
				UE_LOG(
					LogSeinARTSTestSupport,
					Error,
					TEXT("Could not create the test Simulation Content record for '%s': %s"),
					*PackageName,
					*Error);
				return;
			}
		}

		if (!FSeinSimulationContentManifestCodec::SealProfile(
				FSeinSimulationContentManifestCodec::
					CurrentFormatVersion,
				Profile,
				Error))
		{
			UE_LOG(
				LogSeinARTSTestSupport,
				Error,
				TEXT("Could not seal the test Simulation Content profile: %s"),
				*Error);
			return;
		}

		TStrongObjectPtr<USeinSimulationContentManifest> Candidate(
			NewObject<USeinSimulationContentManifest>(
				GetTransientPackage(),
				NAME_None,
				RF_Transient));
		if (!Candidate.IsValid())
		{
			UE_LOG(
				LogSeinARTSTestSupport,
				Error,
				TEXT("Could not allocate the transient test Simulation Content manifest."));
			return;
		}
		Candidate->FormatVersion = static_cast<int32>(
			FSeinSimulationContentManifestCodec::
				CurrentFormatVersion);
		if (!FSeinSimulationContentManifestCodec::UpsertProfile(
				*Candidate,
				Profile,
				Error)
			|| !Candidate->Validate(Error))
		{
			UE_LOG(
				LogSeinARTSTestSupport,
				Error,
				TEXT("Could not validate the transient test Simulation Content manifest: %s"),
				*Error);
			return;
		}

		PreviousManifestSetting = Settings->SimulationContentManifest;
		ModifiedSettings = Settings;
		TransientManifest = MoveTemp(Candidate);
		Settings->SimulationContentManifest =
			TSoftObjectPtr<USeinSimulationContentManifest>(
				TransientManifest.Get());

		UE_LOG(
			LogSeinARTSTestSupport,
			Verbose,
			TEXT("Installed transient Simulation Content fixture (%d contributors, %d records, digest=%s)."),
			Profile.Contributors.Num(),
			Profile.Records.Num(),
			*Profile.RootDigest.ToString(EGuidFormats::Digits));
	}

	void RestoreSimulationContentSetting()
	{
		if (USeinARTSCoreSettings* Settings = ModifiedSettings.Get())
		{
			Settings->SimulationContentManifest =
				PreviousManifestSetting;
		}
		ModifiedSettings.Reset();
		PreviousManifestSetting.Reset();
		TransientManifest.Reset();
	}

	FDelegateHandle AllModulesLoadedHandle;
	TStrongObjectPtr<USeinSimulationContentManifest> TransientManifest;
	TWeakObjectPtr<USeinARTSCoreSettings> ModifiedSettings;
	TSoftObjectPtr<USeinSimulationContentManifest>
		PreviousManifestSetting;
};

IMPLEMENT_MODULE(FSeinARTSTestSupportModule, SeinARTSTestSupport)
