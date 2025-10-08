#include "SAFAssetTypeActions.h"
#include "SeinARTS_Framework_Editor.h"
#include "Assets/SAFAsset.h"
#include "Classes/SAFActor.h"
#include "Modules/ModuleManager.h"

UClass* FAssetTypeActions_SAFAsset::GetSupportedClass() const { return USAFAsset::StaticClass(); }
uint32 FAssetTypeActions_SAFAsset::GetCategories() { 
	return EAssetTypeCategories::Basic | FSeinARTS_Framework_EditorModule::GetAssetCategoryBit(); 
}