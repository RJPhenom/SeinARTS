/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinEntityComponentDetails.cpp
 * @author       RJ Macklem
 * @created      4 Sep 2026
 * @latest       4 Sep 2026
 * @brief        Applies semantic reset behavior to the embedded payload of
 *               every shipped SeinARTS entity authoring component.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "Details/SeinEntityComponentDetails.h"

#include "Components/SeinPayload.h"
#include "DetailLayoutBuilder.h"
#include "Details/SeinSemanticDefault.h"
#include "UObject/UnrealType.h"

TSharedRef<IDetailCustomization> FSeinEntityComponentDetails::MakeInstance()
{
	return MakeShared<FSeinEntityComponentDetails>();
}

void FSeinEntityComponentDetails::CustomizeDetails(
	IDetailLayoutBuilder& DetailBuilder)
{
	const UClass* ComponentClass = DetailBuilder.GetBaseClass();
	if (!ComponentClass)
	{
		return;
	}

	// Native authoring components expose exactly one editable payload struct
	// flattened with ShowOnlyInnerProperties. Find it by contract rather than
	// by field name so extension-owned components inherit this behavior without
	// adding a framework dependency on their payload types.
	for (TFieldIterator<FStructProperty> It(ComponentClass,
		EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		FStructProperty* Property = *It;
		if (!Property || !Property->HasAnyPropertyFlags(CPF_Edit)
			|| !Property->HasMetaData(TEXT("ShowOnlyInnerProperties"))
			|| !Property->Struct
			|| !Property->Struct->IsChildOf(FSeinPayload::StaticStruct()))
		{
			continue;
		}

		const TSharedRef<IPropertyHandle> PayloadHandle =
			DetailBuilder.GetProperty(Property->GetFName(),
				Property->GetOwnerStruct());
		if (PayloadHandle->IsValidHandle())
		{
			if (IDetailPropertyRow* Row =
				DetailBuilder.EditDefaultProperty(PayloadHandle))
			{
				SeinSemanticDefault::ApplyResetOverride(*Row);
			}
		}

		break;
	}
}
