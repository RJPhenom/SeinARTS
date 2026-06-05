/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinInstancedStructDetails.cpp
 * @brief   FInstancedStruct customization that permits UserDefinedStructs in
 *          the picker even when BaseStruct is set.
 */

#include "Details/SeinInstancedStructDetails.h"

#include "DetailWidgetRow.h"
#include "DetailLayoutBuilder.h"
#include "IDetailChildrenBuilder.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "Modules/ModuleManager.h"

#include "Widgets/Input/SComboButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/SlateIconFinder.h"

#include "StructUtils/InstancedStruct.h"
#include "StructUtils/UserDefinedStruct.h"
#include "StructViewerModule.h"
#include "StructViewerFilter.h"
#include "SInstancedStructPicker.h"   // FInstancedStructFilter
#include "InstancedStructDetails.h"   // FInstancedStructDataDetails
#include "Factories/SeinSimComponentFactory.h"

#define LOCTEXT_NAMESPACE "SeinInstancedStructDetails"

DEFINE_LOG_CATEGORY_STATIC(LogSeinEditorPicker, Log, All);

// =============================================================================
// Custom filter
// =============================================================================
//
// UE's FInstancedStructFilter hardcodes `bAllowUserDefinedStructs = false`
// whenever BaseStruct is set. Additionally, UE's UUserDefinedStruct compiler
// (see UserDefinedStructureCompilerUtils.cpp) explicitly calls
// `StructToClean->SetSuperStruct(nullptr)` during every compile — so UDS can
// never satisfy an IsChildOf(BaseStruct) check anyway.
//
// We work around both issues by discriminating by asset kind:
//   - Native UScriptStruct: use IsChildOf(BaseStruct) like UE does
//   - UUserDefinedStruct:   accept all (see fallback note below).
//
class FSeinInstancedStructFilter : public IStructViewerFilter
{
public:
	TWeakObjectPtr<const UScriptStruct> BaseStruct = nullptr;
	bool bAllowBaseStruct = false;
	/** When true, only SeinDeterministic-marked structs are accepted (both
	 *  native USTRUCTs with the meta and UDSes tagged by USeinSimComponentFactory).
	 *  Set via `meta = (SeinDeterministicOnly)` on the FInstancedStruct property. */
	bool bRestrictToSeinDeterministic = false;
	/** When true, the picker is the entity bridge's ComponentData picker. The
	 *  filter then requires entity-component eligibility per
	 *  `USeinSimComponentFactory::IsSeinEntityComponentStruct` — native structs
	 *  must subclass FSeinComponent, UDSes must carry the SeinEntityComponent
	 *  meta, and SeinSubData-marked structs are excluded outright.
	 *  Set via `meta = (SeinEntityComponentsOnly)` on the FInstancedStruct
	 *  (or TArray<FInstancedStruct>) property. */
	bool bRestrictToEntityComponents = false;

	virtual bool IsStructAllowed(
		const FStructViewerInitializationOptions& /*InInitOptions*/,
		const UScriptStruct* InStruct,
		TSharedRef<FStructViewerFilterFuncs> /*InFilterFuncs*/) override
	{
		if (!InStruct)
		{
			return false;
		}

		// Entity-bridge ComponentData path: strictest filter. Requires
		// FSeinComponent inheritance (natives) or SeinEntityComponent meta
		// (UDSes), AND SeinDeterministic in both cases. Excludes
		// SeinSubData-marked structs (per-class movement sub-data etc.).
		if (bRestrictToEntityComponents)
		{
			return USeinSimComponentFactory::IsSeinEntityComponentStruct(InStruct);
		}

		// Sein-restricted path: gate by the SeinDeterministic UField meta.
		// Covers both native USTRUCTs marked via `USTRUCT(meta = (SeinDeterministic))`
		// and UDSes tagged by USeinSimComponentFactory on creation. Sub-data
		// pickers (e.g. FSeinMovementComponent::MovementClassData) use this
		// — they want any deterministic struct, including SeinSubData ones.
		if (bRestrictToSeinDeterministic)
		{
			return USeinSimComponentFactory::IsSeinDeterministicStruct(InStruct);
		}

		// Blueprint-authored struct path: accept all UserDefinedStructs.
		// C++ structs still go through the BaseStruct filter below.
		if (InStruct->IsA<UUserDefinedStruct>())
		{
			return true;
		}

		// Native struct path: IsChildOf(BaseStruct).
		if (const UScriptStruct* Base = BaseStruct.Get())
		{
			if (!InStruct->IsChildOf(Base))
			{
				return false;
			}
			if (!bAllowBaseStruct && InStruct == Base)
			{
				return false;
			}
			return true;
		}

		// No base specified — accept all native structs.
		return true;
	}

