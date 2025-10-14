#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWindow.h"
#include "Templates/SharedPointer.h"

class USAFAsset;

class SSAFAssetPickerDialog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSAFAssetPickerDialog) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	static TSubclassOf<USAFAsset> OpenDialog(const FText& InTitle);

private:
	TSubclassOf<USAFAsset> GetSelectedClass() const { return SelectedClass; }
	
	FReply OnGenericClicked();
	FReply OnUnitClicked();
	FReply OnPawnClicked();
	FReply OnOKClicked();
	FReply OnCancelClicked();

	void OnClassPicked(UClass* InChosenClass);

private:
	TSubclassOf<USAFAsset> SelectedClass;
	TSharedPtr<SWindow> ParentWindow;
	bool bOKClicked = false;
};