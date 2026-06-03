/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSimComponentFactory.cpp
 * @brief   Implementation of the UDS-based component factory.
 */

#include "Factories/SeinSimComponentFactory.h"
#include "SeinARTSEditorModule.h"
#include "Settings/PluginSettings.h"
#include "Components/SeinComponent.h"
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
	if (!Struct) return false;

	// Diagnostic logging — Verbose level so it doesn't spam the log every
	// frame, but enable via `log LogSeinEditorPicker Verbose` to trace which
	// structs are evaluated and what verdict they get. Useful when a struct
	// you expect to see in the entity-bridge picker doesn't appear.
	auto LogResult = [Struct](bool bAccept, const TCHAR* Reason)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[SeinPickerFilter] %s -> %s (%s)"),
			*Struct->GetName(),
			bAccept ? TEXT("ACCEPT") : TEXT("reject"),
			Reason);
		return bAccept;
	};

	// SeinSubData veto wins regardless of source. A per-class sub-data struct
	// (e.g. FSeinWheeledMovementData) inherits FSeinComponent for storage
	// uniformity but should not appear in the entity bridge's top-level
	// picker — it surfaces only inside its owning component's
	// FInstancedStruct.MovementClassData picker.
	if (Struct->HasMetaData(SeinSubDataMetaKey)) return LogResult(false, TEXT("SeinSubData veto"));

	const bool bHasDeterministic = Struct->HasMetaData(SeinDeterministicMetaKey);
	if (!bHasDeterministic) return LogResult(false, TEXT("no SeinDeterministic meta"));

	// UDS path: explicit SeinEntityComponent tag required (set on creation by
	// this factory). UE's UDS compiler nullifies supersuper, so IsChildOf
	// against FSeinComponent doesn't work as an inheritance check here.
	if (Struct->IsA<UUserDefinedStruct>())
	{
		const bool bHasEntityCompMeta = Struct->HasMetaData(SeinEntityComponentMetaKey);
		return LogResult(bHasEntityCompMeta,
			bHasEntityCompMeta ? TEXT("UDS with SeinEntityComponent meta")
			                   : TEXT("UDS missing SeinEntityComponent meta"));
	}

	// Native USTRUCT path: must inherit FSeinComponent.
	if (const UScriptStruct* SS = Cast<UScriptStruct>(Struct))
	{
		const bool bChildOfSein = SS->IsChildOf(FSeinComponent::StaticStruct());
		return LogResult(bChildOfSein,
			bChildOfSein ? TEXT("native USTRUCT IsChildOf FSeinComponent")
			             : TEXT("native USTRUCT NOT IsChildOf FSeinComponent"));
	}

	return LogResult(false, TEXT("not a UScriptStruct"));
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