	virtual bool IsUnloadedStructAllowed(
		const FStructViewerInitializationOptions& /*InInitOptions*/,
		const FSoftObjectPath& /*InStructPath*/,
		TSharedRef<FStructViewerFilterFuncs> /*InFilterFuncs*/) override
	{
		// Unloaded structs surface by soft path only — we can't see their
		// USTRUCT meta (SeinDeterministic / SeinSubData / SeinEntityComponent)
		// without forcing a load. In restricted modes that gate on those
		// metas, allow-by-default leaks phantom structs from the global asset
		// registry into our picker (observed: "Module Settings",
		// "Root Module Settings" from unrelated plugin UDSes cached in the
		// project's saved asset registry). Reject in restricted modes; allow
		// in the open-ended path so generic FInstancedStruct pickers still
		// behave like UE's default.
		if (bRestrictToEntityComponents || bRestrictToSeinDeterministic)
		{
			return false;
		}
		return true;
	}
};

TSharedRef<IPropertyTypeCustomization> FSeinInstancedStructDetails::MakeInstance()
{
	UE_LOG(LogSeinEditorPicker, Verbose, TEXT("MakeInstance: creating FSeinInstancedStructDetails"));
	return MakeShared<FSeinInstancedStructDetails>();
}

void FSeinInstancedStructDetails::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	StructProperty = PropertyHandle;
	PropUtils = CustomizationUtils.GetPropertyUtilities();

	// Resolve the current struct once now, and refresh the cache whenever the
	// property value changes (edit / undo / reset-to-default). The paint-time
	// header getters read the cache instead of re-running EnumerateConstRawData
	// every paint. A struct *type* pick rebuilds this whole customization via
	// ForceRefresh (OnStructPicked), which re-runs CustomizeHeader and re-primes
	// the cache, so it can never describe a stale type.
	RefreshCachedScriptStruct();
	PropertyHandle->SetOnPropertyValueChanged(
		FSimpleDelegate::CreateSP(this, &FSeinInstancedStructDetails::RefreshCachedScriptStruct));

	UE_LOG(LogSeinEditorPicker, Verbose,
		TEXT("CustomizeHeader invoked. Property=%s  BaseStructMeta=%s"),
		*PropertyHandle->GetPropertyDisplayName().ToString(),
		PropertyHandle->HasMetaData(TEXT("BaseStruct"))
			? *PropertyHandle->GetMetaData(TEXT("BaseStruct"))
			: TEXT("<none>"));

	HeaderRow
	.NameContent()
	[
		PropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(250.f)
	.VAlign(VAlign_Center)
	[
		SAssignNew(ComboButton, SComboButton)
		.OnGetMenuContent(this, &FSeinInstancedStructDetails::GenerateStructPicker)
		.ContentPadding(FMargin(2.f, 2.f))
		.ToolTipText(this, &FSeinInstancedStructDetails::GetTooltipText)
		.ButtonContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.f, 0.f, 4.f, 0.f)
			[
				SNew(SImage)
				.Image(this, &FSeinInstancedStructDetails::GetDisplayValueIcon)
			]
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.FillWidth(1.f)
			[
				SNew(STextBlock)
				.Text(this, &FSeinInstancedStructDetails::GetDisplayValueString)
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
		]
	]
	.IsEnabled(PropertyHandle->IsEditable());
}

void FSeinInstancedStructDetails::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// Delegate child-row rendering to UE's public node builder so we inherit
	// proper handling of nested FInstancedStruct fields, category grouping,
	// and struct-value change notifications.
	TSharedRef<FInstancedStructDataDetails> DataDetails =
		MakeShared<FInstancedStructDataDetails>(PropertyHandle);
	ChildBuilder.AddCustomBuilder(DataDetails);
}

// =============================================================================
// Picker construction (the piece that differs from UE's default)
// =============================================================================

const UScriptStruct* FSeinInstancedStructDetails::GetBaseStructFromMeta() const
{
	static const FName NAME_BaseStruct("BaseStruct");
	if (StructProperty.IsValid() && StructProperty->HasMetaData(NAME_BaseStruct))
	{
		const FString& BaseStructName = StructProperty->GetMetaData(NAME_BaseStruct);
		return UClass::TryFindTypeSlow<UScriptStruct>(BaseStructName);
	}
	return nullptr;
}

const UScriptStruct* FSeinInstancedStructDetails::GetCurrentScriptStruct() const
{
	if (!StructProperty.IsValid())
	{
		return nullptr;
	}

	const UScriptStruct* Found = nullptr;
	StructProperty->EnumerateConstRawData(
		[&Found](const void* RawData, const int32 /*DataIndex*/, const int32 /*NumDatas*/)
		{
			if (const FInstancedStruct* Inst = static_cast<const FInstancedStruct*>(RawData))
			{
				Found = Inst->GetScriptStruct();
			}
			return false; // stop after first
		});
	return Found;
}

