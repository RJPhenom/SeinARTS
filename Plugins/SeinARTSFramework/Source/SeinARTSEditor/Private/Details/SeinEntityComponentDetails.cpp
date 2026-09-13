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
#include "Authoring/SeinEntityComponent.h"
#include "Details/SeinAutoTagDetails.h"
#include "DetailLayoutBuilder.h"
#include "Details/SeinSemanticDefault.h"
#include "UObject/UnrealType.h"
#include "ClassViewerFilter.h"
#include "PropertyRestriction.h"
#include "Util/SeinDefaultMoveAbilityAuthoring.h"
#include "Components/SeinMovementPayload.h"

namespace
{
	class FDefaultMoveAbilityFilter final : public IClassViewerFilter
	{
	public:
		TArray<TWeakObjectPtr<UObject>> Contexts;
		virtual bool IsClassAllowed(const FClassViewerInitializationOptions&, const UClass* Class,
			TSharedRef<FClassViewerFilterFuncs>) override
		{
			if (Contexts.IsEmpty() || !SeinDefaultMoveAbilityAuthoring::IsEligibleClass(Class)) return false;
			FText Error;
			for (const auto& Context : Contexts)
				if (!Context.IsValid() || !SeinDefaultMoveAbilityAuthoring::ValidateSelection(*Context, Class, Error)) return false;
			return true;
		}
		virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions&,
			const TSharedRef<const IUnloadedBlueprintData>, TSharedRef<FClassViewerFilterFuncs>) override
		{
			// GrantedAbilities holds class references, so every eligible grant is loaded.
			return false;
		}
	};
}

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

	if (ComponentClass->IsChildOf(USeinIdentityComponent::StaticClass()))
		SeinAutoTagDetails::AddIdentityActions(DetailBuilder);

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
			if (Property->Struct == FSeinMovementPayload::StaticStruct())
			{
				const auto Selection = PayloadHandle->GetChildHandle(
					GET_MEMBER_NAME_CHECKED(FSeinMovementPayload, DefaultMoveAbility));
				if (Selection.IsValid())
				{
					TArray<UObject*> Contexts;
					Selection->GetOuterObjects(Contexts);
					const auto Filter = MakeShared<FDefaultMoveAbilityFilter>();
					for (UObject* Context : Contexts) Filter->Contexts.Add(Context);
					const auto Restriction = MakeShared<FPropertyRestriction>(
						FText::FromString(TEXT("Choose a granted, non-passive Point ability.")));
					Restriction->AddClassFilter(Filter);
					Selection->AddRestriction(Restriction);
				}
			}
			if (IDetailPropertyRow* Row =
				DetailBuilder.EditDefaultProperty(PayloadHandle))
			{
				SeinSemanticDefault::ApplyResetOverride(*Row);
			}
		}

		break;
	}
}
