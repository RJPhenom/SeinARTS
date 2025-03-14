


#include "SAFCameraPawnCustomization.h"

void FSAFCameraPawnCustomization::SortDetailsCategories(const TMap<FName, IDetailCategoryBuilder*>& AllCategoryMap) {
	(*AllCategoryMap.Find(FName("Transform")))->SetSortOrder(0);
	(*AllCategoryMap.Find(FName("SeinARTS")))->SetSortOrder(1);
}

void FSAFCameraPawnCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) {
	DetailBuilder.SortCategories(&SortDetailsCategories);
}

TSharedRef<IDetailCustomization> FSAFCameraPawnCustomization::MakeInstance() {
	return MakeShareable(new FSAFCameraPawnCustomization);
}