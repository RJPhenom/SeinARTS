#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "AssetTypeCategories.h"

class IAssetTypeActions;

class FSeinARTS_Framework_EditorModule : public IModuleInterface {

public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	// Accessor for asset category bit used by asset type actions
	static EAssetTypeCategories::Type GetAssetCategoryBit();

private:

	void RegisterAssetTypeAction(class IAssetTools& AssetTools, const TSharedRef<class IAssetTypeActions>& Action);
	void RegisterAssetTypeActions();
	void UnregisterAssetTypeActions();

	TArray<TSharedPtr<class IAssetTypeActions>> RegisteredAssetTypeActions;
	static EAssetTypeCategories::Type SeinARTSAssetCategoryBit;
	
};