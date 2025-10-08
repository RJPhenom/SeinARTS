#include "Factories/SAFActorFactory.h"
#include "Classes/SAFActor.h"
#include "Engine/Blueprint.h"
#include "SeinARTS_Framework_Editor.h"

USAFActorFactory::USAFActorFactory() {
	bCreateNew    = true;
	bEditAfterNew = true;
	SupportedClass = UBlueprint::StaticClass();
	BlueprintType  = EBlueprintType::BPTYPE_Normal;
	ParentClass    = ASAFActor::StaticClass();
}

bool USAFActorFactory::ConfigureProperties() {
	if (!ParentClass || !ParentClass->IsChildOf<AActor>()) ParentClass = ASAFActor::StaticClass();
	return true;
}

FText USAFActorFactory::GetDisplayName() const {
	return NSLOCTEXT("SeinARTS", "CreateSeinARTSClass", "SeinARTS Class");
}

uint32 USAFActorFactory::GetMenuCategories() const {
	return FSeinARTS_Framework_EditorModule::GetAssetCategoryBit();
}

FName USAFActorFactory::GetNewAssetThumbnailOverride() const {
	return TEXT("ClassThumbnail.SAFActor");
}
