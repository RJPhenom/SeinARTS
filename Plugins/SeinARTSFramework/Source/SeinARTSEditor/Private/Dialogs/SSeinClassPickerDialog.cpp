/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SSeinClassPickerDialog.cpp
 * @date:		3/27/2026
 * @author:		RJ Macklem
 * @brief:		Implementation of the generic class picker dialog.
 */

#include "Dialogs/SSeinClassPickerDialog.h"
#include "SeinARTSEditorStyle.h"

#include "ClassViewerModule.h"
#include "ClassViewerFilter.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "SeinARTSEditor"

// ==================== Class Filter ====================

class FSeinClassFilter : public IClassViewerFilter
{
public:
	FSeinClassFilter(UClass* InBaseClass)
		: FilterBaseClass(InBaseClass)
	{
	}

	virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
	{
		return InClass &&
			!InClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_Hidden) &&
			InClass->IsChildOf(FilterBaseClass);
	}

	virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef<const IUnloadedBlueprintData> InUnloadedClassData, TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
	{
		return InUnloadedClassData->IsChildOf(FilterBaseClass);
	}

private:
	UClass* FilterBaseClass;
};

// ==================== Widget ====================

void SSeinClassPickerDialog::Construct(const FArguments& InArgs, UClass* InBaseClass, const FText& InQuickButtonLabel, const FText& InQuickButtonTooltip)
{
	BaseClass = InBaseClass;
	SelectedClass = InBaseClass;

	// Class viewer options. ListView (not TreeView) keeps the list flat: only
	// classes the filter allows are shown. TreeView would also display parent
	// classes for navigational context even when filtered out, which defeats
	// the "only descendants of the base class" intent.
	FClassViewerInitializationOptions Options;
	Options.Mode = EClassViewerMode::ClassPicker;
	Options.DisplayMode = EClassViewerDisplayMode::ListView;
	Options.ClassFilters.Add(MakeShared<FSeinClassFilter>(InBaseClass));
	Options.bShowUnloadedBlueprints = true;
	Options.bShowNoneOption = false;
	Options.bShowObjectRootClass = false;
	Options.bShowDefaultClasses = false;

	FClassViewerModule& ClassViewerModule = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer");
	TSharedRef<SWidget> ClassViewer = ClassViewerModule.CreateClassViewer(
		Options,
		FOnClassPicked::CreateSP(this, &SSeinClassPickerDialog::OnClassPicked)
	);

	const FSlateBrush* WordmarkBrush = FSeinARTSEditorStyle::Get().GetBrush("SeinARTS.Wordmark");

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(8.0f)
		[
			SNew(SVerticalBox)

			// Wordmark header. Fixed size matching the brush's native 488x126
			// aspect ratio (scaled down to half). Using SBox overrides with the
			// correct ratio prevents the image from being squashed horizontally.
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 4, 0, 12)
			.HAlign(HAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(244.0f)
				.HeightOverride(63.0f)
				[
					SNew(SImage)
					.Image(WordmarkBrush)
				]
			]

			// Quick buttons
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 8)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 4)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("QuickSelect", "Quick Select"))
					.Font(FAppStyle::GetFontStyle("BoldFont"))
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SUniformGridPanel)
					.SlotPadding(FMargin(4.0f))

					+ SUniformGridPanel::Slot(0, 0)
					[
						SNew(SButton)
						.HAlign(HAlign_Center)
						.Text(InQuickButtonLabel)
						.ToolTipText(InQuickButtonTooltip)
						.OnClicked(this, &SSeinClassPickerDialog::OnQuickButtonClicked, static_cast<UClass*>(InBaseClass))
					]
				]
			]

			// Class viewer
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(0, 0, 0, 8)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 4)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AllClasses", "All Classes"))
					.Font(FAppStyle::GetFontStyle("BoldFont"))
				]

				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SNew(SBox)
					.MinDesiredHeight(200.0f)
					[
						ClassViewer
					]
				]
			]

			// OK / Cancel
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("OK", "OK"))
					.OnClicked(this, &SSeinClassPickerDialog::OnOKClicked)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("Cancel", "Cancel"))
					.OnClicked(this, &SSeinClassPickerDialog::OnCancelClicked)
				]
			]
		]
	];
}

UClass* SSeinClassPickerDialog::OpenDialog(const FText& InTitle, UClass* InBaseClass, const FText& InQuickButtonLabel, const FText& InQuickButtonTooltip)
{
	TSharedRef<SSeinClassPickerDialog> Dialog = SNew(SSeinClassPickerDialog, InBaseClass, InQuickButtonLabel, InQuickButtonTooltip);

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(InTitle)
		.ClientSize(FVector2D(400, 550))
		.SupportsMinimize(false)
		.SupportsMaximize(false);

	Window->SetContent(Dialog);
	Dialog->ParentWindow = Window;

	GEditor->EditorAddModalWindow(Window);

	if (Dialog->bOKClicked && Dialog->SelectedClass)
	{
		return Dialog->SelectedClass;
	}

	return nullptr;
}

FReply SSeinClassPickerDialog::OnQuickButtonClicked(UClass* InClass)
{
	SelectedClass = InClass;
	bOKClicked = true;

	if (ParentWindow.IsValid())
	{
		ParentWindow->RequestDestroyWindow();
	}

	return FReply::Handled();
}

FReply SSeinClassPickerDialog::OnOKClicked()
{
	bOKClicked = true;

	if (ParentWindow.IsValid())
	{
		ParentWindow->RequestDestroyWindow();
	}

	return FReply::Handled();
}

FReply SSeinClassPickerDialog::OnCancelClicked()
{
	bOKClicked = false;

	if (ParentWindow.IsValid())
	{
		ParentWindow->RequestDestroyWindow();
	}

	return FReply::Handled();
}

void SSeinClassPickerDialog::OnClassPicked(UClass* InChosenClass)
{
	SelectedClass = InChosenClass;
}

#undef LOCTEXT_NAMESPACE
