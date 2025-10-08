#include "SeinARTS_Framework_Editor.h"
#include "SeinARTS_Framework_EditorStyle.h"

#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetTypeCategories.h"
#include "SAFAssetTypeActions.h"
#include "Factories/Factory.h"
#include "Factories/SAFAssetFactory.h"

#define LOCTEXT_NAMESPACE "FSeinARTS_Framework_EditorModule"

EAssetTypeCategories::Type FSeinARTS_Framework_EditorModule::SeinARTSAssetCategoryBit = EAssetTypeCategories::Misc;

void FSeinARTS_Framework_EditorModule::StartupModule() {
	FSeinARTS_Framework_EditorStyle::Initialize();
	RegisterAssetTypeActions();
}

void FSeinARTS_Framework_EditorModule::ShutdownModule() {
	UnregisterAssetTypeActions();
	FSeinARTS_Framework_EditorStyle::Shutdown();
}

void FSeinARTS_Framework_EditorModule::RegisterAssetTypeAction(IAssetTools& AssetTools, const TSharedRef<IAssetTypeActions>& Action) {
	AssetTools.RegisterAssetTypeActions(Action);
	RegisteredAssetTypeActions.Add(Action);
}

void FSeinARTS_Framework_EditorModule::RegisterAssetTypeActions() {
	// Acquire AssetTools
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	// Create custom category (once)
	if (SeinARTSAssetCategoryBit == EAssetTypeCategories::Misc)
		SeinARTSAssetCategoryBit = AssetTools.RegisterAdvancedAssetCategory(FName("SeinARTS"), NSLOCTEXT("SeinARTS", "SeinARTSAssetCategory", "SeinARTS"));

	// Register Asset Type Actions for USAFAsset and ASAFActor blueprint classes (treated as assets for menu)
	{
		TSharedRef<FAssetTypeActions_SAFAsset> SAFAssetTypeActions = MakeShared<FAssetTypeActions_SAFAsset>();
		RegisterAssetTypeAction(AssetTools, SAFAssetTypeActions);
	}
}

void FSeinARTS_Framework_EditorModule::UnregisterAssetTypeActions() {
	if (FModuleManager::Get().IsModuleLoaded("AssetTools"))	{
		IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
		for (const TSharedPtr<IAssetTypeActions>& Action : RegisteredAssetTypeActions)
			if (Action.IsValid()) AssetTools.UnregisterAssetTypeActions(Action.ToSharedRef());
	}

	RegisteredAssetTypeActions.Empty();
}

EAssetTypeCategories::Type FSeinARTS_Framework_EditorModule::GetAssetCategoryBit() {
	return SeinARTSAssetCategoryBit;
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FSeinARTS_Framework_EditorModule, SeinARTS_Framework_Editor)