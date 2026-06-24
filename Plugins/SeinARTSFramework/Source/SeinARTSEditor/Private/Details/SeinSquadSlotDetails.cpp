/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSquadSlotDetails.cpp
 *
 * Hides FSeinSquadSlot::OffsetTransform when the owning squad's chosen Formation
 * Class doesn't use authored slot offsets. See the header for the rationale.
 */

#include "Details/SeinSquadSlotDetails.h"

#include "Components/SeinSquadComponent.h"
#include "Formations/SeinFormation.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"

TSharedRef<IPropertyTypeCustomization> FSeinSquadSlotDetails::MakeInstance()
{
	return MakeShared<FSeinSquadSlotDetails>();
}

void FSeinSquadSlotDetails::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// Array-element row: the index name. The array wrapper keeps the insert/delete/duplicate controls
	// around this; the fields render on expand via CustomizeChildren.
	HeaderRow
	.NameContent()
	[
		PropertyHandle->CreatePropertyNameWidget()
	];
}

void FSeinSquadSlotDetails::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	const bool bShowOffset = OwningFormationUsesSlotOffsets(PropertyHandle);

	// Rebuild the panel when the squad's Formation Class changes so the offset shows/hides live. Bind on
	// the parent's FormationClass handle (slot -> Slots array -> FSeinSquadComponent). Setter is single-
	// delegate, so when several slots bind, the last wins — one ForceRefresh rebuilds them all.
	if (const TSharedPtr<IPropertyHandle> SlotsArray = PropertyHandle->GetParentHandle())
	{
		if (const TSharedPtr<IPropertyHandle> Component = SlotsArray->GetParentHandle())
		{
			if (const TSharedPtr<IPropertyHandle> FormationClassHandle =
				Component->GetChildHandle(GET_MEMBER_NAME_CHECKED(FSeinSquadComponent, FormationClass)))
			{
				const TSharedPtr<IPropertyUtilities> Utils = CustomizationUtils.GetPropertyUtilities();
				FormationClassHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([Utils]()
				{
					if (Utils.IsValid()) { Utils->ForceRefresh(); }
				}));
			}
		}
	}

	// Re-emit each editable child (matching default visibility: skip the runtime-only, non-CPF_Edit
	// fields), dropping OffsetTransform when the chosen formation ignores authored offsets.
	static const FName OffsetName = GET_MEMBER_NAME_CHECKED(FSeinSquadSlot, OffsetTransform);
	uint32 NumChildren = 0;
	PropertyHandle->GetNumChildren(NumChildren);
	for (uint32 i = 0; i < NumChildren; ++i)
	{
		const TSharedPtr<IPropertyHandle> Child = PropertyHandle->GetChildHandle(i);
		if (!Child.IsValid()) { continue; }
		const FProperty* ChildProp = Child->GetProperty();
		if (!ChildProp || !ChildProp->HasAnyPropertyFlags(CPF_Edit)) { continue; }
		if (!bShowOffset && ChildProp->GetFName() == OffsetName) { continue; }
		ChildBuilder.AddProperty(Child.ToSharedRef());
	}
}

bool FSeinSquadSlotDetails::OwningFormationUsesSlotOffsets(TSharedRef<IPropertyHandle> SlotHandle)
{
	// slot -> Slots array -> FSeinSquadComponent
	const TSharedPtr<IPropertyHandle> SlotsArray = SlotHandle->GetParentHandle();
	const TSharedPtr<IPropertyHandle> Component  = SlotsArray.IsValid() ? SlotsArray->GetParentHandle() : nullptr;
	if (!Component.IsValid()) { return true; }

	const TSharedPtr<IPropertyHandle> FormationClassHandle =
		Component->GetChildHandle(GET_MEMBER_NAME_CHECKED(FSeinSquadComponent, FormationClass));
	if (!FormationClassHandle.IsValid()) { return true; }

	TArray<void*> RawData;
	FormationClassHandle->AccessRawData(RawData);
	if (RawData.Num() == 0 || RawData[0] == nullptr) { return true; }

	const TSoftClassPtr<USeinFormation>* SoftPtr =
		static_cast<const TSoftClassPtr<USeinFormation>*>(RawData[0]);
	if (!SoftPtr || SoftPtr->IsNull()) { return true; }   // EMPTY = the slot formation (default) -> show

	UClass* FormationClass = SoftPtr->LoadSynchronous();
	if (!FormationClass) { return true; }                 // unresolved -> don't hide

	const USeinFormation* CDO = FormationClass->GetDefaultObject<USeinFormation>();
	return CDO ? CDO->UsesAuthoredSlotOffsets() : true;
}