void FSeinInstancedStructDetails::RefreshCachedScriptStruct()
{
	CachedScriptStruct = GetCurrentScriptStruct();
}

FText FSeinInstancedStructDetails::GetDisplayValueString() const
{
	if (CachedScriptStruct)
	{
		return CachedScriptStruct->GetDisplayNameText();
	}
	return LOCTEXT("NoneOption", "None");
}

FText FSeinInstancedStructDetails::GetTooltipText() const
{
	if (CachedScriptStruct)
	{
		return CachedScriptStruct->GetToolTipText();
	}
	return LOCTEXT("PickerTooltip",
		"Select a struct type. BP-authored structs (inheriting from the "
		"required base) are included.");
}

const FSlateBrush* FSeinInstancedStructDetails::GetDisplayValueIcon() const
{
	if (CachedScriptStruct)
	{
		return FSlateIconFinder::FindIconBrushForClass(CachedScriptStruct, "ClassIcon.Object");
	}
	return nullptr;
}

TSharedRef<SWidget> FSeinInstancedStructDetails::GenerateStructPicker()
{
	static const FName NAME_ExcludeBaseStruct("ExcludeBaseStruct");
	static const FName NAME_SeinDeterministicOnly("SeinDeterministicOnly");
	static const FName NAME_SeinEntityComponentsOnly("SeinEntityComponentsOnly");
	const bool bExcludeBaseStruct =
		StructProperty.IsValid() && StructProperty->HasMetaData(NAME_ExcludeBaseStruct);
	const bool bRestrictToSeinDeterministic =
		StructProperty.IsValid() && StructProperty->HasMetaData(NAME_SeinDeterministicOnly);
	const bool bRestrictToEntityComponents =
		StructProperty.IsValid() && StructProperty->HasMetaData(NAME_SeinEntityComponentsOnly);

	const UScriptStruct* BaseStruct = GetBaseStructFromMeta();

	UE_LOG(LogSeinEditorPicker, Verbose,
		TEXT("GenerateStructPicker: BaseStruct=%s  ExcludeBaseStruct=%d  SeinDeterministicOnly=%d  SeinEntityComponentsOnly=%d"),
		BaseStruct ? *BaseStruct->GetName() : TEXT("<null>"),
		bExcludeBaseStruct ? 1 : 0,
		bRestrictToSeinDeterministic ? 1 : 0,
		bRestrictToEntityComponents ? 1 : 0);

	TSharedRef<FSeinInstancedStructFilter> Filter = MakeShared<FSeinInstancedStructFilter>();
	Filter->BaseStruct = BaseStruct;
	Filter->bAllowBaseStruct = !bExcludeBaseStruct;
	Filter->bRestrictToSeinDeterministic = bRestrictToSeinDeterministic;
	Filter->bRestrictToEntityComponents = bRestrictToEntityComponents;

	FStructViewerInitializationOptions Options;
	Options.bShowNoneOption = true;
	Options.bShowUnloadedStructs = true;
	Options.NameTypeToDisplay = EStructViewerNameTypeToDisplay::DisplayName;
	Options.DisplayMode = EStructViewerDisplayMode::ListView;
	Options.StructFilter = Filter;
	Options.SelectedStruct = GetCurrentScriptStruct();
	Options.PropertyHandle = StructProperty;

	FStructViewerModule& StructViewerModule =
		FModuleManager::LoadModuleChecked<FStructViewerModule>(TEXT("StructViewer"));

	return SNew(SBox)
		.WidthOverride(280.f)
		.MaxDesiredHeight(500.f)
		[
			StructViewerModule.CreateStructViewer(
				Options,
				FOnStructPicked::CreateSP(this, &FSeinInstancedStructDetails::OnStructPicked))
		];
}

void FSeinInstancedStructDetails::OnStructPicked(const UScriptStruct* InStruct)
{
	if (StructProperty.IsValid() && StructProperty->IsValidHandle())
	{
		FScopedTransaction Transaction(LOCTEXT("OnStructPicked", "Set Struct"));
		StructProperty->NotifyPreChange();

		StructProperty->EnumerateRawData(
			[InStruct](void* RawData, const int32 /*DataIndex*/, const int32 /*NumDatas*/)
			{
				if (FInstancedStruct* Inst = static_cast<FInstancedStruct*>(RawData))
				{
					Inst->InitializeAs(InStruct);
				}
				return true;
			});

		StructProperty->NotifyPostChange(EPropertyChangeType::ValueSet);
		StructProperty->NotifyFinishedChangingProperties();
		StructProperty->SetExpanded(true);

		if (PropUtils.IsValid())
		{
			PropUtils->ForceRefresh();
		}
	}

	if (ComboButton.IsValid())
	{
		ComboButton->SetIsOpen(false);
	}
}

#undef LOCTEXT_NAMESPACE
