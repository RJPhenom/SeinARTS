#include "Dialogs/SSAFActorPickerDialog.h"
#include "Classes/SAFActor.h"
#include "Classes/Units/SAFUnit.h"
#include "Classes/Units/SAFPawn.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SWindow.h"
#include "Widgets/Images/SImage.h"
#include "Editor/ClassViewer/Public/ClassViewerModule.h"
#include "Editor/ClassViewer/Public/ClassViewerFilter.h"
#include "Styling/AppStyle.h"
#include "Framework/Docking/TabManager.h"
#include "SeinARTS_Framework_EditorStyle.h"

class FSAFActorClassFilter : public IClassViewerFilter
{
public:
	virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
	{
		return InClass && InClass->IsChildOf(ASAFActor::StaticClass()) && !InClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists);
	}

	virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef<const IUnloadedBlueprintData> InUnloadedClassData, TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
	{
		return InUnloadedClassData->IsChildOf(ASAFActor::StaticClass());
	}
};

void SSAFActorPickerDialog::Construct(const FArguments& InArgs)
{
	SelectedClass = ASAFActor::StaticClass();

	FClassViewerModule& ClassViewerModule = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer");

	FClassViewerInitializationOptions Options;
	Options.Mode = EClassViewerMode::ClassPicker;
	Options.ClassFilters.Add(MakeShareable(new FSAFActorClassFilter));
	Options.bShowObjectRootClass = false;
	Options.bShowUnloadedBlueprints = true;
	Options.bShowNoneOption = false;

	TSharedRef<SWidget> ClassViewer = ClassViewerModule.CreateClassViewer(Options, FOnClassPicked::CreateSP(this, &SSAFActorPickerDialog::OnClassPicked));

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
		.Padding(0)
		[
			SNew(SBox)
			.WidthOverride(500)
			[
				SNew(SGridPanel)
				.FillColumn(0, 1.0f)

				// Branded Header
				+ SGridPanel::Slot(0, 0)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
					.BorderBackgroundColor(FLinearColor(0.01f, 0.01f, 0.01f, 1.0f))
					.Padding(FMargin(5, 15))
					[
						SNew(SBox)
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						[
							SNew(SImage)
							.Image(FSeinARTS_Framework_EditorStyle::Get().GetBrush("SeinARTS.Wordmark"))
						]
					]
				]

				// Content Area
				+ SGridPanel::Slot(0, 1)
				.Padding(5, 5, 5, 5)
				[
					SNew(SGridPanel)
					.FillColumn(0, 1.0f)

					// Common section
					+ SGridPanel::Slot(0, 0)
					.Padding(2, 0, 2, 2)
					[
						SNew(SExpandableArea)
						.InitiallyCollapsed(false)
						.HeaderContent()
						[
							SNew(STextBlock)
							.Text(NSLOCTEXT("SeinARTS", "Common", "COMMON"))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
							.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						]
						.BodyContent()
						[
							SNew(SBorder)
							.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
							.BorderBackgroundColor(FLinearColor(0.0125f, 0.0125f, 0.0125f, 1.0f))
							.Padding(5, 0, 0, 5)
							[
								SNew(SGridPanel)
								.FillColumn(0, 0.3f)
								.FillColumn(1, 0.7f)
								.FillRow(0, 0.0f)
								.FillRow(1, 0.0f)
								.FillRow(2, 0.0f)

							+ SGridPanel::Slot(0, 0)
							.Padding(0, 3)
							.VAlign(VAlign_Center)
							[
								SNew(SButton)
								.ButtonStyle(FAppStyle::Get(), "Button")
								.ContentPadding(FMargin(3, 3, 3, 3))
								.OnClicked(this, &SSAFActorPickerDialog::OnGenericClicked)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot()
									.AutoWidth()
									.VAlign(VAlign_Center)
									.Padding(0, 0, 3, 0)
									[
										SNew(SImage)
										.Image(FSeinARTS_Framework_EditorStyle::Get().GetBrush("SeinARTS.GrayIcon"))
									]
									+ SHorizontalBox::Slot()
									.VAlign(VAlign_Center)
									[
										SNew(STextBlock)
										.Text(NSLOCTEXT("SeinARTS", "GenericClass", "Generic Class"))
										.Justification(ETextJustify::Left)
									]
								]
							]

							+ SGridPanel::Slot(1, 0)
							.Padding(10, 5, 5, 10)
							.VAlign(VAlign_Top)
							[
								SNew(STextBlock)
								.Text(NSLOCTEXT("SeinARTS", "GenericActorDesc", 
                                    "Actors that have an asset identity and can be selected. "
                                    "These might include environment objects like sandbads."))
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
								.ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f, 1.0f))
								.AutoWrapText(true)
								.WrapTextAt(0.0f)
							]

							+ SGridPanel::Slot(0, 1)
							.Padding(0, 3)
							.VAlign(VAlign_Center)
							[
								SNew(SButton)
								.ButtonStyle(FAppStyle::Get(), "Button")
								.ContentPadding(FMargin(3, 3, 3, 3))
								.OnClicked(this, &SSAFActorPickerDialog::OnUnitClicked)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot()
									.AutoWidth()
									.VAlign(VAlign_Center)
									.HAlign(HAlign_Left)
									.Padding(0, 0, 3, 0)
									[
										SNew(SImage)
										.Image(FSeinARTS_Framework_EditorStyle::Get().GetBrush("SeinARTS.GrayIcon"))
									]
									+ SHorizontalBox::Slot()
									.VAlign(VAlign_Center)
									[
										SNew(STextBlock)
										.Text(NSLOCTEXT("SeinARTS", "UnitClass", "Unit Class"))
										.Justification(ETextJustify::Left)
									]
								]
							]

							+ SGridPanel::Slot(1, 1)
							.Padding(10, 5, 5, 10)
							.VAlign(VAlign_Top)
							[
								SNew(STextBlock)
								.Text(NSLOCTEXT("SeinARTS", "UnitActorDesc", 
                                    "A logic-layer actor that has an Ability System Component "
                                    "and receives orders via Tagged Abilities."))
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
								.ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f, 1.0f))
								.AutoWrapText(true)
								.WrapTextAt(0.0f)
							]

							+ SGridPanel::Slot(0, 2)
							.Padding(0, 3)
							.VAlign(VAlign_Center)
							[
								SNew(SButton)
								.ButtonStyle(FAppStyle::Get(), "Button")
								.ContentPadding(FMargin(3, 3, 3, 3))
								.OnClicked(this, &SSAFActorPickerDialog::OnPawnClicked)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot()
									.AutoWidth()
									.VAlign(VAlign_Center)
									.Padding(0, 0, 3, 0)
									[
										SNew(SImage)
										.Image(FSeinARTS_Framework_EditorStyle::Get().GetBrush("SeinARTS.GrayIcon"))
									]
									+ SHorizontalBox::Slot()
									.VAlign(VAlign_Center)
									[
										SNew(STextBlock)
										.Text(NSLOCTEXT("SeinARTS", "PawnClass", "Pawn Class"))
										.Justification(ETextJustify::Left)
									]
								]
							]

							+ SGridPanel::Slot(1, 2)
							.Padding(10, 5, 5, 10)
							.VAlign(VAlign_Top)
							[
								SNew(STextBlock)
								.Text(NSLOCTEXT("SeinARTS", "PawnActorDesc", 
                                    "A graphics-layer actor that represents a Unit via mesh, capsule, "
                                    "movement and navigation."))
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
								.ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f, 1.0f))
								.AutoWrapText(true)
								.WrapTextAt(0.0f)
							]
						]
					]
				]

				// Separator
				+ SGridPanel::Slot(0, 1)
				.Padding(0, 5)
				[
					SNew(SSeparator)
					.ColorAndOpacity(FLinearColor(0.2f, 0.2f, 0.2f, 1.0f))
					.Thickness(1.0f)
				]

				// All Classes section
				+ SGridPanel::Slot(0, 2)
				.Padding(2, 5, 2, 0)
				[
					SNew(SExpandableArea)
					.InitiallyCollapsed(false)
					.HeaderContent()
					[
						SNew(STextBlock)
						.Text(NSLOCTEXT("SeinARTS", "AllClasses", "ALL CLASSES"))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					]
					.BodyContent()
					[
						SNew(SBox)
						.HeightOverride(280)
						[
							ClassViewer
						]
					]
				]

				// Buttons
				+ SGridPanel::Slot(0, 3)
					[
						SNew(SUniformGridPanel)
						.SlotPadding(FMargin(3, 0))

						+ SUniformGridPanel::Slot(0, 0)
						[
							SNew(SSpacer)
						]

						+ SUniformGridPanel::Slot(1, 0)
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "Button")
							.ContentPadding(FMargin(3, 3, 3, 3))
							.OnClicked(this, &SSAFActorPickerDialog::OnOKClicked)
							.IsEnabled_Lambda([this]() { return SelectedClass != nullptr; })
							[
								SNew(STextBlock)
								.Text(NSLOCTEXT("SeinARTS", "OK", "OK"))
								.Justification(ETextJustify::Left)
							]
						]

						+ SUniformGridPanel::Slot(2, 0)
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "Button")
							.ContentPadding(FMargin(3, 3, 6, 3))
							.OnClicked(this, &SSAFActorPickerDialog::OnCancelClicked)
							[
								SNew(STextBlock)
								.Text(NSLOCTEXT("SeinARTS", "Cancel", "Cancel"))
								.Justification(ETextJustify::Left)
							]
						]
					]
				]
			]
		]
	];
}

