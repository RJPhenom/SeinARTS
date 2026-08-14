/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinSimComponentFactory.cpp
 * @author       RJ Macklem
 * @created      2 Jun 2026
 * @latest       14 Aug 2026
 * @brief        Implements Component-struct creation and compile-stable UDS
 *               metadata stamping.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "Factories/SeinSimComponentFactory.h"
#include "SeinARTSEditorModule.h"
#include "Settings/PluginSettings.h"
#include "Components/SeinComponent.h"
#include "Components/SeinComponentEligibility.h"
#include "Kismet2/StructureEditorUtils.h"
#include "StructUtils/UserDefinedStruct.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"

#define LOCTEXT_NAMESPACE "SeinARTSEditor"

const FName USeinSimComponentFactory::SeinDeterministicMetaKey(TEXT("SeinDeterministic"));
const FName USeinSimComponentFactory::SeinEntityComponentMetaKey(TEXT("SeinEntityComponent"));
const FName USeinSimComponentFactory::SeinSubDataMetaKey(TEXT("SeinSubData"));

namespace
{
	bool MarkerMatches(
		const UUserDefinedStruct& Struct,
		const UUserDefinedStructEditorData* EditorData,
		const FName Key,
		const bool bEnabled)
	{
		if (!bEnabled)
		{
			return !Struct.HasMetaData(Key)
				&& (!EditorData || !EditorData->MetaData.Contains(Key));
		}

		const FString* PersistentValue = EditorData
			? EditorData->MetaData.Find(Key)
			: nullptr;
		return Struct.HasMetaData(Key)
			&& Struct.GetMetaData(Key) == TEXT("true")
			&& (!EditorData || (PersistentValue && *PersistentValue == TEXT("true")));
	}

	void ApplyMarker(
		UUserDefinedStruct& Struct,
		UUserDefinedStructEditorData* EditorData,
		const FName Key,
		const bool bEnabled)
	{
		if (bEnabled)
		{
			Struct.SetMetaData(Key, TEXT("true"));
			if (EditorData)
			{
				EditorData->MetaData.FindOrAdd(Key) = TEXT("true");
			}
		}
		else
		{
			Struct.RemoveMetaData(Key);
			if (EditorData)
			{
				EditorData->MetaData.Remove(Key);
			}
		}
	}

	void MarkUserDefinedStruct(
		UUserDefinedStruct* Struct,
		const bool bEntityComponent)
	{
		if (!Struct)
		{
			return;
		}

		UUserDefinedStructEditorData* EditorData =
			Cast<UUserDefinedStructEditorData>(Struct->EditorData);
		const bool bNeedsUpdate =
			!MarkerMatches(
				*Struct,
				EditorData,
				USeinSimComponentFactory::SeinDeterministicMetaKey,
				true)
			|| !MarkerMatches(
				*Struct,
				EditorData,
				USeinSimComponentFactory::SeinEntityComponentMetaKey,
				bEntityComponent)
			|| !MarkerMatches(
				*Struct,
				EditorData,
				USeinSimComponentFactory::SeinSubDataMetaKey,
				!bEntityComponent);
		if (!bNeedsUpdate)
		{
			return;
		}

		Struct->Modify();
		if (EditorData)
		{
			EditorData->Modify();
		}

		ApplyMarker(
			*Struct,
			EditorData,
			USeinSimComponentFactory::SeinDeterministicMetaKey,
			true);
		ApplyMarker(
			*Struct,
			EditorData,
			USeinSimComponentFactory::SeinEntityComponentMetaKey,
			bEntityComponent);
		ApplyMarker(
			*Struct,
			EditorData,
			USeinSimComponentFactory::SeinSubDataMetaKey,
			!bEntityComponent);
		Struct->MarkPackageDirty();
	}
}

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

	MarkUserDefinedStructAsEntityComponent(NewUDS);

	return NewUDS;
}

void USeinSimComponentFactory::MarkUserDefinedStructAsEntityComponent(
	UUserDefinedStruct* Struct)
{
	MarkUserDefinedStruct(Struct, true);
}

void USeinSimComponentFactory::MarkUserDefinedStructAsSubData(
	UUserDefinedStruct* Struct)
{
	MarkUserDefinedStruct(Struct, false);
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
