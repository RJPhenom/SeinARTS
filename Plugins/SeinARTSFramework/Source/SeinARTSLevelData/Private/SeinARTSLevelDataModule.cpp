#include "SeinARTSLevelDataModule.h"
#include "SeinARTSLevelDataLog.h"

#include "Modules/ModuleManager.h"
#include "SeinLayerConfig.h"
#include "SeinLevelData.h"
#include "SeinLevelDataSubsystem.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Serialization/SeinLevelDataCanonicalStateProvider.h"
#include "UObject/UObjectIterator.h"

#if WITH_EDITOR
#include "PropertyEditorModule.h"
#include "Volumes/SeinLevelVolume.h"
#include "Details/SeinLevelVolumeDetails.h"
#endif

// Module-shared log categories (declared in SeinARTSLevelDataLog.h) — defined
// once here so they register at module load and always show in the Output Log
// category filter.
DEFINE_LOG_CATEGORY(LogSeinLevelData);
DEFINE_LOG_CATEGORY(LogSeinLevelDataSubsystem);

namespace
{
	FSeinSimulationContentDiscoveryRoot MakePackageDiscoveryRoot(
		const UClass* RootClass)
	{
		check(RootClass);

		FSeinSimulationContentDiscoveryRoot Root;
		Root.RootClassPath = RootClass->GetPathName();
		Root.StableRecordKindId =
			FSeinSimulationContentManifestCodec::GetCurrentRecordKindId();
		Root.RecordRevision =
			FSeinSimulationContentManifestCodec::CurrentRecordRevision;
		return Root;
	}
}

void FSeinARTSLevelDataModule::StartupModule()
{
	SimulationContentRegistrationHandle.Reset();
	CanonicalStateRegistrationHandle.Reset();

	FString CanonicalStateError;
	CanonicalStateRegistrationHandle =
		SeinRegisterLevelDataCanonicalStateProvider(
			CanonicalStateError);
	if (!CanonicalStateRegistrationHandle.IsValid())
	{
		UE_LOG(LogSeinLevelData, Error,
			TEXT("Level Data canonical-state provider failed to register: %s"),
			*CanonicalStateError);
	}

	FSeinSimulationContentContributorDescriptor ContentDescriptor;
	ContentDescriptor.StableContributorId = TEXT("seinarts.leveldata");
	ContentDescriptor.ContributorRevision = 1;
	ContentDescriptor.DiscoveryRoots = {
		MakePackageDiscoveryRoot(USeinLevelData::StaticClass()),
		MakePackageDiscoveryRoot(USeinLayerConfig::StaticClass()),
	};

	FString ContentRegistrationError;
	SimulationContentRegistrationHandle =
		FSeinSimulationContentRegistry::RegisterContributor(
			ContentDescriptor,
			&ContentRegistrationError);
	if (!SimulationContentRegistrationHandle.IsValid())
	{
		UE_LOG(
			LogSeinLevelData,
			Error,
			TEXT("Simulation-content contributor '%s' failed to register: %s"),
			*ContentDescriptor.StableContributorId,
			*ContentRegistrationError);
	}

#if WITH_EDITOR
	// Details panel for ASeinLevelVolume: nests the "Bake Level Data" button
	// above BakedAsset in a "Bake" sub-group under the shared "SeinARTS"
	// category (see FSeinLevelVolumeDetails for why this can't be a plain
	// CallInEditor button).
	FPropertyEditorModule& PropertyModule =
		FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomClassLayout(
		ASeinLevelVolume::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FSeinLevelVolumeDetails::MakeInstance));
	PropertyModule.NotifyCustomizationModuleChanged();
#endif
}

void FSeinARTSLevelDataModule::PreUnloadCallback()
{
	ReleaseModuleOwnedState();
}

void FSeinARTSLevelDataModule::ShutdownModule()
{
	ReleaseModuleOwnedState();
}

void FSeinARTSLevelDataModule::ReleaseModuleOwnedState()
{
	check(IsInGameThread());
	for (TObjectIterator<USeinWorldSubsystem> It; It; ++It)
	{
		if (!It->HasAnyFlags(RF_ClassDefaultObject))
		{
			It->TerminateAndReleaseForModuleUnload(
				TEXT("SeinARTSLevelData"),
				TEXT("the deterministic level substrate is unloading"));
		}
	}
	for (TObjectIterator<USeinLevelDataSubsystem> It; It; ++It)
	{
		if (!It->HasAnyFlags(RF_ClassDefaultObject))
		{
			It->ReleaseModuleOwnedStateForModuleUnload();
		}
	}

#if WITH_EDITOR
	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule =
			FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomClassLayout(ASeinLevelVolume::StaticClass()->GetFName());
		PropertyModule.NotifyCustomizationModuleChanged();
	}
#endif

	SimulationContentRegistrationHandle.Reset();
	CanonicalStateRegistrationHandle.Reset();
}

IMPLEMENT_MODULE(FSeinARTSLevelDataModule, SeinARTSLevelData)
