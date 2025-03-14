

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "DetailCategorybuilder.h"
#include "DetailLayoutBuilder.h"

/**
 * 
 */
class SEINARTS_FRAMEWORK_EDITOR_API FSAFCameraPawnCustomization : public IDetailCustomization
{
public:

	static void SortDetailsCategories(const TMap<FName, IDetailCategoryBuilder*>& AllCategoryMap);
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

	static TSharedRef<IDetailCustomization> MakeInstance();
};
