#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "SAFAssetFactory.generated.h"

UCLASS()
class SEINARTS_FRAMEWORK_EDITOR_API USAFAssetFactory : public UFactory {
	GENERATED_BODY()
	
public:

	USAFAssetFactory();

	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual bool ShouldShowInNewMenu() const override { return true; }
	virtual FText GetDisplayName() const override;
	virtual uint32 GetMenuCategories() const override;
	virtual FName  GetNewAssetThumbnailOverride() const override;

};