TSubclassOf<ASAFActor> SSAFActorPickerDialog::OpenDialog(const FText& InTitle)
{
	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(InTitle)
		.SizingRule(ESizingRule::Autosized)
		.SupportsMaximize(false)
		.SupportsMinimize(false);

	TSharedRef<SSAFActorPickerDialog> Dialog = SNew(SSAFActorPickerDialog);
	Dialog->ParentWindow = Window;

	Window->SetContent(Dialog);

	FSlateApplication::Get().AddModalWindow(Window, FGlobalTabmanager::Get()->GetRootWindow());

	return Dialog->bOKClicked ? Dialog->GetSelectedClass() : nullptr;
}

FReply SSAFActorPickerDialog::OnGenericClicked()
{
	SelectedClass = ASAFActor::StaticClass();
	bOKClicked = true;
	if (ParentWindow.IsValid())
	{
		ParentWindow->RequestDestroyWindow();
	}
	return FReply::Handled();
}

FReply SSAFActorPickerDialog::OnUnitClicked()
{
	SelectedClass = ASAFUnit::StaticClass();
	bOKClicked = true;
	if (ParentWindow.IsValid())
	{
		ParentWindow->RequestDestroyWindow();
	}
	return FReply::Handled();
}

FReply SSAFActorPickerDialog::OnPawnClicked()
{
	SelectedClass = ASAFPawn::StaticClass();
	bOKClicked = true;
	if (ParentWindow.IsValid())
	{
		ParentWindow->RequestDestroyWindow();
	}
	return FReply::Handled();
}

FReply SSAFActorPickerDialog::OnOKClicked()
{
	bOKClicked = true;
	if (ParentWindow.IsValid())
	{
		ParentWindow->RequestDestroyWindow();
	}
	return FReply::Handled();
}

FReply SSAFActorPickerDialog::OnCancelClicked()
{
	bOKClicked = false;
	if (ParentWindow.IsValid())
	{
		ParentWindow->RequestDestroyWindow();
	}
	return FReply::Handled();
}

void SSAFActorPickerDialog::OnClassPicked(UClass* InChosenClass)
{
	SelectedClass = InChosenClass;
}