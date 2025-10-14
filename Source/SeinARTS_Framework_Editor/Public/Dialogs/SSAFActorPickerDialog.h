#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWindow.h"
#include "Templates/SharedPointer.h"

class ASAFActor;

class SSAFActorPickerDialog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSAFActorPickerDialog) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	static TSubclassOf<ASAFActor> OpenDialog(const FText& InTitle);

private:
	TSubclassOf<ASAFActor> GetSelectedClass() const { return SelectedClass; }
	
	FReply OnGenericClicked();
	FReply OnUnitClicked();
	FReply OnPawnClicked();
	FReply OnOKClicked();
	FReply OnCancelClicked();

	void OnClassPicked(UClass* InChosenClass);

private:
	TSubclassOf<ASAFActor> SelectedClass;
	TSharedPtr<SWindow> ParentWindow;
	bool bOKClicked = false;
};