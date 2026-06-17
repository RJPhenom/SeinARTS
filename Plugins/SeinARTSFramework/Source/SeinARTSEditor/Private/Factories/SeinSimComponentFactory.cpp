/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSimComponentFactory.cpp
 * @brief   Implementation of the UDS-based component factory.
 */

#include "Factories/SeinSimComponentFactory.h"
#include "SeinARTSEditorModule.h"
#include "Settings/PluginSettings.h"
#include "Components/SeinComponent.h"
#include "Components/SeinComponentEligibility.h"
#include "Kismet2/StructureEditorUtils.h"
#include "StructUtils/UserDefinedStruct.h"

#define LOCTEXT_NAMESPACE "SeinARTSEditor"

const FName USeinSimComponentFactory::SeinDeterministicMetaKey(TEXT("SeinDeterministic"));
const FName USeinSimComponentFactory::SeinEntityComponentMetaKey(TEXT("SeinEntityComponent"));
const FName USeinSimComponentFactory::SeinSubDataMetaKey(TEXT("SeinSubData"));

USeinSimComponentFactory::USeinSimComponentFactory()
{
	SupportedClass = UUserDefinedStruct::StaticClass();
	bCreateNew = FStructureEditorUtils::UserDefinedStructEnabled();
	bEditAfterNew = true;
}

UObject* USeinSimComponentFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	ensure(UUserDefinedStruct::StaticClass() == Class);
	UUserDefinedStruct* NewUDS = FStructureEditorUtils::CreateUserDefinedStruct(InParent, Name, Flags);
	if (!NewUDS) return nullptr;

	// Tag the UDS at the struct level so both pin-type and struct-viewer filters
	// can detect it via UStruct::HasMetaData — same path that native USTRUCTs
	// marked `USTRUCT(meta = (SeinDeterministic))` use.
	NewUDS->SetMetaData(SeinDeterministicMetaKey, TEXT("true"));

	// Designer-authored UDSes created via this factory are intended to surface
	// in the entity bridge's ComponentData picker — mark them as entity
	// components. Native USTRUCTs reach the same picker via FSeinComponent
	// inheritance; UDSes can't (UE's UDS compiler clears supersuper on every
	// recompile) so the explicit meta tag is the substitute.
	NewUDS->SetMetaData(SeinEntityComponentMetaKey, TEXT("true"));

	return NewUDS;
}

bool USeinSimComponentFactory::IsSeinDeterministicStruct(const UStruct* Struct)
{
	return Struct && Struct->HasMetaData(SeinDeterministicMetaKey);
}

bool USeinSimComponentFactory::IsSeinEntityComponentStruct(const UStruct* Struct)
{
	// Single source of truth lives in CoreEntity so the K2 Get/Set Component node
	// menu (which can't depend on this Editor module) applies the SAME rule — see
	// SeinComponentEligibility::IsEntityComponentStruct. Verbose trace retained for
	// diagnosing why an expected struct doesn't appear in the picker
	// (`log LogSeinEditorPicker Verbose`).
	const bool bAccept = SeinComponentEligibility::IsEntityComponentStruct(Struct);
	UE_LOG(LogTemp, Verbose, TEXT("[SeinPickerFilter] %s -> %s"),
		Struct ? *Struct->GetName() : TEXT("<null>"),
		bAccept ? TEXT("ACCEPT") : TEXT("reject"));
	return bAccept;
}

FText USeinSimComponentFactory::GetDisplayName() const
{
	return LOCTEXT("SeinSimComponentFactoryDisplayName", "SeinARTS Component");
}

uint32 USeinSimComponentFactory::GetMenuCategories() const
{
	uint32 Categories = FSeinARTSEditorModule::GetAssetCategoryBit();
	if (GetDefault<USeinARTSCoreSettings>()->bShowComponentInBasicCategory)
	{
		Categories |= EAssetTypeCategories::Basic;
	}
	return Categories;
}

#undef LOCTEXT_NAMESPACE
