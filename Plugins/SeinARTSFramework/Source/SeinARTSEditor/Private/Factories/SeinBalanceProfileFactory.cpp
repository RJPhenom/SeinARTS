/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinBalanceProfileFactory.cpp
 */

#include "Factories/SeinBalanceProfileFactory.h"
#include "SeinARTSEditorModule.h"
#include "Balance/SeinBalanceProfile.h"

#define LOCTEXT_NAMESPACE "SeinARTSEditor"

USeinBalanceProfileFactory::USeinBalanceProfileFactory()
{
	SupportedClass = USeinBalanceProfile::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* USeinBalanceProfileFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* /*Context*/, FFeedbackContext* /*Warn*/)
{
	return NewObject<USeinBalanceProfile>(InParent, Class, Name, Flags);
}

FText USeinBalanceProfileFactory::GetDisplayName() const
{
	return LOCTEXT("SeinBalanceProfileFactoryName", "Balance Data Asset");
}

uint32 USeinBalanceProfileFactory::GetMenuCategories() const
{
	return FSeinARTSEditorModule::GetAssetCategoryBit();
}

#undef LOCTEXT_NAMESPACE
