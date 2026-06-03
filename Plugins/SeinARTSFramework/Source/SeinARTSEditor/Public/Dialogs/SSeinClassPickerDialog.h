/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SSeinClassPickerDialog.h
 * @date:		3/27/2026
 * @author:		RJ Macklem
 * @brief:		Generic modal dialog for picking a parent class within a hierarchy.
 */

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

/**
 * Slate modal dialog for selecting a class within a given hierarchy.
 * Provides a quick-select button for the base class and a full class
 * hierarchy viewer for advanced selection.
 *
 * Used by all SeinARTS factories (Unit, Ability, etc.).
 */
class SSeinClassPickerDialog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSeinClassPickerDialog) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UClass* InBaseClass, const FText& InQuickButtonLabel, const FText& InQuickButtonTooltip);

	/**
	 * Open the modal dialog and return the selected class.
	 * @param InTitle - Title for the dialog window
	 * @param InBaseClass - Root class to filter the hierarchy
	 * @param InQuickButtonLabel - Label for the quick-select button
	 * @param InQuickButtonTooltip - Tooltip for the quick-select button
	 * @return Selected UClass, or nullptr if cancelled
	 */
	static UClass* OpenDialog(const FText& InTitle, UClass* InBaseClass, const FText& InQuickButtonLabel, const FText& InQuickButtonTooltip);

private:
	UClass* BaseClass = nullptr;
	UClass* SelectedClass = nullptr;
	TSharedPtr<SWindow> ParentWindow;
	bool bOKClicked = false;

	FReply OnQuickButtonClicked(UClass* InClass);
	FReply OnOKClicked();
	FReply OnCancelClicked();
	void OnClassPicked(UClass* InChosenClass);
};
