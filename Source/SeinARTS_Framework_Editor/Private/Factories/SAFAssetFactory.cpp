#include "Factories/SAFAssetFactory.h"
#include "Assets/SAFAsset.h"
#include "SeinARTS_Framework_Editor.h"
#include "Dialogs/SSAFAssetPickerDialog.h"

USAFAssetFactory::USAFAssetFactory() {
	bCreateNew    = true;
	bEditAfterNew = true;
	SupportedClass = USAFAsset::StaticClass();
	ParentClass = USAFAsset::StaticClass();
	Formats.Add(TEXT("safasset;SeinARTS Asset"));
}

bool USAFAssetFactory::ConfigureProperties()
{
	ParentClass = SSAFAssetPickerDialog::OpenDialog(NSLOCTEXT("SeinARTS", "CreateSAFAsset", "Create SeinARTS Asset"));
	return ParentClass != nullptr;
}

UObject* USAFAssetFactory::FactoryCreateNew(
	UClass* InClass, UObject* InParent, FName Name,
	EObjectFlags Flags, UObject* /*Context*/, FFeedbackContext* /*Warn*/
) {
	UClass* ClassToUse = ParentClass ? ParentClass.Get() : USAFAsset::StaticClass();
	return NewObject<UObject>(InParent, ClassToUse, Name, Flags | RF_Transactional);
}

FText USAFAssetFactory::GetDisplayName() const {
	return NSLOCTEXT("SeinARTS", "CreateSeinARTSAsset", "SeinARTS Asset");
}

uint32 USAFAssetFactory::GetMenuCategories() const {
	return EAssetTypeCategories::Basic | FSeinARTS_Framework_EditorModule::GetAssetCategoryBit();
}

FName USAFAssetFactory::GetNewAssetThumbnailOverride() const {
	return TEXT("ClassThumbnail.SAFAsset");
}
