#pragma once

#include "CoreMinimal.h"
#include "Factories/BlueprintFactory.h"
#include "SAFActorFactory.generated.h"

class ASAFActor;

UCLASS()
class SEINARTS_FRAMEWORK_EDITOR_API USAFActorFactory : public UBlueprintFactory {
	GENERATED_BODY()

public:

	USAFActorFactory();

	virtual bool ShouldShowInNewMenu() const override { return true; }
	virtual bool ConfigureProperties() override;
	virtual FText GetDisplayName() const override;
	virtual uint32 GetMenuCategories() const override;
	virtual FName GetNewAssetThumbnailOverride() const override;
    
};
