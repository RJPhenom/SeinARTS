#include "SeinARTSLevelDataModule.h"
#include "SeinARTSLevelDataLog.h"
#include "Modules/ModuleManager.h"

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

void FSeinARTSLevelDataModule::StartupModule()
{
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

void FSeinARTSLevelDataModule::ShutdownModule()
{
#if WITH_EDITOR
	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule =
			FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomClassLayout(ASeinLevelVolume::StaticClass()->GetFName());
	}
#endif
}

IMPLEMENT_MODULE(FSeinARTSLevelDataModule, SeinARTSLevelData)
